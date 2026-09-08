#pragma once

#include <algorithm>
#include <random>
#include <vector>

#include "materialconfig.h"
#include "particle.h"
#include "godot_cpp/variant/vector2i.hpp"

using namespace godot;

class WorldGrid;

class SandSimulationChunk {
public:
	static constexpr int SIZE = 64;
	Particle grid[SIZE * SIZE];
	bool is_active = true;

	// Array of material configurations indexed by mat_id
	static std::vector<MaterialConfig> mat_registry;

	inline int get_index(int x, int y) { return y * SIZE + x; }
	inline bool in_bounds(int x, int y) const { return x >= 0 && x < SIZE && y >= 0 && y < SIZE; }

	void tick(bool alternate_direction, WorldGrid &world_grid, Vector2i world_origin);

private:
	bool update_particle(int x, int y, WorldGrid &world_grid, Vector2i world_origin);
	bool try_move_or_swap(int src_x, int src_y, int dst_x, int dst_y, const MaterialConfig &src_config, WorldGrid &world_grid, Vector2i world_origin);
	
	// Chemical reaction methods
	void check_acid_reactions(int x, int y, const WorldGrid &world_grid, Vector2i world_origin);
	void check_fire_reactions(int x, int y, const WorldGrid &world_grid, Vector2i world_origin);
	void check_neighborhood_reactions(int x, int y, const WorldGrid &world_grid, Vector2i world_origin);
};
