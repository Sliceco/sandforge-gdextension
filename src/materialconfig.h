#pragma once

#include "godot_cpp/variant/color.hpp"

namespace godot {

enum class MatterState : std::uint8_t {
    EMPTY = 0,
    SOLID_FIXED,   // Stone, Brick
    SOLID_POWDER,  // Sand, Gunpowder
    LIQUID,        // Water, Oil, Acid
    GAS            // Smoke, Steam, Fire
};

struct MaterialConfig {
    std::uint8_t id = 0;
    MatterState state = MatterState::EMPTY;
    Color color = Color(0, 0, 0, 0);        // Default RGBA8888 color
    std::uint8_t density = 0;       // Used to determine floating/sinking (e.g., Oil float on Water)
    std::uint8_t dispersion = 0;    // How far liquids flow horizontally per frame
    std::uint8_t flammability = 0;  // Chance to catch fire
    std::uint8_t acid_reactive = 0; // Does it dissolve when touching acid?
};

} // namespace godot
