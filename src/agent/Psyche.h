// Heuristica — needs, emotions, memory, relationships, cultural norms, skills.
// FROZEN CONTRACT (plain data + tiny inline helpers).
#pragma once

#include <array>

#include "core/Types.h"

namespace heur {

// ---------------------------------------------------------------------------
// Needs — satisfaction level in [0,1]: 1 = fully satisfied, 0 = desperate.
// ---------------------------------------------------------------------------
enum class Need : std::uint8_t { Food, Rest, Social, Warmth, Fun, Intimacy, Count };
inline constexpr int kNeedCount = static_cast<int>(Need::Count);
const char* needName(Need n);

struct Needs {
    std::array<float, kNeedCount> level{0.8f, 0.8f, 0.7f, 0.9f, 0.7f, 0.6f};

    float get(Need n) const { return level[std::size_t(n)]; }
    void set(Need n, float v) { level[std::size_t(n)] = clamp01(v); }
    void add(Need n, float d) { set(n, get(n) + d); }
    // Heuristic urgency in [0,1]: convex so that near-empty needs dominate decision making.
    float urgency(Need n) const {
        const float d = 1.f - get(n);
        return clamp01(d * d * (3.f - 2.f * d) * 0.6f + d * d * d * 0.4f);
    }
};

// ---------------------------------------------------------------------------
// Emotions — intensities in [0,1], decaying toward 0 each tick.
// ---------------------------------------------------------------------------
enum class Emotion : std::uint8_t { Joy, Sadness, Anger, Fear, Guilt, Pride, Envy, Count };
inline constexpr int kEmotionCount = static_cast<int>(Emotion::Count);
const char* emotionName(Emotion e);

struct Emotions {
    std::array<float, kEmotionCount> v{};
    float get(Emotion e) const { return v[std::size_t(e)]; }
    void set(Emotion e, float x) { v[std::size_t(e)] = clamp01(x); }
    void add(Emotion e, float d) { set(e, get(e) + d); }
};

// Emotional stability state machine.
enum class MentalState : std::uint8_t { Stable, Stressed, Breaking };
// What a mental break looks like (chosen from the dominant negative emotion).
enum class BreakType : std::uint8_t {
    None,
    Rampage,      // anger: attacks people, vandalises structures
    Despair,      // sadness: mopes alone, neglects work
    Panic,        // fear: flees from everyone, hides
    Kleptomania,  // envy/greed: compulsive stealing
};
const char* breakTypeName(BreakType b);

// ---------------------------------------------------------------------------
// Episodic memory
// ---------------------------------------------------------------------------
enum class MemoryType : std::uint8_t {
    ReceivedGift,     // other = giver
    GaveGift,         // other = receiver
    StolenFrom,       // other = thief
    Stole,            // other = victim (kNoAgent if from communal storage)
    WitnessedTheft,   // other = thief, subject = victim
    Attacked,         // other = attacker
    AttackedSomeone,  // other = victim
    WitnessedAttack,  // other = attacker, subject = victim
    Chatted,          // other = partner in conversation
    Comforted,        // other = comforter
    Mated,            // other = mate
    ChildBorn,        // other = child
    LovedOneDied,     // other = deceased
    Punished,         // other = punisher
    PunishedSomeone,  // other = wrongdoer
    Forgave,          // other = forgiven
    BuiltTogether,    // other = co-builder
    Traded,           // other = trade partner
    Rejected,         // other = who rejected my advances
    Taught,           // other = teacher
    Count
};
const char* memoryTypeName(MemoryType m);

struct Memory {
    MemoryType type = MemoryType::Chatted;
    AgentId other = kNoAgent;
    AgentId subject = kNoAgent;
    Tick when = 0;
    float intensity = 0.f;  // 0..1, fades over time
};

// ---------------------------------------------------------------------------
// Relationships (directed: how *I* feel about *them*)
// ---------------------------------------------------------------------------
struct Relationship {
    float affinity = 0.f;    // -1 hate .. +1 love/close friend
    float trust = 0.5f;      // 0..1 expectation they act well toward me
    float attraction = 0.f;  // 0..1 romantic/sexual interest
    float balance = 0.f;     // reciprocity ledger: >0 they gave me more (I owe them), <0 I gave more
    float grievance = 0.f;   // 0..1 unresolved wrongs they did to me (drives revenge / confrontation)
    int interactions = 0;
    Tick lastSeen = 0;
};

// ---------------------------------------------------------------------------
// Culturally learned norms: beliefs about what is normal/acceptable in society.
// Inherited from parents at birth and updated by observation (social learning).
// ---------------------------------------------------------------------------
struct Norms {
    float theftTolerance = 0.20f;         // 0..1 how acceptable stealing seems
    float violenceTolerance = 0.10f;      // 0..1 how acceptable violence seems
    float sharingExpectation = 0.50f;     // 0..1 how much sharing is expected
    float punishmentExpectation = 0.50f;  // 0..1 perceived chance wrongdoing gets punished
};

// ---------------------------------------------------------------------------
// Skills grow with practice (diminishing returns), in [0,1].
// ---------------------------------------------------------------------------
enum class Skill : std::uint8_t { Foraging, Woodcutting, Mining, Building, Farming, Crafting, Fighting, Socializing, Count };
inline constexpr int kSkillCount = static_cast<int>(Skill::Count);
const char* skillName(Skill s);

struct Skills {
    std::array<float, kSkillCount> v{};
    float get(Skill s) const { return v[std::size_t(s)]; }
    void set(Skill s, float x) { v[std::size_t(s)] = clamp01(x); }
};

}  // namespace heur
