#include <cstdlib>
#include <iostream>
#include <vector>

#include "materialconfig.h"
#include "world_grid.h"

namespace {
constexpr int STONE = 1;
constexpr int SAND = 2;
constexpr int WATER = 3;

void expect_material(const WorldGrid &world, int x, int y, int material, const char *message) {
	if (world.get_particle_readonly(x, y).mat_id != material) {
		std::cerr << message << '\n';
		std::exit(EXIT_FAILURE);
	}
}

Particle particle(int material) {
	Particle value;
	value.mat_id = material;
	return value;
}

void configure_materials() {
	std::vector<MaterialConfig> materials(4);
	materials[STONE] = { STONE, MatterState::SOLID_FIXED, Color(), 100, 0, 0, 0 };
	materials[SAND] = { SAND, MatterState::SOLID_POWDER, Color(), 50, 0, 0, 0 };
	materials[WATER] = { WATER, MatterState::LIQUID, Color(), 30, 4, 0, 0 };
	WorldGrid::set_material_registry(materials);
}

void test_vertical_crossing_updates_once() {
	WorldGrid world;
	world.set_particle(0, 66, particle(STONE));
	world.tick();

	world.set_particle(0, 63, particle(SAND));
	world.tick();

	expect_material(world, 0, 64, SAND, "sand must enter an existing chunk exactly once");
	expect_material(world, 0, 65, 0, "sand must not move twice at a vertical chunk boundary");

	world.tick();
	expect_material(world, 0, 65, SAND, "sand in a previously inactive destination chunk must wake next tick");
}

void test_diagonal_crossing_updates_once() {
	WorldGrid world;
	world.set_particle(65, 3, particle(STONE));
	world.tick();

	world.set_particle(63, 1, particle(STONE));
	world.set_particle(62, 1, particle(STONE));
	world.set_particle(63, 0, particle(WATER));
	world.tick();

	expect_material(world, 64, 1, WATER, "water must enter an existing diagonal chunk exactly once");
	expect_material(world, 64, 2, 0, "water must not move twice at a diagonal chunk boundary");
}

void test_sand_column_settles_without_holes() {
	WorldGrid world;
	for (int y = 0; y <= 50; ++y) {
		world.set_particle(10, y, particle(STONE));
		world.set_particle(20, y, particle(STONE));
	}
	for (int x = 10; x <= 20; ++x) {
		world.set_particle(x, 50, particle(STONE));
	}
	for (int y = 0; y < 20; ++y) {
		for (int x = 11; x < 20; ++x) {
			world.set_particle(x, y, particle(SAND));
		}
	}

	for (int tick = 0; tick < 80; ++tick) {
		world.tick();
	}

	for (int x = 11; x < 20; ++x) {
		for (int y = 30; y < 50; ++y) {
			expect_material(world, x, y, SAND, "sand must settle without checkerboard holes");
		}
	}
}

void test_boundary_crossing_sand_column_stays_contiguous() {
	WorldGrid world;
	for (int y = 0; y <= 130; ++y) {
		world.set_particle(60, y, particle(STONE));
		world.set_particle(70, y, particle(STONE));
	}
	for (int x = 60; x <= 70; ++x) {
		world.set_particle(x, 130, particle(STONE));
	}
	for (int y = 0; y < 40; ++y) {
		for (int x = 61; x < 70; ++x) {
			world.set_particle(x, y, particle(SAND));
		}
	}

	for (int tick = 0; tick < 140; ++tick) {
		world.tick();

		for (int x = 61; x < 70; ++x) {
			bool found_sand = false;
			bool found_gap = false;
			for (int y = 0; y < 130; ++y) {
				const int material = world.get_particle_readonly(x, y).mat_id;
				if (material == SAND) {
					if (found_gap) {
						std::cerr << "sand gap at tick " << tick << ", (" << x << ", " << y << ")\n";
						std::exit(EXIT_FAILURE);
					}
					found_sand = true;
				} else if (found_sand) {
					found_gap = true;
				}
			}
		}
	}
}
} // namespace

int main() {
	configure_materials();
	test_vertical_crossing_updates_once();
	test_diagonal_crossing_updates_once();
	test_sand_column_settles_without_holes();
	test_boundary_crossing_sand_column_stays_contiguous();
	return EXIT_SUCCESS;
}
