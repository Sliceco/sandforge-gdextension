#include "gravity_field.h"

#include <algorithm>
#include <cstdlib>

using namespace godot;

int GravityField::add_source(Vector2i position, int radius) {
	Source source;
	source.id = next_id++;
	source.position = position;
	source.radius = std::max(radius, 0);
	sources.push_back(source);
	return source.id;
}

bool GravityField::remove_source(int id) {
	auto it = std::find_if(sources.begin(), sources.end(), [id](const Source &source) { return source.id == id; });
	if (it == sources.end())
		return false;
	sources.erase(it);
	return true;
}

const GravityField::Source *GravityField::nearest(int world_x, int world_y) const {
	const Source *best = nullptr;
	std::int64_t best_distance = 0;
	for (const Source &source : sources) {
		const std::int64_t dx = source.position.x - world_x;
		const std::int64_t dy = source.position.y - world_y;
		const std::int64_t distance = dx * dx + dy * dy;
		if (source.radius > 0 && distance > static_cast<std::int64_t>(source.radius) * source.radius)
			continue;
		// Ties go to the source added first, so the result never depends
		// on anything but the sources and the cell.
		if (best == nullptr || distance < best_distance) {
			best = &source;
			best_distance = distance;
		}
	}
	return best;
}

GravityFrame GravityField::frame_toward(const Source *source, int world_x, int world_y) {
	if (source == nullptr)
		return GravityFrame();
	const int dx = source->position.x - world_x;
	const int dy = source->position.y - world_y;
	// On a diagonal, vertical wins, so each diagonal belongs to the quadrant
	// above or below the source. The source's own cell keeps the default.
	if (std::abs(dx) > std::abs(dy))
		return GravityFrame::from_down(dx > 0 ? 1 : -1, 0);
	if (dy != 0)
		return GravityFrame::from_down(0, dy > 0 ? 1 : -1);
	return GravityFrame();
}

GravityField::Height GravityField::height_at(int world_x, int world_y) const {
	Height height;
	const Source *source = nearest(world_x, world_y);
	if (source == nullptr) {
		height.value = -static_cast<std::int64_t>(world_y);
		return height;
	}
	height.outside = false;
	height.value = std::max(std::abs(static_cast<std::int64_t>(source->position.x) - world_x), std::abs(static_cast<std::int64_t>(source->position.y) - world_y));
	return height;
}
