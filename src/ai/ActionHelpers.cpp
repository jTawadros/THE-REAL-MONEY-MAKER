// Heuristica — shared movement & heuristic helpers for behaviours/actions.
#include "ai/ActionHelpers.h"

#include <algorithm>
#include <unordered_map>

#include "agent/Agent.h"
#include "ai/Action.h"
#include "core/Config.h"
#include "sim/Simulation.h"
#include "world/Pathfinding.h"
#include "world/World.h"

namespace heur {

namespace {
// When chasing a moving goal, keep following the old path if the goal only drifted a little
// relative to how far away it still is (saves A* calls during long chases).
bool goalDriftAcceptable(const Agent& self, Vec2i goal) {
    if (self.pathGoal.x < 0) return false;
    const int drift = chebyshev(goal, self.pathGoal);
    const int dist = chebyshev(self.pos, goal);
    return drift <= 1 + dist / 6;
}

bool recomputePath(Agent& self, Simulation& sim, Vec2i goal, int range) {
    self.path = findPath(sim.world(), self.pos, goal, range);
    std::reverse(self.path.begin(), self.path.end());  // next step at back()
    self.pathGoal = goal;
    self.pathRange = range;
    if (self.path.empty()) {
        ++self.pathFailures;
        return false;
    }
    self.pathFailures = 0;
    return true;
}
}  // namespace

MoveResult stepToward(Agent& self, Simulation& sim, Vec2i goal, int range) {
    if (range < 0) range = 0;
    if (chebyshev(self.pos, goal) <= range) return MoveResult::Arrived;

    const World& world = sim.world();
    bool valid = !self.path.empty() && self.pathRange == range &&
                 (self.pathGoal == goal || goalDriftAcceptable(self, goal));
    if (valid) {
        const Vec2i next = self.path.back();
        valid = chebyshev(self.pos, next) == 1 && world.walkable(next);
    }
    if (!valid && !recomputePath(self, sim, goal, range)) return MoveResult::Blocked;

    self.pos = self.path.back();
    self.path.pop_back();
    // Path exhausted but goal moved out of range: recompute next tick.
    if (chebyshev(self.pos, goal) <= range) return MoveResult::Arrived;
    return MoveResult::Moving;
}

bool stepAway(Agent& self, Simulation& sim, Vec2i threat) {
    const World& world = sim.world();
    const float current = euclid(self.pos, threat);
    Vec2i best = self.pos;
    float bestScore = current;
    for (const Vec2i d : kDirs8) {
        const Vec2i n = self.pos + d;
        if (!world.walkable(n)) continue;
        // Diagonal corner-cutting check.
        if (d.x != 0 && d.y != 0 &&
            (!world.walkable(self.pos + Vec2i{d.x, 0}) || !world.walkable(self.pos + Vec2i{0, d.y})))
            continue;
        const float s = euclid(n, threat) - 0.15f * (world.moveCost(n) - 1.f);
        if (s > bestScore + 1e-3f) {
            bestScore = s;
            best = n;
        }
    }
    clearPath(self);
    if (best == self.pos) return false;
    self.pos = best;
    return true;
}

void clearPath(Agent& self) {
    self.path.clear();
    self.pathGoal = Vec2i{-1, -1};
    self.pathRange = -1;
}

int countWitnesses(const Simulation& sim, Vec2i where, AgentId ex1, AgentId ex2) {
    int n = 0;
    for (const Agent* a : sim.agentsNear(where, cfg::kWitnessRadius)) {
        if (!a->alive || a->id == ex1 || a->id == ex2) continue;
        if (a->actionKind == kinds::Sleep) continue;
        ++n;
    }
    return n;
}

float dailyFoodUnits(const Agent& /*a*/) {
    // DESIGN.md: adult ≈ 2.4 units/day. Children need less (kChildDecayMultiplier) but we keep the
    // conservative adult baseline so planning heuristics err on the side of stockpiling.
    return cfg::kFoodDecay * float(cfg::kTicksPerDay) / cfg::kFoodPerUnit;
}

int foodOwned(const Agent& a, const Simulation& sim) {
    // Hot path (millions of calls per run): build an owner -> stored-food index once per tick instead of
    // scanning every structure on every call. Heuristic callers tolerate within-tick staleness.
    struct Index {
        const Simulation* sim = nullptr;
        Tick tick = ~Tick(0);
        std::size_t structures = 0;
        std::unordered_map<AgentId, int> byOwner;
    };
    static Index idx;  // single-threaded simulation
    if (idx.sim != &sim || idx.tick != sim.now() || idx.structures != sim.world().structures().size()) {
        idx.sim = &sim;
        idx.tick = sim.now();
        idx.structures = sim.world().structures().size();
        idx.byOwner.clear();
        for (const auto& [sid, s] : sim.world().structures()) {
            if (!s.complete || s.owner == kNoAgent) continue;
            if (s.type != StructureType::Storage && s.type != StructureType::Hut) continue;
            idx.byOwner[s.owner] += s.stock.get(Item::Food);
        }
    }
    int food = a.inventory.get(Item::Food);
    if (auto it = idx.byOwner.find(a.id); it != idx.byOwner.end()) food += it->second;
    if (a.partner != kNoAgent)
        if (auto it = idx.byOwner.find(a.partner); it != idx.byOwner.end()) food += it->second;
    // A communal hut that is my home counts as mine too.
    if (const Structure* h = sim.world().structure(a.home); h && h->complete && h->owner == kNoAgent)
        food += h->stock.get(Item::Food);
    return food;
}

}  // namespace heur
