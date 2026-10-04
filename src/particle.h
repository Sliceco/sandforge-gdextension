#pragma once

#include <cstdint>

namespace godot {

enum ParticleFlags : std::uint8_t {
	PARTICLE_FLAG_NONE = 0,
	PARTICLE_FLAG_UPDATED = (1 << 0), // Moved this tick
	PARTICLE_FLAG_REACTED = (1 << 1), // Produced by a reaction this tick
	// Flags that only live for one tick; WorldGrid clears them before the next.
	PARTICLE_FLAGS_TRANSIENT = PARTICLE_FLAG_UPDATED | PARTICLE_FLAG_REACTED,
	// Persistent flow state (see Particle::get_rest() and get_flow_dir()),
	// kept across ticks and in snapshots.
	PARTICLE_REST_MASK = (3 << 2),
	PARTICLE_FLOW_DIR_SET = (1 << 4), // Has a sideways flow direction
	PARTICLE_FLOW_DIR_NEGATIVE = (1 << 5), // ...and it points toward -x
	PARTICLE_FLOW_FOLLOW = (1 << 6) // Settled, but following fluid ahead of it downhill
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

	// Fluids keep sliding sideways in their flow direction until blocked,
	// then turn around. The rest counter counts those turnarounds since the
	// particle last fell (or rose, for gases), saturating at REST_SETTLED.
	// A fluid that keeps bouncing between obstacles without ever dropping
	// has no lower cell to reach and is ping-ponging on a level surface;
	// once settled it only slides toward a cell it can fall into, so still
	// pools stop moving and their chunks can sleep. Any fall, or being
	// pushed by a falling particle, resets the counter.
	static constexpr int REST_SHIFT = 2;
	static constexpr int REST_SETTLED = 3;

	int get_rest() const { return (flags & PARTICLE_REST_MASK) >> REST_SHIFT; }
	bool is_settled() const { return get_rest() >= REST_SETTLED; }
	void set_rest(int rest) {
		flags = static_cast<std::uint8_t>((flags & ~PARTICLE_REST_MASK) | ((rest << REST_SHIFT) & PARTICLE_REST_MASK));
	}

	// A settled particle whose neighbor just flowed away downhill follows
	// it in its flow direction until blocked, so a whole resting layer can
	// drain through an opening further away than it would look for one.
	bool is_following() const { return flags & PARTICLE_FLOW_FOLLOW; }
	void set_following(bool following) {
		flags = static_cast<std::uint8_t>(following ? (flags | PARTICLE_FLOW_FOLLOW) : (flags & ~PARTICLE_FLOW_FOLLOW));
	}

	// -1 or 1, or 0 if the particle hasn't slid sideways yet.
	int get_flow_dir() const {
		if (!(flags & PARTICLE_FLOW_DIR_SET))
			return 0;
		return (flags & PARTICLE_FLOW_DIR_NEGATIVE) ? -1 : 1;
	}
	void set_flow_dir(int dir) {
		flags = static_cast<std::uint8_t>(flags & ~(PARTICLE_FLOW_DIR_SET | PARTICLE_FLOW_DIR_NEGATIVE));
		if (dir != 0)
			flags |= PARTICLE_FLOW_DIR_SET | (dir < 0 ? PARTICLE_FLOW_DIR_NEGATIVE : 0);
	}
};

} // namespace godot