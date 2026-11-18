// Heuristica — global tunables.
// FROZEN CONTRACT for names; values may be retuned during balancing.
// Module-specific tunables belong in an anonymous namespace inside that module's .cpp.
#pragma once

namespace heur::cfg {

// ---- Time -----------------------------------------------------------------
inline constexpr int kTicksPerDay = 240;          // 1 tick ~ 6 in-game minutes; agents move 1 tile/tick
inline constexpr int kDaysPerSeason = 8;
inline constexpr int kSeasonsPerYear = 4;
inline constexpr int kDaysPerYear = kDaysPerSeason * kSeasonsPerYear;
inline constexpr float kNightStart = 0.80f;       // fraction of day when night begins
inline constexpr float kNightEnd = 0.20f;         // fraction of day when night ends

// ---- Life cycle (in days) -------------------------------------------------
inline constexpr int kAdultAgeDays = 12;
inline constexpr int kElderAgeDays = 55;
inline constexpr int kOldAgeDeathStartDays = 65;  // natural-death hazard ramps up after this
inline constexpr int kPregnancyDays = 3;
inline constexpr int kMateCooldownDays = 5;       // min days between conceptions (incl. 3 days pregnancy)
inline constexpr int kInitialAgents = 30;

// ---- Needs: satisfaction in [0,1]; decay per tick ---------------------------
inline constexpr float kFoodDecay = 1.0f / (kTicksPerDay * 1.4f);
inline constexpr float kRestDecay = 1.0f / (kTicksPerDay * 1.1f);
inline constexpr float kSocialDecay = 1.0f / (kTicksPerDay * 2.0f);
inline constexpr float kWarmthDecayCold = 1.0f / (kTicksPerDay * 0.5f);  // at temperature 0
inline constexpr float kFunDecay = 1.0f / (kTicksPerDay * 2.5f);
inline constexpr float kIntimacyDecay = 1.0f / (kTicksPerDay * 3.0f);
inline constexpr float kChildDecayMultiplier = 0.7f;   // children's needs decay slower

inline constexpr float kFoodPerUnit = 0.30f;           // Food satisfaction restored per food unit eaten
inline constexpr float kHerbHealAmount = 0.25f;        // health restored per herb

// ---- Health: [0,1] ----------------------------------------------------------
inline constexpr float kStarvationDamage = 1.0f / (kTicksPerDay * 1.5f);  // per tick at Food == 0
inline constexpr float kFreezingDamage = 1.0f / (kTicksPerDay * 3.0f);    // per tick at Warmth == 0
inline constexpr float kExhaustionDamage = 1.0f / (kTicksPerDay * 6.0f);  // per tick at Rest == 0
inline constexpr float kNaturalHealing = 1.0f / (kTicksPerDay * 3.0f);    // per tick when needs are ok

// ---- Perception / AI --------------------------------------------------------
inline constexpr int kSightRadius = 10;          // chebyshev tiles agents can perceive
inline constexpr int kWitnessRadius = 7;         // radius within which deeds are noticed
inline constexpr int kSearchRadius = 28;         // max resource search radius
inline constexpr int kThinkInterval = 10;        // ticks between re-evaluation while busy
inline constexpr float kCommitmentBonus = 0.12f; // hysteresis: bonus for continuing the current kind of action
inline constexpr int kMaxMemories = 40;

// ---- Body -------------------------------------------------------------------
inline constexpr int kBaseCarry = 10;            // + up to kStrengthCarry by Strength trait
inline constexpr int kStrengthCarry = 10;

// ---- Psyche -----------------------------------------------------------------
inline constexpr float kBreakdownThreshold = 0.85f;  // stress level that may trigger a mental break
inline constexpr int kBreakdownMinTicks = kTicksPerDay / 4;
inline constexpr int kBreakdownMaxTicks = kTicksPerDay / 2;

}  // namespace heur::cfg
