// Heuristica — PRIVATE header shared by the survival & economy behaviour files
// (src/ai/SurvivalBehaviors.cpp and src/ai/survival/*.cpp). Not part of the frozen contract.
//
// File map:
//   survival/Common.cpp    shared queries: seasons, homes & stores, food drive, material demand, projects
//   survival/Needs.cpp     Eat, EatFromStorage, Sleep, WarmUp (+ feeding fires), Heal
//   survival/Gathering.cpp Forage, Fish, Chop, Mine, GatherHerbs
//   survival/Building.cpp  Build (start / continue / repair), HelpBuild (+ communal repair)
//   survival/Economy.cpp   Farm, Craft, Deposit, Withdraw
//   survival/Leisure.cpp   Idle, Wander, Explore, GoHome, Mope, Play, MakeArt
#pragma once

#include <optional>
#include <string>

#include "agent/Agent.h"
#include "ai/Action.h"
#include "ai/ActionHelpers.h"
#include "core/Config.h"
#include "core/Types.h"
#include "world/World.h"

namespace heur {

class Simulation;

namespace survival {

// ---- Tunables shared by several survival files (file-local ones live in each .cpp) ----------
namespace tune {
inline constexpr float kNightWorkFactor = 0.35f;     // productive-work multiplier at night (urgency lifts it to 1)
inline constexpr float kNightLeisureFactor = 0.55f;  // leisure multiplier at night
inline constexpr float kGatherHalfDist = 14.f;       // distanceFactor half-distance when walking to a resource
inline constexpr float kStructHalfDist = 12.f;       // distanceFactor half-distance when walking to a structure
inline constexpr int kStoreRadius = 25;              // storages / farms within this are "reachable"
inline constexpr int kChildSearchRadius = 12;        // children forage close by
inline constexpr float kEatUntil = 0.78f;            // keep eating while Food is below this (one unit = +0.30)
inline constexpr int kTicksPerBite = 2;
inline constexpr int kMaxTravelTicks = 110;          // actions give up walking after this many ticks
inline constexpr float kPracticeWork = 0.025f;       // Agent::practice amount per tick of continuous work (~+0.0005 skill/tick)
inline constexpr float kFuelPerWood = 100.f;         // campfire ticks of burn per wood
inline constexpr int kSeedsPerPlanting = 2;
inline constexpr int kToolWood = 2;                  // crafting recipe: 2 wood + 1 stone -> 1 tools
inline constexpr int kToolStone = 1;
inline constexpr float kRepairBelow = 0.5f;          // structures below this condition get repaired
}  // namespace tune

// ---- Registration (one per file) ------------------------------------------------------------
void registerNeedBehaviors(BehaviorList& out);
void registerGatherBehaviors(BehaviorList& out);
void registerBuildBehaviors(BehaviorList& out);
void registerEconomyBehaviors(BehaviorList& out);
void registerLeisureBehaviors(BehaviorList& out);

// ---- Time, season & stage ---------------------------------------------------------------------
bool isChild(const Agent& a, const Simulation& sim);
bool isElder(const Agent& a, const Simulation& sim);
float eveningness(const World& w);                    // 0..1 ramp during the hour before nightfall
float nightWorkFactor(const World& w, float urgency); // 1 by day; kNightWorkFactor..1 at night
float nightLeisureFactor(const World& w);
float winterPrep(const World& w);                     // 0..1 how pressing stockpiling for winter is right now
float coldSeason(const World& w);                     // 0..1 how cold the season is / is about to become

// ---- Homes & stores ----------------------------------------------------------------------------
// Own complete hut; else partner's; else (children) a parent's. nullptr if none.
const Structure* effectiveHome(const Agent& a, const Simulation& sim);
// True when `owner` is a.id, a's partner, or (for children) a parent. False for kNoAgent.
bool isHousehold(const Agent& a, AgentId owner, const Simulation& sim);
// Legitimate access to a complete Storage/Hut/Farm stock: household-owned, communal, or a.home.
bool canUseStore(const Agent& a, const Structure& s, const Simulation& sim);
// Food in reachable usable stores (communal stores count at most a fair share each).
int storedFoodAccessible(const Agent& a, const Simulation& sim, int radius);

// ---- Food drive (shared by forage / fish / explore) --------------------------------------------
enum class FoodPurpose { Hunger, Winter, Reserve, Surplus };
struct FoodDrive {
    float score = 0.f;    // total gathering drive (before distance)
    float hunger = 0.f;   // immediate-hunger part
    int want = 1;         // units to gather before stopping
    FoodPurpose purpose = FoodPurpose::Hunger;
    std::string reason;
};
FoodDrive foodDrive(const Agent& a, Simulation& sim);  // cached per agent per tick
// Mouths my household stores must feed: me + partner + dependent children (children count less).
float householdMouths(const Agent& a, const Simulation& sim);
// Days the household's food (my inventory + household stores) lasts for householdMouths().
float reserveDays(const Agent& a, const Simulation& sim);

// ---- Resources & tools ----------------------------------------------------------------------------
int searchRadius(const Agent& a, const Simulation& sim);
// world.findNearestResource(minAmount 1), memoised for the agent's current think.
std::optional<Vec2i> nearestResource(const Agent& a, Simulation& sim, ResourceKind k, int radius);
bool ownsTools(const Agent& a);
void wearTools(Agent& a, Simulation& sim, float chance);

// ---- Projects ----------------------------------------------------------------------------------------
// self.project if it is a live, incomplete site within abandon distance; else nullptr.
const Structure* activeProject(const Agent& a, const Simulation& sim);
float projectDrive(const Agent& a, const Structure& s, const Simulation& sim);
float deliveredFraction(const Structure& s);
float allowedProgress(const Structure& s);                 // labour cap given materials delivered so far
bool siteCanProgress(const Structure& s, const Inventory& inv);
bool carriesMissingMaterial(const Structure& s, const Inventory& inv);
float craftDesire(const Agent& a);
float buildLabour(const Agent& a, const Simulation& sim);  // labour units per tick
// Completion bookkeeping: stats, log, homes, emotions, clearing project pointers.
void onStructureCompleted(Structure& s, Agent& worker, Simulation& sim);

struct Demand {
    float score = 0.f;
    int units = 0;
    std::string reason;   // inspector rationale: "need wood for my hut"
    std::string forWhat;  // label suffix: "for my hut"
};
// Why (and how much) the agent wants an item right now: projects, tools, fuel, seeds, herbs.
Demand materialDemand(const Agent& a, Simulation& sim, Item item);

// ---- Naming -------------------------------------------------------------------------------------------
const char* structNoun(StructureType t);
// "my hut", "our hut", "the campfire", "the communal storage", "Mara's farm"
std::string structPhrase(const Agent& a, const Structure& s, const Simulation& sim);
// Third-person phrase for sim.log lines: "the campfire", "the communal storage", "Mara's farm".
std::string structPhraseWorld(const Structure& s, const Simulation& sim);
std::string fmt1(float v);  // one decimal

// ---- Action plumbing ----------------------------------------------------------------------------------
inline ActionStatus finish(Agent& self, ActionStatus st) {
    clearPath(self);
    return st;
}

}  // namespace survival
}  // namespace heur
