// Heuristica — helpers shared by every Behavior/Action implementation.
// FROZEN CONTRACT. Implemented in ai/ActionHelpers.cpp.
#pragma once

#include <string>
#include <vector>

#include "core/Types.h"

namespace heur {

class Agent;
class Simulation;

enum class MoveResult { Arrived, Moving, Blocked };

// Moves `self` at most one tile toward `goal` using cached A* paths (recomputed when the goal changes,
// the next step became unwalkable, or the cached path ran out). Arrived when chebyshev(pos, goal) <= range.
// Blocked when no path exists (after which callers should usually return ActionStatus::Failed).
MoveResult stepToward(Agent& self, Simulation& sim, Vec2i goal, int range = 1);

// Step one tile directly away from `threat` (best walkable neighbour maximising distance).
bool stepAway(Agent& self, Simulation& sim, Vec2i threat);

// Drop the cached path (call when an action finishes or is aborted).
void clearPath(Agent& self);

// ---- Utility-curve helpers (all return 0..1) ---------------------------------
// 1 at distance 0, 0.5 at `halfAt`, ->0 far away.
inline float distanceFactor(int dist, float halfAt = 8.f) { return 1.f / (1.f + float(dist) / halfAt); }

// Number of living agents (other than ex1/ex2) within cfg::kWitnessRadius of `where` who are awake
// (not currently sleeping). Used for moral "who would see me" heuristics.
int countWitnesses(const Simulation& sim, Vec2i where, AgentId ex1, AgentId ex2 = kNoAgent);

// Daily food requirement in food units for this agent (≈ cfg::kFoodDecay * ticksPerDay / kFoodPerUnit).
float dailyFoodUnits(const Agent& a);

// "Wealth" heuristic: inventory food + owned storage/home food, in food units.
int foodOwned(const Agent& a, const Simulation& sim);

}  // namespace heur
