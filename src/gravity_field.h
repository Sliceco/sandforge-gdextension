#pragma once

#include "godot_cpp/variant/vector2i.hpp"

#include <cstdint>
#include <vector>

using namespace godot;

// Which way is "down" for a cell, for the movement rules. They are written
// for the default frame (down is +y, sideways is x) and read and write cells
// through this frame, so with gravity pointing another way every rule turns
// with it. Frames are exact quarter turns of the default, so particles
// behave the same in each of the four directions.
struct GravityFrame {
	// World offset of one cell sideways (+side) and one cell down.
	int side_x = 1, side_y = 0;
	int down_x = 0, down_y = 1;

	// The frame whose down is (down_x, down_y), a unit grid direction.
	static GravityFrame from_down(int down_x, int down_y) {
		// Rotating the default frame so (0, 1) lands on the new down takes
		// (1, 0) to (down_y, -down_x).
		return GravityFrame{ down_y, -down_x, down_x, down_y };
	}

	bool is_default() const { return down_x == 0 && down_y == 1; }

	// World offset of `side` cells sideways and `down` cells down.
	Vector2i to_world(int side, int down) const {
		return Vector2i(side * side_x + down * down_x, side * side_y + down * down_y);
	}
	// Components of a world vector along the side and down axes.
	int side_of(int x, int y) const { return x * side_x + y * side_y; }
	int down_of(int x, int y) const { return x * down_x + y * down_y; }

	bool operator==(const GravityFrame &other) const {
		return side_x == other.side_x && side_y == other.side_y && down_x == other.down_x && down_y == other.down_y;
	}
};

// Gravity at one cell.
struct GravitySample {
	// Index in GravityField::get_sources() of the source pulling the cell,
	// or -1 outside every source.
	int source = -1;
	// No gravity at all: outside every source, with gravity outside sources
	// turned off. Loose material then only drifts by its velocity.
	bool weightless = false;
	// The true direction of the pull (a unit vector) and its strength as a
	// multiple of the world's gravity.
	double dir_x = 0.0, dir_y = 1.0;
	double strength = 1.0;
	// The pull's direction rounded to a quarter turn, for the movement rules.
	GravityFrame frame;
	// Squared distance to the source's position (0 outside every source).
	std::int64_t distance_squared = 0;
};

// Point gravity sources. A source pulls everything within its radius toward
// its position. Where sources overlap, the strongest pull wins: the source
// with the smallest distance divided by strength. Outside every source,
// everything falls toward +y, or (with gravity outside sources off) floats.
//
// Movement rules turn in quarter turns (see GravityFrame), along whichever
// axis points at the source most directly, but "height" is the true distance
// to the source (see level()): fluids spread sideways only along a level and
// falls must get closer, so seas settle into rings round a source.
class GravityField {
public:
	struct Source {
		int id = 0;
		Vector2i position;
		// Reach in cells; 0 reaches the whole world.
		int radius = 0;
		// Multiple of the world's gravity; above 0.
		double strength = 1.0;
	};

	// Returns the new source's id.
	int add_source(Vector2i position, int radius, double strength = 1.0);
	bool remove_source(int id);
	void clear() { sources.clear(); }
	bool empty() const { return sources.empty(); }
	const std::vector<Source> &get_sources() const { return sources; }

	void set_gravity_outside_sources(bool enabled) { gravity_outside_sources = enabled; }
	bool get_gravity_outside_sources() const { return gravity_outside_sources; }

	// Everything falls toward +y: no sources, and gravity outside them.
	bool is_uniform() const { return sources.empty() && gravity_outside_sources; }

	GravitySample sample(int world_x, int world_y) const;
	// The pull at a point in cell units (cell (x, y) covers [x, x + 1)),
	// for bodies that move smoothly: measured from the source cell's
	// center. Only the direction, strength, source and weightless fields
	// are filled in.
	GravitySample sample_point(double x, double y) const;

	std::int64_t distance_squared(int source, int world_x, int world_y) const {
		const std::int64_t dx = static_cast<std::int64_t>(sources[source].position.x) - world_x;
		const std::int64_t dy = static_cast<std::int64_t>(sources[source].position.y) - world_y;
		return dx * dx + dy * dy;
	}

	// Whole cells of distance: cells on the same level count as level ground.
	static std::int64_t level(std::int64_t distance_squared);

	// Ordering key for simulating regions "lowest first", so falling
	// material clears space below before what is above it moves: the
	// distance to the source pulling a point, or its height (-y) outside
	// every source, which sorts after every source.
	struct Height {
		int zone = 0;
		std::int64_t value = 0;
		bool operator<(const Height &other) const {
			return zone != other.zone ? zone < other.zone : value < other.value;
		}
		bool operator!=(const Height &other) const { return zone != other.zone || value != other.value; }
	};
	Height height_at(int world_x, int world_y) const;

private:
	// Index of the source pulling the point (offset from source positions
	// by `center`, 0 for cells and 0.5 for points), or -1.
	int pulling(double x, double y, double center) const;

	std::vector<Source> sources;
	bool gravity_outside_sources = true;
	int next_id = 1;
};
