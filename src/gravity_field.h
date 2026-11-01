#pragma once

#include "godot_cpp/variant/vector2i.hpp"

#include <cstdint>
#include <vector>

using namespace godot;

// Which way is "down" for a cell. Movement rules are written for the
// default frame (down is +y, sideways is x) and read and write cells
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

// Point gravity sources. A cell within a source's radius falls toward the
// nearest one, along whichever axis points at it most directly, so the
// space around a source splits into four quadrants (a round body settles
// into a rounded square). Cells outside every source, and every cell when
// there are none, fall toward +y.
class GravityField {
public:
	struct Source {
		int id = 0;
		Vector2i position;
		// Reach in cells; 0 reaches the whole world.
		int radius = 0;
	};

	// Returns the new source's id.
	int add_source(Vector2i position, int radius);
	bool remove_source(int id);
	void clear() { sources.clear(); }
	bool empty() const { return sources.empty(); }
	const std::vector<Source> &get_sources() const { return sources; }

	GravityFrame frame_at(int world_x, int world_y) const {
		if (sources.empty())
			return GravityFrame();
		return frame_toward(nearest(world_x, world_y), world_x, world_y);
	}

	// Ordering key for simulating regions "lowest first", so falling
	// material clears space below before what is above it moves: distance
	// from the source along its quadrant's axis inside a source, and height
	// (-y) outside every source, which always sorts after the inside.
	struct Height {
		bool outside = true;
		std::int64_t value = 0;
		bool operator<(const Height &other) const {
			return outside != other.outside ? !outside : value < other.value;
		}
		bool operator!=(const Height &other) const { return outside != other.outside || value != other.value; }
	};
	Height height_at(int world_x, int world_y) const;

private:
	// The nearest source reaching the cell, or nullptr.
	const Source *nearest(int world_x, int world_y) const;
	static GravityFrame frame_toward(const Source *source, int world_x, int world_y);

	std::vector<Source> sources;
	int next_id = 1;
};
