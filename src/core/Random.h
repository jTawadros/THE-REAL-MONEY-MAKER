// Heuristica — deterministic RNG wrapper (header-only).
// FROZEN CONTRACT.
#pragma once

#include <algorithm>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

namespace heur {

class Random {
public:
    explicit Random(std::uint64_t seed = 0xC0FFEEull) : seed_(seed), eng_(seed) {}

    std::uint64_t seed() const { return seed_; }
    std::uint64_t next() { return eng_(); }

    // Uniform float in [0, 1).
    float uniform() { return std::uniform_real_distribution<float>(0.f, 1.f)(eng_); }
    // Uniform float in [lo, hi).
    float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }
    // Uniform int in [lo, hi] (inclusive).
    int rangeInt(int lo, int hi) {
        if (hi <= lo) return lo;
        return std::uniform_int_distribution<int>(lo, hi)(eng_);
    }
    bool chance(float p) { return uniform() < p; }
    float normal(float mean, float stddev) { return std::normal_distribution<float>(mean, stddev)(eng_); }

    template <class T>
    T& pick(std::vector<T>& v) { return v[std::size_t(rangeInt(0, int(v.size()) - 1))]; }
    template <class T>
    const T& pick(const std::vector<T>& v) { return v[std::size_t(rangeInt(0, int(v.size()) - 1))]; }

    template <class It>
    void shuffle(It first, It last) { std::shuffle(first, last, eng_); }

    // Index chosen with probability proportional to weights[i] (weights >= 0). Returns -1 if all zero.
    int weightedIndex(const std::vector<float>& weights) {
        float sum = 0.f;
        for (float w : weights) sum += (w > 0.f ? w : 0.f);
        if (sum <= 0.f) return -1;
        float r = uniform() * sum;
        for (std::size_t i = 0; i < weights.size(); ++i) {
            const float w = weights[i] > 0.f ? weights[i] : 0.f;
            if (r < w) return int(i);
            r -= w;
        }
        return int(weights.size()) - 1;
    }

    std::mt19937_64& engine() { return eng_; }

private:
    std::uint64_t seed_;
    std::mt19937_64 eng_;
};

}  // namespace heur
