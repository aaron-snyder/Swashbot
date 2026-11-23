// File: Random.h
// Small wrapper around <random>. Replaces rand()/srand(): one engine is seeded once and shared,
// and tests can pass a fixed seed to get repeatable dice rolls.

#pragma once
#include <cstdint>
#include <random>

class Rng {
public:
    Rng() : engine(std::random_device{}()) {}
    explicit Rng(uint32_t seed) : engine(seed) {}

    // Uniform integer in [lo, hi] (inclusive)
    int range(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(engine); }

    // Roll one die with the given number of sides (1..sides)
    int die(int sides) { return range(1, sides); }

    // True `percent`% of the time
    bool chance(int percent) { return range(1, 100) <= percent; }

    // Pick a random element from a non-empty container
    template <typename C>
    const auto& pick(const C& items) { return items[static_cast<size_t>(range(0, static_cast<int>(items.size()) - 1))]; }

private:
    std::mt19937 engine;
};
