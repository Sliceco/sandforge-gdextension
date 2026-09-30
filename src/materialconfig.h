#pragma once

#include "godot_cpp/variant/color.hpp"
#include <cstdint>
#include <string>

namespace godot {

enum class MatterState : std::uint8_t {
	EMPTY = 0,
	SOLID_FIXED, // Stone, Brick
	SOLID_POWDER, // Sand, Gunpowder
	LIQUID, // Water, Oil, Acid
	GAS // Smoke, Steam, Fire
};

struct MaterialConfig {
	std::uint8_t id = 0;
	MatterState state = MatterState::EMPTY;
	Color color = Color(0, 0, 0, 0); // Default RGBA8888 color
	std::uint8_t density = 0; // Used to determine floating/sinking (e.g., Oil float on Water)
	std::uint8_t dispersion = 0; // How far liquids flow horizontally per frame
	std::uint8_t decay_chance = 0; // Chance per tick to convert into decay_into (e.g. Fire -> Smoke)
	std::uint8_t decay_into = 0; // Material ID produced when this material decays
	std::string name; // Stable identifier stored in snapshots to remap IDs on load
	std::uint8_t max_hp = 0; // Starting hp; 0 means any damage breaks it
	std::uint8_t hp_loss_chance = 0; // Chance per tick to lose 1 hp (lifetime, e.g. burning wood)
	std::uint8_t break_into = 0; // Material ID produced when hp is exhausted
	std::uint8_t color_variation = 0; // Max per-particle brightness offset (0-255) when rendering
};

} // namespace godot
