// Heuristica — the heuristic "conscience".
//
// Agents have no hard-coded rules about right and wrong. Every morally loaded deed is scored by a
// weighted sum of heuristics (harm, empathy, kinship, reciprocity, necessity, fairness/envy,
// internal integrity, learned social norms, fear of being seen, current emotions). The result is
// added to the action's utility, so a starving, low-empathy agent among strangers may decide that
// stealing is "right enough", while a well-fed, high-integrity one never would.
//
// FROZEN CONTRACT.
#pragma once

#include <string>

#include "core/Types.h"

namespace heur {

class Agent;
class Simulation;
struct Structure;

enum class Deed : std::uint8_t {
    StealFromAgent,    // take items from another agent's inventory
    StealFromStorage,  // take from a private Storage/Hut you don't own
    Attack,            // hit someone
    Kill,              // (used for appraisal when an attack kills)
    Vandalize,         // damage/destroy a structure
    Share,             // give food to someone in need
    Gift,              // give items as generosity / courtship / gratitude
    Comfort,           // console a sad/stressed agent
    Punish,            // confront/hurt a wrongdoer to enforce norms
    Forgive,           // let go of a grievance
    Help,              // labour on someone else's or a communal project / deposit to communal storage
    Adultery,          // mate with someone who is not your partner, or who has another partner
    Count
};
const char* deedName(Deed d);
bool isTransgression(Deed d);  // Steal*, Attack, Kill, Vandalize, Adultery

struct DeedContext {
    Deed deed = Deed::Share;
    const Agent* target = nullptr;              // victim / beneficiary (may be null)
    const Structure* targetStructure = nullptr; // for storage theft / vandalism / help
    float selfGain = 0.f;  // 0..1 how much this relieves *my* needs / benefits me
    float harm = 0.f;      // 0..1 how much it hurts the target (relative to their situation)
    float benefit = 0.f;   // 0..1 how much it helps the target
    int witnesses = 0;     // agents (other than actor & target) who would see it
};

struct Verdict {
    float utility = 0.f;   // add to the action's score: >0 "feels right / worth it", <0 "feels wrong"
    float guilt = 0.f;     // 0..1 guilt to feel (agent.feel(Guilt, ...)) if carried out
    float pride = 0.f;     // 0..1 warm glow/pride to feel if carried out
    float risk = 0.f;      // 0..1 perceived social risk (getting caught/punished)
    std::string rationale; // short first-person justification, e.g. "I'm starving and he hoards"
};

// How does `self` feel about committing this deed right now? Pure function (no side effects).
Verdict judge(const Agent& self, const DeedContext& ctx, const Simulation& sim);

// How an observer judges someone else's deed (witness or victim).
struct Appraisal {
    float approval = 0.f;       // -1 (outraged) .. +1 (admiring)
    float affinityDelta = 0.f;  // change to observer's affinity for actor
    float trustDelta = 0.f;     // change to observer's trust in actor
    float anger = 0.f;          // anger the observer feels
    float fear = 0.f;           // fear the observer feels
    float grievance = 0.f;      // grievance added toward actor (victim, or kin of victim)
};
Appraisal appraise(const Agent& observer, const Agent& actor, const DeedContext& ctx, const Simulation& sim);

}  // namespace heur
