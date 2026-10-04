#pragma once

#include "godot_cpp/classes/ref_counted.hpp"
#include "godot_cpp/classes/wrapped.hpp"
#include "godot_cpp/variant/color.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/packed_byte_array.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "godot_cpp/variant/vector2i.hpp"

#include "world_grid.h"

using namespace godot;

class SandWorld : public RefCounted {
	GDCLASS(SandWorld, RefCounted)

protected:
	static void _bind_methods();

public:
	SandWorld();
	~SandWorld() override = default;

	// World management
	void clear();
	PackedByteArray save_snapshot() const;
	bool load_snapshot(const PackedByteArray &snapshot);

	// Material configuration. A new world holds the built-in default
	// materials until the first add_material(), set_materials_from_dict() or
	// load_materials_json() call, which clears them before registering.
	void add_material(int id, const String &name, int state, Color color, int density, int dispersion, int decay_chance = 0, int decay_into = 0);
	void set_materials_from_dict(const Dictionary &materials_dict);
	bool load_materials_json(const String &json);
	// Replaces all materials and reactions with the built-in defaults, which
	// then stay when more materials are added (to extend them).
	void load_default_materials();
	static String get_default_materials_json();

	// Reactions: when `material` touches `other`, with probability
	// chance/256 per tick they become `material_into` and `other_into`.
	void add_reaction(int material, int other, int material_into, int other_into, int chance);
	void clear_reactions();

	// Particle operations
	void set_particle(Vector2i pos, int mat_id);
	int get_particle_mat_id(Vector2i pos) const;
	int get_particle_hp(Vector2i pos) const;
	// Returns the damage left over after breaking the particle (0 if it
	// absorbed the hit), so projectiles can carry it into the next cell.
	int damage_particle(Vector2i pos, int amount);

	// Brush operations (Phase 4)
	void brush_line(Vector2i from, Vector2i to, int mat_id, int radius);
	void brush_circle(Vector2i pos, int radius, int mat_id);
	void brush_rectangle(Vector2i pos, Vector2i size, int mat_id);
	void damage_circle(Vector2i pos, int radius, int amount);
	// Damage falls off linearly from `power` at the center; loose material
	// within twice the radius is also flung outward.
	void explosion(Vector2i pos, int radius, int power = 255);

	// Motion. Velocities are in cells per tick (+y is down).
	void apply_impulse(Vector2i pos, int radius, float strength);
	Vector2 get_particle_velocity(Vector2i pos) const;
	void set_particle_velocity(Vector2i pos, Vector2 velocity);
	void set_gravity(float gravity);
	float get_gravity() const;
	void set_max_fall_speed(float speed);
	float get_max_fall_speed() const;

	// Rendering
	PackedByteArray render_to_texture(Vector2i texture_size, Vector2i world_offset);

	// Simulation
	void tick();
	void set_seed(int64_t seed);

	// Debug/Info
	int get_chunk_count() const;

	// Returns one Dictionary per allocated chunk with keys:
	// "chunk_position" (Vector2i), "world_rect" (Rect2i, chunk bounds in
	// world coordinates), and "dirty_rect" (Rect2i, world-space bounds of
	// cells pending simulation next tick; zero-sized if the chunk is
	// asleep). Intended for debug overlays visualizing chunk/dirty-rect
	// state, not for gameplay logic.
	TypedArray<Dictionary> get_debug_chunk_info() const;

private:
	// Clears the materials a new world starts with, the first time the
	// user registers their own.
	void replace_default_materials();

	bool has_default_materials = false;

	// Outward speed (cells per tick) an explosion of power 255 gives loose
	// material at its center.
	static constexpr float EXPLOSION_MAX_PUSH = 5.0f;

	// Validates add_material() arguments into `config`; false (with an
	// error printed) if they are out of range.
	static bool make_material_config(int id, const String &name, int state, Color color, int density, int dispersion, int decay_chance, int decay_into, MaterialConfig &config);
	void add_reaction_rule(int material, int other, int material_into, int other_into, int chance, int damage);

	WorldGrid world_grid;
};
