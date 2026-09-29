#pragma once

#include "godot_cpp/variant/vector2i.hpp"
#include "materialconfig.h"
#include "particle.h"

#include <algorithm>
#include <climits>

using namespace godot;

class WorldGrid;

// Axis-aligned inclusive bounding box of chunk-local cells that require
// simulation. An empty rect means nothing changed since the last tick and
// the chunk can be skipped entirely.
struct DirtyRect {
	int min_x = INT_MAX;
	int min_y = INT_MAX;
	int max_x = INT_MIN;
	int max_y = INT_MIN;

	bool empty() const { return min_x > max_x || min_y > max_y; }

	void clear() {
		min_x = INT_MAX;
		min_y = INT_MAX;
		max_x = INT_MIN;
		max_y = INT_MIN;
	}

	// Grows the rect to include (x, y), clamping to a [0, size - 1] chunk.
	void expand(int x, int y, int size) {
		x = std::clamp(x, 0, size - 1);
		y = std::clamp(y, 0, size - 1);
		min_x = std::min(min_x, x);
		min_y = std::min(min_y, y);
		max_x = std::max(max_x, x);
		max_y = std::max(max_y, y);
	}
};

class SandSimulationChunk {
public:
	static constexpr int SIZE = 64;
	Particle grid[SIZE * SIZE];

	// Cells (padded by a 1-cell margin) that must be simulated on the next
	// tick. Starts fully dirty so a freshly created chunk always runs once.
	DirtyRect dirty_rect{ 0, 0, SIZE - 1, SIZE - 1 };

	inline int get_index(int x, int y) { return y * SIZE + x; }
	inline bool in_bounds(int x, int y) const { return x >= 0 && x < SIZE && y >= 0 && y < SIZE; }

	bool is_active() const { return !dirty_rect.empty(); }

	// Marks a local cell, plus its 3x3 neighborhood, dirty for the next
	// tick. Must be called whenever a cell's contents change, so that both
	// the cell itself and any neighbor that could now move into it are
	// re-evaluated.
	void mark_dirty(int x, int y) {
		dirty_rect.expand(x - 1, y - 1, SIZE);
		dirty_rect.expand(x + 1, y + 1, SIZE);
	}

	void tick(bool alternate_direction, WorldGrid &world_grid, Vector2i world_origin);

private:
	// Local-access fast paths: cells inside this chunk skip the WorldGrid hash
	// lookup; anything outside falls back to world coordinates. Writes still
	// go through WorldGrid so dirty/neighbor wake-up rules apply.
	Particle read_cell(int x, int y, const WorldGrid &world_grid, Vector2i world_origin) const;
	void write_cell(int x, int y, const Particle &p, WorldGrid &world_grid, Vector2i world_origin);

	bool update_particle(int x, int y, WorldGrid &world_grid, Vector2i world_origin);
	// invert_density flips the "denser wins" swap rule so buoyant gases can
	// rise past heavier fluids instead of sinking past lighter ones.
	bool try_move_or_swap(int src_x, int src_y, int dst_x, int dst_y, const MaterialConfig &src_config, WorldGrid &world_grid, Vector2i world_origin, bool invert_density = false);
	// Slides horizontally toward dir through empty cells (up to the
	// material's dispersion) and moves to the farthest one reached; never
	// passes through an occupied cell.
	bool try_disperse(int x, int y, int dir, const MaterialConfig &config, WorldGrid &world_grid, Vector2i world_origin, bool invert_density);

	// Reaction pass. Each returns true if it changed the cell.
	void check_neighborhood_reactions(int x, int y, WorldGrid &world_grid, Vector2i world_origin);
	bool check_reaction_rules(int x, int y, WorldGrid &world_grid, Vector2i world_origin);
	bool check_decay(int x, int y, WorldGrid &world_grid, Vector2i world_origin);
};
