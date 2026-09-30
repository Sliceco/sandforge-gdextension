#include "sand_world.h"

#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/error_macros.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/rect2i.hpp"
#include "godot_cpp/variant/string.hpp"

#include <algorithm>
#include <cstring>

void SandWorld::_bind_methods() {
	ClassDB::bind_method(D_METHOD("clear"), &SandWorld::clear);
	ClassDB::bind_method(D_METHOD("save_snapshot"), &SandWorld::save_snapshot);
	ClassDB::bind_method(D_METHOD("load_snapshot", "snapshot"), &SandWorld::load_snapshot);
	ClassDB::bind_method(D_METHOD("add_material", "id", "name", "state", "color", "density", "dispersion", "decay_chance", "decay_into"),
						 &SandWorld::add_material, DEFVAL(0), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("add_reaction", "material", "other", "material_into", "other_into", "chance"), &SandWorld::add_reaction);
	ClassDB::bind_method(D_METHOD("clear_reactions"), &SandWorld::clear_reactions);
	ClassDB::bind_method(D_METHOD("set_materials_from_dict", "materials"), &SandWorld::set_materials_from_dict);
	ClassDB::bind_method(D_METHOD("set_particle", "pos", "mat_id"), &SandWorld::set_particle);
	ClassDB::bind_method(D_METHOD("get_particle_mat_id", "pos"), &SandWorld::get_particle_mat_id);
	ClassDB::bind_method(D_METHOD("brush_line", "from", "to", "mat_id", "radius"), &SandWorld::brush_line);
	ClassDB::bind_method(D_METHOD("brush_circle", "pos", "radius", "mat_id"), &SandWorld::brush_circle);
	ClassDB::bind_method(D_METHOD("brush_rectangle", "pos", "size", "mat_id"), &SandWorld::brush_rectangle);
	ClassDB::bind_method(D_METHOD("explosion", "pos", "radius"), &SandWorld::explosion);
	ClassDB::bind_method(D_METHOD("render_to_texture", "texture_size", "world_offset"), &SandWorld::render_to_texture);
	ClassDB::bind_method(D_METHOD("tick"), &SandWorld::tick);
	ClassDB::bind_method(D_METHOD("set_seed", "seed"), &SandWorld::set_seed);
	ClassDB::bind_method(D_METHOD("get_chunk_count"), &SandWorld::get_chunk_count);
	ClassDB::bind_method(D_METHOD("get_debug_chunk_info"), &SandWorld::get_debug_chunk_info);
}

SandWorld::SandWorld() {
	// Each world starts with a few default materials (ID 0 stays empty).
	MaterialRegistry &registry = world_grid.get_material_registry();
	registry.set(MaterialConfig{
			.id = 1, // Stone
			.state = MatterState::SOLID_FIXED,
			.color = Color(0.5f, 0.5f, 0.5f, 1.0f),
			.density = 100 });
	registry.set(MaterialConfig{
			.id = 2, // Sand
			.state = MatterState::SOLID_POWDER,
			.color = Color(0.9f, 0.85f, 0.6f, 1.0f),
			.density = 50 });
	registry.set(MaterialConfig{
			.id = 3, // Water
			.state = MatterState::LIQUID,
			.color = Color(0.2f, 0.4f, 0.8f, 1.0f),
			.density = 30,
			.dispersion = 4 });
}

void SandWorld::clear() {
	world_grid.clear();
}

PackedByteArray SandWorld::save_snapshot() const {
	const std::vector<uint8_t> bytes = world_grid.serialize();
	PackedByteArray snapshot;
	snapshot.resize(bytes.size());
	if (!bytes.empty()) {
		memcpy(snapshot.ptrw(), bytes.data(), bytes.size());
	}
	return snapshot;
}

bool SandWorld::load_snapshot(const PackedByteArray &snapshot) {
	const bool loaded = world_grid.deserialize(std::vector<uint8_t>(snapshot.ptr(), snapshot.ptr() + snapshot.size()));
	if (!loaded) {
		ERR_PRINT(String("Failed to load snapshot: ") + String::utf8(world_grid.get_last_error().c_str()));
	}
	return loaded;
}

void SandWorld::add_material(int id, const String &name, int state, Color color, int density, int dispersion, int decay_chance, int decay_into) {
	ERR_FAIL_COND_MSG(id < 1 || id > 255, "Material id must be in 1..255 (0 is reserved for empty).");
	ERR_FAIL_COND_MSG(state < (int)MatterState::EMPTY || state > (int)MatterState::GAS, "Material state must be in 0..4.");
	ERR_FAIL_COND_MSG(decay_into < 0 || decay_into > 255, "decay_into must be a material id in 0..255.");

	world_grid.get_material_registry().set(MaterialConfig{
			.id = (uint8_t)id,
			.state = (MatterState)state,
			.color = color,
			.density = (uint8_t)CLAMP(density, 0, 255),
			.dispersion = (uint8_t)CLAMP(dispersion, 0, 255),
			.decay_chance = (uint8_t)CLAMP(decay_chance, 0, 255),
			.decay_into = (uint8_t)decay_into,
			.name = std::string(name.utf8().get_data()) });
}

void SandWorld::add_reaction(int material, int other, int material_into, int other_into, int chance) {
	ERR_FAIL_COND_MSG(material < 1 || material > 255, "Reaction material must be in 1..255.");
	ERR_FAIL_COND_MSG(other < 0 || other > 255 || material_into < 0 || material_into > 255 || other_into < 0 || other_into > 255,
					  "Reaction material ids must be in 0..255.");
	ERR_FAIL_COND_MSG(chance < 1 || chance > 255, "Reaction chance must be in 1..255.");

	world_grid.get_material_registry().add_reaction((uint8_t)material, ReactionRule{ .other = (uint8_t)other, .self_into = (uint8_t)material_into, .other_into = (uint8_t)other_into, .chance = (uint8_t)chance });
}

void SandWorld::clear_reactions() {
	world_grid.get_material_registry().clear_reactions();
}

void SandWorld::set_materials_from_dict(const Dictionary &materials_dict) {
	Array keys = materials_dict.keys();
	for (int i = 0; i < keys.size(); ++i) {
		Variant key = keys[i];
		Variant value = materials_dict[key];

		Variant::Type key_type = key.get_type();
		if (key_type != Variant::INT && key_type != Variant::FLOAT)
			continue;

		int mat_id = (int)(double)key;
		if (value.get_type() != Variant::DICTIONARY)
			continue;

		Dictionary mat_dict = value;

		int state = mat_dict.get("state", (int)MatterState::EMPTY);
		Color color = mat_dict.get("color", Color(1, 1, 1, 1));
		int density = mat_dict.get("density", 0);
		int dispersion = mat_dict.get("dispersion", 0);
		int decay_chance = mat_dict.get("decay_chance", 0);
		int decay_into = mat_dict.get("decay_into", 0);

		add_material(mat_id, mat_dict.get("name", ""), state, color, density, dispersion, decay_chance, decay_into);

		// A "reactions" array replaces this material's existing rules. Each
		// entry: { other, chance, into = mat_id, other_into = other }.
		if (!mat_dict.has("reactions") || mat_id < 1 || mat_id > 255)
			continue;
		world_grid.get_material_registry().clear_reactions((uint8_t)mat_id);
		Array reactions = mat_dict["reactions"];
		for (int r = 0; r < reactions.size(); ++r) {
			if (reactions[r].get_type() != Variant::DICTIONARY)
				continue;
			Dictionary reaction = reactions[r];
			int other = reaction.get("other", 0);
			add_reaction(mat_id, other, reaction.get("into", mat_id), reaction.get("other_into", other), reaction.get("chance", 0));
		}
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
	buffer.resize(pixel_count * 4); // RGBA8888

	if (pixel_count <= 0)
		return buffer;

	// Precompute the RGBA8 palette once; index 0 (empty) is opaque black.
	const MaterialRegistry &registry = world_grid.get_material_registry();
	uint8_t palette[256][4] = { { 0, 0, 0, 255 } };
	for (int id = 1; id < 256; ++id) {
		const Color &color = registry.get((uint8_t)id).color;
		palette[id][0] = (uint8_t)(color.r * 255);
		palette[id][1] = (uint8_t)(color.g * 255);
		palette[id][2] = (uint8_t)(color.b * 255);
		palette[id][3] = (uint8_t)(color.a * 255);
	}

	uint8_t *data = buffer.ptrw();
	for (int i = 0; i < pixel_count; ++i) {
		std::memcpy(data + i * 4, palette[0], 4);
	}

	// Walk only the chunks overlapping the view: one hash lookup per chunk
	// instead of per pixel, and missing chunks stay background.
	constexpr int SIZE = SandSimulationChunk::SIZE;
	int chunk_min_x, chunk_min_y, chunk_max_x, chunk_max_y;
	WorldGrid::world_to_chunk(world_offset.x, world_offset.y, chunk_min_x, chunk_min_y);
	WorldGrid::world_to_chunk(world_offset.x + texture_size.x - 1, world_offset.y + texture_size.y - 1, chunk_max_x, chunk_max_y);

	for (int cy = chunk_min_y; cy <= chunk_max_y; ++cy) {
		for (int cx = chunk_min_x; cx <= chunk_max_x; ++cx) {
			const SandSimulationChunk *chunk = world_grid.get_chunk(cx, cy);
			if (chunk == nullptr)
				continue;

			// Intersect the chunk with the view, in world coordinates.
			const int x0 = std::max(cx * SIZE, world_offset.x);
			const int x1 = std::min(cx * SIZE + SIZE, world_offset.x + texture_size.x);
			const int y0 = std::max(cy * SIZE, world_offset.y);
			const int y1 = std::min(cy * SIZE + SIZE, world_offset.y + texture_size.y);

			for (int wy = y0; wy < y1; ++wy) {
				const Particle *row = chunk->grid + (wy - cy * SIZE) * SIZE - cx * SIZE;
				uint8_t *out = data + ((wy - world_offset.y) * texture_size.x + (x0 - world_offset.x)) * 4;
				for (int wx = x0; wx < x1; ++wx, out += 4) {
					const uint8_t id = row[wx].mat_id;
					if (id != 0)
						std::memcpy(out, palette[id], 4);
				}
			}
		}
	}

	return buffer;
}

void SandWorld::tick() {
	world_grid.tick();
}

void SandWorld::set_seed(int64_t seed) {
	world_grid.set_seed((uint64_t)seed);
}

int SandWorld::get_chunk_count() const {
	return world_grid.get_chunk_count();
}

TypedArray<Dictionary> SandWorld::get_debug_chunk_info() const {
	TypedArray<Dictionary> result;
	constexpr int SIZE = SandSimulationChunk::SIZE;

	for (const WorldGrid::ChunkDebugInfo &info : world_grid.get_debug_chunk_info()) {
		Dictionary entry;
		entry["chunk_position"] = info.chunk_position;
		entry["world_rect"] = Rect2i(info.chunk_position * SIZE, Vector2i(SIZE, SIZE));

		Rect2i dirty_rect; // Defaults to a zero-sized rect when the chunk is asleep.
		if (info.has_dirty_rect) {
			Vector2i origin = info.chunk_position * SIZE + Vector2i(info.dirty_min_x, info.dirty_min_y);
			Vector2i size = Vector2i(info.dirty_max_x - info.dirty_min_x + 1, info.dirty_max_y - info.dirty_min_y + 1);
			dirty_rect = Rect2i(origin, size);
		}
		entry["dirty_rect"] = dirty_rect;

		result.push_back(entry);
	}

	return result;
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
