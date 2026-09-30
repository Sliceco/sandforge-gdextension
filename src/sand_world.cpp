#include "sand_world.h"

#include "default_materials.h"

#include "godot_cpp/classes/json.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/error_macros.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/rect2i.hpp"
#include "godot_cpp/variant/string.hpp"

#include <algorithm>
#include <cmath>
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
	ClassDB::bind_method(D_METHOD("load_materials_json", "json"), &SandWorld::load_materials_json);
	ClassDB::bind_method(D_METHOD("load_default_materials"), &SandWorld::load_default_materials);
	ClassDB::bind_static_method("SandWorld", D_METHOD("get_default_materials_json"), &SandWorld::get_default_materials_json);
	ClassDB::bind_method(D_METHOD("set_particle", "pos", "mat_id"), &SandWorld::set_particle);
	ClassDB::bind_method(D_METHOD("get_particle_mat_id", "pos"), &SandWorld::get_particle_mat_id);
	ClassDB::bind_method(D_METHOD("get_particle_hp", "pos"), &SandWorld::get_particle_hp);
	ClassDB::bind_method(D_METHOD("damage_particle", "pos", "amount"), &SandWorld::damage_particle);
	ClassDB::bind_method(D_METHOD("brush_line", "from", "to", "mat_id", "radius"), &SandWorld::brush_line);
	ClassDB::bind_method(D_METHOD("brush_circle", "pos", "radius", "mat_id"), &SandWorld::brush_circle);
	ClassDB::bind_method(D_METHOD("brush_rectangle", "pos", "size", "mat_id"), &SandWorld::brush_rectangle);
	ClassDB::bind_method(D_METHOD("damage_circle", "pos", "radius", "amount"), &SandWorld::damage_circle);
	ClassDB::bind_method(D_METHOD("explosion", "pos", "radius", "power"), &SandWorld::explosion, DEFVAL(255));
	ClassDB::bind_method(D_METHOD("render_to_texture", "texture_size", "world_offset"), &SandWorld::render_to_texture);
	ClassDB::bind_method(D_METHOD("tick"), &SandWorld::tick);
	ClassDB::bind_method(D_METHOD("set_seed", "seed"), &SandWorld::set_seed);
	ClassDB::bind_method(D_METHOD("get_chunk_count"), &SandWorld::get_chunk_count);
	ClassDB::bind_method(D_METHOD("get_debug_chunk_info"), &SandWorld::get_debug_chunk_info);
}

SandWorld::SandWorld() {
	load_default_materials();
	has_default_materials = true;
}

void SandWorld::replace_default_materials() {
	if (has_default_materials) {
		world_grid.get_material_registry().clear();
		has_default_materials = false;
	}
}

void SandWorld::load_default_materials() {
	world_grid.get_material_registry().clear();
	has_default_materials = false;
	const bool loaded = load_materials_json(get_default_materials_json());
	ERR_FAIL_COND_MSG(!loaded, "Built-in default materials failed to load.");
}

String SandWorld::get_default_materials_json() {
	return String::utf8(DEFAULT_MATERIALS_JSON);
}

bool SandWorld::load_materials_json(const String &json) {
	Variant parsed = JSON::parse_string(json);
	ERR_FAIL_COND_V_MSG(parsed.get_type() != Variant::DICTIONARY, false, "Materials JSON must be an object keyed by material id.");
	set_materials_from_dict(parsed);
	return true;
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

bool SandWorld::make_material_config(int id, const String &name, int state, Color color, int density, int dispersion, int decay_chance, int decay_into, MaterialConfig &config) {
	ERR_FAIL_COND_V_MSG(id < 1 || id > 255, false, "Material id must be in 1..255 (0 is reserved for empty).");
	ERR_FAIL_COND_V_MSG(state < (int)MatterState::EMPTY || state > (int)MatterState::GAS, false, "Material state must be in 0..4.");
	ERR_FAIL_COND_V_MSG(decay_into < 0 || decay_into > 255, false, "decay_into must be a material id in 0..255.");

	config = MaterialConfig{
		.id = (uint8_t)id,
		.state = (MatterState)state,
		.color = color,
		.density = (uint8_t)CLAMP(density, 0, 255),
		.dispersion = (uint8_t)CLAMP(dispersion, 0, 255),
		.decay_chance = (uint8_t)CLAMP(decay_chance, 0, 255),
		.decay_into = (uint8_t)decay_into,
		.name = std::string(name.utf8().get_data())
	};
	return true;
}

void SandWorld::add_material(int id, const String &name, int state, Color color, int density, int dispersion, int decay_chance, int decay_into) {
	replace_default_materials();
	MaterialConfig config;
	if (make_material_config(id, name, state, color, density, dispersion, decay_chance, decay_into, config)) {
		world_grid.get_material_registry().set(config);
	}
}

void SandWorld::add_reaction(int material, int other, int material_into, int other_into, int chance) {
	// Adding a reaction first means building on the defaults; keep them.
	has_default_materials = false;
	add_reaction_rule(material, other, material_into, other_into, chance, 0);
}

void SandWorld::add_reaction_rule(int material, int other, int material_into, int other_into, int chance, int damage) {
	ERR_FAIL_COND_MSG(material < 1 || material > 255, "Reaction material must be in 1..255.");
	ERR_FAIL_COND_MSG(other < 0 || other > 255 || material_into < 0 || material_into > 255 || other_into < 0 || other_into > 255,
					  "Reaction material ids must be in 0..255.");
	ERR_FAIL_COND_MSG(chance < 1 || chance > 255, "Reaction chance must be in 1..255.");
	ERR_FAIL_COND_MSG(damage < 0 || damage > 255, "Reaction damage must be in 0..255.");

	world_grid.get_material_registry().add_reaction((uint8_t)material, ReactionRule{ .other = (uint8_t)other, .self_into = (uint8_t)material_into, .other_into = (uint8_t)other_into, .chance = (uint8_t)chance, .damage = (uint8_t)damage });
}

void SandWorld::clear_reactions() {
	world_grid.get_material_registry().clear_reactions();
}

void SandWorld::set_materials_from_dict(const Dictionary &materials_dict) {
	replace_default_materials();
	Array keys = materials_dict.keys();
	for (int i = 0; i < keys.size(); ++i) {
		Variant key = keys[i];
		Variant value = materials_dict[key];

		// JSON object keys are always strings, so accept "1" as well as 1.
		int mat_id;
		Variant::Type key_type = key.get_type();
		if (key_type == Variant::INT || key_type == Variant::FLOAT) {
			mat_id = (int)(double)key;
		} else if (key_type == Variant::STRING && String(key).is_valid_int()) {
			mat_id = String(key).to_int();
		} else {
			continue;
		}
		if (value.get_type() != Variant::DICTIONARY)
			continue;

		Dictionary mat_dict = value;

		int state = mat_dict.get("state", (int)MatterState::EMPTY);
		// JSON has no Color type, so also accept HTML strings like "#e6d899".
		Variant color_value = mat_dict.get("color", Color(1, 1, 1, 1));
		Color color = color_value.get_type() == Variant::STRING ? Color::html(color_value) : (Color)color_value;
		int density = mat_dict.get("density", 0);
		int dispersion = mat_dict.get("dispersion", 0);
		int decay_chance = mat_dict.get("decay_chance", 0);
		int decay_into = mat_dict.get("decay_into", 0);

		MaterialConfig config;
		if (!make_material_config(mat_id, mat_dict.get("name", ""), state, color, density, dispersion, decay_chance, decay_into, config))
			continue;
		int break_into = mat_dict.get("break_into", 0);
		ERR_CONTINUE_MSG(break_into < 0 || break_into > 255, "break_into must be a material id in 0..255.");
		config.max_hp = (uint8_t)CLAMP((int)mat_dict.get("max_hp", 0), 0, 255);
		config.hp_loss_chance = (uint8_t)CLAMP((int)mat_dict.get("hp_loss_chance", 0), 0, 255);
		config.break_into = (uint8_t)break_into;
		config.color_variation = (uint8_t)CLAMP((int)mat_dict.get("color_variation", 0), 0, 255);
		world_grid.get_material_registry().set(config);

		// A "reactions" array replaces this material's existing rules. Each
		// entry: { other, chance, into = mat_id, other_into = other, damage = 0 }.
		if (!mat_dict.has("reactions"))
			continue;
		world_grid.get_material_registry().clear_reactions((uint8_t)mat_id);
		Array reactions = mat_dict["reactions"];
		for (int r = 0; r < reactions.size(); ++r) {
			if (reactions[r].get_type() != Variant::DICTIONARY)
				continue;
			Dictionary reaction = reactions[r];
			int other = reaction.get("other", 0);
			add_reaction_rule(mat_id, other, reaction.get("into", mat_id), reaction.get("other_into", other), reaction.get("chance", 0), reaction.get("damage", 0));
		}
	}
}

void SandWorld::set_particle(Vector2i pos, int mat_id) {
	world_grid.set_particle(pos.x, pos.y, world_grid.make_particle((uint8_t)mat_id));
}

int SandWorld::get_particle_mat_id(Vector2i pos) const {
	Particle p = world_grid.get_particle_readonly(pos.x, pos.y);
	return p.mat_id;
}

int SandWorld::get_particle_hp(Vector2i pos) const {
	return world_grid.get_particle_readonly(pos.x, pos.y).hp;
}

int SandWorld::damage_particle(Vector2i pos, int amount) {
	return world_grid.damage_particle(pos.x, pos.y, amount);
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
	int variation[256] = {};
	for (int id = 1; id < 256; ++id) {
		const MaterialConfig &config = registry.get((uint8_t)id);
		palette[id][0] = (uint8_t)(config.color.r * 255);
		palette[id][1] = (uint8_t)(config.color.g * 255);
		palette[id][2] = (uint8_t)(config.color.b * 255);
		palette[id][3] = (uint8_t)(config.color.a * 255);
		variation[id] = config.color_variation;
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
					const Particle &p = row[wx];
					if (p.mat_id == 0)
						continue;
					std::memcpy(out, palette[p.mat_id], 4);
					if (variation[p.mat_id] != 0) {
						// Shade 0..255 maps to a brightness offset of -variation..+variation.
						const int offset = ((int)p.shade - 128) * variation[p.mat_id] / 128;
						for (int c = 0; c < 3; ++c) {
							out[c] = (uint8_t)std::clamp((int)out[c] + offset, 0, 255);
						}
					}
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
	int r2 = radius * radius;
	for (int dy = -radius; dy <= radius; ++dy) {
		for (int dx = -radius; dx <= radius; ++dx) {
			if (dx * dx + dy * dy <= r2) {
				world_grid.set_particle(pos.x + dx, pos.y + dy, world_grid.make_particle((uint8_t)mat_id));
			}
		}
	}
}

void SandWorld::brush_rectangle(Vector2i pos, Vector2i size, int mat_id) {
	for (int y = pos.y; y < pos.y + size.y; ++y) {
		for (int x = pos.x; x < pos.x + size.x; ++x) {
			world_grid.set_particle(x, y, world_grid.make_particle((uint8_t)mat_id));
		}
	}
}

void SandWorld::damage_circle(Vector2i pos, int radius, int amount) {
	int r2 = radius * radius;
	for (int dy = -radius; dy <= radius; ++dy) {
		for (int dx = -radius; dx <= radius; ++dx) {
			if (dx * dx + dy * dy <= r2) {
				world_grid.damage_particle(pos.x + dx, pos.y + dy, amount);
			}
		}
	}
}

void SandWorld::explosion(Vector2i pos, int radius, int power) {
	// Damage falls off linearly with distance but never below 1 inside the
	// radius, so fragile (max_hp 0) material is always cleared.
	int r2 = radius * radius;
	for (int dy = -radius; dy <= radius; ++dy) {
		for (int dx = -radius; dx <= radius; ++dx) {
			if (dx * dx + dy * dy <= r2) {
				const float falloff = 1.0f - std::sqrt((float)(dx * dx + dy * dy)) / (float)(radius + 1);
				world_grid.damage_particle(pos.x + dx, pos.y + dy, std::max(1, (int)(power * falloff)));
			}
		}
	}
}
