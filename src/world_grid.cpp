#include "world_grid.h"

namespace {
constexpr uint8_t SNAPSHOT_MAGIC[] = { 'S', 'F', 'W', '1' };
constexpr uint32_t SNAPSHOT_VERSION = 1;
constexpr size_t SNAPSHOT_HEADER_SIZE = 12;
constexpr size_t SNAPSHOT_CHUNK_SIZE = 8 + SandSimulationChunk::SIZE * SandSimulationChunk::SIZE * 2;

void append_u32(PackedByteArray &data, uint32_t value) {
	for (int shift = 0; shift < 32; shift += 8) {
		data.append(static_cast<uint8_t>(value >> shift));
	}
}

uint32_t read_u32(const PackedByteArray &data, int offset) {
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

Particle &WorldGrid::get_particle(int world_x, int world_y) {
	int chunk_x, chunk_y, local_x, local_y;
	world_to_chunk(world_x, world_y, chunk_x, chunk_y);
	world_to_local(world_x, world_y, local_x, local_y);
	
	SandSimulationChunk *chunk = get_or_create_chunk(chunk_x, chunk_y);
	return chunk->grid[chunk->get_index(local_x, local_y)];
}

Particle WorldGrid::get_particle_readonly(int world_x, int world_y) const {
	int chunk_x, chunk_y, local_x, local_y;
	world_to_chunk(world_x, world_y, chunk_x, chunk_y);
	world_to_local(world_x, world_y, local_x, local_y);
	
	SandSimulationChunk *chunk = get_chunk(chunk_x, chunk_y);
	if (chunk == nullptr) {
		return Particle();  // Return empty particle
	}
	return chunk->grid[chunk->get_index(local_x, local_y)];
}

void WorldGrid::set_particle(int world_x, int world_y, const Particle &p) {
	int chunk_x, chunk_y, local_x, local_y;
	world_to_chunk(world_x, world_y, chunk_x, chunk_y);
	world_to_local(world_x, world_y, local_x, local_y);
	
	SandSimulationChunk *chunk = get_or_create_chunk(chunk_x, chunk_y);
	chunk->grid[chunk->get_index(local_x, local_y)] = p;
	chunk->mark_dirty(local_x, local_y);

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

void WorldGrid::tick() {
	std::vector<Vector2i> chunk_positions;
	chunk_positions.reserve(chunks.size());
	for (const auto &[position, _] : chunks) {
		chunk_positions.push_back(position);
	}

	// New destination chunks are deferred to the following tick.
	for (const Vector2i &position : chunk_positions) {
		SandSimulationChunk *chunk = get_chunk(position.x, position.y);
		if (chunk != nullptr) {
			chunk->tick(alternate_direction, *this, position * SandSimulationChunk::SIZE);
		}
	}
	alternate_direction = !alternate_direction;
	
	// Optionally unload chunks that are far from activity
	// For now, we'll keep them all in memory
}

void WorldGrid::clear() {
	chunks.clear();
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

PackedByteArray WorldGrid::serialize() const {
	PackedByteArray data;
	for (uint8_t byte : SNAPSHOT_MAGIC) {
		data.append(byte);
	}
	append_u32(data, SNAPSHOT_VERSION);
	append_u32(data, static_cast<uint32_t>(chunks.size()));

	for (const auto &[position, chunk] : chunks) {
		append_u32(data, static_cast<uint32_t>(position.x));
		append_u32(data, static_cast<uint32_t>(position.y));
		for (const Particle &particle : chunk->grid) {
			data.append(particle.mat_id);
			data.append(particle.flags);
		}
	}

	return data;
}

bool WorldGrid::deserialize(const PackedByteArray &data) {
	if (data.size() < static_cast<int>(SNAPSHOT_HEADER_SIZE)) {
		return false;
	}
	for (size_t i = 0; i < sizeof(SNAPSHOT_MAGIC); ++i) {
		if (data[static_cast<int>(i)] != SNAPSHOT_MAGIC[i]) {
			return false;
		}
	}
	if (read_u32(data, 4) != SNAPSHOT_VERSION) {
		return false;
	}

	const uint32_t chunk_count = read_u32(data, 8);
	const uint64_t expected_size = SNAPSHOT_HEADER_SIZE + static_cast<uint64_t>(chunk_count) * SNAPSHOT_CHUNK_SIZE;
	if (expected_size != static_cast<uint64_t>(data.size())) {
		return false;
	}

	std::unordered_map<Vector2i, std::unique_ptr<SandSimulationChunk>> restored_chunks;
	int offset = SNAPSHOT_HEADER_SIZE;
	for (uint32_t i = 0; i < chunk_count; ++i) {
		const int chunk_x = static_cast<int32_t>(read_u32(data, offset));
		offset += 4;
		const int chunk_y = static_cast<int32_t>(read_u32(data, offset));
		offset += 4;
		auto chunk = std::make_unique<SandSimulationChunk>();
		bool has_particles = false;
		for (Particle &particle : chunk->grid) {
			particle.mat_id = data[offset++];
			particle.flags = data[offset++];
			has_particles = has_particles || particle.mat_id != 0;
		}
		if (has_particles) {
			const auto [_, inserted] = restored_chunks.emplace(Vector2i(chunk_x, chunk_y), std::move(chunk));
			if (!inserted) {
				return false;
			}
		}
	}

	chunks = std::move(restored_chunks);
	alternate_direction = false;
	return true;
}
