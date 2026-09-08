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
} // namespace

SandSimulationChunk *WorldGrid::get_or_create_chunk(int chunk_x, int chunk_y) {
	Vector2i key(chunk_x, chunk_y);
	auto it = chunks.find(key);
	if (it != chunks.end()) {
		return &it->second;
	}
	return &chunks.emplace(key, SandSimulationChunk()).first->second;
}

SandSimulationChunk *WorldGrid::get_chunk(int chunk_x, int chunk_y) const {
	Vector2i key(chunk_x, chunk_y);
	auto it = chunks.find(key);
	if (it != chunks.end()) {
		return const_cast<SandSimulationChunk *>(&it->second);
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
	chunk->is_active = true;
}

void WorldGrid::tick() {
	// Update all chunks and remove inactive ones (optional optimization)
	for (auto &[key, chunk] : chunks) {
		chunk.tick(alternate_direction);
	}
	alternate_direction = !alternate_direction;
	
	// Optionally unload chunks that are far from activity
	// For now, we'll keep them all in memory
}

void WorldGrid::clear() {
	chunks.clear();
	alternate_direction = false;
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
		for (const Particle &particle : chunk.grid) {
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

	std::unordered_map<Vector2i, SandSimulationChunk> restored_chunks;
	int offset = SNAPSHOT_HEADER_SIZE;
	for (uint32_t i = 0; i < chunk_count; ++i) {
		const int chunk_x = static_cast<int32_t>(read_u32(data, offset));
		offset += 4;
		const int chunk_y = static_cast<int32_t>(read_u32(data, offset));
		offset += 4;
		SandSimulationChunk chunk;
		bool has_particles = false;
		for (Particle &particle : chunk.grid) {
			particle.mat_id = data[offset++];
			particle.flags = data[offset++];
			has_particles = has_particles || particle.mat_id != 0;
		}
		if (has_particles) {
			const auto [_, inserted] = restored_chunks.emplace(Vector2i(chunk_x, chunk_y), chunk);
			if (!inserted) {
				return false;
			}
		}
	}

	chunks = std::move(restored_chunks);
	alternate_direction = false;
	return true;
}
