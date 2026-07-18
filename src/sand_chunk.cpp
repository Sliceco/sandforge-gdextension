#include <algorithm>
#include <random>
#include <vector>

#include "materialconfig.h"
#include "particle.h"

class SandSimulationChunk {
public:
	static const int SIZE = 64;
	godot::Particle grid[SIZE * SIZE];
	bool is_active = true;

	// Array of material configurations indexed by mat_id
	static std::vector<MaterialConfig> mat_registry;

	inline int get_index(int x, int y) { return y * SIZE + x; }
	inline bool in_bounds(int x, int y) { return x >= 0 && x < SIZE && y >= 0 && y < SIZE; }

	void tick(bool alternate_direction) {
		if (!is_active)
			return;

		bool active_this_frame = false;
		// Clear update flags from the previous frame
		for (int i = 0; i < SIZE * SIZE; ++i) {
			grid[i].flags &= ParticleFlags::PARTICLE_FLAG_NONE; // Clear the updated flag
		}

		// Loop bottom-to-top to let items fall naturally
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

		is_active = active_this_frame;
	}

private:
	bool update_particle(int x, int y) {
		int idx = get_index(x, y);
		godot::Particle &p = grid[idx];

		// If the particle is empty or has been updated this frame, skip it
		if (p.mat_id == 0 || (p.flags & PARTICLE_FLAG_UPDATED))
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

	bool try_move_or_swap(int src_x, int src_y, int dst_x, int dst_y, const MaterialConfig &src_config) {
		if (!in_bounds(dst_x, dst_y))
			return false; // In a full landscape, check neighbor chunks instead

		int src_idx = get_index(src_x, src_y);
		int dst_idx = get_index(dst_x, dst_y);
		godot::Particle &dst_p = grid[dst_idx];

		const MaterialConfig &dst_config = mat_registry[dst_p.mat_id];

		// Move into empty space or displace a lighter/less dense material (e.g. sand sinking in water)
		if (dst_p.mat_id == 0 || (dst_config.state != MatterState::SOLID_FIXED && src_config.density > dst_config.density)) {
			godot::Particle temp = grid[src_idx];

			grid[src_idx] = dst_p; // Swap destination item back to source
			grid[dst_idx] = temp; // Place moving item into destination

			grid[dst_idx].flags |= PARTICLE_FLAG_UPDATED;
			return true;
		}

		return false;
	}
};
