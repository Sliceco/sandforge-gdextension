#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "materialconfig.h"

namespace godot {

// A neighbor-triggered transformation evaluated from the point of view of
// the material it is registered on: when a cell of that material touches a
// cell of `other`, with probability chance/256 per tick the cell becomes
// `self_into` and the neighbor becomes `other_into`. Setting a product to
// the original material leaves that cell unchanged (e.g. acid that
// dissolves stone without being consumed). With a non-zero `damage`, the
// neighbor instead loses that much hp per successful roll and only becomes
// `other_into` once its hp is exhausted (e.g. acid eroding hard stone).
struct ReactionRule {
	std::uint8_t other = 0;
	std::uint8_t self_into = 0;
	std::uint8_t other_into = 0;
	std::uint8_t chance = 0;
	std::uint8_t damage = 0;
};

// Per-world material definitions and reaction rules, indexed by mat_id.
// Every uint8_t mat_id has an entry, so lookups never need bounds checks;
// unregistered IDs keep a default (EMPTY state) config.
class MaterialRegistry {
public:
	static constexpr int MAX_MATERIALS = 256;

	const MaterialConfig &get(std::uint8_t mat_id) const { return materials[mat_id]; }
	void set(const MaterialConfig &config) { materials[config.id] = config; }

	const std::vector<ReactionRule> &get_reactions(std::uint8_t mat_id) const { return reactions[mat_id]; }
	void add_reaction(std::uint8_t mat_id, const ReactionRule &rule) { reactions[mat_id].push_back(rule); }

	void clear_reactions(std::uint8_t mat_id) { reactions[mat_id].clear(); }

	void clear_reactions() {
		for (std::vector<ReactionRule> &rules : reactions) {
			rules.clear();
		}
	}

	// Removes every material and reaction rule.
	void clear() {
		materials = {};
		clear_reactions();
	}

private:
	std::array<MaterialConfig, MAX_MATERIALS> materials{};
	std::array<std::vector<ReactionRule>, MAX_MATERIALS> reactions;
};

} // namespace godot
