// Heuristica — PRIVATE helpers shared by the social behaviour files (ai/SocialBehaviors.cpp and
// ai/social/*.cpp). Not part of the frozen contract.
//
// File map:
//   Conversation.cpp : Chat (+gossip), Comfort, Teach, Mourn, FollowParent
//   Exchange.cpp     : Share, Gift, Trade, FeedChild, Beg
//   Crime.cpp        : Steal (pickpocket), StealStorage, Vandalize
//   Conflict.cpp     : Attack, Flee, Confront (punish), Apologize, Forgive
//   Romance.cpp      : Court, Mate
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "agent/Agent.h"
#include "ai/Action.h"
#include "ai/ActionHelpers.h"
#include "ai/Morality.h"
#include "core/Config.h"
#include "core/Types.h"
#include "sim/Simulation.h"
#include "social/Social.h"
#include "social/SocialInternal.h"
#include "world/World.h"

namespace heur::social {

// ---- Registration (one per file) ------------------------------------------------
void addConversationBehaviors(BehaviorList& out);
void addExchangeBehaviors(BehaviorList& out);
void addCrimeBehaviors(BehaviorList& out);
void addConflictBehaviors(BehaviorList& out);
void addRomanceBehaviors(BehaviorList& out);

// ---- Cross-file queries (implemented by the file that owns the action type) ----------
// Whom `a` is currently hitting (Attack, or the fight phase of Confront). kNoAgent if nobody.
AgentId attackTargetOf(const Agent& a);   // Conflict.cpp
// Whom `a` is currently chatting with. kNoAgent if nobody.
AgentId chatTargetOf(const Agent& a);     // Conversation.cpp

// ---- State & kinship --------------------------------------------------------------
inline bool isAsleep(const Agent& a) { return a.actionKind == kinds::Sleep; }
inline bool isChildAgent(const Agent& a, const Simulation& sim) { return a.stage(sim.now()) == LifeStage::Child; }
inline bool isRampaging(const Agent& a) { return a.inBreakdown() && a.breakType == BreakType::Rampage; }
inline bool isFighting(const Agent& a) { return a.actionKind == kinds::Attack || a.actionKind == kinds::Flee; }
// sim.agent(id) that is alive, else nullptr (kNoAgent -> nullptr).
Agent* living(Simulation& sim, AgentId id);
const Agent* living(const Simulation& sim, AgentId id);
// Blood relatives (parent/child/sibling) — NOT partners (Agent::isFamily includes partners).
bool bloodKin(const Agent& a, const Agent& b);
inline bool isParentOf(const Agent& parent, const Agent& child) {
    return child.mother == parent.id || child.father == parent.id;
}
// Family (incl. partner) or a close friend.
bool cares(const Agent& self, const Agent& other);
// 0.2..1: leisure/social drives fade when my own body is in trouble (hunger, cold).
float needsDamp(const Agent& a);

// ---- Food & items -------------------------------------------------------------------
int foodToSate(const Agent& a);               // whole food units to reach ~95% Food
int spareFood(const Agent& a);                // inventory food beyond what `a` wants to keep (Greed/hunger aware)
float itemValue(Item i);                      // barter value in food-equivalents
// Inventory -> inventory, limited by what `from` has and `to` can carry. Returns units moved.
int giveItems(Agent& from, Agent& to, Item item, int n);
// Food: `to` eats on the spot up to sated, the rest goes into their inventory. Returns units given.
int feedFood(Agent& from, Agent& to, int n);
std::string describeItems(const ItemCounts& c);   // "3 food and 1 tools"
std::string describeItems(Item i, int n);         // "3 food"

// ---- Relationships & memory -------------------------------------------------------------
void nudgeRelation(Agent& a, AgentId other, float dAffinity, float dTrust, Tick now);
// Reciprocity ledgers: receiver owes giver (value in food-equivalents).
void addBalance(Agent& giver, Agent& receiver, float value);
// Most recent memory of type `t` about `other` (kNoAgent = about anyone) no older than `within` ticks.
const Memory* recentMemory(const Agent& a, MemoryType t, AgentId other, Tick now, Tick within);
Memory* recentMemory(Agent& a, MemoryType t, AgentId other, Tick now, Tick within);

// ---- Combat -------------------------------------------------------------------------------
// 0..~1: Strength, Fighting skill, tools, health, life stage.
float combatPower(const Agent& a, const Simulation& sim);
enum class StrikeResult { Missed, Hit, Killed };
// One melee swing. Applies damage (victim.damage(..., "killed by X", attacker.id)), practice, rethink.
StrikeResult strike(Agent& attacker, Agent& victim, Simulation& sim);

// ---- Actions ----------------------------------------------------------------------------------
inline ActionStatus finish(Agent& self, ActionStatus s) {
    clearPath(self);
    return s;
}
inline void pushProposal(std::vector<Proposal>& out, float score, std::unique_ptr<Action> a, std::string reason) {
    if (!(score > 0.f) || !a) return;
    Proposal p;
    p.score = score;
    p.action = std::move(a);
    p.reason = std::move(reason);
    out.push_back(std::move(p));
}
DeedContext makeDeed(Deed d, const Agent* target, float selfGain, float harm, float benefit, int witnesses,
                     const Structure* st = nullptr);

// ---- Text ---------------------------------------------------------------------------------------
std::string pct(float v01);                     // "72%"
inline const char* heShe(const Agent& a) { return a.sex == Sex::Female ? "she" : "he"; }
inline const char* himHer(const Agent& a) { return a.sex == Sex::Female ? "her" : "him"; }
inline const char* hisHer(const Agent& a) { return a.sex == Sex::Female ? "her" : "his"; }
std::string watchers(int witnesses);            // "nobody watching" / "2 watching"
// "situation; rationale" (rationale from the conscience, if any).
std::string withWhy(const std::string& situation, const Verdict& v);
const char* structureNoun(StructureType t);

}  // namespace heur::social
