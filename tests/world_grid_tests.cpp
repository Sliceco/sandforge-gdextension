#include <cstdlib>
#include <iostream>
#include <vector>

#include "material_registry.h"
#include "materialconfig.h"
#include "world_grid.h"

namespace {
constexpr int STONE = 1;
constexpr int SAND = 2;
constexpr int WATER = 3;
constexpr int ACID = 4;
constexpr int WOOD = 5;
constexpr int EMBER = 6;
constexpr int BRICK = 7;
constexpr int SMOKE = 8;

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

void configure_materials(WorldGrid &world) {
	MaterialRegistry &materials = world.get_material_registry();
	materials.set({ STONE, MatterState::SOLID_FIXED, Color(), 100, 0 });
	materials.set({ SAND, MatterState::SOLID_POWDER, Color(), 50, 0 });
	materials.set({ WATER, MatterState::LIQUID, Color(), 30, 4 });
	materials.set({ ACID, MatterState::LIQUID, Color(), 45, 3 });
	materials.set({ WOOD, MatterState::SOLID_FIXED, Color(), 100, 0 });
	materials.set({ EMBER, MatterState::SOLID_FIXED, Color(), 100, 0 });
	materials.set({ BRICK, MatterState::SOLID_FIXED, Color(), 100, 0 });
	materials.set({ SMOKE, MatterState::GAS, Color(), 1, 3, 2, 0 });
}

void test_vertical_crossing_updates_once() {
	WorldGrid world;
	configure_materials(world);
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
	configure_materials(world);
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
	configure_materials(world);
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
	configure_materials(world);
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
void test_swapped_particle_keeps_moving() {
	WorldGrid world;
	configure_materials(world);
	for (int x = 0; x <= 10; ++x) {
		world.set_particle(x, 10, particle(STONE));
	}
	world.set_particle(4, 9, particle(STONE));
	world.set_particle(7, 9, particle(STONE));
	world.set_particle(6, 9, particle(WATER));
	world.set_particle(5, 8, particle(SAND));

	// Water slides into (5, 9) and the sand above then swaps with it, pushing
	// the already-moved water up to (5, 8). It must still fall into (6, 9).
	for (int tick = 0; tick < 5; ++tick) {
		world.tick();
	}
	expect_material(world, 5, 9, SAND, "sand must sink below water");
	expect_material(world, 6, 9, WATER, "water swapped after moving must not stay frozen");
}
void test_liquid_does_not_pass_through_walls() {
	WorldGrid world;
	configure_materials(world);
	for (int x = 0; x <= 20; ++x) {
		world.set_particle(x, 10, particle(STONE));
	}
	world.set_particle(8, 9, particle(STONE));
	world.set_particle(10, 9, particle(STONE));
	world.set_particle(9, 9, particle(WATER));

	for (int tick = 0; tick < 20; ++tick) {
		world.tick();
		expect_material(world, 9, 9, WATER, "water in a 1-cell pocket must not disperse through its walls");
	}
}

void test_reaction_rule_applies() {
	WorldGrid world;
	configure_materials(world);
	world.get_material_registry().add_reaction(WOOD, { ACID, 0, ACID, 255 });
	world.set_particle(5, 5, particle(WOOD));
	world.set_particle(6, 5, particle(ACID));

	world.tick();
	expect_material(world, 5, 5, 0, "wood touching acid must dissolve");
}

void test_probabilistic_reaction_does_not_stall() {
	WorldGrid world;
	configure_materials(world);
	world.get_material_registry().add_reaction(WOOD, { ACID, 0, ACID, 2 });
	for (int x = 0; x <= 4; ++x) {
		world.set_particle(x, 10, particle(BRICK));
	}
	world.set_particle(0, 9, particle(BRICK));
	world.set_particle(4, 9, particle(BRICK));
	world.set_particle(1, 9, particle(ACID));
	world.set_particle(2, 9, particle(ACID));
	world.set_particle(3, 9, particle(WOOD));

	// Nothing can move, so only the pending reaction keeps the chunk awake.
	for (int tick = 0; tick < 3000; ++tick) {
		world.tick();
	}
	if (world.get_particle_readonly(3, 9).mat_id == WOOD) {
		std::cerr << "a low-chance reaction must keep retrying after the chunk would sleep\n";
		std::exit(EXIT_FAILURE);
	}
}

void test_decay_does_not_stall() {
	WorldGrid world;
	configure_materials(world);
	for (int x = 0; x <= 2; ++x) {
		world.set_particle(x, 0, particle(BRICK));
		world.set_particle(x, 2, particle(BRICK));
	}
	world.set_particle(0, 1, particle(BRICK));
	world.set_particle(2, 1, particle(BRICK));
	world.set_particle(1, 1, particle(SMOKE));

	for (int tick = 0; tick < 3000; ++tick) {
		world.tick();
	}
	expect_material(world, 1, 1, 0, "trapped smoke must eventually decay");
}

void test_reactions_do_not_chain_within_a_tick() {
	for (int ember_x : { 0, 31 }) {
		WorldGrid world;
		configure_materials(world);
		world.get_material_registry().add_reaction(WOOD, { EMBER, EMBER, EMBER, 255 });
		for (int x = 1; x <= 30; ++x) {
			world.set_particle(x, 5, particle(WOOD));
		}
		world.set_particle(ember_x, 5, particle(EMBER));

		world.tick();
		const int step = ember_x == 0 ? 1 : -1;
		expect_material(world, ember_x + step, 5, EMBER, "wood next to an ember must ignite");
		expect_material(world, ember_x + 2 * step, 5, WOOD, "fire must spread at most one cell per tick in either direction");
	}
}

void test_same_seed_is_deterministic() {
	WorldGrid worlds[2];
	for (WorldGrid &world : worlds) {
		configure_materials(world);
		world.set_seed(1234);
		for (int x = 0; x <= 40; ++x) {
			world.set_particle(x, 30, particle(STONE));
		}
		for (int y = 0; y < 10; ++y) {
			for (int x = 15; x < 25; ++x) {
				world.set_particle(x, y, particle(y % 2 == 0 ? WATER : SAND));
			}
		}
		for (int tick = 0; tick < 60; ++tick) {
			world.tick();
		}
	}
	for (int y = 0; y < 30; ++y) {
		for (int x = 0; x <= 40; ++x) {
			if (worlds[0].get_particle_readonly(x, y).mat_id != worlds[1].get_particle_readonly(x, y).mat_id) {
				std::cerr << "worlds with the same seed diverged at (" << x << ", " << y << ")\n";
				std::exit(EXIT_FAILURE);
			}
		}
	}
}
} // namespace

int main() {
	test_vertical_crossing_updates_once();
	test_diagonal_crossing_updates_once();
	test_sand_column_settles_without_holes();
	test_boundary_crossing_sand_column_stays_contiguous();
	test_swapped_particle_keeps_moving();
	test_liquid_does_not_pass_through_walls();
	test_reaction_rule_applies();
	test_probabilistic_reaction_does_not_stall();
	test_decay_does_not_stall();
	test_reactions_do_not_chain_within_a_tick();
	test_same_seed_is_deterministic();
	return EXIT_SUCCESS;
}
