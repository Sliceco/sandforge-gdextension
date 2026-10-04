#include "sand_chunk.h"

#include "material_registry.h"
#include "materialconfig.h"
#include "particle.h"
#include "sim_random.h"
#include "world_grid.h"

#include <algorithm>
#include <vector>

using namespace godot;

namespace {
// How many times its dispersion a fluid looks sideways for a cell it could
// fall into before settling, and once settled.
constexpr int SETTLED_SEARCH_FACTOR = 8;

// The 8 neighbors in ring order, so starting at a random index and walking
// the ring visits them without a fixed directional preference.
constexpr int NEIGHBOR_OFFSETS[8][2] = {
	{ -1, -1 },
	{ 0, -1 },
	{ 1, -1 },
	{ 1, 0 },
	{ 1, 1 },
	{ 0, 1 },
	{ -1, 1 },
	{ -1, 0 }
};

// Returns the particle that replaces `source` when it turns into `into`.
// Products carry PARTICLE_FLAG_REACTED so they can't react again (and chain
// across the grid in scan order) within the same tick. UPDATED is kept so a
// particle that already moved this tick still can't move again.
Particle reaction_product(const WorldGrid &world_grid, Particle source, std::uint8_t into) {
	Particle product = world_grid.convert_particle(source, into);
	if (product.mat_id != 0)
		product.flags |= ParticleFlags::PARTICLE_FLAG_REACTED;
	return product;
}

// The neighbor after a damaging reaction hits it: worn down by `damage`, or
// broken into its material's break_into once the damage reaches its hp.
Particle damaged_product(const WorldGrid &world_grid, Particle neighbor, std::uint8_t damage) {
	if (damage < std::max<int>(neighbor.hp, 1)) {
		neighbor.hp = static_cast<std::uint8_t>(neighbor.hp - damage);
		neighbor.flags |= ParticleFlags::PARTICLE_FLAG_REACTED;
		return neighbor;
	}
	return reaction_product(world_grid, neighbor, world_grid.get_material_registry().get(neighbor.mat_id).break_into);
}
} // namespace

void SandSimulationChunk::tick(bool alternate_direction, WorldGrid &world_grid, Vector2i world_origin) {
	if (dirty_rect.empty())
		return;

	// Snapshot the region to simulate this tick, then reset dirty_rect so it
	// can accumulate whatever region needs simulating on the next tick.
	DirtyRect active_rect = dirty_rect;
	dirty_rect.clear();

	// Phase 1: Movement pass - bottom-to-top to let items fall naturally
	for (int y = active_rect.max_y; y >= active_rect.min_y; --y) {
		// Alternate horizontal scan direction to prevent bias asymmetry
		if (alternate_direction) {
			for (int x = active_rect.min_x; x <= active_rect.max_x; ++x) {
				update_particle(x, y, world_grid, world_origin);
			}
		} else {
			for (int x = active_rect.max_x; x >= active_rect.min_x; --x) {
				update_particle(x, y, world_grid, world_origin);
			}
		}
	}

	// Phase 2: Reaction pass - check for chemical interactions
	for (int y = active_rect.min_y; y <= active_rect.max_y; ++y) {
		for (int x = active_rect.min_x; x <= active_rect.max_x; ++x) {
			check_neighborhood_reactions(x, y, world_grid, world_origin);
		}
	}
}

Particle SandSimulationChunk::read_cell(int x, int y, const WorldGrid &world_grid, Vector2i world_origin) const {
	if (in_bounds(x, y))
		return grid[y * SIZE + x];
	return world_grid.get_particle_readonly(world_origin.x + x, world_origin.y + y);
}

void SandSimulationChunk::write_cell(int x, int y, const Particle &p, WorldGrid &world_grid, Vector2i world_origin) {
	if (in_bounds(x, y)) {
		// world_origin is a multiple of SIZE, so this is this chunk's key.
		world_grid.set_particle_in_chunk(*this, world_origin.x >> 6, world_origin.y >> 6, x, y, p);
		return;
	}
	world_grid.set_particle(world_origin.x + x, world_origin.y + y, p);
}

bool SandSimulationChunk::update_particle(int x, int y, WorldGrid &world_grid, Vector2i world_origin) {
	int idx = get_index(x, y);
	Particle &p = grid[idx];

	// If the particle is empty, skip it.
	if (p.mat_id == 0)
		return false;

	// A particle that crossed into this chunk may be encountered after its
	// source chunk already moved it this frame. Keep it dirty so it resumes on
	// the next tick after WorldGrid clears the transient update flag.
	if (p.flags & ParticleFlags::PARTICLE_FLAG_UPDATED) {
		mark_dirty(x, y);
		return false;
	}

	// Get the material configuration for this particle
	const MaterialConfig &config = world_grid.get_material_registry().get(p.mat_id);
	if (config.state == MatterState::SOLID_FIXED)
		return false;

	SimRandom &random = world_grid.get_random();

	// Try moving down (Powders and Liquids)
	if (config.state == MatterState::SOLID_POWDER || config.state == MatterState::LIQUID) {
		if (try_move_or_swap(x, y, x, y + 1, config, world_grid, world_origin))
			return true;

		// Diagonal fall down-left or down-right
		int side_dir = random.next_sign();
		if (try_move_or_swap(x, y, x + side_dir, y + 1, config, world_grid, world_origin))
			return true;
		if (try_move_or_swap(x, y, x - side_dir, y + 1, config, world_grid, world_origin))
			return true;
	}

	// Horizontal dispersion (Liquids only)
	if (config.state == MatterState::LIQUID) {
		int side_dir = p.get_flow_dir() != 0 ? p.get_flow_dir() : random.next_sign();
		if (try_disperse(x, y, side_dir, config, world_grid, world_origin, false))
			return true;
		if (try_disperse(x, y, -side_dir, config, world_grid, world_origin, false))
			return true;
	}

	// Gases (Smoke, Fire, etc.) behave like liquids but rise instead of
	// fall, so movement mirrors the powder/liquid logic with an inverted
	// density rule (lighter gas displaces denser gas/fluid above it).
	if (config.state == MatterState::GAS) {
		if (try_move_or_swap(x, y, x, y - 1, config, world_grid, world_origin, true))
			return true;

		int side_dir = random.next_sign();
		if (try_move_or_swap(x, y, x + side_dir, y - 1, config, world_grid, world_origin, true))
			return true;
		if (try_move_or_swap(x, y, x - side_dir, y - 1, config, world_grid, world_origin, true))
			return true;

		if (p.get_flow_dir() != 0)
			side_dir = p.get_flow_dir();
		if (try_disperse(x, y, side_dir, config, world_grid, world_origin, true))
			return true;
		if (try_disperse(x, y, -side_dir, config, world_grid, world_origin, true))
			return true;
	}

	return false;
}

bool SandSimulationChunk::try_disperse(int x, int y, int dir, const MaterialConfig &config, WorldGrid &world_grid, Vector2i world_origin, bool invert_density) {
	if (config.dispersion == 0)
		return false;

	Particle self = grid[get_index(x, y)];

	// Walk outward while cells are empty, stopping at the first occupied one
	// so the particle can't jump over walls or other particles.
	int reach = 0;
	for (int i = 1; i <= config.dispersion; ++i) {
		if (read_cell(x + dir * i, y, world_grid, world_origin).mat_id != 0)
			break;
		reach = i;
	}

	if (self.is_settled()) {
		// A settled particle only leaves its resting place toward somewhere
		// it can fall, e.g. a channel opening beside a still pool, or
		// following the fluid ahead of it toward one.
		const int drop = find_drop(x, y, dir, config, world_grid, world_origin, invert_density);
		if (self.is_following() && dir == self.get_flow_dir()) {
			if (reach > 0)
				return try_move_or_swap(x, y, x + dir * (drop > 0 ? std::min(drop, reach) : reach), y, config, world_grid, world_origin, invert_density, true);
			// Blocked: back to resting. Stop here rather than turning toward
			// another drop, or a crowd of followers can sway back and forth.
			self.set_following(false);
			write_cell(x, y, self, world_grid, world_origin);
			return true;
		}
		if (drop == 0)
			return false;
		return try_move_or_swap(x, y, x + dir * std::min(drop, static_cast<int>(config.dispersion)), y, config, world_grid, world_origin, invert_density, true);
	}

	// Turning around only counts toward settling when there is nowhere
	// lower to flow to on either side, so a fluid crowded by its neighbors
	// on its way down a slope keeps going.
	const bool turning = self.get_flow_dir() == -dir;
	const bool may_settle = turning && find_drop(x, y, dir, config, world_grid, world_origin, invert_density) == 0 && find_drop(x, y, -dir, config, world_grid, world_origin, invert_density) == 0;

	// With no empty cell to slide into, the adjacent cell may still be
	// displaced by the usual density rule.
	return try_move_or_swap(x, y, x + dir * std::max(reach, 1), y, config, world_grid, world_origin, invert_density, true, may_settle);
}

int SandSimulationChunk::find_drop(int x, int y, int dir, const MaterialConfig &config, const WorldGrid &world_grid, Vector2i world_origin, bool invert_density) const {
	const int fall_dy = invert_density ? -1 : 1;
	const int search = config.dispersion * SETTLED_SEARCH_FACTOR;
	for (int i = 1; i <= search; ++i) {
		if (read_cell(x + dir * i, y, world_grid, world_origin).mat_id != 0)
			return 0;
		if (can_displace(config, read_cell(x + dir * i, y + fall_dy, world_grid, world_origin), world_grid, invert_density))
			return i;
	}
	return 0;
}

bool SandSimulationChunk::can_displace(const MaterialConfig &src_config, const Particle &destination, const WorldGrid &world_grid, bool invert_density) const {
	if (destination.mat_id == 0)
		return true;
	const MaterialConfig &dst_config = world_grid.get_material_registry().get(destination.mat_id);
	if (dst_config.state == MatterState::SOLID_FIXED)
		return false;
	// Normally the denser material sinks past the lighter one. Rising
	// gases invert this so the lighter gas floats past whatever is above.
	return invert_density ? (src_config.density < dst_config.density) : (src_config.density > dst_config.density);
}

bool SandSimulationChunk::try_move_or_swap(int src_x, int src_y, int dst_x, int dst_y, const MaterialConfig &src_config, WorldGrid &world_grid, Vector2i world_origin, bool invert_density, bool sideways, bool may_settle) {
	const Vector2i source_position = world_origin + Vector2i(src_x, src_y);
	const Vector2i destination_position = world_origin + Vector2i(dst_x, dst_y);
	Particle destination = read_cell(dst_x, dst_y, world_grid, world_origin);
	if (!can_displace(src_config, destination, world_grid, invert_density))
		return false;

	Particle source = grid[get_index(src_x, src_y)];
	// Resting fluid that starts to fall or flow toward a drop pulls the
	// resting fluid beside it along. Those moves always make progress
	// downhill, so this never starts an endless back-and-forth, and fluid
	// already in motion (e.g. a falling stream) skips the search.
	const bool was_settled = source.is_settled();
	source.flags |= ParticleFlags::PARTICLE_FLAG_UPDATED;
	if (sideways) {
		if (may_settle)
			source.set_rest(std::min(source.get_rest() + 1, static_cast<int>(Particle::REST_SETTLED)));
		source.set_flow_dir(dst_x > src_x ? 1 : -1);
	} else {
		source.set_rest(0);
		source.set_following(false);
	}

	if (destination.mat_id == 0) {
		write_cell(src_x, src_y, Particle(), world_grid, world_origin);
		write_cell(dst_x, dst_y, source, world_grid, world_origin);
		world_grid.mark_particle_updated(destination_position.x, destination_position.y);
		if (was_settled && src_config.dispersion > 0)
			pull_sideways_neighbors(src_x, src_y, sideways ? (dst_x > src_x ? 1 : -1) : 0, src_config.dispersion * SETTLED_SEARCH_FACTOR, world_grid, world_origin);
		return true;
	}

	// A particle pushed aside by a falling one has been disturbed and must
	// be free to spread out again.
	if (!sideways) {
		destination.set_rest(0);
		destination.set_following(false);
	}
	write_cell(src_x, src_y, destination, world_grid, world_origin);
	write_cell(dst_x, dst_y, source, world_grid, world_origin);
	world_grid.mark_particle_updated(destination_position.x, destination_position.y);
	// The displaced particle may already carry transient flags from
	// earlier this tick; track its new cell too, or they are never
	// cleared and it stays frozen.
	if (destination.flags & ParticleFlags::PARTICLE_FLAGS_TRANSIENT)
		world_grid.mark_particle_updated(source_position.x, source_position.y);
	return true;
}

void SandSimulationChunk::pull_sideways_neighbors(int x, int y, int move_dir, int search, WorldGrid &world_grid, Vector2i world_origin) {
	for (int dx : { -1, 1 }) {
		// After a sideways move only fluid left behind follows.
		if (dx == move_dir)
			continue;
		// The nearest particle on that side, across a short gap, so a layer
		// already broken up by draining keeps draining.
		for (int i = 1; i <= search; ++i) {
			Particle neighbor = read_cell(x + dx * i, y, world_grid, world_origin);
			if (neighbor.mat_id == 0)
				continue;
			if (neighbor.is_settled()) {
				neighbor.set_following(true);
				neighbor.set_flow_dir(-dx);
				write_cell(x + dx * i, y, neighbor, world_grid, world_origin);
			}
			break;
		}
	}
}

void SandSimulationChunk::check_neighborhood_reactions(int x, int y, WorldGrid &world_grid, Vector2i world_origin) {
	const Particle &p = grid[get_index(x, y)];
	// Skip empty cells and products of a reaction earlier this tick.
	if (p.mat_id == 0 || (p.flags & ParticleFlags::PARTICLE_FLAG_REACTED))
		return;

	// A reaction that kept this cell's material (e.g. burning wood igniting
	// a neighbor) must not pause its decay or lifetime.
	const std::uint8_t mat_id = p.mat_id;
	if (check_reaction_rules(x, y, world_grid, world_origin) && grid[get_index(x, y)].mat_id != mat_id)
		return;
	if (check_decay(x, y, world_grid, world_origin))
		return;
	check_hp_loss(x, y, world_grid, world_origin);
}

bool SandSimulationChunk::check_reaction_rules(int x, int y, WorldGrid &world_grid, Vector2i world_origin) {
	const Particle self = grid[get_index(x, y)];
	const std::vector<ReactionRule> &rules = world_grid.get_material_registry().get_reactions(self.mat_id);
	if (rules.empty())
		return false;

	SimRandom &random = world_grid.get_random();
	const Vector2i self_position = world_origin + Vector2i(x, y);
	const int start = static_cast<int>(random.next_below(8));
	bool eligible = false;

	for (int i = 0; i < 8; ++i) {
		const int *offset = NEIGHBOR_OFFSETS[(start + i) % 8];
		const Vector2i neighbor_position = self_position + Vector2i(offset[0], offset[1]);
		const Particle neighbor = read_cell(x + offset[0], y + offset[1], world_grid, world_origin);
		if (neighbor.flags & ParticleFlags::PARTICLE_FLAG_REACTED)
			continue;

		for (const ReactionRule &rule : rules) {
			if (rule.other != neighbor.mat_id)
				continue;
			eligible = true;
			if (random.next_u8() >= rule.chance)
				continue;

			// Route through world_grid so both cells (and any neighboring
			// chunk sharing a border) get marked dirty.
			write_cell(x, y, reaction_product(world_grid, self, rule.self_into), world_grid, world_origin);
			world_grid.mark_particle_updated(self_position.x, self_position.y);
			if (rule.damage > 0) {
				write_cell(x + offset[0], y + offset[1], damaged_product(world_grid, neighbor, rule.damage), world_grid, world_origin);
				world_grid.mark_particle_updated(neighbor_position.x, neighbor_position.y);
			} else if (rule.other_into != neighbor.mat_id) {
				write_cell(x + offset[0], y + offset[1], reaction_product(world_grid, neighbor, rule.other_into), world_grid, world_origin);
				world_grid.mark_particle_updated(neighbor_position.x, neighbor_position.y);
			}
			return true;
		}
	}

	// A reaction could have happened but lost its roll. Stay dirty so it is
	// retried next tick instead of stalling once nothing nearby moves.
	if (eligible)
		mark_dirty(x, y);
	return false;
}

bool SandSimulationChunk::check_decay(int x, int y, WorldGrid &world_grid, Vector2i world_origin) {
	const Particle self = grid[get_index(x, y)];
	const MaterialConfig &config = world_grid.get_material_registry().get(self.mat_id);
	if (config.decay_chance == 0)
		return false;

	if (world_grid.get_random().next_u8() >= config.decay_chance) {
		// Keep decaying material (e.g. fire trapped under a ceiling) awake
		// until it actually decays.
		mark_dirty(x, y);
		return false;
	}

	// Burnt-out fire (and similar decaying materials) turns into its
	// configured byproduct, e.g. Fire -> Smoke. Route through world_grid so
	// the cell (and any neighboring chunk sharing this border) wakes up.
	const Vector2i position = world_origin + Vector2i(x, y);
	write_cell(x, y, reaction_product(world_grid, self, config.decay_into), world_grid, world_origin);
	world_grid.mark_particle_updated(position.x, position.y);
	return true;
}

bool SandSimulationChunk::check_hp_loss(int x, int y, WorldGrid &world_grid, Vector2i world_origin) {
	Particle self = grid[get_index(x, y)];
	const MaterialConfig &config = world_grid.get_material_registry().get(self.mat_id);
	if (config.hp_loss_chance == 0)
		return false;

	if (world_grid.get_random().next_u8() >= config.hp_loss_chance) {
		// Stay awake until the particle has worn out.
		mark_dirty(x, y);
		return false;
	}

	if (self.hp > 1) {
		self.hp--;
		write_cell(x, y, self, world_grid, world_origin);
		return true;
	}

	// Worn out, e.g. burning wood crumbling into ash.
	const Vector2i position = world_origin + Vector2i(x, y);
	write_cell(x, y, reaction_product(world_grid, self, config.break_into), world_grid, world_origin);
	world_grid.mark_particle_updated(position.x, position.y);
	return true;
}
