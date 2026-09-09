#pragma once

#include <unordered_map>
#include <functional>
#include <memory>

#include "godot_cpp/variant/packed_byte_array.hpp"
#include "godot_cpp/variant/vector2i.hpp"
#include "sand_chunk.h"

using namespace godot;

// Custom hash function for Vector2i to use with unordered_map
namespace std {
template<>
struct hash<Vector2i> {
	std::size_t operator()(const Vector2i &v) const noexcept {
		return std::hash<int64_t>()(((int64_t)v.x << 32) | ((uint32_t)v.y));
	}
};
}

class WorldGrid {
public:
	// Get or create a chunk at the given chunk coordinates
	SandSimulationChunk *get_or_create_chunk(int chunk_x, int chunk_y);
	
	// Get a chunk if it exists, otherwise return nullptr
	SandSimulationChunk *get_chunk(int chunk_x, int chunk_y) const;
	
	// Convert world pixel coordinates to chunk coordinates
	static void world_to_chunk(int world_x, int world_y, int &chunk_x, int &chunk_y) {
		chunk_x = world_x >> 6;  // Divide by 64
		chunk_y = world_y >> 6;
	}
	
	// Convert world pixel coordinates to local chunk coordinates
	static void world_to_local(int world_x, int world_y, int &local_x, int &local_y) {
		local_x = world_x & 63;  // Modulo 64
		local_y = world_y & 63;
	}
	
	// Get a particle at world coordinates, creating chunks as needed.
	// NOTE: mutating the returned reference bypasses dirty-rect tracking;
	// prefer set_particle() so the change (and any affected neighbor
	// chunk) is correctly marked for re-simulation.
	Particle &get_particle(int world_x, int world_y);
	
	// Get a particle at world coordinates, returns empty particle if chunk doesn't exist
	Particle get_particle_readonly(int world_x, int world_y) const;
	
	// Set a particle at world coordinates, creating chunks as needed
	void set_particle(int world_x, int world_y, const Particle &p);
	
	// Update all active chunks
	void tick();
	
	// Clear all chunks
	void clear();

	// Serialize and restore allocated chunks without exposing grid internals.
	PackedByteArray serialize() const;
	bool deserialize(const PackedByteArray &data);
	
	// Get the number of active chunks
	int get_chunk_count() const { return chunks.size(); }
	
	// Get material registry reference
	static const std::vector<MaterialConfig> &get_material_registry() {
		return SandSimulationChunk::mat_registry;
	}
	
	// Set material registry
	static void set_material_registry(const std::vector<MaterialConfig> &registry) {
		SandSimulationChunk::mat_registry = registry;
	}

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
	std::unordered_map<Vector2i, std::unique_ptr<SandSimulationChunk>> chunks;
	bool alternate_direction = false;
};
