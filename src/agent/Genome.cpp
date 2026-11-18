// Heuristica — heritable traits: founder generation, inheritance with mutation, genetic distance.
#include "agent/Genome.h"

#include <cmath>

namespace heur {

namespace {

constexpr float kFounderMean = 0.5f;
constexpr float kFounderStdDev = 0.18f;
constexpr float kGeneMin = 0.02f;
constexpr float kGeneMax = 0.98f;

// Inheritance: probability of copying mom's gene, dad's gene; remainder blends both.
constexpr float kPickMom = 0.40f;
constexpr float kPickDad = 0.40f;

float clampGene(float v) { return clampf(v, kGeneMin, kGeneMax); }

}  // namespace

const char* traitName(Trait t) {
    switch (t) {
        case Trait::Empathy: return "Empathy";
        case Trait::Integrity: return "Integrity";
        case Trait::Greed: return "Greed";
        case Trait::Aggression: return "Aggression";
        case Trait::Vengefulness: return "Vengefulness";
        case Trait::Sociability: return "Sociability";
        case Trait::Industriousness: return "Industriousness";
        case Trait::Curiosity: return "Curiosity";
        case Trait::Stability: return "Stability";
        case Trait::Bravery: return "Bravery";
        case Trait::Creativity: return "Creativity";
        case Trait::Libido: return "Libido";
        case Trait::Strength: return "Strength";
        case Trait::Intelligence: return "Intelligence";
        default: return "?";
    }
}

Genome Genome::random(Random& rng) {
    Genome g;
    for (float& gene : g.genes) gene = clampGene(rng.normal(kFounderMean, kFounderStdDev));
    return g;
}

Genome Genome::inherit(const Genome& mom, const Genome& dad, Random& rng, float mutationRate, float mutationSize) {
    Genome child;
    for (std::size_t i = 0; i < child.genes.size(); ++i) {
        const float r = rng.uniform();
        float v;
        if (r < kPickMom) {
            v = mom.genes[i];
        } else if (r < kPickMom + kPickDad) {
            v = dad.genes[i];
        } else {
            v = 0.5f * (mom.genes[i] + dad.genes[i]);
        }
        if (mutationSize > 0.f && rng.chance(mutationRate)) v += rng.normal(0.f, mutationSize);
        child.genes[i] = clampGene(v);
    }
    return child;
}

float Genome::distance(const Genome& o) const {
    float sum = 0.f;
    for (std::size_t i = 0; i < genes.size(); ++i) sum += std::fabs(genes[i] - o.genes[i]);
    return genes.empty() ? 0.f : sum / float(genes.size());
}

}  // namespace heur
