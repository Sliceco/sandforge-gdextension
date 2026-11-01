#include "gravity_field.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace godot;

int GravityField::add_source(Vector2i position, int radius, double strength) {
	Source source;
	source.id = next_id++;
	source.position = position;
	source.radius = std::max(radius, 0);
	source.strength = strength > 0.0 ? strength : 1.0;
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

std::int64_t GravityField::level(std::int64_t distance_squared) {
	std::int64_t root = static_cast<std::int64_t>(std::sqrt(static_cast<double>(distance_squared)));
	// Correct the rounding of the double square root.
	while (root * root > distance_squared)
		--root;
	while ((root + 1) * (root + 1) <= distance_squared)
		++root;
	return root;
}

int GravityField::pulling(double x, double y, double center) const {
	int best = -1;
	double best_weighted = 0.0;
	for (int i = 0; i < static_cast<int>(sources.size()); ++i) {
		const Source &source = sources[i];
		const double dx = source.position.x + center - x;
		const double dy = source.position.y + center - y;
		const double d2 = dx * dx + dy * dy;
		if (source.radius > 0 && d2 > static_cast<double>(source.radius) * source.radius)
			continue;
		// Strongest pull: smallest distance / strength, compared squared.
		// Ties go to the source added first, so the result depends only on
		// the sources and the point.
		const double weighted = d2 / (source.strength * source.strength);
		if (best < 0 || weighted < best_weighted) {
			best = i;
			best_weighted = weighted;
		}
	}
	return best;
}

GravitySample GravityField::sample(int world_x, int world_y) const {
	GravitySample result;
	if (sources.empty() && gravity_outside_sources)
		return result;
	const int index = pulling(world_x, world_y, 0.0);
	if (index < 0) {
		if (!gravity_outside_sources) {
			result.weightless = true;
			result.dir_y = 0.0;
			result.strength = 0.0;
		}
		return result;
	}
	const Source &source = sources[index];
	const std::int64_t d2 = distance_squared(index, world_x, world_y);
	result.source = index;
	result.strength = source.strength;
	result.distance_squared = d2;
	const int dx = source.position.x - world_x;
	const int dy = source.position.y - world_y;
	if (d2 == 0) {
		// At the source itself: no pull either way. Keep the default frame
		// so the rules still have one.
		result.dir_y = 0.0;
		result.strength = 0.0;
		return result;
	}
	const double length = std::sqrt(static_cast<double>(d2));
	result.dir_x = dx / length;
	result.dir_y = dy / length;
	// On a diagonal, vertical wins, so each diagonal belongs to the quadrant
	// above or below the source.
	if (std::abs(dx) > std::abs(dy))
		result.frame = GravityFrame::from_down(dx > 0 ? 1 : -1, 0);
	else
		result.frame = GravityFrame::from_down(0, dy > 0 ? 1 : -1);
	return result;
}

GravityField::Height GravityField::height_at(int world_x, int world_y) const {
	Height height;
	const int index = pulling(world_x, world_y, 0.0);
	if (index < 0) {
		height.zone = static_cast<int>(sources.size());
		height.value = -static_cast<std::int64_t>(world_y);
		return height;
	}
	height.zone = index;
	height.value = distance_squared(index, world_x, world_y);
	return height;
}

GravitySample GravityField::sample_point(double x, double y) const {
	GravitySample result;
	if (sources.empty() && gravity_outside_sources)
		return result;
	const int index = pulling(x, y, 0.5);
	if (index < 0) {
		if (!gravity_outside_sources) {
			result.weightless = true;
			result.dir_y = 0.0;
			result.strength = 0.0;
		}
		return result;
	}
	const Source &source = sources[index];
	result.source = index;
	const double dx = source.position.x + 0.5 - x;
	const double dy = source.position.y + 0.5 - y;
	const double length = std::sqrt(dx * dx + dy * dy);
	if (length == 0.0) {
		result.dir_y = 0.0;
		result.strength = 0.0;
		return result;
	}
	result.strength = source.strength;
	result.dir_x = dx / length;
	result.dir_y = dy / length;
	return result;
}
