#include <algorithm>
#include <cmath>
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
	for (int material : { SAND, WATER, ACID }) {
		MaterialConfig config = materials.get(material);
		config.friction = material == SAND ? 64 : 16;
		materials.set(config);
	}
}

void fail(const char *message) {
	std::cerr << message << '\n';
	std::exit(EXIT_FAILURE);
}

bool any_chunk_active(const WorldGrid &world) {
	for (const WorldGrid::ChunkDebugInfo &info : world.get_debug_chunk_info()) {
		if (info.has_dirty_rect)
			return true;
	}
	return false;
}

int count_material(const WorldGrid &world, int min_x, int min_y, int max_x, int max_y, int material) {
	int count = 0;
	for (int y = min_y; y <= max_y; ++y) {
		for (int x = min_x; x <= max_x; ++x) {
			count += world.get_particle_readonly(x, y).mat_id == material;
		}
	}
	return count;
}

// Stone basin with walls at x = 0 and x = width + 1 and a floor at y = 40;
// water fills the bottom rows and leaves a partial top row, the layout that
// used to slide back and forth forever.
void build_partly_filled_basin(WorldGrid &world, int width, int full_rows, int extra) {
	for (int x = 0; x <= width + 1; ++x) {
		world.set_particle(x, 40, particle(STONE));
	}
	for (int y = 20; y < 40; ++y) {
		world.set_particle(0, y, particle(STONE));
		world.set_particle(width + 1, y, particle(STONE));
	}
	for (int y = 40 - full_rows; y < 40; ++y) {
		for (int x = 1; x <= width; ++x) {
			world.set_particle(x, y, particle(WATER));
		}
	}
	for (int i = 0; i < extra; ++i) {
		world.set_particle(1 + i * 3, 39 - full_rows, particle(WATER));
	}
}

int tick_until_asleep(WorldGrid &world, int max_ticks) {
	for (int tick = 1; tick <= max_ticks; ++tick) {
		world.tick();
		if (!any_chunk_active(world))
			return tick;
	}
	return -1;
}

void test_still_pool_settles_and_sleeps() {
	WorldGrid world;
	configure_materials(world);
	build_partly_filled_basin(world, 100, 3, 20);
	const int water = count_material(world, 1, 20, 100, 39, WATER);

	if (tick_until_asleep(world, 2000) < 0)
		fail("a pool with a partial top row must settle and let its chunks sleep");
	if (count_material(world, 1, 20, 100, 39, WATER) != water)
		fail("settling must not create or destroy water");
	// The partial row stays on top of the full ones: nothing piles up.
	if (count_material(world, 1, 20, 100, 35, WATER) != 0)
		fail("settled water must stay level");
}

void test_settled_pool_flows_into_an_opened_gap() {
	WorldGrid world;
	configure_materials(world);
	build_partly_filled_basin(world, 60, 3, 10);
	// An empty lower basin under and to the right of the pool.
	for (int x = 0; x <= 91; ++x) {
		world.set_particle(x, 45, particle(STONE));
	}
	for (int y = 30; y < 45; ++y) {
		world.set_particle(0, y, particle(STONE));
		world.set_particle(91, y, particle(STONE));
	}
	if (tick_until_asleep(world, 2000) < 0)
		fail("pool must settle before the wall is opened");

	// Breach the wall at the top of the pool, far from most of the water.
	world.set_particle(61, 36, particle(0));
	world.set_particle(61, 37, particle(0));
	if (tick_until_asleep(world, 4000) < 0)
		fail("drained pool must settle again");
	// The breach exposes the top full row and the partial row above it.
	if (count_material(world, 1, 41, 90, 44, WATER) < 60)
		fail("settled water must flow out through a newly opened gap");
}

void test_settling_is_reset_by_falling() {
	WorldGrid world;
	configure_materials(world);
	build_partly_filled_basin(world, 30, 2, 5);
	if (tick_until_asleep(world, 2000) < 0)
		fail("pool must settle");
	// Drain the basin through its floor: every particle has to fall again.
	for (int x = 1; x <= 30; ++x) {
		world.set_particle(x, 40, particle(0));
	}
	for (int x = -10; x <= 50; ++x) {
		world.set_particle(x, 60, particle(STONE));
	}
	for (int y = 41; y < 60; ++y) {
		world.set_particle(-10, y, particle(STONE));
		world.set_particle(50, y, particle(STONE));
	}
	if (tick_until_asleep(world, 4000) < 0)
		fail("drained water must settle again");
	if (count_material(world, 1, 20, 30, 39, WATER) != 0)
		fail("settled water must still fall when its floor is removed");
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

void expect(bool condition, const char *message) {
	if (!condition) {
		std::cerr << message << '\n';
		std::exit(EXIT_FAILURE);
	}
}

// Stone that takes 100 damage to break and crumbles into sand.
void configure_hard_stone(WorldGrid &world) {
	configure_materials(world);
	MaterialConfig stone = world.get_material_registry().get(STONE);
	stone.max_hp = 100;
	stone.break_into = SAND;
	world.get_material_registry().set(stone);
}

void test_hp_loss_wears_out_material() {
	WorldGrid world;
	configure_materials(world);
	MaterialConfig ember = world.get_material_registry().get(EMBER);
	ember.max_hp = 3;
	ember.hp_loss_chance = 255;
	ember.break_into = BRICK;
	world.get_material_registry().set(ember);
	world.set_particle(5, 5, world.make_particle(EMBER));
	expect(world.get_particle_readonly(5, 5).hp == 3, "new particles must start at the material's max_hp");

	// Isolated and unable to move, only its lifetime keeps the chunk awake.
	for (int tick = 0; tick < 3000 && world.get_particle_readonly(5, 5).mat_id == EMBER; ++tick) {
		world.tick();
	}
	expect_material(world, 5, 5, BRICK, "material losing hp over time must turn into break_into");
	expect(world.get_particle_readonly(5, 5).hp == 0, "break products must start at their own max_hp");
}

void test_reacting_without_changing_material_keeps_wearing_out() {
	WorldGrid world;
	configure_materials(world);
	MaterialConfig ember = world.get_material_registry().get(EMBER);
	ember.max_hp = 20;
	ember.hp_loss_chance = 255;
	ember.break_into = BRICK;
	world.get_material_registry().set(ember);
	// Like burning wood igniting the air around it: the ember stays an ember.
	world.get_material_registry().add_reaction(EMBER, { 0, EMBER, SMOKE, 255 });
	world.set_particle(5, 5, world.make_particle(EMBER));

	for (int tick = 0; tick < 200 && world.get_particle_readonly(5, 5).mat_id == EMBER; ++tick) {
		world.tick();
	}
	expect_material(world, 5, 5, BRICK, "reactions that keep a particle's material must not refill its hp");
}

void test_damage_particle_absorbs_and_breaks() {
	WorldGrid world;
	configure_hard_stone(world);
	world.set_particle(5, 5, world.make_particle(STONE));

	expect(world.damage_particle(5, 5, 60) == 0, "a particle surviving a hit must absorb all of it");
	expect(world.get_particle_readonly(5, 5).hp == 40, "damage must reduce hp");
	expect_material(world, 5, 5, STONE, "a damaged particle must keep its material");

	expect(world.damage_particle(5, 5, 50) == 10, "breaking a particle must return the leftover damage");
	expect_material(world, 5, 5, SAND, "a broken particle must become break_into");

	expect(world.damage_particle(5, 5, 7) == 6, "fragile material must cost 1 damage to break");
	expect_material(world, 5, 5, 0, "fragile material without break_into must be destroyed");
	expect(world.damage_particle(5, 5, 7) == 7, "empty cells must not absorb damage");
}

void test_reaction_damage_erodes_neighbor() {
	WorldGrid world;
	configure_hard_stone(world);
	world.get_material_registry().add_reaction(ACID, { STONE, ACID, 0, 255, 40 });
	for (int x = 4; x <= 6; ++x) {
		world.set_particle(x, 6, particle(BRICK));
	}
	world.set_particle(4, 5, particle(BRICK));
	world.set_particle(6, 5, particle(BRICK));
	world.set_particle(5, 4, world.make_particle(STONE));
	world.set_particle(5, 5, particle(ACID));

	world.tick();
	expect_material(world, 5, 4, STONE, "a damaging reaction must not convert a neighbor that has hp left");
	expect(world.get_particle_readonly(5, 4).hp < 100, "a damaging reaction must reduce the neighbor's hp");

	for (int tick = 0; tick < 3000 && world.get_particle_readonly(5, 4).mat_id == STONE; ++tick) {
		world.tick();
	}
	expect(world.get_particle_readonly(5, 4).mat_id != STONE, "repeated reaction damage must break the neighbor");
}

void test_snapshot_keeps_hp_and_shade() {
	WorldGrid source;
	configure_hard_stone(source);
	Particle stone = source.make_particle(STONE);
	stone.shade = 17;
	source.set_particle(3, 3, stone);
	source.damage_particle(3, 3, 25);

	WorldGrid loaded;
	configure_hard_stone(loaded);
	expect(loaded.deserialize(source.serialize()), "snapshot must load");
	expect(loaded.get_particle_readonly(3, 3).hp == 75, "snapshots must keep particle hp");
	expect(loaded.get_particle_readonly(3, 3).shade == 17, "snapshots must keep particle shade");
}

void test_empty_writes_do_not_allocate_and_idle_chunks_free() {
	WorldGrid world;
	configure_materials(world);
	world.set_particle(500, 500, particle(0));
	if (world.get_chunk_count() != 0) {
		std::cerr << "erasing unallocated space must not allocate a chunk\n";
		std::exit(EXIT_FAILURE);
	}

	world.set_particle(5, 5, particle(STONE));
	world.set_particle(5, 5, particle(0));
	for (int i = 0; i < 3; ++i) {
		world.tick();
	}
	if (world.get_chunk_count() != 0) {
		std::cerr << "asleep empty chunks must be freed\n";
		std::exit(EXIT_FAILURE);
	}

	world.set_particle(5, 5, particle(STONE));
	for (int i = 0; i < 3; ++i) {
		world.tick();
	}
	if (world.get_chunk_count() != 1) {
		std::cerr << "chunks holding particles must be kept\n";
		std::exit(EXIT_FAILURE);
	}
}

void test_snapshot_remaps_by_name() {
	WorldGrid source;
	MaterialRegistry &sm = source.get_material_registry();
	sm.set({ 1, MatterState::SOLID_FIXED, Color(), 100, 0, 0, 0, "stone" });
	sm.set({ 2, MatterState::SOLID_POWDER, Color(), 50, 0, 0, 0, "sand" });
	source.set_particle(1, 1, particle(1));
	source.set_particle(70, -3, particle(2));
	std::vector<uint8_t> bytes = source.serialize();
	if (bytes != source.serialize()) {
		std::cerr << "identical worlds must serialize identically\n";
		std::exit(EXIT_FAILURE);
	}

	WorldGrid swapped;
	MaterialRegistry &wm = swapped.get_material_registry();
	wm.set({ 1, MatterState::SOLID_POWDER, Color(), 50, 0, 0, 0, "sand" });
	wm.set({ 2, MatterState::SOLID_FIXED, Color(), 100, 0, 0, 0, "stone" });
	if (!swapped.deserialize(bytes)) {
		std::cerr << "snapshot must load when names resolve\n";
		std::exit(EXIT_FAILURE);
	}
	expect_material(swapped, 1, 1, 2, "stone must remap to its new id");
	expect_material(swapped, 70, -3, 1, "sand must remap to its new id");

	WorldGrid missing;
	missing.get_material_registry().set({ 1, MatterState::SOLID_FIXED, Color(), 100, 0, 0, 0, "stone" });
	if (missing.deserialize(bytes) || missing.get_last_error().empty()) {
		std::cerr << "snapshot using an unknown material must fail with an error\n";
		std::exit(EXIT_FAILURE);
	}
}

int find_material_in_column(const WorldGrid &world, int x, int min_y, int max_y, int material) {
	for (int y = min_y; y <= max_y; ++y) {
		if (world.get_particle_readonly(x, y).mat_id == material)
			return y;
	}
	return -1;
}

void test_gravity_accelerates_falls() {
	WorldGrid world;
	configure_materials(world);
	for (int x = -50; x <= 50; ++x) {
		world.set_particle(x, 300, particle(STONE));
	}
	world.set_particle(0, 0, particle(SAND));
	for (int tick = 0; tick < 40; ++tick) {
		world.tick();
	}
	const int y = find_material_in_column(world, 0, 0, 299, SAND);
	// 1 cell per tick without velocity; accelerating at 0.25 cells/tick^2
	// to the 6 cells/tick cap covers well over that.
	if (y <= 100)
		fail("falling sand must accelerate beyond 1 cell per tick");
	if (y > 40 * 6)
		fail("falling sand must not exceed the maximum fall speed");

	for (int tick = 0; tick < 100; ++tick) {
		world.tick();
	}
	// A hard landing may scatter it a little sideways.
	if (count_material(world, -50, 299, 50, 299, SAND) != 1)
		fail("accelerated sand must stop on the floor, not pass through it");
	Particle landed;
	for (int x = -50; x <= 50; ++x) {
		if (world.get_particle_readonly(x, 299).mat_id == SAND)
			landed = world.get_particle_readonly(x, 299);
	}
	if (landed.vx != 0 || landed.vy != 0)
		fail("landed sand must come to rest");
	if (any_chunk_active(world))
		fail("resting sand must let its chunk sleep");
}

void test_max_fall_speed_limits_falls() {
	WorldGrid world;
	configure_materials(world);
	world.set_max_fall_speed(2 * Particle::VELOCITY_SCALE);
	world.set_particle(0, 0, particle(SAND));
	for (int tick = 0; tick < 50; ++tick) {
		world.tick();
	}
	const int y = find_material_in_column(world, 0, 0, 200, SAND);
	if (y < 60 || y > 100)
		fail("fall speed must level off at max_fall_speed");
}

void test_impulse_throws_and_friction_stops() {
	WorldGrid world;
	configure_materials(world);
	for (int x = -5; x <= 200; ++x) {
		world.set_particle(x, 50, particle(STONE));
	}
	world.set_particle(10, 49, particle(SAND));
	world.tick();
	// Push from the left: up and to the right.
	world.apply_impulse(8, 50, 4, 5 * Particle::VELOCITY_SCALE);
	const Particle thrown = world.get_particle_readonly(10, 49);
	if (thrown.vx <= 0 || thrown.vy >= 0)
		fail("an impulse must push particles away from its center");

	if (tick_until_asleep(world, 500) < 0)
		fail("thrown sand must come to rest");
	const int landed_x = [&] {
		for (int x = 0; x <= 200; ++x) {
			if (world.get_particle_readonly(x, 49).mat_id == SAND)
				return x;
		}
		return -1;
	}();
	if (landed_x <= 12)
		fail("thrown sand must travel sideways");
}

void test_fast_particles_do_not_tunnel() {
	WorldGrid world;
	configure_materials(world);
	for (int y = 0; y <= 20; ++y) {
		world.set_particle(-1, y, particle(STONE));
		world.set_particle(20, y, particle(STONE));
	}
	for (int x = -1; x <= 20; ++x) {
		world.set_particle(x, 21, particle(STONE));
	}
	world.set_particle(17, 20, particle(WATER));
	world.set_particle_velocity(17, 20, 127, 0);
	for (int tick = 0; tick < 50; ++tick) {
		world.tick();
	}
	if (count_material(world, 0, 0, 19, 20, WATER) != 1)
		fail("a fast particle must stop at a 1-cell wall, not jump it");
}

void test_hard_landing_splashes_liquid() {
	WorldGrid world;
	configure_materials(world);
	for (int x = -40; x <= 40; ++x) {
		world.set_particle(x, 200, particle(STONE));
	}
	world.set_particle(0, 0, particle(WATER));
	int max_vx = 0;
	for (int tick = 0; tick < 100; ++tick) {
		world.tick();
		for (int x = -40; x <= 40; ++x) {
			max_vx = std::max(max_vx, std::abs(static_cast<int>(world.get_particle_readonly(x, 199).vx)));
		}
	}
	if (max_vx == 0)
		fail("liquid landing hard must splash sideways");
}

void test_snapshot_keeps_velocity_and_loads_version_3() {
	WorldGrid source;
	configure_materials(source);
	source.set_particle(3, 3, particle(SAND));
	source.set_particle_velocity(3, 3, 20, -30);
	WorldGrid loaded;
	configure_materials(loaded);
	if (!loaded.deserialize(source.serialize()))
		fail("snapshot must load");
	const Particle p = loaded.get_particle_readonly(3, 3);
	if (p.vx != 20 || p.vy != -30)
		fail("snapshots must keep particle velocity");

	// Rewrite as a version 3 snapshot: 4 bytes per cell, no velocity.
	std::vector<uint8_t> v4 = source.serialize();
	const size_t cells = SandSimulationChunk::SIZE * SandSimulationChunk::SIZE;
	const size_t chunk_start = v4.size() - (8 + cells * 6);
	std::vector<uint8_t> v3(v4.begin(), v4.begin() + chunk_start + 8);
	v3[4] = 3;
	for (size_t i = 0; i < cells; ++i) {
		for (size_t b = 0; b < 4; ++b) {
			v3.push_back(v4[chunk_start + 8 + i * 6 + b]);
		}
	}
	WorldGrid old_loaded;
	configure_materials(old_loaded);
	if (!old_loaded.deserialize(v3))
		fail("version 3 snapshots must still load");
	const Particle old_p = old_loaded.get_particle_readonly(3, 3);
	if (old_p.mat_id != SAND || old_p.vx != 0 || old_p.vy != 0)
		fail("version 3 particles must load at rest");
}

void test_raised_water_levels_out() {
	WorldGrid world;
	configure_materials(world);
	for (int x = 39; x <= 201; ++x) {
		world.set_particle(x, 100, particle(STONE));
	}
	for (int y = 40; y < 100; ++y) {
		world.set_particle(39, y, particle(STONE));
		world.set_particle(201, y, particle(STONE));
	}
	for (int y = 90; y < 100; ++y) {
		for (int x = 40; x <= 200; ++x) {
			world.set_particle(x, y, particle(WATER));
		}
	}
	// A tall block dumped on one side of the pool, with a cliff face.
	for (int y = 70; y < 90; ++y) {
		for (int x = 140; x <= 200; ++x) {
			world.set_particle(x, y, particle(WATER));
		}
	}
	const int water = count_material(world, 40, 40, 200, 99, WATER);
	if (tick_until_asleep(world, 600) < 0)
		fail("a dumped block of water must level out and settle");
	if (count_material(world, 40, 40, 200, 99, WATER) != water)
		fail("levelling must not create or destroy water");
	// 161 columns: 10 full rows plus 1220 cells is 7 more full rows and a
	// partial one, so a level surface spans rows 82 and 83. A stray cell or
	// two may settle on the partial row far from its end; a step of the
	// dumped block would be hundreds.
	if (count_material(world, 40, 40, 200, 81, WATER) > 4)
		fail("levelled water must not keep a raised step");
	if (count_material(world, 40, 83, 200, 83, WATER) != 161)
		fail("levelled water must fill every row below its surface");
}

void test_falling_liquid_does_not_shoot_sideways() {
	WorldGrid world;
	configure_materials(world);
	// A tall block of water on a narrow ledge with open air on both sides.
	for (int x = 40; x <= 80; ++x) {
		world.set_particle(x, 100, particle(STONE));
	}
	for (int y = 40; y < 100; ++y) {
		for (int x = 40; x <= 80; ++x) {
			world.set_particle(x, y, particle(WATER));
		}
	}
	// It pours off the edges. The streams falling past the ledge are not a
	// pool at rest, so their tops must not be pushed out along the rows
	// below into thin air.
	// Spilling off the ledge spreads it a few dozen cells; being shot out
	// along whole rows would carry it hundreds.
	for (int tick = 0; tick < 60; ++tick) {
		world.tick();
	}
	if (count_material(world, -400, 0, -100, 300, WATER) + count_material(world, 220, 0, 500, 300, WATER) != 0)
		fail("falling liquid must not shoot out sideways");
}

void test_liquid_column_spreads_through_its_falling_sides() {
	WorldGrid world;
	configure_materials(world);
	// A walled basin with a shallow pool, and a tall column of water dropped
	// into it near one wall.
	for (int x = 0; x <= 215; ++x) {
		world.set_particle(x, 176, particle(STONE));
	}
	for (int y = 60; y < 176; ++y) {
		world.set_particle(0, y, particle(STONE));
		world.set_particle(215, y, particle(STONE));
	}
	for (int y = 140; y < 176; ++y) {
		for (int x = 1; x < 215; ++x) {
			world.set_particle(x, y, particle(WATER));
		}
	}
	for (int y = 15; y < 140; ++y) {
		for (int x = 25; x < 90; ++x) {
			world.set_particle(x, y, particle(WATER));
		}
	}
	// The column's sides pour down as it slumps. That falling water must not
	// wall the rest in: the column has to level out across the basin rather
	// than stand on the pool.
	for (int tick = 0; tick < 300; ++tick) {
		world.tick();
	}
	if (count_material(world, 1, 0, 215, 90, WATER) != 0)
		fail("a liquid column must level out across its basin");
	if (count_material(world, 150, 100, 215, 140, WATER) == 0)
		fail("a liquid column must raise the far side of its basin");
}

void test_particle_state_queries() {
	WorldGrid world;
	configure_materials(world);
	world.set_particle(0, 0, particle(STONE));
	world.set_particle(1, 0, particle(SAND));
	world.set_particle(2, 0, particle(WATER));
	world.set_particle(3, 0, particle(SMOKE));
	world.set_particle(4, 0, particle(99)); // never registered
	const int chunks = world.get_chunk_count();
	const MatterState expected[] = { MatterState::SOLID_FIXED, MatterState::SOLID_POWDER, MatterState::LIQUID, MatterState::GAS, MatterState::EMPTY, MatterState::EMPTY };
	for (int x = 0; x < 6; ++x) {
		if (world.get_particle_state(x, 0) != expected[x])
			fail("get_particle_state must report the cell's matter state");
	}
	if (!world.is_solid(0, 0) || !world.is_solid(1, 0) || world.is_solid(2, 0) || world.is_solid(5, 0))
		fail("is_solid must be true for fixed solids and powders only");
	if (world.get_particle_state(-500, 900) != MatterState::EMPTY || world.is_solid(-500, 900))
		fail("a cell in a missing chunk must read as empty");
	if (world.get_chunk_count() != chunks)
		fail("state queries must not allocate chunks");
}

void test_state_masks_span_chunk_seams() {
	WorldGrid world;
	configure_materials(world);
	// A rect crossing the chunk seams at x = -64, 0 and 64 and y = 0, with
	// material on both sides of each and a missing chunk inside it.
	for (int x : { -70, -65, -64, -1, 0, 63, 64, 69 }) {
		world.set_particle(x, -1, particle(STONE));
		world.set_particle(x, 0, particle(WATER));
		world.set_particle(x, 3, particle(SAND));
	}
	const int min_x = -70, min_y = -6, width = 140, height = 76;
	const int chunks = world.get_chunk_count();
	std::vector<std::uint8_t> states(width * height, 255), solids(width * height, 255), liquids(width * height, 255);
	world.get_state_mask(min_x, min_y, width, height, states.data());
	world.get_solid_mask(min_x, min_y, width, height, WorldGrid::STATE_MASK_SOLIDS, solids.data());
	world.get_solid_mask(min_x, min_y, width, height, WorldGrid::STATE_MASK_LIQUIDS, liquids.data());
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const MatterState state = world.get_particle_state(min_x + x, min_y + y);
			const int i = y * width + x;
			if (states[i] != (std::uint8_t)state)
				fail("get_state_mask must match get_particle_state cell by cell");
			if (solids[i] != (state == MatterState::SOLID_FIXED || state == MatterState::SOLID_POWDER))
				fail("get_solid_mask must mark solids with the default mask");
			if (liquids[i] != (state == MatterState::LIQUID))
				fail("get_solid_mask must honor a custom state mask");
		}
	}
	if (solids[(-1 - min_y) * width + (64 - min_x)] != 1 || liquids[(0 - min_y) * width + (-65 - min_x)] != 1)
		fail("get_solid_mask must see material on both sides of chunk seams");
	if (world.get_chunk_count() != chunks)
		fail("mask queries must not allocate chunks");
}

void test_raycast() {
	WorldGrid world;
	configure_materials(world);
	world.set_particle(-70, 3, particle(STONE));
	world.set_particle(70, 3, particle(STONE));
	world.set_particle(30, 3, particle(WATER));
	world.set_particle(5, -20, particle(BRICK));
	const int chunks = world.get_chunk_count();

	// Across chunk seams and negative coordinates, from either side; liquid
	// doesn't stop the default mask.
	WorldGrid::RaycastHit hit = world.raycast(-100.5, 3.5, 100.5, 3.5, WorldGrid::STATE_MASK_SOLIDS);
	if (!hit.hit || hit.x != -70 || hit.y != 3 || hit.mat_id != STONE || hit.normal_x != -1 || hit.normal_y != 0)
		fail("raycast must hit the first solid going right, facing back");
	hit = world.raycast(100.5, 3.5, -100.5, 3.5, WorldGrid::STATE_MASK_SOLIDS);
	if (!hit.hit || hit.x != 70 || hit.normal_x != 1 || hit.normal_y != 0)
		fail("raycast must hit the first solid going left, facing back");
	hit = world.raycast(5.5, 50.5, 5.5, -50.5, WorldGrid::STATE_MASK_SOLIDS);
	if (!hit.hit || hit.x != 5 || hit.y != -20 || hit.normal_x != 0 || hit.normal_y != 1)
		fail("raycast must hit going up across a chunk seam");

	// A custom mask stops at the liquid instead.
	hit = world.raycast(0.5, 3.5, 100.5, 3.5, WorldGrid::STATE_MASK_LIQUIDS);
	if (!hit.hit || hit.x != 30 || hit.mat_id != WATER)
		fail("raycast must honor a custom state mask");

	// Misses; the end cell itself counts.
	if (world.raycast(-60.5, 3.5, 60.5, 3.5, WorldGrid::STATE_MASK_SOLIDS).hit)
		fail("raycast must miss when nothing solid is on the segment");
	hit = world.raycast(60.5, 3.5, 70.2, 3.9, WorldGrid::STATE_MASK_SOLIDS);
	if (!hit.hit || hit.x != 70)
		fail("raycast must test the cell the segment ends in");

	// Starting inside a solid hits at once with no face.
	hit = world.raycast(70.5, 3.5, 90.5, 3.5, WorldGrid::STATE_MASK_SOLIDS);
	if (!hit.hit || hit.x != 70 || hit.normal_x != 0 || hit.normal_y != 0)
		fail("raycast from inside a solid must hit its own cell");

	// A diagonal wall one cell thick, crossed exactly through cell corners,
	// must not let the ray slip between two diagonal neighbors.
	for (int i = 0; i < 20; ++i) {
		world.set_particle(200 + i, 219 - i, particle(STONE));
	}
	hit = world.raycast(200.0, 200.0, 220.0, 220.0, WorldGrid::STATE_MASK_SOLIDS);
	if (!hit.hit || hit.x + hit.y != 419)
		fail("raycast must not pass through a diagonal wall");
	if (world.get_chunk_count() != chunks + 1)
		fail("raycast must not allocate chunks");
}

void test_gravity_field_quadrants() {
	GravityField field;
	expect(field.sample(5, 5).frame.is_default(), "without sources everything must fall toward +y");
	const int planet = field.add_source(Vector2i(0, 0), 100);
	const GravityFrame left = field.sample(-10, 3).frame;
	expect(left.down_x == 1 && left.down_y == 0, "a cell left of a source must fall toward +x");
	const GravityFrame below = field.sample(2, 10).frame;
	expect(below.down_x == 0 && below.down_y == -1, "a cell below a source must fall toward -y");
	const GravityFrame corner = field.sample(7, -7).frame;
	expect(corner.down_x == 0 && corner.down_y == 1, "a cell on a diagonal must fall vertically");
	expect(field.sample(150, 0).frame.is_default(), "a cell out of every source's reach must fall toward +y");
	// The frame is a quarter turn: side is down turned the same way as
	// (1, 0) is from (0, 1).
	expect(left.side_x == 0 && left.side_y == -1, "the side axis must turn with down");
	field.add_source(Vector2i(60, 0), 0);
	const GravityFrame near_second = field.sample(45, 0).frame;
	expect(near_second.down_x == 1, "a cell must fall toward the nearest source");
	expect(field.remove_source(planet) && !field.remove_source(planet), "a source must be removable once");
	expect(field.sample(-10, 3).frame.down_x == 1, "the remaining unlimited source reaches everywhere");
}

// test_raised_water_levels_out() turned a quarter: gravity toward +x, with
// the default world's (x, y) at (y, -x).
void test_sideways_gravity_levels_water() {
	WorldGrid world;
	configure_materials(world);
	world.get_gravity_field().add_source(Vector2i(100000, 0), 0);
	auto set = [&](int x, int y, int material) { world.set_particle(y, -x, particle(material)); };
	for (int x = 39; x <= 201; ++x)
		set(x, 100, STONE);
	for (int y = 40; y < 100; ++y) {
		set(39, y, STONE);
		set(201, y, STONE);
	}
	for (int y = 90; y < 100; ++y) {
		for (int x = 40; x <= 200; ++x)
			set(x, y, WATER);
	}
	for (int y = 70; y < 90; ++y) {
		for (int x = 140; x <= 200; ++x)
			set(x, y, WATER);
	}
	// Default (x, y) in [40, 200] x [lo, hi] is world [lo, hi] x [-200, -40].
	auto count = [&](int lo, int hi) { return count_material(world, lo, -200, hi, -40, WATER); };
	const int water = count(40, 99);
	if (tick_until_asleep(world, 600) < 0)
		fail("water under sideways gravity must level out and settle");
	if (count(40, 99) != water)
		fail("levelling under sideways gravity must not create or destroy water");
	if (count(40, 81) > 4)
		fail("water levelled sideways must not keep a raised step");
	if (count(83, 83) != 161)
		fail("water levelled sideways must fill every row below its surface");
}

void test_sand_falls_toward_gravity_source() {
	WorldGrid world;
	configure_materials(world);
	world.get_gravity_field().add_source(Vector2i(0, 0), 0);
	for (int y = -3; y <= 3; ++y) {
		for (int x = -3; x <= 3; ++x)
			world.set_particle(x, y, particle(STONE));
	}
	world.set_particle(30, 0, particle(SAND));
	world.set_particle(0, -30, particle(SAND));
	world.set_particle(-30, 1, particle(SAND));
	world.set_particle(1, 30, particle(SAND));
	if (tick_until_asleep(world, 200) < 0)
		fail("sand around a gravity source must come to rest");
	// A hard landing may scatter a grain sideways along the face it hits.
	expect(count_material(world, 4, -3, 4, 3, SAND) == 1, "sand right of a source must fall toward -x onto the core");
	expect(count_material(world, -3, -4, 3, -4, SAND) == 1, "sand above a source must fall toward +y onto the core");
	expect(count_material(world, -4, -3, -4, 3, SAND) == 1, "sand left of a source must fall toward +x onto the core");
	expect(count_material(world, -3, 4, 3, 4, SAND) == 1, "sand below a source must fall toward -y onto the core");
}

// Material poured round a core settles into its four quadrants and goes to
// sleep. Without care at the seams between quadrants, where down turns,
// liquid used to creep outward along them and grains to swap back and
// forth between two quadrants forever.
void test_material_round_a_source_settles() {
	WorldGrid world;
	configure_materials(world);
	world.get_gravity_field().add_source(Vector2i(0, 0), 0);
	int sand = 0, water = 0;
	for (int y = -90; y <= 90; ++y) {
		for (int x = -90; x <= 90; ++x) {
			const int d2 = x * x + y * y;
			if (d2 <= 20 * 20) {
				world.set_particle(x, y, particle(STONE));
			} else if (d2 <= 40 * 40 && (x + y * 3) % 7 == 0) {
				world.set_particle(x, y, particle(SAND));
				++sand;
			} else if (d2 > 60 * 60 && d2 <= 66 * 66) {
				world.set_particle(x, y, particle(WATER));
				++water;
			}
		}
	}
	if (tick_until_asleep(world, 1500) < 0)
		fail("material round a gravity source must settle and sleep");
	if (count_material(world, -200, -200, 200, 200, SAND) != sand || count_material(world, -200, -200, 200, 200, WATER) != water)
		fail("settling round a gravity source must not create or destroy material");
	// Everything ends up lower (closer, by the larger axis) than where the
	// water started.
	const int outside = count_material(world, -200, -200, 200, -67, WATER) + count_material(world, -200, 67, 200, 200, WATER) + count_material(world, -200, -66, -67, 66, WATER) + count_material(world, 67, -66, 200, 66, WATER);
	if (outside != 0)
		fail("liquid round a gravity source must not creep outward");
}

void test_gravity_field_zones() {
	GravityField field;
	const int left = field.add_source(Vector2i(-100, 0), 150);
	field.add_source(Vector2i(100, 0), 150);
	const GravitySample near_left = field.sample(-1, 0);
	expect(near_left.source == 0 && near_left.frame.down_x == -1, "between equal sources the nearer must pull");
	expect(field.sample(1, 0).source == 1, "past the midpoint the other source must pull");
	const GravitySample diagonal = field.sample(-70, -30);
	expect(std::abs(diagonal.dir_x + std::sqrt(0.5)) < 1e-9 && std::abs(diagonal.dir_y - std::sqrt(0.5)) < 1e-9, "the pull must point straight at the source");
	expect(diagonal.frame.down_y == 1, "the frame must follow the axis that points at the source most directly");
	// Twice as strong pulls from twice as far: the zones now meet at x = 33.
	field.remove_source(left);
	field.add_source(Vector2i(-100, 0), 150, 2.0);
	expect(field.get_sources()[field.sample(30, 0).source].strength == 2.0 && field.sample(36, 0).source == 0, "a stronger source must pull from further away");
	expect(field.sample(0, 400).source == -1 && !field.sample(0, 400).weightless, "outside every source, gravity must pull toward +y by default");
	field.set_gravity_outside_sources(false);
	expect(field.sample(0, 400).weightless, "outside every source, gravity outside sources off means no gravity");
	expect(!field.sample(-90, 0).weightless, "inside a source gravity must still pull");
}

// Water poured round a core must settle into a ring of even depth, not
// pool where two quadrants meet.
void test_water_round_a_source_settles_into_a_ring() {
	WorldGrid world;
	configure_materials(world);
	world.get_gravity_field().add_source(Vector2i(0, 0), 0);
	int water = 0;
	for (int y = -80; y <= 80; ++y) {
		for (int x = -80; x <= 80; ++x) {
			const int d2 = x * x + y * y;
			if (d2 <= 20 * 20) {
				world.set_particle(x, y, particle(STONE));
			} else if (d2 > 50 * 50 && d2 <= 56 * 56 && x > -20) {
				// A lopsided shell, so levelling has to move water round.
				world.set_particle(x, y, particle(WATER));
				++water;
			}
		}
	}
	if (tick_until_asleep(world, 3000) < 0)
		fail("water round a gravity source must settle and sleep");
	if (count_material(world, -100, -100, 100, 100, WATER) != water)
		fail("levelling round a gravity source must not create or destroy water");
	// The outermost water along each of 72 directions.
	int lowest = 1000, highest = 0;
	for (int i = 0; i < 72; ++i) {
		const double angle = i * 3.14159265358979 / 36.0;
		int surface = 0;
		for (int r = 20; r < 100; ++r) {
			const int x = static_cast<int>(std::lround(std::cos(angle) * r));
			const int y = static_cast<int>(std::lround(std::sin(angle) * r));
			if (world.get_particle_readonly(x, y).mat_id == WATER)
				surface = r;
		}
		lowest = std::min(lowest, surface);
		highest = std::max(highest, surface);
	}
	if (lowest < 20 || highest - lowest > 3)
		fail("water round a gravity source must level into a ring");
}

void test_sand_falls_along_the_pull() {
	WorldGrid world;
	configure_materials(world);
	world.get_gravity_field().add_source(Vector2i(0, 0), 0);
	for (int y = -10; y <= 10; ++y) {
		for (int x = -10; x <= 10; ++x) {
			if (x * x + y * y <= 100)
				world.set_particle(x, y, particle(STONE));
		}
	}
	// Dropped at about 40 degrees off the axis, it falls straight at the
	// source, not along the axis and then round.
	world.set_particle(45, -38, particle(SAND));
	if (tick_until_asleep(world, 300) < 0)
		fail("sand falling toward a source must come to rest");
	bool near_line = false;
	for (int y = -20; y <= 0; ++y) {
		for (int x = 0; x <= 20; ++x) {
			if (world.get_particle_readonly(x, y).mat_id == SAND)
				near_line = std::abs(std::atan2(-y, x) - std::atan2(38.0, 45.0)) < 0.25;
		}
	}
	if (!near_line)
		fail("sand must fall along the true direction of the pull");
}

void test_weightless_material_drifts() {
	WorldGrid world;
	configure_materials(world);
	world.get_gravity_field().set_gravity_outside_sources(false);
	world.set_particle(30, 0, particle(STONE));
	world.set_particle(0, 0, particle(SAND));
	world.set_particle(0, 10, particle(WATER));
	world.set_particle_velocity(0, 0, Particle::VELOCITY_SCALE, 0);
	for (int i = 0; i < 10; ++i)
		world.tick();
	expect_material(world, 10, 0, SAND, "weightless sand must drift by its velocity");
	expect_material(world, 0, 10, WATER, "weightless water at rest must stay put");
	if (tick_until_asleep(world, 100) < 0)
		fail("drifting material must come to rest when it hits something");
	expect_material(world, 29, 0, SAND, "drifting sand must stop at what it hits");
}

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
	test_empty_writes_do_not_allocate_and_idle_chunks_free();
	test_snapshot_remaps_by_name();
	test_hp_loss_wears_out_material();
	test_reacting_without_changing_material_keeps_wearing_out();
	test_damage_particle_absorbs_and_breaks();
	test_reaction_damage_erodes_neighbor();
	test_snapshot_keeps_hp_and_shade();
	test_still_pool_settles_and_sleeps();
	test_settled_pool_flows_into_an_opened_gap();
	test_settling_is_reset_by_falling();
	test_raised_water_levels_out();
	test_falling_liquid_does_not_shoot_sideways();
	test_liquid_column_spreads_through_its_falling_sides();
	test_gravity_accelerates_falls();
	test_max_fall_speed_limits_falls();
	test_impulse_throws_and_friction_stops();
	test_fast_particles_do_not_tunnel();
	test_hard_landing_splashes_liquid();
	test_snapshot_keeps_velocity_and_loads_version_3();
	test_particle_state_queries();
	test_state_masks_span_chunk_seams();
	test_raycast();
	test_gravity_field_quadrants();
	test_sideways_gravity_levels_water();
	test_sand_falls_toward_gravity_source();
	test_material_round_a_source_settles();
	test_gravity_field_zones();
	test_water_round_a_source_settles_into_a_ring();
	test_sand_falls_along_the_pull();
	test_weightless_material_drifts();
	return EXIT_SUCCESS;
}
