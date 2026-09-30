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
	// Remaining durability, starting at the material's max_hp. Lost to damage
	// and, for materials with hp_loss_chance, over time; at 0 the particle
	// turns into the material's break_into.
	std::uint8_t hp = 0;
	// Random per-particle value fixed at creation; scales the material's
	// color_variation when rendering so grains keep their shade as they move.
	std::uint8_t shade = 128;
};

} // namespace godot