// Heuristica — utility-AI decision maker.
// FROZEN CONTRACT. Implemented in ai/Brain.cpp.
#pragma once

#include "ai/Action.h"

namespace heur {

class Agent;
class Simulation;

class Brain {
public:
    Brain();  // registers survival + social behaviours
    // Re-evaluates options when the agent is idle, its action finished, wantsRethink is set, or
    // now >= nextThink (and the current action is interruptible). Picks via scored proposals.
    void think(Agent& self, Simulation& sim);
    // Ticks the current action; clears it on Done/Failed.
    void act(Agent& self, Simulation& sim);
    // Aborts the current action (e.g. death).
    void abort(Agent& self, Simulation& sim);
    const BehaviorList& behaviors() const { return behaviors_; }

private:
    BehaviorList behaviors_;
    std::vector<Proposal> scratch_;
};

}  // namespace heur
