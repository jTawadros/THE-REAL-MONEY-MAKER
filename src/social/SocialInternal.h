// Heuristica — PRIVATE extension of social/Social.h (not part of the frozen contract).
// Used by social/Social.cpp and the social behaviours in ai/social/*.cpp only.
#pragma once

#include <cstdint>

#include "ai/Morality.h"
#include "core/Types.h"

namespace heur {

class Agent;
class Simulation;

// How much the deed's target (ctx.target) knows about it.
enum class TargetAwareness : std::uint8_t {
    Identifies,   // the target knows exactly who did it (default broadcastDeed behaviour)
    NoticesLoss,  // theft/vandalism: the target notices the loss but not the culprit (a disapproving
                  // witness may still tell them, upgrading this to Identifies)
    Unaware,      // the target doesn't notice at all (unless a witness tells them)
};

// broadcastDeed() with explicit target awareness. broadcastDeed(...) == broadcastDeedEx(..., Identifies).
void broadcastDeedEx(Simulation& sim, Agent& actor, const DeedContext& ctx, Vec2i where, TargetAwareness awareness);

}  // namespace heur
