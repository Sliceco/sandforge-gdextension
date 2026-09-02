#pragma once

namespace godot {

enum ParticleFlags : std::uint8_t {
    PARTICLE_FLAG_NONE    = 0,
    PARTICLE_FLAG_UPDATED = (1 << 0),
    PARTICLE_FLAG_BURNING = (1 << 1) // Easy to expand later
};

struct Particle {
    std::uint8_t mat_id = 0;
    std::uint8_t flags = ParticleFlags::PARTICLE_FLAG_NONE;
};

} // namespace godot