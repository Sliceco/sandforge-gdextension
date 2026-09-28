#pragma once

#include <cstdint>

namespace godot {

enum ParticleFlags : std::uint8_t {
	PARTICLE_FLAG_NONE = 0,
	PARTICLE_FLAG_UPDATED = (1 << 0), // Moved this tick
	PARTICLE_FLAG_REACTED = (1 << 1), // Produced by a reaction this tick
	// Flags that only live for one tick; WorldGrid clears them before the next.
	PARTICLE_FLAGS_TRANSIENT = PARTICLE_FLAG_UPDATED | PARTICLE_FLAG_REACTED
};

struct Particle {
	std::uint8_t mat_id = 0;
	std::uint8_t flags = ParticleFlags::PARTICLE_FLAG_NONE;
};

} // namespace godot