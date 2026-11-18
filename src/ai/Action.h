// Heuristica — Utility-AI action framework.
//
//   Behavior  : a generator. Each tick the brain re-thinks, every Behavior inspects the agent & world and
//               pushes zero or more scored Proposals (concrete Actions bound to targets).
//   Action    : a stateful multi-tick task (walk somewhere, then do something). Returned by proposals.
//   Brain     : collects proposals from every registered Behavior, applies personality/breakdown modifiers,
//               commitment hysteresis and bounded-rational noise, then runs the winner.
//
// Adding a new thing agents can do = write one Behavior + one Action and register it. That's how agents
// "can do anything": the action space is open-ended and the brain needs no changes.
//
// FROZEN CONTRACT.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace heur {

class Agent;
class Simulation;

enum class ActionStatus { Running, Done, Failed };

class Action {
public:
    virtual ~Action() = default;
    // Stable machine id, one of kinds::* below. Used for hysteresis & breakdown modifiers.
    virtual const char* kind() const = 0;
    // Human readable, present-progressive: "Stealing food from Mara".
    virtual std::string label() const = 0;
    // Advance one tick. Most actions: stepToward(target) until arrived, then work for N ticks.
    virtual ActionStatus tick(Agent& self, Simulation& sim) = 0;
    // Called when the brain replaces this action before it finished (or the agent died).
    virtual void onAbort(Agent& /*self*/, Simulation& /*sim*/) {}
    // If false the brain will not preempt it for a better option (it may still be aborted by breakdowns).
    virtual bool interruptible() const { return true; }
};

struct Proposal {
    float score = 0.f;               // utility; typical range 0..1.5. <= 0 proposals are discarded.
    std::unique_ptr<Action> action;  // must be non-null
    std::string reason;              // short rationale shown in the inspector ("starving, berries nearby")
};

class Behavior {
public:
    virtual ~Behavior() = default;
    virtual const char* name() const = 0;
    // Inspect (don't mutate) the agent and push candidate proposals. May use sim.rng().
    virtual void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) = 0;
};

using BehaviorList = std::vector<std::unique_ptr<Behavior>>;

// Implemented in ai/SurvivalBehaviors.cpp
void registerSurvivalBehaviors(BehaviorList& out);
// Implemented in ai/SocialBehaviors.cpp
void registerSocialBehaviors(BehaviorList& out);

// Stable action kinds. Every Action::kind() must return one of these pointers' strings.
namespace kinds {
// --- survival / economy (ai/SurvivalBehaviors.cpp) ---
inline constexpr const char* Idle = "idle";
inline constexpr const char* Wander = "wander";
inline constexpr const char* Explore = "explore";
inline constexpr const char* Forage = "forage";          // berries / grain -> food, seeds
inline constexpr const char* Fish = "fish";
inline constexpr const char* Eat = "eat";                // eat from own inventory
inline constexpr const char* EatFromStorage = "eat_storage";  // own / communal storage or home
inline constexpr const char* Sleep = "sleep";
inline constexpr const char* Chop = "chop";              // trees -> wood
inline constexpr const char* Mine = "mine";              // rocks -> stone
inline constexpr const char* GatherHerbs = "gather_herbs";
inline constexpr const char* Heal = "heal";              // use herbs on self
inline constexpr const char* Build = "build";            // start/continue a construction project
inline constexpr const char* HelpBuild = "help_build";   // labour on someone else's/communal project
inline constexpr const char* Farm = "farm";              // plant / tend / harvest
inline constexpr const char* Craft = "craft";            // wood+stone -> tools
inline constexpr const char* Deposit = "deposit";        // put items into own/communal storage
inline constexpr const char* Withdraw = "withdraw";      // take from own/communal storage (legitimately)
inline constexpr const char* WarmUp = "warm_up";         // go to fire/shelter; feed campfire
inline constexpr const char* MakeArt = "make_art";
inline constexpr const char* Play = "play";
inline constexpr const char* Mope = "mope";              // despair breakdown / grief
inline constexpr const char* GoHome = "go_home";
// --- social (ai/SocialBehaviors.cpp) ---
inline constexpr const char* Chat = "chat";              // includes gossip
inline constexpr const char* Share = "share";            // give food to needy
inline constexpr const char* Gift = "gift";
inline constexpr const char* Trade = "trade";
inline constexpr const char* Steal = "steal";            // from agent
inline constexpr const char* StealStorage = "steal_storage";
inline constexpr const char* Attack = "attack";
inline constexpr const char* Flee = "flee";
inline constexpr const char* Comfort = "comfort";
inline constexpr const char* Court = "court";            // build attraction, propose partnership
inline constexpr const char* Mate = "mate";
inline constexpr const char* FeedChild = "feed_child";
inline constexpr const char* FollowParent = "follow_parent";
inline constexpr const char* Beg = "beg";
inline constexpr const char* Teach = "teach";
inline constexpr const char* Confront = "confront";      // punish / demand restitution
inline constexpr const char* Apologize = "apologize";    // make amends (return goods)
inline constexpr const char* Forgive = "forgive";
inline constexpr const char* Mourn = "mourn";
inline constexpr const char* Vandalize = "vandalize";
}  // namespace kinds

}  // namespace heur
