// Heuristica — population statistics & display labels. See Census.h.
#include "render/Census.h"

#include <algorithm>
#include <cstdio>

#include "core/Config.h"

namespace heur {

Census takeCensus(const Simulation& sim) {
    Census c;
    const Tick now = sim.now();
    c.tick = now;
    c.day = dayOfTick(now);
    for (const auto& up : sim.agents()) {
        const Agent& a = *up;
        if (!a.alive) continue;
        ++c.population;
        switch (a.stage(now)) {
            case LifeStage::Child: ++c.children; break;
            case LifeStage::Adult: ++c.adults; break;
            case LifeStage::Elder: ++c.elders; break;
        }
        (a.sex == Sex::Male ? c.males : c.females)++;
        if (a.pregnant) ++c.pregnant;
        if (a.mentalState == MentalState::Stressed) ++c.stressed;
        if (a.inBreakdown()) ++c.inBreakdown;
        if (a.actionKind == kinds::Sleep) ++c.sleeping;
        c.maxGeneration = std::max(c.maxGeneration, a.generation);
        c.avgMood += a.mood();
        c.avgStress += a.stress;
        c.avgHealth += a.health;
        c.avgFood += a.needs.get(Need::Food);
        c.avgStability += a.emotionalStability();
        c.avgAgeDays += float(a.ageDays(now));
        c.foodCarried += a.inventory.get(Item::Food);
        for (int t = 0; t < kTraitCount; ++t) c.avgTraits[std::size_t(t)] += a.genome.genes[std::size_t(t)];
        for (int n = 0; n < kNormCount; ++n) c.avgNorms[std::size_t(n)] += normValue(a.norms, n);
        c.clanSizes[a.clan]++;
    }
    if (c.population > 0) {
        const float inv = 1.f / float(c.population);
        c.avgMood *= inv;
        c.avgStress *= inv;
        c.avgHealth *= inv;
        c.avgFood *= inv;
        c.avgStability *= inv;
        c.avgAgeDays *= inv;
        for (float& t : c.avgTraits) t *= inv;
        for (float& n : c.avgNorms) n *= inv;
    }
    for (const auto& [id, s] : sim.world().structures()) {
        if (s.complete) {
            ++c.structuresComplete;
            c.completeByType[std::size_t(s.type)]++;
        } else {
            ++c.structuresBuilding;
        }
    }
    c.wildFood = sim.world().wildFoodTotal();
    return c;
}

std::vector<const Agent*> livingAgents(const Simulation& sim) {
    std::vector<const Agent*> out;
    out.reserve(sim.agents().size());
    for (const auto& up : sim.agents())
        if (up && up->alive) out.push_back(up.get());
    std::sort(out.begin(), out.end(), [](const Agent* a, const Agent* b) { return a->id < b->id; });
    return out;
}

float normValue(const Norms& n, int index) {
    switch (index) {
        case 0: return n.theftTolerance;
        case 1: return n.violenceTolerance;
        case 2: return n.sharingExpectation;
        case 3: return n.punishmentExpectation;
        default: return 0.f;
    }
}

const char* traitLabel(Trait t) {
    static constexpr const char* k[] = {"Empathy",   "Integrity", "Greed",      "Aggression",   "Vengefulness",
                                        "Sociability", "Industriousness", "Curiosity", "Stability", "Bravery",
                                        "Creativity", "Libido",    "Strength",   "Intelligence"};
    static_assert(sizeof(k) / sizeof(k[0]) == kTraitCount);
    return k[std::size_t(t)];
}

const char* traitShort(Trait t) {
    static constexpr const char* k[] = {"Empath", "Integr", "Greed", "Aggro", "Venge", "Social", "Indust",
                                        "Curio",  "Stable", "Brave", "Create", "Libido", "Strong", "Intel"};
    static_assert(sizeof(k) / sizeof(k[0]) == kTraitCount);
    return k[std::size_t(t)];
}

const char* needLabel(Need n) {
    static constexpr const char* k[] = {"Food", "Rest", "Social", "Warmth", "Fun", "Intimacy"};
    static_assert(sizeof(k) / sizeof(k[0]) == kNeedCount);
    return k[std::size_t(n)];
}

const char* emotionLabel(Emotion e) {
    static constexpr const char* k[] = {"Joy", "Sadness", "Anger", "Fear", "Guilt", "Pride", "Envy"};
    static_assert(sizeof(k) / sizeof(k[0]) == kEmotionCount);
    return k[std::size_t(e)];
}

const char* breakLabel(BreakType b) {
    switch (b) {
        case BreakType::None: return "none";
        case BreakType::Rampage: return "RAMPAGE";
        case BreakType::Despair: return "DESPAIR";
        case BreakType::Panic: return "PANIC";
        case BreakType::Kleptomania: return "KLEPTOMANIA";
    }
    return "?";
}

const char* mentalLabel(MentalState m) {
    switch (m) {
        case MentalState::Stable: return "Stable";
        case MentalState::Stressed: return "Stressed";
        case MentalState::Breaking: return "BREAKDOWN";
    }
    return "?";
}

const char* stageLabel(LifeStage s) {
    switch (s) {
        case LifeStage::Child: return "child";
        case LifeStage::Adult: return "adult";
        case LifeStage::Elder: return "elder";
    }
    return "?";
}

const char* seasonLabel(Season s) {
    switch (s) {
        case Season::Spring: return "Spring";
        case Season::Summer: return "Summer";
        case Season::Autumn: return "Autumn";
        case Season::Winter: return "Winter";
    }
    return "?";
}

const char* normLabel(int index) {
    static constexpr const char* k[] = {"Theft tolerance", "Violence tolerance", "Sharing expectation",
                                        "Punishment expect."};
    return (index >= 0 && index < kNormCount) ? k[index] : "?";
}

const char* normShort(int index) {
    static constexpr const char* k[] = {"theft", "violence", "sharing", "punish"};
    return (index >= 0 && index < kNormCount) ? k[index] : "?";
}

const char* structureLabel(StructureType t) {
    static constexpr const char* k[] = {"Campfire", "Hut", "Storage", "Farm", "Wall", "Workshop", "Monument"};
    static_assert(sizeof(k) / sizeof(k[0]) == kStructureTypeCount);
    return k[std::size_t(t)];
}

const char* eventLabel(EventKind k) {
    static constexpr const char* n[] = {"birth", "death", "theft",     "gift",   "fight", "build", "romance",
                                        "break", "justice", "social", "world",  "misc"};
    static_assert(sizeof(n) / sizeof(n[0]) == std::size_t(EventKind::Count));
    return n[std::size_t(k)];
}

std::string describeMemory(const Memory& m, const Simulation& sim) {
    const std::string o = sim.nameOf(m.other);
    const std::string s = sim.nameOf(m.subject);
    switch (m.type) {
        case MemoryType::ReceivedGift: return "got a gift from " + o;
        case MemoryType::GaveGift: return "gave a gift to " + o;
        case MemoryType::StolenFrom: return "was robbed by " + o;
        case MemoryType::Stole: return m.other == kNoAgent ? "stole from a storage" : "stole from " + o;
        case MemoryType::WitnessedTheft: return "saw " + o + " rob " + s;
        case MemoryType::Attacked: return "was attacked by " + o;
        case MemoryType::AttackedSomeone: return "attacked " + o;
        case MemoryType::WitnessedAttack: return "saw " + o + " attack " + s;
        case MemoryType::Chatted: return "chatted with " + o;
        case MemoryType::Comforted: return "was comforted by " + o;
        case MemoryType::Mated: return "made love with " + o;
        case MemoryType::ChildBorn: return "had a child, " + o;
        case MemoryType::LovedOneDied: return "lost " + o;
        case MemoryType::Punished: return "was punished by " + o;
        case MemoryType::PunishedSomeone: return "punished " + o;
        case MemoryType::Forgave: return "forgave " + o;
        case MemoryType::BuiltTogether: return "built with " + o;
        case MemoryType::Traded: return "traded with " + o;
        case MemoryType::Rejected: return "was rejected by " + o;
        case MemoryType::Taught: return "was taught by " + o;
        default: return "?";
    }
}

int dayOfTick(Tick t) { return int(t / Tick(cfg::kTicksPerDay)); }

std::string clockString(Tick t) {
    const int inDay = int(t % Tick(cfg::kTicksPerDay));
    const int minutes = inDay * 24 * 60 / cfg::kTicksPerDay;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", (minutes / 60) % 24, minutes % 60);
    return buf;
}

int temperatureCelsius(float t01) { return int(std::lround(-8.f + clamp01(t01) * 42.f)); }

std::string fmtSigned(float v, int decimals) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%+.*f", decimals, double(v));
    return buf;
}

std::string fmtFixed(float v, int decimals) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, double(v));
    return buf;
}

std::string fmtCompact(int v) {
    char buf[32];
    if (v >= 100000) std::snprintf(buf, sizeof(buf), "%dk", v / 1000);
    else if (v >= 10000) std::snprintf(buf, sizeof(buf), "%.1fk", double(v) / 1000.0);
    else std::snprintf(buf, sizeof(buf), "%d", v);
    return buf;
}

}  // namespace heur
