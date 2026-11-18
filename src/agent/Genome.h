// Heuristica — heritable personality/physical traits.
// FROZEN CONTRACT.
#pragma once

#include <array>

#include "core/Random.h"
#include "core/Types.h"

namespace heur {

// All traits are genes in [0,1]. 0.5 is population-typical.
enum class Trait : std::uint8_t {
    Empathy,          // feels others' pain/joy; weighs harm & help to others in moral heuristics
    Integrity,        // strength of internal rules ("stealing is wrong" regardless of payoff)
    Greed,            // desire to accumulate beyond need; envy of the wealthy
    Aggression,       // willingness to use force; anger proneness
    Vengefulness,     // drive to retaliate vs. forgive
    Sociability,      // social need strength & enjoyment of company
    Industriousness,  // enjoys work, building, gathering surplus
    Curiosity,        // exploration drive; adds randomness to decisions
    Stability,        // emotional stability (inverse neuroticism): dampens emotion swings & stress gain
    Bravery,          // fear resistance & risk tolerance (witnesses, fights)
    Creativity,       // art/monuments; fun from making things
    Libido,           // mating drive
    Strength,         // combat, carry capacity, labour speed
    Intelligence,     // learning rate, planning (farms, tools, storage), less decision noise
    Count
};
inline constexpr int kTraitCount = static_cast<int>(Trait::Count);
const char* traitName(Trait t);

struct Genome {
    std::array<float, kTraitCount> genes{};

    float get(Trait t) const { return genes[std::size_t(t)]; }
    void set(Trait t, float v) { genes[std::size_t(t)] = clamp01(v); }

    // Founder genome: each gene ~ N(0.5, 0.18) clamped to [0.02, 0.98].
    static Genome random(Random& rng);
    // Child genome: per gene pick mom/dad/blend, then mutate with probability mutationRate by N(0, mutationSize).
    static Genome inherit(const Genome& mom, const Genome& dad, Random& rng,
                          float mutationRate = 0.10f, float mutationSize = 0.10f);
    // Mean absolute gene difference, 0..1.
    float distance(const Genome& o) const;
};

}  // namespace heur
