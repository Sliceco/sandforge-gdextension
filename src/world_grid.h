#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "godot_cpp/variant/vector2i.hpp"
#include "material_registry.h"
#include "sand_chunk.h"
#include "sim_random.h"

using namespace godot;

// Custom hash function for Vector2i to use with unordered_map
namespace std {
template <>
struct hash<Vector2i> {
	std::size_t operator()(const Vector2i &v) const noexcept {
		return std::hash<int64_t>()(((int64_t)v.x << 32) | ((uint32_t)v.y));
	}
};
} //namespace std

class WorldGrid {
public:
	// Get or create a chunk at the given chunk coordinates
	SandSimulationChunk *get_or_create_chunk(int chunk_x, int chunk_y);

	// Get a chunk if it exists, otherwise return nullptr
	SandSimulationChunk *get_chunk(int chunk_x, int chunk_y) const;

	// Convert world pixel coordinates to chunk coordinates
	static void world_to_chunk(int world_x, int world_y, int &chunk_x, int &chunk_y) {
		chunk_x = world_x >> 6; // Divide by 64
		chunk_y = world_y >> 6;
	}

	// Convert world pixel coordinates to local chunk coordinates
	static void world_to_local(int world_x, int world_y, int &local_x, int &local_y) {
		local_x = world_x & 63; // Modulo 64
		local_y = world_y & 63;
	}

	// Get a particle at world coordinates, returns empty particle if chunk doesn't exist
	Particle get_particle_readonly(int world_x, int world_y) const;

	// Set a particle at world coordinates, creating chunks as needed. Writing
	// an empty particle into an unallocated chunk is a no-op.
	void set_particle(int world_x, int world_y, const Particle &p);

	// Same as set_particle() for a cell already known to live in `chunk`
	// (at chunk_x, chunk_y), skipping the hash lookup for the owning chunk.
	void set_particle_in_chunk(SandSimulationChunk &chunk, int chunk_x, int chunk_y, int local_x, int local_y, const Particle &p);

	// A new particle of `mat_id` at full hp with a random shade. Use this
	// (not a bare Particle) when placing material from outside the simulation.
	Particle make_particle(std::uint8_t mat_id);

	// `source` turned into `into`: full hp for the new material, keeping its
	// shade and flags. Converting into its own material keeps its hp.
	// Returns an empty particle when `into` is 0.
	Particle convert_particle(Particle source, std::uint8_t into) const;

	// Deals `amount` damage to the particle at a cell. A particle breaks into
	// its material's break_into once the damage reaches its hp (at least 1).
	// Returns the damage left over after breaking it, or 0 if the particle
	// absorbed the hit; an empty cell absorbs nothing. Projectiles can keep
	// spending the leftover on the next cell along their path.
	int damage_particle(int world_x, int world_y, int amount);

	// Records a cell holding a particle with transient flags (moved or
	// reacted this tick) so they can be cleared before the next tick
	// without scanning an entire chunk.
	void mark_particle_updated(int world_x, int world_y);

	// Update all active chunks
	void tick();

	// Clear all chunks
	void clear();

	// Serialize and restore allocated chunks without exposing grid internals.
	// Snapshots carry an ID -> name table; on load, IDs are remapped by name
	// and load fails (see get_last_error()) if a used material is unknown.
	std::vector<uint8_t> serialize() const;
	bool deserialize(const std::vector<uint8_t> &data);

	// Why the last deserialize() failed; empty if it succeeded.
	const std::string &get_last_error() const { return last_error; }

	// Get the number of active chunks
	int get_chunk_count() const { return chunks.size(); }

	// Materials and reaction rules for this world.
	MaterialRegistry &get_material_registry() { return materials; }
	const MaterialRegistry &get_material_registry() const { return materials; }

	// All simulation randomness comes from here, so a world replays
	// identically for a given seed and initial state.
	SimRandom &get_random() { return random; }
	void set_seed(std::uint64_t seed) { random.seed(seed); }

	// Per-chunk debug snapshot: chunk coordinates plus its current
	// dirty rect (in local chunk-space cells). Used by debug overlays to
	// visualize chunk boundaries and pending simulation regions.
	struct ChunkDebugInfo {
		Vector2i chunk_position;
		bool has_dirty_rect = false;
		int dirty_min_x = 0, dirty_min_y = 0, dirty_max_x = 0, dirty_max_y = 0;
	};

	// Snapshot of every allocated chunk's position and dirty rect, for debug visualization.
	std::vector<ChunkDebugInfo> get_debug_chunk_info() const;

private:
	void clear_updated_particles();

	std::unordered_map<Vector2i, std::unique_ptr<SandSimulationChunk>> chunks;
	std::vector<Vector2i> updated_particles;
	bool alternate_direction = false;
	std::string last_error;
	MaterialRegistry materials;
	SimRandom random;
};
