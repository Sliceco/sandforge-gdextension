#include "sand_world.h"

#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/string.hpp"

void SandWorld::_bind_methods() {
	ClassDB::bind_method(D_METHOD("clear"), &SandWorld::clear);
	ClassDB::bind_method(D_METHOD("add_material", "id", "name", "state", "color", "density", "dispersion", "flammability", "acid_reactive"), 
		&SandWorld::add_material);
	ClassDB::bind_method(D_METHOD("set_materials_from_dict", "materials"), &SandWorld::set_materials_from_dict);
	ClassDB::bind_method(D_METHOD("set_particle", "pos", "mat_id"), &SandWorld::set_particle);
	ClassDB::bind_method(D_METHOD("get_particle_mat_id", "pos"), &SandWorld::get_particle_mat_id);
	ClassDB::bind_method(D_METHOD("brush_line", "from", "to", "mat_id", "radius"), &SandWorld::brush_line);
	ClassDB::bind_method(D_METHOD("brush_circle", "pos", "radius", "mat_id"), &SandWorld::brush_circle);
	ClassDB::bind_method(D_METHOD("brush_rectangle", "pos", "size", "mat_id"), &SandWorld::brush_rectangle);
	ClassDB::bind_method(D_METHOD("explosion", "pos", "radius"), &SandWorld::explosion);
	ClassDB::bind_method(D_METHOD("render_to_texture", "texture_size", "world_offset"), &SandWorld::render_to_texture);
	ClassDB::bind_method(D_METHOD("tick"), &SandWorld::tick);
	ClassDB::bind_method(D_METHOD("get_chunk_count"), &SandWorld::get_chunk_count);
}

SandWorld::SandWorld() {
	// Initialize with default materials
	auto &registry = SandSimulationChunk::mat_registry;
	if (registry.empty()) {
		registry.resize(256);  // Support up to 256 material types
		
		// Empty (ID 0) - already at default
		
		// Stone (ID 1)
		registry[1] = MaterialConfig{
			.id = 1,
			.state = MatterState::SOLID_FIXED,
			.color = Color(0.5f, 0.5f, 0.5f, 1.0f),
			.density = 100,
			.dispersion = 0,
			.flammability = 0,
			.acid_reactive = 0
		};
		
		// Sand (ID 2)
		registry[2] = MaterialConfig{
			.id = 2,
			.state = MatterState::SOLID_POWDER,
			.color = Color(0.9f, 0.85f, 0.6f, 1.0f),
			.density = 50,
			.dispersion = 0,
			.flammability = 0,
			.acid_reactive = 0
		};
		
		// Water (ID 3)
		registry[3] = MaterialConfig{
			.id = 3,
			.state = MatterState::LIQUID,
			.color = Color(0.2f, 0.4f, 0.8f, 1.0f),
			.density = 30,
			.dispersion = 4,
			.flammability = 0,
			.acid_reactive = 0
		};
	}
}

void SandWorld::clear() {
	world_grid.clear();
}

void SandWorld::add_material(int id, const String &name, int state, Color color, int density, int dispersion, int flammability, int acid_reactive) {
	auto &registry = SandSimulationChunk::mat_registry;
	
	// Ensure registry is large enough
	if ((size_t)id >= registry.size()) {
		registry.resize(id + 1);
	}
	
	registry[id] = MaterialConfig{
		.id = (uint8_t)id,
		.state = (MatterState)state,
		.color = color,
		.density = (uint8_t)density,
		.dispersion = (uint8_t)dispersion,
		.flammability = (uint8_t)flammability,
		.acid_reactive = (uint8_t)acid_reactive
	};
}

void SandWorld::set_materials_from_dict(const Dictionary &materials_dict) {
	for (int i = 0; i < materials_dict.size(); ++i) {
		Variant key = materials_dict.keys()[i];
		Variant value = materials_dict[key];
		
		if (!key.is_num())
			continue;
		
		int mat_id = (int)key;
		if (value.get_type() != Variant::DICTIONARY)
			continue;
		
		Dictionary mat_dict = value;
		
		int state = mat_dict.get("state", (int)MatterState::EMPTY);
		Color color = mat_dict.get("color", Color(1, 1, 1, 1));
		int density = mat_dict.get("density", 0);
		int dispersion = mat_dict.get("dispersion", 0);
		int flammability = mat_dict.get("flammability", 0);
		int acid_reactive = mat_dict.get("acid_reactive", 0);
		
		add_material(mat_id, "", state, color, density, dispersion, flammability, acid_reactive);
	}
}

void SandWorld::set_particle(Vector2i pos, int mat_id) {
	Particle p;
	p.mat_id = mat_id;
	p.flags = ParticleFlags::PARTICLE_FLAG_NONE;
	world_grid.set_particle(pos.x, pos.y, p);
}

int SandWorld::get_particle_mat_id(Vector2i pos) const {
	Particle p = world_grid.get_particle_readonly(pos.x, pos.y);
	return p.mat_id;
}

PackedByteArray SandWorld::render_to_texture(Vector2i texture_size, Vector2i world_offset) {
	PackedByteArray buffer;
	int pixel_count = texture_size.x * texture_size.y;
	buffer.resize(pixel_count * 4);  // RGBA8888
	
	auto &registry = SandSimulationChunk::mat_registry;
	uint8_t *data = buffer.ptrw();
	
	for (int y = 0; y < texture_size.y; ++y) {
		for (int x = 0; x < texture_size.x; ++x) {
			int world_x = world_offset.x + x;
			int world_y = world_offset.y + y;
			
			Particle p = world_grid.get_particle_readonly(world_x, world_y);
			Color color(0, 0, 0, 0);  // Default transparent black
			
			if (p.mat_id > 0 && p.mat_id < (int)registry.size()) {
				color = registry[p.mat_id].color;
			}
			
			int pixel_idx = (y * texture_size.x + x) * 4;
			data[pixel_idx + 0] = (uint8_t)(color.r * 255);
			data[pixel_idx + 1] = (uint8_t)(color.g * 255);
			data[pixel_idx + 2] = (uint8_t)(color.b * 255);
			data[pixel_idx + 3] = (uint8_t)(color.a * 255);
		}
	}
	
	return buffer;
}

void SandWorld::tick() {
	world_grid.tick();
}

int SandWorld::get_chunk_count() const {
	return world_grid.get_chunk_count();
}

void SandWorld::brush_line(Vector2i from, Vector2i to, int mat_id, int radius) {
	// Bresenham's line algorithm with circle brush
	int x0 = from.x, y0 = from.y;
	int x1 = to.x, y1 = to.y;
	
	int dx = abs(x1 - x0);
	int dy = abs(y1 - y0);
	int sx = x0 < x1 ? 1 : -1;
	int sy = y0 < y1 ? 1 : -1;
	int err = dx - dy;
	
	Particle p;
	p.mat_id = mat_id;
	p.flags = ParticleFlags::PARTICLE_FLAG_NONE;
	
	int x = x0, y = y0;
	while (true) {
		brush_circle(Vector2i(x, y), radius, mat_id);
		
		if (x == x1 && y == y1)
			break;
		
		int e2 = 2 * err;
		if (e2 > -dy) {
			err -= dy;
			x += sx;
		}
		if (e2 < dx) {
			err += dx;
			y += sy;
		}
	}
}

void SandWorld::brush_circle(Vector2i pos, int radius, int mat_id) {
	Particle p;
	p.mat_id = mat_id;
	p.flags = ParticleFlags::PARTICLE_FLAG_NONE;
	
	int r2 = radius * radius;
	for (int dy = -radius; dy <= radius; ++dy) {
		for (int dx = -radius; dx <= radius; ++dx) {
			if (dx * dx + dy * dy <= r2) {
				world_grid.set_particle(pos.x + dx, pos.y + dy, p);
			}
		}
	}
}

void SandWorld::brush_rectangle(Vector2i pos, Vector2i size, int mat_id) {
	Particle p;
	p.mat_id = mat_id;
	p.flags = ParticleFlags::PARTICLE_FLAG_NONE;
	
	for (int y = pos.y; y < pos.y + size.y; ++y) {
		for (int x = pos.x; x < pos.x + size.x; ++x) {
			world_grid.set_particle(x, y, p);
		}
	}
}

void SandWorld::explosion(Vector2i pos, int radius) {
	// Clear particles in explosion radius (destroy effect)
	Particle empty;
	empty.mat_id = 0;
	empty.flags = ParticleFlags::PARTICLE_FLAG_NONE;
	
	int r2 = radius * radius;
	for (int dy = -radius; dy <= radius; ++dy) {
		for (int dx = -radius; dx <= radius; ++dx) {
			if (dx * dx + dy * dy <= r2) {
				world_grid.set_particle(pos.x + dx, pos.y + dy, empty);
			}
		}
	}
}
