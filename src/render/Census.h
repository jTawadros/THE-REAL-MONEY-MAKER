// Heuristica — population statistics & display labels shared by the live UI and headless reports.
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "agent/Agent.h"
#include "core/Types.h"
#include "sim/Simulation.h"
#include "world/World.h"

namespace heur {

inline constexpr int kNormCount = 4;  // theft, violence, sharing, punishment

// A snapshot of the living population, computed directly from the agents.
struct Census {
    Tick tick = 0;
    int day = 0;
    int population = 0;
    int adults = 0;
    int children = 0;
    int elders = 0;
    int males = 0;
    int females = 0;
    int pregnant = 0;
    int stressed = 0;
    int inBreakdown = 0;
    int sleeping = 0;
    int maxGeneration = 0;
    float avgMood = 0.f;
    float avgStress = 0.f;
    float avgHealth = 0.f;
    float avgFood = 0.f;
    float avgStability = 0.f;
    float avgAgeDays = 0.f;
    int foodCarried = 0;
    std::array<float, kTraitCount> avgTraits{};
    std::array<float, kNormCount> avgNorms{};
    std::map<int, int> clanSizes;  // clan id -> living members
    // World
    int structuresComplete = 0;
    int structuresBuilding = 0;
    std::array<int, kStructureTypeCount> completeByType{};
    float wildFood = 0.f;
};

Census takeCensus(const Simulation& sim);

// Living agents sorted by id (stable order for cycling selection and the agent list).
std::vector<const Agent*> livingAgents(const Simulation& sim);

float normValue(const Norms& n, int index);

// ---- Display labels (fixed width-friendly; independent of other modules' name tables) ----------
const char* traitLabel(Trait t);   // "Empathy", "Industriousness", ...
const char* traitShort(Trait t);   // <= 6 chars
const char* needLabel(Need n);
const char* emotionLabel(Emotion e);
const char* breakLabel(BreakType b);
const char* mentalLabel(MentalState m);
const char* stageLabel(LifeStage s);
const char* seasonLabel(Season s);
const char* normLabel(int index);       // "Theft tolerance", ...
const char* normShort(int index);       // "theft", ...
const char* structureLabel(StructureType t);
const char* eventLabel(EventKind k);
// "was robbed by", "chatted with", ... (memory type -> verb phrase; "%s" marks the other agent)
std::string describeMemory(const Memory& m, const Simulation& sim);

// In-game clock helpers.
int dayOfTick(Tick t);
std::string clockString(Tick t);          // "14:30"
int temperatureCelsius(float temperature01);  // display mapping of World::temperature()

// Formatting helpers.
std::string fmtSigned(float v, int decimals = 2);  // "+0.12"
std::string fmtFixed(float v, int decimals = 2);   // "0.12"
std::string fmtCompact(int v);                     // 1234 -> "1.2k"

}  // namespace heur
