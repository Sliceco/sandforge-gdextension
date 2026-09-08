#pragma once

#include "godot_cpp/classes/ref_counted.hpp"
#include "godot_cpp/classes/wrapped.hpp"
#include "godot_cpp/variant/color.hpp"
#include "godot_cpp/variant/vector2i.hpp"
#include "godot_cpp/variant/packed_byte_array.hpp"

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
	
	// Material configuration
	void add_material(int id, const String &name, int state, Color color, int density, int dispersion, int flammability, int acid_reactive);
	void set_materials_from_dict(const Dictionary &materials_dict);
	
	// Particle operations
	void set_particle(Vector2i pos, int mat_id);
	int get_particle_mat_id(Vector2i pos) const;
	
	// Brush operations (Phase 4)
	void brush_line(Vector2i from, Vector2i to, int mat_id, int radius);
	void brush_circle(Vector2i pos, int radius, int mat_id);
	void brush_rectangle(Vector2i pos, Vector2i size, int mat_id);
	void explosion(Vector2i pos, int radius);
	
	// Rendering
	PackedByteArray render_to_texture(Vector2i texture_size, Vector2i world_offset);
	
	// Simulation
	void tick();
	
	// Debug/Info
	int get_chunk_count() const;

private:
	WorldGrid world_grid;
};
