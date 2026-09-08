#include <algorithm>
#include <random>
#include <vector>

#include "materialconfig.h"
#include "particle.h"
#include "sand_chunk.h"
#include "world_grid.h"

using namespace godot;

// Initialize static registry
std::vector<MaterialConfig> SandSimulationChunk::mat_registry;

void SandSimulationChunk::tick(bool alternate_direction, WorldGrid &world_grid, Vector2i world_origin) {
	if (!is_active)
		return;

	bool active_this_frame = false;
	// Clear update flags from the previous frame
	for (int i = 0; i < SIZE * SIZE; ++i) {
		grid[i].flags &= ParticleFlags::PARTICLE_FLAG_NONE; // Clear the updated flag
	}

	// Phase 1: Movement pass - bottom-to-top to let items fall naturally
	for (int y = SIZE - 1; y >= 0; --y) {
		// Alternate horizontal scan direction to prevent bias asymmetry
		if (alternate_direction) {
			for (int x = 0; x < SIZE; ++x) {
				if (update_particle(x, y, world_grid, world_origin))
					active_this_frame = true;
			}
		} else {
			for (int x = SIZE - 1; x >= 0; --x) {
				if (update_particle(x, y, world_grid, world_origin))
					active_this_frame = true;
			}
		}
	}

	// Phase 2: Reaction pass - check for chemical interactions
	for (int y = 0; y < SIZE; ++y) {
		for (int x = 0; x < SIZE; ++x) {
			check_neighborhood_reactions(x, y, world_grid, world_origin);
		}
	}

	is_active = active_this_frame;
}

bool SandSimulationChunk::update_particle(int x, int y, WorldGrid &world_grid, Vector2i world_origin) {
	int idx = get_index(x, y);
	Particle &p = grid[idx];

	// If the particle is empty or has been updated this frame, skip it
	if (p.mat_id == 0 || (p.flags & ParticleFlags::PARTICLE_FLAG_UPDATED))
		return false;

	// Bounds check on material registry
	if (p.mat_id >= (int)mat_registry.size())
		return false;

	// Get the material configuration for this particle
	const MaterialConfig &config = mat_registry[p.mat_id];
	if (config.state == MatterState::SOLID_FIXED)
		return false;

	// Try moving down (Powders and Liquids)
	if (config.state == MatterState::SOLID_POWDER || config.state == MatterState::LIQUID) {
		if (try_move_or_swap(x, y, x, y + 1, config, world_grid, world_origin))
			return true;

		// Diagonal fall down-left or down-right
		int side_dir = (rand() % 2 == 0) ? 1 : -1;
		if (try_move_or_swap(x, y, x + side_dir, y + 1, config, world_grid, world_origin))
			return true;
		if (try_move_or_swap(x, y, x - side_dir, y + 1, config, world_grid, world_origin))
			return true;
	}

	// Horizontal dispersion (Liquids only)
	if (config.state == MatterState::LIQUID) {
		int side_dir = (rand() % 2 == 0) ? 1 : -1;
		// Check up to dispersion limit
		for (int i = 1; i <= config.dispersion; ++i) {
			if (try_move_or_swap(x, y, x + (side_dir * i), y, config, world_grid, world_origin))
				return true;
			if (try_move_or_swap(x, y, x - (side_dir * i), y, config, world_grid, world_origin))
				return true;
		}
	}

	return false;
}

bool SandSimulationChunk::try_move_or_swap(int src_x, int src_y, int dst_x, int dst_y, const MaterialConfig &src_config, WorldGrid &world_grid, Vector2i world_origin) {
	const Vector2i source_position = world_origin + Vector2i(src_x, src_y);
	const Vector2i destination_position = world_origin + Vector2i(dst_x, dst_y);
	Particle destination = world_grid.get_particle_readonly(destination_position.x, destination_position.y);

	if (destination.mat_id == 0) {
		destination = grid[get_index(src_x, src_y)];
		destination.flags |= ParticleFlags::PARTICLE_FLAG_UPDATED;
		world_grid.set_particle(source_position.x, source_position.y, Particle());
		world_grid.set_particle(destination_position.x, destination_position.y, destination);
		return true;
	}

	if (destination.mat_id >= (int)mat_registry.size())
		return false;

	const MaterialConfig &dst_config = mat_registry[destination.mat_id];
	if (dst_config.state == MatterState::SOLID_FIXED)
		return false;

	if (src_config.density > dst_config.density) {
		Particle source = grid[get_index(src_x, src_y)];
		source.flags |= ParticleFlags::PARTICLE_FLAG_UPDATED;
		world_grid.set_particle(source_position.x, source_position.y, destination);
		world_grid.set_particle(destination_position.x, destination_position.y, source);
		return true;
	}

	return false;
}

void SandSimulationChunk::check_neighborhood_reactions(int x, int y, const WorldGrid &world_grid, Vector2i world_origin) {
	Particle &p = grid[get_index(x, y)];
	if (p.mat_id == 0 || p.mat_id >= (int)mat_registry.size())
		return;  // Empty or invalid particle, skip

	const MaterialConfig &config = mat_registry[p.mat_id];

	if (config.acid_reactive > 0) {
		check_acid_reactions(x, y, world_grid, world_origin);
	}

	if (config.flammability > 0) {
		check_fire_reactions(x, y, world_grid, world_origin);
	}
}

void SandSimulationChunk::check_acid_reactions(int x, int y, const WorldGrid &world_grid, Vector2i world_origin) {
	Particle &p = grid[get_index(x, y)];
	if (p.mat_id == 0 || p.mat_id >= (int)mat_registry.size())
		return;

	const MaterialConfig &config = mat_registry[p.mat_id];
	const int ACID_MAT_ID = 4;
	
	for (int dx = -1; dx <= 1; ++dx) {
		for (int dy = -1; dy <= 1; ++dy) {
			if (dx == 0 && dy == 0)
				continue;

			Particle neighbor = world_grid.get_particle_readonly(world_origin.x + x + dx, world_origin.y + y + dy);
			if (neighbor.mat_id == ACID_MAT_ID && (rand() % 256) < config.acid_reactive) {
				p.mat_id = 0;
				return;
			}
		}
	}
}

void SandSimulationChunk::check_fire_reactions(int x, int y, const WorldGrid &world_grid, Vector2i world_origin) {
	Particle &p = grid[get_index(x, y)];
	if (p.mat_id == 0 || p.mat_id >= (int)mat_registry.size())
		return;

	const MaterialConfig &config = mat_registry[p.mat_id];
	const int FIRE_MAT_ID = 5;
	
	for (int dx = -1; dx <= 1; ++dx) {
		for (int dy = -1; dy <= 1; ++dy) {
			if (dx == 0 && dy == 0)
				continue;

			Particle neighbor = world_grid.get_particle_readonly(world_origin.x + x + dx, world_origin.y + y + dy);
			if (neighbor.mat_id == FIRE_MAT_ID && (rand() % 256) < config.flammability) {
				p.mat_id = FIRE_MAT_ID;
				p.flags |= ParticleFlags::PARTICLE_FLAG_BURNING;
				return;
			}
		}
	}
}
