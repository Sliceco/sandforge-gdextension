#pragma once

#include <cstdint>

// Small, fast, seedable PRNG (xorshift64*) owned by each WorldGrid so a
// world's simulation is reproducible for a given seed and independent of
// other worlds and of the global rand() state.
class SimRandom {
public:
	static constexpr std::uint64_t DEFAULT_SEED = 0x9E3779B97F4A7C15ull;

	explicit SimRandom(std::uint64_t seed_value = DEFAULT_SEED) { seed(seed_value); }

	void seed(std::uint64_t seed_value) {
		// SplitMix64 scrambles small or similar seeds into well-mixed state;
		// xorshift must never start at zero.
		std::uint64_t z = seed_value + 0x9E3779B97F4A7C15ull;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		state = z ^ (z >> 31);
		if (state == 0) {
			state = DEFAULT_SEED;
		}
	}

	std::uint32_t next_u32() {
		state ^= state >> 12;
		state ^= state << 25;
		state ^= state >> 27;
		return static_cast<std::uint32_t>((state * 0x2545F4914F6CDD1Dull) >> 32);
	}

	// Uniform in [0, 255], for comparing against 0-255 material chances.
	std::uint8_t next_u8() { return static_cast<std::uint8_t>(next_u32() >> 24); }

	// Returns +1 or -1 with equal probability.
	int next_sign() { return (next_u32() >> 31) ? 1 : -1; }

	// Uniform in [0, bound).
	std::uint32_t next_below(std::uint32_t bound) { return static_cast<std::uint32_t>((static_cast<std::uint64_t>(next_u32()) * bound) >> 32); }

private:
	std::uint64_t state = DEFAULT_SEED;
};
