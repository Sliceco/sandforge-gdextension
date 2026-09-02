#include "world_grid.h"

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
