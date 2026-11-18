// Heuristica — social dynamics: witnessing, reputation via gossip, norm learning, attraction.
// FROZEN CONTRACT. Implemented in social/Social.cpp.
#pragma once

#include "ai/Morality.h"
#include "core/Types.h"

namespace heur {

class Agent;
class Simulation;

// Announce a deed that just happened at `where`. Every living, awake agent within cfg::kWitnessRadius
// (except the actor) runs appraise(): updates relationship (affinity/trust/grievance) toward the actor,
// feels anger/fear/joy, stores a memory (WitnessedTheft/WitnessedAttack/...), and nudges its Norms
// (social learning: e.g. seeing theft go unpunished raises theftTolerance; seeing punishment raises
// punishmentExpectation and lowers theftTolerance). The target (victim/beneficiary) reacts strongest
// even if out of sight for direct deeds (they notice their stuff is gone). Sets wantsRethink on
// strongly affected agents (victims of attacks, etc). Also logs notable deeds via sim.log().
void broadcastDeed(Simulation& sim, Agent& actor, const DeedContext& ctx, Vec2i where);

// Conversation: speaker shares opinions of up to 2 third parties the speaker feels strongly about.
// The listener blends them into its own relationships weighted by its trust in the speaker
// (reputation spreads; thieves become known). Also mutual affinity/trust bump scaled by Sociability,
// and partial norm convergence between the two.
void gossip(Simulation& sim, Agent& speaker, Agent& listener);

// Heuristic attraction of `a` toward `b` (0..1): health, mood, genetic dissimilarity (incest
// avoidance: near-0 for family), status (food/structures), existing affinity, age suitability.
float attraction(const Agent& a, const Agent& b, const Simulation& sim);

// Would `chooser` accept `suitor` as partner/mate right now? Uses attraction, trust, existing partner
// loyalty (Integrity), mood and Libido. Deterministic given state + sim.rng().
bool acceptsAdvance(const Agent& chooser, const Agent& suitor, Simulation& sim);

// Average trust others hold in `a` (reputation, 0..1). 0.5 if nobody knows them.
float reputation(const Agent& a, const Simulation& sim);

}  // namespace heur
