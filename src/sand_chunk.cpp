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
	if (dirty_rect.empty())
		return;

	// Snapshot the region to simulate this tick, then reset dirty_rect so it
	// can accumulate whatever region needs simulating on the next tick.
	DirtyRect active_rect = dirty_rect;
	dirty_rect.clear();

	// Phase 1: Movement pass - bottom-to-top to let items fall naturally
	for (int y = active_rect.max_y; y >= active_rect.min_y; --y) {
		// Alternate horizontal scan direction to prevent bias asymmetry
		if (alternate_direction) {
			for (int x = active_rect.min_x; x <= active_rect.max_x; ++x) {
				update_particle(x, y, world_grid, world_origin);
			}
		} else {
			for (int x = active_rect.max_x; x >= active_rect.min_x; --x) {
				update_particle(x, y, world_grid, world_origin);
			}
		}
	}

	// Phase 2: Reaction pass - check for chemical interactions
	for (int y = active_rect.min_y; y <= active_rect.max_y; ++y) {
		for (int x = active_rect.min_x; x <= active_rect.max_x; ++x) {
			check_neighborhood_reactions(x, y, world_grid, world_origin);
		}
	}
}

bool SandSimulationChunk::update_particle(int x, int y, WorldGrid &world_grid, Vector2i world_origin) {
	int idx = get_index(x, y);
	Particle &p = grid[idx];

	// If the particle is empty, skip it.
	if (p.mat_id == 0)
		return false;

	// A particle that crossed into this chunk may be encountered after its
	// source chunk already moved it this frame. Keep it dirty so it resumes on
	// the next tick after WorldGrid clears the transient update flag.
	if (p.flags & ParticleFlags::PARTICLE_FLAG_UPDATED) {
		mark_dirty(x, y);
		return false;
	}

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

	// Gases (Smoke, Fire, etc.) behave like liquids but rise instead of
	// fall, so movement mirrors the powder/liquid logic with an inverted
	// density rule (lighter gas displaces denser gas/fluid above it).
	if (config.state == MatterState::GAS) {
		if (try_move_or_swap(x, y, x, y - 1, config, world_grid, world_origin, true))
			return true;

		int side_dir = (rand() % 2 == 0) ? 1 : -1;
		if (try_move_or_swap(x, y, x + side_dir, y - 1, config, world_grid, world_origin, true))
			return true;
		if (try_move_or_swap(x, y, x - side_dir, y - 1, config, world_grid, world_origin, true))
			return true;

		for (int i = 1; i <= config.dispersion; ++i) {
			if (try_move_or_swap(x, y, x + (side_dir * i), y, config, world_grid, world_origin, true))
				return true;
			if (try_move_or_swap(x, y, x - (side_dir * i), y, config, world_grid, world_origin, true))
				return true;
		}
	}

	return false;
}

bool SandSimulationChunk::try_move_or_swap(int src_x, int src_y, int dst_x, int dst_y, const MaterialConfig &src_config, WorldGrid &world_grid, Vector2i world_origin, bool invert_density) {
	const Vector2i source_position = world_origin + Vector2i(src_x, src_y);
	const Vector2i destination_position = world_origin + Vector2i(dst_x, dst_y);
	Particle destination = world_grid.get_particle_readonly(destination_position.x, destination_position.y);

	if (destination.mat_id == 0) {
		destination = grid[get_index(src_x, src_y)];
		destination.flags |= ParticleFlags::PARTICLE_FLAG_UPDATED;
		world_grid.set_particle(source_position.x, source_position.y, Particle());
		world_grid.set_particle(destination_position.x, destination_position.y, destination);
		world_grid.mark_particle_updated(destination_position.x, destination_position.y);
		return true;
	}

	if (destination.mat_id >= (int)mat_registry.size())
		return false;

	const MaterialConfig &dst_config = mat_registry[destination.mat_id];
	if (dst_config.state == MatterState::SOLID_FIXED)
		return false;

	// Normally the denser material sinks past the lighter one. Rising
	// gases invert this so the lighter gas floats past whatever is above.
	bool src_wins = invert_density ? (src_config.density < dst_config.density) : (src_config.density > dst_config.density);
	if (src_wins) {
		Particle source = grid[get_index(src_x, src_y)];
		source.flags |= ParticleFlags::PARTICLE_FLAG_UPDATED;
		world_grid.set_particle(source_position.x, source_position.y, destination);
		world_grid.set_particle(destination_position.x, destination_position.y, source);
		world_grid.mark_particle_updated(destination_position.x, destination_position.y);
		return true;
	}

	return false;
}

void SandSimulationChunk::check_neighborhood_reactions(int x, int y, WorldGrid &world_grid, Vector2i world_origin) {
	Particle &p = grid[get_index(x, y)];
	if (p.mat_id == 0 || p.mat_id >= (int)mat_registry.size())
		return; // Empty or invalid particle, skip

	const MaterialConfig &config = mat_registry[p.mat_id];

	if (config.acid_reactive > 0) {
		check_acid_reactions(x, y, world_grid, world_origin);
	}

	if (config.flammability > 0) {
		check_fire_reactions(x, y, world_grid, world_origin);
	}

	if (config.decay_chance > 0) {
		check_decay_reactions(x, y, world_grid, world_origin);
	}
}

void SandSimulationChunk::check_acid_reactions(int x, int y, WorldGrid &world_grid, Vector2i world_origin) {
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
				// Route through world_grid so the vacated cell (and any
				// neighboring chunk sharing this border) gets marked dirty.
				world_grid.set_particle(world_origin.x + x, world_origin.y + y, Particle());
				return;
			}
		}
	}
}

void SandSimulationChunk::check_fire_reactions(int x, int y, WorldGrid &world_grid, Vector2i world_origin) {
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
				Particle ignited = p;
				ignited.mat_id = FIRE_MAT_ID;
				ignited.flags |= ParticleFlags::PARTICLE_FLAG_BURNING;
				// Route through world_grid so this cell (and any neighboring
				// chunk sharing this border) gets marked dirty.
				world_grid.set_particle(world_origin.x + x, world_origin.y + y, ignited);
				return;
			}
		}
	}
}

void SandSimulationChunk::check_decay_reactions(int x, int y, WorldGrid &world_grid, Vector2i world_origin) {
	Particle &p = grid[get_index(x, y)];
	if (p.mat_id == 0 || p.mat_id >= (int)mat_registry.size())
		return;

	const MaterialConfig &config = mat_registry[p.mat_id];
	if (config.decay_chance == 0 || (rand() % 256) >= config.decay_chance)
		return;

	// Burnt-out fire (and similar decaying materials) turns into its
	// configured byproduct, e.g. Fire -> Smoke. Route through world_grid so
	// the cell (and any neighboring chunk sharing this border) wakes up.
	Particle decayed;
	decayed.mat_id = config.decay_into;
	world_grid.set_particle(world_origin.x + x, world_origin.y + y, decayed);
}
