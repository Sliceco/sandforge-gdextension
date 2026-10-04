#include "world_grid.h"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace {
constexpr uint8_t SNAPSHOT_MAGIC[] = { 'S', 'F', 'W', '1' };
constexpr uint32_t SNAPSHOT_VERSION = 3;
constexpr size_t SNAPSHOT_HEADER_SIZE = 12;
constexpr size_t SNAPSHOT_CHUNK_SIZE = 8 + SandSimulationChunk::SIZE * SandSimulationChunk::SIZE * 4;

void append_u32(std::vector<uint8_t> &data, uint32_t value) {
	for (int shift = 0; shift < 32; shift += 8) {
		data.push_back(static_cast<uint8_t>(value >> shift));
	}
}

uint32_t read_u32(const std::vector<uint8_t> &data, uint64_t offset) {
	uint32_t value = 0;
	for (int shift = 0; shift < 32; shift += 8) {
		value |= static_cast<uint32_t>(data[offset++]) << shift;
	}
	return value;
}

// Maps a local coordinate across a chunk boundary to the mirrored coordinate
// in the neighboring chunk (e.g. the rightmost column mirrors to column 0 of
// the chunk to the right).
int mirror_coordinate(int local, int delta, int size) {
	if (delta == -1)
		return size - 1;
	if (delta == 1)
		return 0;
	return local;
}
} // namespace

SandSimulationChunk *WorldGrid::get_or_create_chunk(int chunk_x, int chunk_y) {
	Vector2i key(chunk_x, chunk_y);
	auto it = chunks.find(key);
	if (it != chunks.end()) {
		return it->second.get();
	}
	return chunks.emplace(key, std::make_unique<SandSimulationChunk>()).first->second.get();
}

SandSimulationChunk *WorldGrid::get_chunk(int chunk_x, int chunk_y) const {
	Vector2i key(chunk_x, chunk_y);
	auto it = chunks.find(key);
	if (it != chunks.end()) {
		return it->second.get();
	}
	return nullptr;
}

Particle WorldGrid::get_particle_readonly(int world_x, int world_y) const {
	int chunk_x, chunk_y, local_x, local_y;
	world_to_chunk(world_x, world_y, chunk_x, chunk_y);
	world_to_local(world_x, world_y, local_x, local_y);

	SandSimulationChunk *chunk = get_chunk(chunk_x, chunk_y);
	if (chunk == nullptr) {
		return Particle(); // Return empty particle
	}
	return chunk->grid[chunk->get_index(local_x, local_y)];
}

void WorldGrid::set_particle(int world_x, int world_y, const Particle &p) {
	int chunk_x, chunk_y, local_x, local_y;
	world_to_chunk(world_x, world_y, chunk_x, chunk_y);
	world_to_local(world_x, world_y, local_x, local_y);

	// Erasing unallocated space is a no-op; don't materialize an empty chunk.
	SandSimulationChunk *chunk = p.mat_id == 0 ? get_chunk(chunk_x, chunk_y) : get_or_create_chunk(chunk_x, chunk_y);
	if (chunk == nullptr) {
		return;
	}
	set_particle_in_chunk(*chunk, chunk_x, chunk_y, local_x, local_y, p);
}

void WorldGrid::set_particle_in_chunk(SandSimulationChunk &chunk, int chunk_x, int chunk_y, int local_x, int local_y, const Particle &p) {
	chunk.grid[chunk.get_index(local_x, local_y)] = p;
	chunk.mark_dirty(local_x, local_y);

	// An edit on a chunk edge can enable movement in a neighboring chunk
	// (e.g. clearing space so a sleeping chunk above can now fall into it).
	// Mark the mirrored border cell dirty on any existing neighbor that
	// shares this cell's boundary, so it re-evaluates just that region on
	// the next tick instead of staying asleep indefinitely.
	constexpr int SIZE = SandSimulationChunk::SIZE;
	const int dy_start = (local_y == 0) ? -1 : 0;
	const int dy_end = (local_y == SIZE - 1) ? 1 : 0;
	const int dx_start = (local_x == 0) ? -1 : 0;
	const int dx_end = (local_x == SIZE - 1) ? 1 : 0;
	for (int dy = dy_start; dy <= dy_end; ++dy) {
		for (int dx = dx_start; dx <= dx_end; ++dx) {
			if (dx == 0 && dy == 0)
				continue;
			SandSimulationChunk *neighbor = get_chunk(chunk_x + dx, chunk_y + dy);
			if (neighbor == nullptr)
				continue;
			const int mirrored_x = mirror_coordinate(local_x, dx, SIZE);
			const int mirrored_y = mirror_coordinate(local_y, dy, SIZE);
			neighbor->mark_dirty(mirrored_x, mirrored_y);
		}
	}
}

Particle WorldGrid::make_particle(uint8_t mat_id) {
	if (mat_id == 0) {
		return Particle();
	}
	Particle p;
	p.mat_id = mat_id;
	p.hp = materials.get(mat_id).max_hp;
	p.shade = random.next_u8();
	return p;
}

Particle WorldGrid::convert_particle(Particle source, uint8_t into) const {
	if (into == 0) {
		return Particle();
	}
	// A reaction that leaves a cell's material unchanged (e.g. burning wood
	// igniting its surroundings) must not refill its hp.
	if (into != source.mat_id) {
		source.mat_id = into;
		source.hp = materials.get(into).max_hp;
		// A new material starts unsettled (e.g. steam condensing to water).
		source.set_rest(0);
		source.set_following(false);
	}
	return source;
}

int WorldGrid::damage_particle(int world_x, int world_y, int amount) {
	if (amount <= 0) {
		return 0;
	}
	Particle p = get_particle_readonly(world_x, world_y);
	if (p.mat_id == 0) {
		return amount;
	}

	const int toughness = std::max<int>(p.hp, 1);
	if (amount < toughness) {
		p.hp = static_cast<uint8_t>(p.hp - amount);
		set_particle(world_x, world_y, p);
		return 0;
	}
	set_particle(world_x, world_y, convert_particle(p, materials.get(p.mat_id).break_into));
	return amount - toughness;
}

void WorldGrid::mark_particle_updated(int world_x, int world_y) {
	updated_particles.emplace_back(world_x, world_y);
}

void WorldGrid::clear_updated_particles() {
	for (const Vector2i &position : updated_particles) {
		int chunk_x, chunk_y, local_x, local_y;
		world_to_chunk(position.x, position.y, chunk_x, chunk_y);
		world_to_local(position.x, position.y, local_x, local_y);

		SandSimulationChunk *chunk = get_chunk(chunk_x, chunk_y);
		if (chunk != nullptr) {
			Particle &particle = chunk->grid[chunk->get_index(local_x, local_y)];
			particle.flags &= ~ParticleFlags::PARTICLE_FLAGS_TRANSIENT;
		}
	}
	updated_particles.clear();
}

void WorldGrid::tick() {
	clear_updated_particles();

	std::vector<Vector2i> chunk_positions;
	chunk_positions.reserve(chunks.size());
	for (const auto &[position, _] : chunks) {
		chunk_positions.push_back(position);
	}

	// Process chunks in a deterministic, gravity-consistent order instead of
	// relying on unordered_map's hash-bucket iteration order. Without this,
	// whether a chunk is ticked before or after its neighbor across a chunk
	// boundary is arbitrary and can flip depending on hashing, so material
	// falling across that seam intermittently gets an extra same-frame step
	// in one chunk but not the other. That produces a stable per-seam bias
	// (a staggered, comb-like surface right at chunk edges) instead of the
	// smooth multi-chunk cascade seen within a single chunk's own bottom-to-
	// top scan. Sorting bottom row of chunks first (largest chunk_y first),
	// with horizontal order matching the same alternate_direction used for
	// the intra-chunk column scan, lets a falling column clear space in the
	// chunk below before the chunk above is simulated, so cross-chunk falls
	// cascade within one tick just like they do inside a single chunk.
	std::sort(chunk_positions.begin(), chunk_positions.end(), [this](const Vector2i &a, const Vector2i &b) {
		if (a.y != b.y)
			return a.y > b.y;
		return alternate_direction ? a.x < b.x : a.x > b.x;
	});

	// New destination chunks are deferred to the following tick.
	std::vector<Vector2i> ticked_positions;
	for (const Vector2i &position : chunk_positions) {
		SandSimulationChunk *chunk = get_chunk(position.x, position.y);
		if (chunk != nullptr) {
			if (chunk->is_active()) {
				ticked_positions.push_back(position);
			}
			chunk->tick(alternate_direction, *this, position * SandSimulationChunk::SIZE);
		}
	}
	alternate_direction = !alternate_direction;

	// Free chunks that just went to sleep with nothing in them. Only chunks
	// that were awake this tick are scanned, so idle chunks cost nothing.
	for (const Vector2i &position : ticked_positions) {
		auto it = chunks.find(position);
		if (it != chunks.end() && !it->second->is_active() && it->second->is_empty()) {
			chunks.erase(it);
		}
	}
}

void WorldGrid::clear() {
	chunks.clear();
	updated_particles.clear();
	alternate_direction = false;
}

std::vector<WorldGrid::ChunkDebugInfo> WorldGrid::get_debug_chunk_info() const {
	std::vector<ChunkDebugInfo> info;
	info.reserve(chunks.size());
	for (const auto &[position, chunk] : chunks) {
		ChunkDebugInfo entry;
		entry.chunk_position = position;
		entry.has_dirty_rect = !chunk->dirty_rect.empty();
		if (entry.has_dirty_rect) {
			entry.dirty_min_x = chunk->dirty_rect.min_x;
			entry.dirty_min_y = chunk->dirty_rect.min_y;
			entry.dirty_max_x = chunk->dirty_rect.max_x;
			entry.dirty_max_y = chunk->dirty_rect.max_y;
		}
		info.push_back(entry);
	}
	return info;
}

std::vector<uint8_t> WorldGrid::serialize() const {
	std::vector<uint8_t> data;
	for (uint8_t byte : SNAPSHOT_MAGIC) {
		data.push_back(byte);
	}
	append_u32(data, SNAPSHOT_VERSION);
	append_u32(data, static_cast<uint32_t>(chunks.size()));

	// Material table: id -> name for every named material, so a load can
	// remap IDs if the material setup changed since the save.
	std::vector<const MaterialConfig *> named;
	for (int id = 1; id < MaterialRegistry::MAX_MATERIALS; ++id) {
		const MaterialConfig &config = materials.get(static_cast<uint8_t>(id));
		if (!config.name.empty()) {
			named.push_back(&config);
		}
	}
	append_u32(data, static_cast<uint32_t>(named.size()));
	for (const MaterialConfig *config : named) {
		data.push_back(config->id);
		append_u32(data, static_cast<uint32_t>(config->name.size()));
		for (char c : config->name) {
			data.push_back(static_cast<uint8_t>(c));
		}
	}

	// Sorted so identical worlds produce identical bytes.
	std::vector<Vector2i> positions;
	positions.reserve(chunks.size());
	for (const auto &[position, _] : chunks) {
		positions.push_back(position);
	}
	std::sort(positions.begin(), positions.end(), [](const Vector2i &a, const Vector2i &b) {
		return a.y != b.y ? a.y < b.y : a.x < b.x;
	});

	for (const Vector2i &position : positions) {
		const SandSimulationChunk &chunk = *chunks.at(position);
		append_u32(data, static_cast<uint32_t>(position.x));
		append_u32(data, static_cast<uint32_t>(position.y));
		for (const Particle &particle : chunk.grid) {
			data.push_back(particle.mat_id);
			data.push_back(particle.flags);
			data.push_back(particle.hp);
			data.push_back(particle.shade);
		}
	}

	return data;
}

bool WorldGrid::deserialize(const std::vector<uint8_t> &data) {
	last_error.clear();
	auto fail = [this](const char *message) {
		last_error = message;
		return false;
	};

	const uint64_t size = data.size();
	if (size < SNAPSHOT_HEADER_SIZE) {
		return fail("Snapshot is too small.");
	}
	for (size_t i = 0; i < sizeof(SNAPSHOT_MAGIC); ++i) {
		if (data[i] != SNAPSHOT_MAGIC[i]) {
			return fail("Snapshot has an invalid header.");
		}
	}
	if (read_u32(data, 4) != SNAPSHOT_VERSION) {
		return fail("Unsupported snapshot version.");
	}

	const uint32_t chunk_count = read_u32(data, 8);
	uint64_t offset = SNAPSHOT_HEADER_SIZE;

	if (size - offset < 4) {
		return fail("Snapshot is truncated.");
	}
	const uint32_t material_count = read_u32(data, offset);
	offset += 4;

	// Snapshot material ID -> current material ID, resolved by name.
	std::array<int, MaterialRegistry::MAX_MATERIALS> remap;
	remap.fill(-1);
	remap[0] = 0;
	std::array<bool, MaterialRegistry::MAX_MATERIALS> in_table{};
	std::unordered_map<int, std::string> unresolved_names;
	for (uint32_t i = 0; i < material_count; ++i) {
		if (size - offset < 5) {
			return fail("Snapshot is truncated.");
		}
		const uint8_t id = data[offset];
		const uint32_t name_length = read_u32(data, offset + 1);
		offset += 5;
		if (id == 0 || in_table[id] || name_length > size - offset) {
			return fail("Snapshot material table is corrupt.");
		}
		std::string name;
		for (uint32_t c = 0; c < name_length; ++c) {
			name.push_back(static_cast<char>(data[offset + c]));
		}
		offset += name_length;
		in_table[id] = true;

		// Prefer the same ID when it still has that name, else the lowest match.
		if (materials.get(id).name == name) {
			remap[id] = id;
			continue;
		}
		for (int candidate = 1; candidate < MaterialRegistry::MAX_MATERIALS; ++candidate) {
			if (materials.get(static_cast<uint8_t>(candidate)).name == name) {
				remap[id] = candidate;
				break;
			}
		}
		if (remap[id] < 0) {
			remap[id] = -2; // Only an error if a cell actually uses it.
			unresolved_names[id] = name;
		}
	}

	const uint64_t expected_size = offset + static_cast<uint64_t>(chunk_count) * SNAPSHOT_CHUNK_SIZE;
	if (expected_size != size) {
		return fail("Snapshot size does not match its header.");
	}

	std::unordered_map<Vector2i, std::unique_ptr<SandSimulationChunk>> restored_chunks;
	for (uint32_t i = 0; i < chunk_count; ++i) {
		const int chunk_x = static_cast<int32_t>(read_u32(data, offset));
		offset += 4;
		const int chunk_y = static_cast<int32_t>(read_u32(data, offset));
		offset += 4;
		auto chunk = std::make_unique<SandSimulationChunk>();
		bool has_particles = false;
		for (Particle &particle : chunk->grid) {
			const uint8_t stored_id = data[offset++];
			particle.flags = data[offset++] & ~ParticleFlags::PARTICLE_FLAGS_TRANSIENT;
			if (stored_id != 0 && !in_table[stored_id]) {
				// Saved before this material had a name: nothing to remap by.
				remap[stored_id] = stored_id;
			}
			if (remap[stored_id] == -2) {
				last_error = "Snapshot uses material '" + unresolved_names[stored_id] + "' which is not registered in this world.";
				return false;
			}
			particle.mat_id = static_cast<uint8_t>(remap[stored_id]);
			// Clamp in case the material's max_hp was lowered since the save.
			particle.hp = std::min(data[offset++], materials.get(particle.mat_id).max_hp);
			particle.shade = data[offset++];
			has_particles = has_particles || particle.mat_id != 0;
		}
		if (has_particles) {
			const auto [_, inserted] = restored_chunks.emplace(Vector2i(chunk_x, chunk_y), std::move(chunk));
			if (!inserted) {
				return fail("Snapshot contains duplicate chunks.");
			}
		}
	}

	chunks = std::move(restored_chunks);
	updated_particles.clear();
	alternate_direction = false;
	return true;
}
