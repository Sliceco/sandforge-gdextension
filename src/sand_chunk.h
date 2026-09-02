#pragma once

#include <algorithm>
#include <random>
#include <vector>

#include "materialconfig.h"
#include "particle.h"

using namespace godot;

class SandSimulationChunk {
public:
	static const int SIZE = 64;
	Particle grid[SIZE * SIZE];
	bool is_active = true;

	// Array of material configurations indexed by mat_id
	static std::vector<MaterialConfig> mat_registry;

	inline int get_index(int x, int y) { return y * SIZE + x; }
	inline bool in_bounds(int x, int y) const { return x >= 0 && x < SIZE && y >= 0 && y < SIZE; }

	void tick(bool alternate_direction);

private:
	bool update_particle(int x, int y);
	bool try_move_or_swap(int src_x, int src_y, int dst_x, int dst_y, const MaterialConfig &src_config);
};
