// Heuristica — an autonomous character.
// FROZEN CONTRACT. Data is public on purpose: behaviours, social systems and the UI all read it.
#pragma once

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "agent/Genome.h"
#include "agent/Psyche.h"
#include "core/Types.h"

namespace heur {

class Action;
class Simulation;

enum class Sex : std::uint8_t { Female, Male };
enum class LifeStage : std::uint8_t { Child, Adult, Elder };
const char* lifeStageName(LifeStage s);

struct AgentStats {
    int thefts = 0;
    int timesStolenFrom = 0;
    int giftsGiven = 0;
    int giftsReceived = 0;
    int attacks = 0;
    int kills = 0;
    int childrenBorn = 0;
    int structuresBuilt = 0;
    int foodGathered = 0;
    int breakdowns = 0;
    int punishmentsGiven = 0;
};

// One option the brain weighed on its last decision (for the inspector UI).
struct ConsideredOption {
    std::string label;
    float score = 0.f;
    std::string reason;
};

class Agent {
public:
    Agent(AgentId id, std::string name, Sex sex, Genome genome, Tick birthTick);
    ~Agent();
    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;

    // ---- Identity & lineage --------------------------------------------------
    AgentId id;
    std::string name;
    Sex sex;
    Genome genome;
    Tick birthTick;               // founders: wrapped (Tick(0) - age); always use unsigned (now - birthTick)
    AgentId mother = kNoAgent;
    AgentId father = kNoAgent;
    AgentId partner = kNoAgent;   // current bonded mate (mutual when both set)
    std::vector<AgentId> children;
    int clan = 0;                 // tribe/lineage id inherited from mother (each founder group shares one)
    int generation = 0;

    // ---- Body ----------------------------------------------------------------
    Vec2i pos;
    float health = 1.f;           // 0..1, death at 0
    bool alive = true;
    std::string causeOfDeath;
    AgentId killer = kNoAgent;
    Inventory inventory;
    StructureId home = kNoStructure;     // Hut this agent sleeps in
    StructureId project = kNoStructure;  // construction site this agent is committed to

    // ---- Reproduction ----------------------------------------------------------
    bool pregnant = false;
    Tick dueTick = 0;
    Genome fetusGenome;
    AgentId fetusFather = kNoAgent;
    Tick lastConceptionTick = 0;

    // ---- Psyche ----------------------------------------------------------------
    Needs needs;
    Emotions emotions;
    float stress = 0.f;                     // 0..1
    MentalState mentalState = MentalState::Stable;
    BreakType breakType = BreakType::None;
    Tick breakEnd = 0;
    Norms norms;
    Skills skills;
    std::deque<Memory> memories;            // newest at back, capped at cfg::kMaxMemories
    std::unordered_map<AgentId, Relationship> relations;

    // ---- AI state ----------------------------------------------------------------
    std::unique_ptr<Action> action;         // current action (may be null)
    std::string actionKind;                 // kind() of current action ("" if none)
    std::string actionLabel;                // label() of current action for UI
    float actionScore = 0.f;                // utility score it was chosen with
    Tick actionStarted = 0;
    Tick nextThink = 0;                     // brain re-evaluates at/after this tick
    bool wantsRethink = false;              // set by events (e.g. being attacked) to force a decision
    std::string thought;                    // last decision rationale (human-readable)
    std::vector<ConsideredOption> considered;
    // Pathing cache used by stepToward() (stored REVERSED: next step is path.back()):
    std::vector<Vec2i> path;
    Vec2i pathGoal{-1, -1};
    int pathRange = -1;
    int pathFailures = 0;

    AgentStats stats;

    // ---- Queries ------------------------------------------------------------------
    float trait(Trait t) const { return genome.get(t); }
    int ageDays(Tick now) const;
    LifeStage stage(Tick now) const;
    bool isAdult(Tick now) const { return stage(now) != LifeStage::Child; }
    // Adult, healthy enough, not pregnant, not on conception cooldown (females), not elderly-infertile.
    bool isFertile(Tick now) const;
    float mood() const;                   // -1..1 aggregate of emotions & needs
    float emotionalStability() const;     // 0..1 (for UI/heuristics): high = calm and resilient
    bool inBreakdown() const { return mentalState == MentalState::Breaking; }
    int carryCapacity() const;            // cfg::kBaseCarry + Strength * cfg::kStrengthCarry
    int freeCarry() const { return std::max(0, carryCapacity() - inventory.total()); }
    // Days of food on hand: inventory food * kFoodPerUnit / daily food decay.
    float foodSecurityDays() const;

    // ---- Relationships ------------------------------------------------------------
    Relationship& rel(AgentId other);                 // creates a default entry if missing
    const Relationship* findRel(AgentId other) const; // nullptr if unknown
    float affinityFor(AgentId other) const;           // 0 if unknown
    float trustIn(AgentId other) const;               // 0.5 if unknown
    // Parent, child, sibling (shares a parent) or partner.
    bool isFamily(const Agent& other) const;

    // ---- Mutators (psyche module) ---------------------------------------------------
    // Adds emotion scaled by emotional stability: amount * (1.5 - Stability). Also feeds stress for
    // negative emotions.
    void feel(Emotion e, float amount);
    // Appends a memory (evicting the oldest past cfg::kMaxMemories).
    void remember(MemoryType t, AgentId other, Tick now, float intensity, AgentId subject = kNoAgent);
    // Eats up to `units` food from own inventory; returns units eaten. Restores Need::Food, feels Joy.
    int eatFromInventory(int units);
    // Restores Need::Food directly (eating from a storage / being fed).
    void eatUnits(int units);
    // Reduces health; at 0 sets alive=false, causeOfDeath=cause, killer=by.
    void damage(float amount, const std::string& cause, AgentId by = kNoAgent);
    // Skill gain with diminishing returns, scaled by Intelligence.
    void practice(Skill s, float amount);

    // ---- Per-tick updates (called by Simulation, before the brain) ----------------------
    // Need decay (temperature-dependent warmth, child multiplier), passive need restoration
    // (warmth near fire/shelter, social when near friends, fun near monuments), starvation/freezing/
    // exhaustion damage, natural healing, old-age death hazard.
    void updateBody(Simulation& sim);
    // Emotion decay, stress accumulation from unmet needs and negative emotions (scaled by Stability),
    // stress relief, MentalState transitions, triggering/ending breakdowns (sets breakType/breakEnd,
    // wantsRethink, logs via sim.log), memory fading, relationship drift (grievance decay, etc).
    void updateMind(Simulation& sim);
};

}  // namespace heur
