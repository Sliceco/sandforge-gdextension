#include <algorithm>
#include <random>
#include <vector>

#include "materialconfig.h"
#include "particle.h"
#include "sand_chunk.h"

using namespace godot;

// Initialize static registry
std::vector<MaterialConfig> SandSimulationChunk::mat_registry;

void SandSimulationChunk::tick(bool alternate_direction) {
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
				if (update_particle(x, y))
					active_this_frame = true;
			}
		} else {
			for (int x = SIZE - 1; x >= 0; --x) {
				if (update_particle(x, y))
					active_this_frame = true;
			}
		}
	}

	// Phase 2: Reaction pass - check for chemical interactions
	for (int y = 0; y < SIZE; ++y) {
		for (int x = 0; x < SIZE; ++x) {
			check_neighborhood_reactions(x, y);
		}
	}

	is_active = active_this_frame;
}

bool SandSimulationChunk::update_particle(int x, int y) {
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
		if (try_move_or_swap(x, y, x, y + 1, config))
			return true;

		// Diagonal fall down-left or down-right
		int side_dir = (rand() % 2 == 0) ? 1 : -1;
		if (try_move_or_swap(x, y, x + side_dir, y + 1, config))
			return true;
		if (try_move_or_swap(x, y, x - side_dir, y + 1, config))
			return true;
	}

	// Horizontal dispersion (Liquids only)
	if (config.state == MatterState::LIQUID) {
		int side_dir = (rand() % 2 == 0) ? 1 : -1;
		// Check up to dispersion limit
		for (int i = 1; i <= config.dispersion; ++i) {
			if (try_move_or_swap(x, y, x + (side_dir * i), y, config))
				return true;
			if (try_move_or_swap(x, y, x - (side_dir * i), y, config))
				return true;
		}
	}

	return false;
}

bool SandSimulationChunk::try_move_or_swap(int src_x, int src_y, int dst_x, int dst_y, const MaterialConfig &src_config) {
	if (!in_bounds(dst_x, dst_y))
		return false; // In a full landscape, check neighbor chunks instead

	int src_idx = get_index(src_x, src_y);
	int dst_idx = get_index(dst_x, dst_y);
	Particle &dst_p = grid[dst_idx];

	// Empty space - can always move there
	if (dst_p.mat_id == 0)
		goto perform_swap;

	// Check if destination material is passable
	if (dst_p.mat_id < (int)mat_registry.size()) {
		const MaterialConfig &dst_config = mat_registry[dst_p.mat_id];
		if (dst_config.state == MatterState::SOLID_FIXED)
			return false;  // Solid fixed blocks cannot be displaced
		
		// Can displace if destination is lighter
		if (src_config.density > dst_config.density)
			goto perform_swap;
	}
	return false;

perform_swap:
		Particle temp = grid[src_idx];

		grid[src_idx] = dst_p; // Swap destination item back to source
		grid[dst_idx] = temp; // Place moving item into destination

		grid[dst_idx].flags |= ParticleFlags::PARTICLE_FLAG_UPDATED;
		return true;
	}

	return false;
}

void SandSimulationChunk::check_neighborhood_reactions(int x, int y) {
	Particle &p = grid[get_index(x, y)];
	if (p.mat_id == 0 || p.mat_id >= (int)mat_registry.size())
		return;  // Empty or invalid particle, skip

	const MaterialConfig &config = mat_registry[p.mat_id];

	// Check for acid reactions
	if (config.acid_reactive > 0) {
		check_acid_reactions(x, y);
	}

	// Check for fire spreading
	if (config.flammability > 0) {
		check_fire_reactions(x, y);
	}
}

void SandSimulationChunk::check_acid_reactions(int x, int y) {
	Particle &p = grid[get_index(x, y)];
	
	// Check all 8 neighbors for acid (ID 4, configurable)
	const int ACID_MAT_ID = 4;
	
	for (int dx = -1; dx <= 1; ++dx) {
		for (int dy = -1; dy <= 1; ++dy) {
			if (dx == 0 && dy == 0)
				continue;  // Skip self
			
			int nx = x + dx;
			int ny = y + dy;
			
			if (!in_bounds(nx, ny))
				continue;
			
			Particle &neighbor = grid[get_index(nx, ny)];
			if (neighbor.mat_id == ACID_MAT_ID) {
				// Acid dissolves this particle with some probability
				// Probability based on acid_reactive value (0-255)
				if ((rand() % 256) < p.acid_reactive) {
					p.mat_id = 0;  // Dissolve
					return;
				}
			}
		}
	}
}

void SandSimulationChunk::check_fire_reactions(int x, int y) {
	Particle &p = grid[get_index(x, y)];
	
	// Check for nearby fire (ID 5, configurable)
	const int FIRE_MAT_ID = 5;
	
	for (int dx = -1; dx <= 1; ++dx) {
		for (int dy = -1; dy <= 1; ++dy) {
			if (dx == 0 && dy == 0)
				continue;
			
			int nx = x + dx;
			int ny = y + dy;
			
			if (!in_bounds(nx, ny))
				continue;
			
			Particle &neighbor = grid[get_index(nx, ny)];
			if (neighbor.mat_id == FIRE_MAT_ID) {
				// This particle catches fire with probability based on flammability
				if ((rand() % 256) < p.flammability) {
					p.mat_id = FIRE_MAT_ID;
					p.flags |= ParticleFlags::PARTICLE_FLAG_BURNING;
					return;
				}
			}
		}
	}
}
