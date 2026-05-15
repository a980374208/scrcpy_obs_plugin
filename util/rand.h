#pragma once

#include <cstdint>
#include <random>

struct sc_rand {
	std::mt19937_64 engine;
	sc_rand() : engine(std::random_device{}()) {}

	inline void seed(uint64_t s) { this->engine.seed(s); }

	uint32_t sc_rand_u32();
	uint64_t sc_rand_u64();
};
