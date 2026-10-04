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
	return EXIT_SUCCESS;
}
