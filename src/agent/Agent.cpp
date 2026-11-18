// Heuristica — an autonomous character: body (needs, health, ageing) and mind (emotions, stress,
// mental breaks, memory fading, relationship drift).
#include "agent/Agent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

#include "ai/Action.h"
#include "core/Config.h"
#include "sim/Simulation.h"
#include "world/World.h"

namespace heur {

namespace {

constexpr float kTPD = float(cfg::kTicksPerDay);

// ---- Needs / body ------------------------------------------------------------
constexpr float kPregnantFoodMult = 1.25f;      // eating for two
constexpr float kSleepFoodMult = 0.85f;         // slower metabolism while asleep
constexpr float kSleepSocialFunMult = 0.5f;     // social/fun barely decay while asleep
constexpr float kFireWarmthGain = 1.f / 55.f;   // per tick near a lit campfire
constexpr float kShelterWarmthGain = 1.f / 75.f;
constexpr float kNightExposureMult = 1.25f;     // exposed at night loses warmth faster
constexpr float kSleepExposureMult = 1.15f;     // sleeping rough
constexpr float kAmbientWarmStart = 0.45f;      // temperature above which ambient air re-warms you
constexpr float kAmbientWarmRate = 1.f / 100.f; // per tick per unit of temperature above start
constexpr int kCompanyRadius = 3;
constexpr float kCompanyAffinity = 0.25f;       // affinity needed for someone to count as company
constexpr float kCompanySocialGain = 0.0022f;   // per tick per (weighted) friend nearby, max 2
constexpr float kMonumentFunGain = 0.0030f;
constexpr float kNeedZero = 0.001f;             // "need is empty" threshold for damage
constexpr float kHealSleepMult = 1.5f;
constexpr float kHealElderMult = 0.7f;
// Old age: linear hazard in days past kOldAgeDeathStartDays; cumulative H(x)=k x^2/2 -> ~25% alive at 80d.
constexpr float kOldAgeHazardPerDay2 = 0.0124f;

// ---- Emotions ------------------------------------------------------------------
// Half-lives in days (negative emotions are additionally scaled by Stability).
constexpr std::array<float, kEmotionCount> kEmotionHalfLifeDays = {
    0.40f,  // Joy
    0.80f,  // Sadness
    0.35f,  // Anger
    0.25f,  // Fear
    1.50f,  // Guilt
    0.80f,  // Pride
    0.50f,  // Envy
};
constexpr float kEmotionFloor = 0.002f;
constexpr float kNegFeelStress = 0.30f;  // stress per unit of negative feeling, * (1.2 - Stability)
constexpr float kPosFeelRelief = 0.04f;  // stress relief per unit of positive feeling

constexpr float kHungerEmotionRate = 0.004f;   // per tick at maximal hunger (split anger/fear by Aggression)
constexpr float kColdEmotionRate = 0.0025f;
constexpr float kLonelyEmotionRate = 0.0020f;
constexpr float kTiredEmotionRate = 0.0012f;
constexpr float kContentJoyRate = 0.0015f;
constexpr int kEnvyInterval = 20;              // ticks between envy samples (staggered by id)
constexpr float kEnvyGain = 0.12f;             // per sample at maximal wealth gap & greed
constexpr int kRuminateInterval = 20;

// ---- Stress --------------------------------------------------------------------
constexpr float kStressFromMood = 0.0020f;     // per tick per unit of negative mood
constexpr float kStressFromNeeds = 0.0060f;    // per tick per unit of unmet-need load
constexpr float kNeedDeadZone = 0.08f;         // unmet-need load below this causes no stress
constexpr float kReliefBase = 0.0003f;         // * (0.4 + Stability)
constexpr float kReliefMood = 0.0020f;         // per tick per unit of mood above kReliefMoodAbove
constexpr float kReliefMoodAbove = 0.15f;      // contentment, not mere okay-ness, heals stress
constexpr float kReliefSleep = 0.0020f;
constexpr float kReliefCompany = 0.0008f;      // per tick per weighted friend (max 2)
constexpr float kReliefMonument = 0.0005f;
constexpr float kStableBelow = 0.5f;
constexpr float kBreakChance = 0.015f;         // per tick at threshold, * (1.6 - Stability) * overshoot
constexpr float kCatharsisStress = 0.4f;
constexpr float kCatharsisEmotion = 0.5f;

// ---- Memory / relationships ----------------------------------------------------
constexpr int kMemoryInterval = 20;
constexpr float kMemoryForgetBelow = 0.02f;
constexpr int kRelationInterval = 60;
constexpr float kAffinityRegressAbove = 0.70f;
constexpr float kAffinityRegressHalfLifeDays = 10.f;
constexpr float kBalanceHalfLifeDays = 6.f;

bool isNegative(Emotion e) {
    return e == Emotion::Sadness || e == Emotion::Anger || e == Emotion::Fear || e == Emotion::Guilt ||
           e == Emotion::Envy;
}

// Per-tick multiplicative factor for a given half-life (in ticks), applied `ticks` times.
float decayFactor(float halfLifeTicks, float ticks = 1.f) {
    if (halfLifeTicks <= 0.f) return 0.f;
    return std::exp(-0.69314718f * ticks / halfLifeTicks);
}

bool staggered(Tick now, AgentId id, int interval) {
    return (now + Tick(id) * 7u) % Tick(interval) == 0;
}

float memoryHalfLifeDays(MemoryType t) {
    switch (t) {
        case MemoryType::LovedOneDied: return 10.f;
        case MemoryType::Attacked:
        case MemoryType::ChildBorn:
        case MemoryType::Mated: return 8.f;
        case MemoryType::StolenFrom:
        case MemoryType::Punished:
        case MemoryType::AttackedSomeone:
        case MemoryType::Stole: return 6.f;
        case MemoryType::WitnessedAttack:
        case MemoryType::WitnessedTheft:
        case MemoryType::ReceivedGift:
        case MemoryType::Comforted:
        case MemoryType::Forgave:
        case MemoryType::PunishedSomeone:
        case MemoryType::Taught: return 4.f;
        case MemoryType::Rejected:
        case MemoryType::GaveGift:
        case MemoryType::BuiltTogether:
        case MemoryType::Traded: return 3.f;
        case MemoryType::Chatted: return 1.5f;
        default: return 3.f;
    }
}

// Weighted count (0..2) of liked agents within kCompanyRadius.
float companyNearby(const Agent& self, const Simulation& sim) {
    float w = 0.f;
    for (const Agent* o : sim.agentsNear(self.pos, kCompanyRadius, self.id)) {
        const float aff = self.affinityFor(o->id);
        if (aff > kCompanyAffinity) w += 0.5f + clamp01((aff - kCompanyAffinity) / 0.5f) * 0.5f;
        if (w >= 2.f) break;
    }
    return std::min(w, 2.f);
}

const char* breakDescription(BreakType b) {
    switch (b) {
        case BreakType::Rampage: return "flies into a violent rage";
        case BreakType::Despair: return "collapses into despair";
        case BreakType::Panic: return "panics and flees from everyone";
        case BreakType::Kleptomania: return "is gripped by a compulsion to steal";
        default: return "loses control";
    }
}

const char* recoveryDescription(BreakType b) {
    switch (b) {
        case BreakType::Rampage: return "calms down after the rage";
        case BreakType::Despair: return "pulls out of despair";
        case BreakType::Panic: return "stops panicking";
        case BreakType::Kleptomania: return "regains self-control";
        default: return "comes back to their senses";
    }
}

Emotion breakEmotion(BreakType b) {
    switch (b) {
        case BreakType::Rampage: return Emotion::Anger;
        case BreakType::Despair: return Emotion::Sadness;
        case BreakType::Panic: return Emotion::Fear;
        case BreakType::Kleptomania: return Emotion::Envy;
        default: return Emotion::Sadness;
    }
}

}  // namespace

// ============================================================================
// Construction
// ============================================================================
Agent::Agent(AgentId id_, std::string name_, Sex sex_, Genome genome_, Tick birthTick_)
    : id(id_), name(std::move(name_)), sex(sex_), genome(genome_), birthTick(birthTick_) {
    nextThink = 0;
}

Agent::~Agent() = default;

// ============================================================================
// Queries
// ============================================================================
int Agent::ageDays(Tick now) const {
    // birthTick may be a wrapped value (founders born "before" tick 0): unsigned modular subtraction only.
    return int((now - birthTick) / Tick(cfg::kTicksPerDay));
}

LifeStage Agent::stage(Tick now) const {
    const int age = ageDays(now);
    if (age < cfg::kAdultAgeDays) return LifeStage::Child;
    if (age < cfg::kElderAgeDays) return LifeStage::Adult;
    return LifeStage::Elder;
}

bool Agent::isFertile(Tick now) const {
    if (!alive || health <= 0.4f) return false;
    const int age = ageDays(now);
    if (age < cfg::kAdultAgeDays) return false;
    if (sex == Sex::Female) {
        if (pregnant || age >= cfg::kElderAgeDays) return false;
        if (lastConceptionTick == 0) return true;
        return (now - lastConceptionTick) >= Tick(cfg::kMateCooldownDays) * Tick(cfg::kTicksPerDay);
    }
    return age < cfg::kOldAgeDeathStartDays;
}

float Agent::mood() const {
    const auto E = [&](Emotion e) { return emotions.get(e); };
    const float emo = 1.0f * E(Emotion::Joy) + 0.6f * E(Emotion::Pride) - 0.9f * E(Emotion::Sadness) -
                      0.6f * E(Emotion::Anger) - 0.7f * E(Emotion::Fear) - 0.6f * E(Emotion::Guilt) -
                      0.3f * E(Emotion::Envy);
    // Weighted need satisfaction; food & warmth matter most.
    constexpr std::array<float, kNeedCount> w = {2.0f, 1.0f, 1.0f, 1.5f, 0.6f, 0.4f};
    float sum = 0.f, wsum = 0.f;
    for (int i = 0; i < kNeedCount; ++i) {
        sum += w[std::size_t(i)] * needs.level[std::size_t(i)];
        wsum += w[std::size_t(i)];
    }
    // Centred on 0.65: merely 'okay' needs are neutral; only thriving lifts mood.
    const float needTerm = clampf((sum / wsum - 0.65f) * 1.3f, -0.6f, 0.4f) -
                           0.25f * std::max(needs.urgency(Need::Food), needs.urgency(Need::Warmth));
    const float healthTerm = -0.2f * (1.f - health);
    return clampf(0.7f * emo + needTerm + healthTerm, -1.f, 1.f);
}

float Agent::emotionalStability() const {
    const float negLoad = clamp01(0.5f * (emotions.get(Emotion::Sadness) + emotions.get(Emotion::Anger) +
                                          emotions.get(Emotion::Fear) + emotions.get(Emotion::Guilt) +
                                          0.5f * emotions.get(Emotion::Envy)));
    float s = 0.45f * (1.f - stress) + 0.30f * trait(Trait::Stability) + 0.25f * (1.f - negLoad);
    if (inBreakdown()) s *= 0.6f;
    return clamp01(s);
}

int Agent::carryCapacity() const {
    return cfg::kBaseCarry + int(std::lround(trait(Trait::Strength) * float(cfg::kStrengthCarry)));
}

float Agent::foodSecurityDays() const {
    const float perDay = cfg::kFoodDecay * kTPD;
    return float(inventory.get(Item::Food)) * cfg::kFoodPerUnit / perDay;
}

// ============================================================================
// Relationships
// ============================================================================
Relationship& Agent::rel(AgentId other) { return relations[other]; }

const Relationship* Agent::findRel(AgentId other) const {
    const auto it = relations.find(other);
    return it == relations.end() ? nullptr : &it->second;
}

float Agent::affinityFor(AgentId other) const {
    const Relationship* r = findRel(other);
    return r ? r->affinity : 0.f;
}

float Agent::trustIn(AgentId other) const {
    const Relationship* r = findRel(other);
    return r ? r->trust : 0.5f;
}

bool Agent::isFamily(const Agent& o) const {
    if (o.id == id) return false;
    // Parent / child
    if (o.id == mother || o.id == father) return true;
    if (o.mother == id || o.father == id) return true;
    if (std::find(children.begin(), children.end(), o.id) != children.end()) return true;
    // Sibling / half-sibling
    if (mother != kNoAgent && o.mother == mother) return true;
    if (father != kNoAgent && o.father == father) return true;
    // Partner
    if (partner != kNoAgent && partner == o.id) return true;
    if (o.partner != kNoAgent && o.partner == id) return true;
    return false;
}

// ============================================================================
// Mutators
// ============================================================================
void Agent::feel(Emotion e, float amount) {
    if (amount == 0.f || !std::isfinite(amount)) return;
    if (amount < 0.f) {  // explicit dampening (e.g. being comforted): no stability scaling, no stress
        emotions.add(e, amount);
        return;
    }
    const float S = trait(Trait::Stability);
    if (isNegative(e)) {
        const float scaled = amount * (1.5f - S);
        emotions.add(e, scaled);
        stress = clamp01(stress + scaled * kNegFeelStress * (1.2f - S));
    } else {
        const float scaled = amount * (0.8f + 0.4f * S);
        emotions.add(e, scaled);
        stress = clamp01(stress - scaled * kPosFeelRelief);
    }
}

void Agent::remember(MemoryType t, AgentId other, Tick now, float intensity, AgentId subject) {
    Memory m;
    m.type = t;
    m.other = other;
    m.subject = subject;
    m.when = now;
    m.intensity = clamp01(intensity);
    memories.push_back(m);
    while (memories.size() > std::size_t(cfg::kMaxMemories)) memories.pop_front();
}

int Agent::eatFromInventory(int units) {
    if (units <= 0) return 0;
    const int eaten = inventory.take(Item::Food, units);
    if (eaten > 0) eatUnits(eaten);
    return eaten;
}

void Agent::eatUnits(int units) {
    if (units <= 0) return;
    const float hunger = needs.urgency(Need::Food);
    needs.add(Need::Food, float(units) * cfg::kFoodPerUnit);
    // Eating when hungry feels good and calms hunger-fear.
    const float joy = std::min(0.35f, 0.08f * float(units)) * (0.25f + 1.5f * hunger);
    feel(Emotion::Joy, joy);
    if (hunger > 0.3f) {
        emotions.add(Emotion::Fear, -0.10f * float(units) * hunger);
        emotions.add(Emotion::Anger, -0.05f * float(units) * hunger);
    }
}

void Agent::damage(float amount, const std::string& cause, AgentId by) {
    if (!alive || !(amount > 0.f)) return;
    health -= amount;
    if (by != kNoAgent) {
        // Being hurt by someone: fear (less if brave) and anger (more if aggressive).
        feel(Emotion::Fear, amount * 1.5f * (1.2f - trait(Trait::Bravery)));
        feel(Emotion::Anger, amount * 1.5f * (0.3f + trait(Trait::Aggression)));
        wantsRethink = true;
    } else {
        // Starving / freezing: a creeping dread.
        feel(Emotion::Fear, amount * 1.0f * (1.1f - trait(Trait::Bravery)));
    }
    if (health <= 0.f) {
        health = 0.f;
        alive = false;
        causeOfDeath = cause;
        killer = by;
    }
}

void Agent::practice(Skill s, float amount) {
    if (!(amount > 0.f)) return;
    const float v = skills.get(s);
    const float gain = amount * (0.5f + trait(Trait::Intelligence)) * (1.f - v) * 0.02f;
    skills.set(s, v + gain);
}

// ============================================================================
// Body
// ============================================================================
void Agent::updateBody(Simulation& sim) {
    if (!alive) return;
    const World& world = sim.world();
    const Tick now = sim.now();
    const LifeStage st = stage(now);
    const bool child = st == LifeStage::Child;
    const bool elder = st == LifeStage::Elder;
    const bool asleep = actionKind == kinds::Sleep;
    const float mult = child ? cfg::kChildDecayMultiplier : 1.f;

    // ---- Food ----
    float foodDecay = cfg::kFoodDecay * mult;
    if (pregnant) foodDecay *= kPregnantFoodMult;
    if (asleep) foodDecay *= kSleepFoodMult;
    needs.add(Need::Food, -foodDecay);

    // ---- Rest (the Sleep action restores it; don't fight it while asleep) ----
    if (!asleep) needs.add(Need::Rest, -cfg::kRestDecay * mult * (elder ? 1.1f : 1.f));

    // ---- Warmth ----
    const float temp = clamp01(world.temperature());
    const bool sheltered = world.isSheltered(pos);
    const bool fire = world.nearWarmth(pos);
    if (sheltered || fire) {
        float gain = 0.f;
        if (fire) gain += kFireWarmthGain;
        if (sheltered) gain += kShelterWarmthGain;
        needs.add(Need::Warmth, gain);
    } else {
        float decay = cfg::kWarmthDecayCold * std::pow(1.f - temp, 1.5f);
        if (world.isNight()) decay *= kNightExposureMult;
        if (asleep) decay *= kSleepExposureMult;
        if (child) decay *= 1.1f;  // small bodies lose heat faster (but other needs decay slower)
        const float ambient = std::max(0.f, temp - kAmbientWarmStart) * kAmbientWarmRate;
        needs.add(Need::Warmth, ambient - decay);
    }

    // ---- Social ----
    const float soc = trait(Trait::Sociability);
    float socialDecay = cfg::kSocialDecay * mult * (0.5f + soc);
    if (asleep) socialDecay *= kSleepSocialFunMult;
    needs.add(Need::Social, -socialDecay);
    if (!asleep) {
        const float company = companyNearby(*this, std::as_const(sim));
        if (company > 0.f) needs.add(Need::Social, kCompanySocialGain * company);
    }

    // ---- Fun ----
    float funDecay = cfg::kFunDecay * mult;
    if (asleep) funDecay *= kSleepSocialFunMult;
    needs.add(Need::Fun, -funDecay);
    if (!asleep && world.nearMonument(pos)) needs.add(Need::Fun, kMonumentFunGain * (0.5f + trait(Trait::Creativity)));

    // ---- Intimacy (adults only, scaled by Libido) ----
    if (!child) {
        float d = cfg::kIntimacyDecay * (0.3f + 1.4f * trait(Trait::Libido));
        if (elder) d *= 0.5f;
        needs.add(Need::Intimacy, -d);
    }

    // ---- Damage ----
    if (needs.get(Need::Food) <= kNeedZero) damage(cfg::kStarvationDamage, "starvation");
    if (!alive) return;
    if (needs.get(Need::Warmth) <= kNeedZero) damage(cfg::kFreezingDamage, "the cold");
    if (!alive) return;
    if (needs.get(Need::Rest) <= kNeedZero) damage(cfg::kExhaustionDamage, "exhaustion");
    if (!alive) return;

    // ---- Natural healing ----
    if (health < 1.f && needs.get(Need::Food) > 0.3f && needs.get(Need::Warmth) > 0.3f &&
        needs.get(Need::Rest) > 0.2f) {
        float heal = cfg::kNaturalHealing;
        if (asleep) heal *= kHealSleepMult;
        if (elder) heal *= kHealElderMult;
        health = std::min(1.f, health + heal);
    }

    // ---- Old age ----
    const float ageF = float(double(now - birthTick) / double(cfg::kTicksPerDay));
    const float past = ageF - float(cfg::kOldAgeDeathStartDays);
    if (past > 0.f) {
        const float hazardPerDay = kOldAgeHazardPerDay2 * past * (1.f + (1.f - health));
        const float p = hazardPerDay / kTPD;
        if (sim.rng().chance(p)) damage(health + 1.f, "old age");
    }
}

// ============================================================================
// Mind
// ============================================================================
void Agent::updateMind(Simulation& sim) {
    if (!alive) return;
    const Tick now = sim.now();
    const float S = trait(Trait::Stability);
    const bool asleep = actionKind == kinds::Sleep;
    const bool child = stage(now) == LifeStage::Child;

    // ---- 1. Emotion decay toward 0 ----
    {
        const float negScale = 1.25f - 0.5f * S;  // stable minds recover faster
        for (int i = 0; i < kEmotionCount; ++i) {
            const Emotion e = Emotion(i);
            float hl = kEmotionHalfLifeDays[std::size_t(i)] * kTPD;
            if (isNegative(e)) hl *= negScale;
            float v = emotions.v[std::size_t(i)] * decayFactor(hl);
            if (v < kEmotionFloor) v = 0.f;
            emotions.v[std::size_t(i)] = v;
        }
    }

    // ---- 2. Need-driven emotions ----
    const float uFood = needs.urgency(Need::Food);
    const float uWarm = needs.urgency(Need::Warmth);
    const float uRest = needs.urgency(Need::Rest);
    const float uSocial = needs.urgency(Need::Social);
    const float uFun = needs.urgency(Need::Fun);
    const float aggr = trait(Trait::Aggression);
    const float soc = trait(Trait::Sociability);
    if (uFood > 0.4f) {
        const float k = kHungerEmotionRate * (uFood - 0.4f) / 0.6f;
        feel(Emotion::Anger, k * (0.1f + 0.7f * aggr));
        feel(Emotion::Fear, k * (1.0f - 0.6f * aggr) * (1.2f - trait(Trait::Bravery)));
    }
    if (uWarm > 0.5f) {
        const float k = kColdEmotionRate * (uWarm - 0.5f) / 0.5f;
        feel(Emotion::Fear, k * 0.6f);
        feel(Emotion::Sadness, k * 0.4f);
    }
    if (uSocial > 0.4f && !asleep) feel(Emotion::Sadness, kLonelyEmotionRate * (uSocial - 0.4f) / 0.6f * (0.3f + soc));
    if (uRest > 0.6f && !asleep) feel(Emotion::Anger, kTiredEmotionRate * (uRest - 0.6f) / 0.4f);
    {
        const float content = clamp01((1.f - std::max({uFood, uWarm, uRest, uSocial})) - 0.55f) / 0.45f;
        if (content > 0.f) feel(Emotion::Joy, kContentJoyRate * content);
    }

    // Envy: sample visible agents who carry much more food than me (cheap, staggered).
    if (staggered(now, id, kEnvyInterval) && !asleep) {
        const float greed = trait(Trait::Greed);
        const int mine = inventory.get(Item::Food);
        int richest = 0;
        for (const Agent* o : std::as_const(sim).agentsNear(pos, cfg::kSightRadius, id)) {
            if (isFamily(*o)) continue;  // family wealth is shared wealth
            richest = std::max(richest, o->inventory.get(Item::Food));
        }
        const float gap = float(richest - mine);
        if (gap > 3.f) {
            const float g = clamp01((gap - 3.f) / 10.f);
            feel(Emotion::Envy, kEnvyGain * g * (0.2f + 1.2f * greed) * (0.5f + uFood));
        }
    }

    // Rumination: strong recent memories keep feelings alive (grief, resentment, remorse).
    if (staggered(now, id + 3u, kRuminateInterval) && !memories.empty()) {
        float grief = 0.f, resent = 0.f, remorse = 0.f, hurt = 0.f;
        for (const Memory& m : memories) {
            switch (m.type) {
                case MemoryType::LovedOneDied: grief += m.intensity; break;
                case MemoryType::StolenFrom:
                case MemoryType::Attacked:
                case MemoryType::Punished: resent += m.intensity; break;
                case MemoryType::Stole:
                case MemoryType::AttackedSomeone: remorse += m.intensity; break;
                case MemoryType::Rejected: hurt += m.intensity; break;
                default: break;
            }
        }
        const float k = float(kRuminateInterval) / kTPD;  // per-day rates
        if (grief > 0.f) feel(Emotion::Sadness, std::min(grief, 1.5f) * 0.25f * k);
        if (resent > 0.f) feel(Emotion::Anger, std::min(resent, 1.5f) * 0.15f * trait(Trait::Vengefulness) * k);
        if (remorse > 0.f)
            feel(Emotion::Guilt, std::min(remorse, 1.5f) * 0.12f * (0.3f + 0.7f * trait(Trait::Integrity)) *
                                     (0.5f + trait(Trait::Empathy)) * k);
        if (hurt > 0.f) feel(Emotion::Sadness, std::min(hurt, 1.f) * 0.1f * k);
    }

    // ---- 3. Stress ----
    const float m = mood();
    const float company = asleep ? 0.f : companyNearby(*this, std::as_const(sim));
    {
        const float weighted = 0.45f * uFood + 0.25f * uWarm + 0.15f * uRest + 0.10f * uSocial * (0.5f + soc) +
                               0.05f * uFun;
        const float load = std::max(weighted, 0.8f * std::max(uFood, uWarm));
        const float unmet = std::max(0.f, load - kNeedDeadZone) / (1.f - kNeedDeadZone);
        const float gain = (kStressFromMood * std::max(0.f, -m) + kStressFromNeeds * unmet) * (1.3f - S);
        float relief = kReliefBase * (0.4f + S) + kReliefMood * std::max(0.f, m - kReliefMoodAbove) +
                        kReliefCompany * company;
        if (asleep) relief += kReliefSleep;
        if (!asleep && sim.world().nearMonument(pos)) relief += kReliefMonument;
        stress = clamp01(stress + gain - relief);
    }

    // ---- 4. Mental state machine & breakdowns ----
    if (mentalState == MentalState::Breaking) {
        if (now >= breakEnd) {
            const BreakType ended = breakType;
            stress *= kCatharsisStress;
            const Emotion dom = breakEmotion(ended);
            emotions.set(dom, emotions.get(dom) * kCatharsisEmotion);
            breakType = BreakType::None;
            breakEnd = 0;
            mentalState = stress < kStableBelow ? MentalState::Stable : MentalState::Stressed;
            wantsRethink = true;
            sim.log(EventKind::Breakdown, name + " " + recoveryDescription(ended), id);
        }
    } else if (stress >= cfg::kBreakdownThreshold) {
        mentalState = MentalState::Stressed;
        const float overshoot = 1.f + (stress - cfg::kBreakdownThreshold) * 6.f;
        const float p = kBreakChance * (1.6f - S) * overshoot;
        if (sim.rng().chance(p)) {
            const float greed = trait(Trait::Greed);
            const float brave = trait(Trait::Bravery);
            const float rampage = emotions.get(Emotion::Anger) * (0.7f + 0.6f * aggr) + 0.15f * uFood * aggr;
            const float despair = emotions.get(Emotion::Sadness) + 0.1f * uSocial + 0.05f;
            const float panic = emotions.get(Emotion::Fear) * (1.2f - 0.5f * brave) + 0.15f * uWarm * (1.f - brave);
            const float klepto = emotions.get(Emotion::Envy) * (0.6f + 0.8f * greed) + 0.5f * uFood * greed;
            // The dominant emotion usually wins (cubic weights), but close calls can go either way.
            constexpr std::array<BreakType, 4> kTypes = {BreakType::Rampage, BreakType::Despair, BreakType::Panic,
                                                         BreakType::Kleptomania};
            const std::vector<float> weights = {rampage * rampage * rampage, despair * despair * despair,
                                                panic * panic * panic, klepto * klepto * klepto};
            const int pick = sim.rng().weightedIndex(weights);
            BreakType b = pick < 0 ? BreakType::Despair : kTypes[std::size_t(pick)];
            if (child && b == BreakType::Rampage) b = BreakType::Despair;  // children sulk, they don't fight

            breakType = b;
            mentalState = MentalState::Breaking;
            breakEnd = now + Tick(sim.rng().rangeInt(cfg::kBreakdownMinTicks, cfg::kBreakdownMaxTicks));
            wantsRethink = true;
            stats.breakdowns++;
            sim.stats().breakdowns++;

            // Name the biggest stressor so the log explains the snap.
            struct Cause { float v; const char* what; };
            const Cause causes[] = {
                {uFood, "starving"},
                {uWarm, "freezing"},
                {uRest * 0.8f, "exhausted"},
                {uSocial * (0.3f + soc), "lonely"},
                {emotions.get(Emotion::Sadness), "heartbroken"},
                {emotions.get(Emotion::Anger), "furious"},
                {emotions.get(Emotion::Fear), "terrified"},
                {emotions.get(Emotion::Guilt), "wracked with guilt"},
                {emotions.get(Emotion::Envy), "eaten up by envy"},
            };
            const Cause* top = &causes[0];
            for (const Cause& c : causes)
                if (c.v > top->v) top = &c;
            sim.log(EventKind::Breakdown, name + " snaps: " + breakDescription(b) + " (" + top->what + ")", id);
        }
    } else {
        mentalState = stress < kStableBelow ? MentalState::Stable : MentalState::Stressed;
    }

    // ---- 5. Memory fading ----
    if (staggered(now, id + 11u, kMemoryInterval) && !memories.empty()) {
        for (Memory& mem : memories)
            mem.intensity *= decayFactor(memoryHalfLifeDays(mem.type) * kTPD, float(kMemoryInterval));
        std::erase_if(memories, [](const Memory& mem) { return mem.intensity < kMemoryForgetBelow; });
    }

    // ---- 6. Relationship drift ----
    if (staggered(now, id, kRelationInterval) && !relations.empty()) {
        const float vengeful = trait(Trait::Vengefulness);
        const float ticks = float(kRelationInterval);
        // Grudges fade over ~1.5 days (forgiving) to ~9 days (vengeful); stability helps let go.
        const float grievanceHL = (1.5f + 7.5f * vengeful) * (1.2f - 0.4f * S) * kTPD;
        const float gF = decayFactor(grievanceHL, ticks);
        const float aF = decayFactor(kAffinityRegressHalfLifeDays * kTPD, ticks);
        const float bF = decayFactor(kBalanceHalfLifeDays * kTPD, ticks);
        for (auto it = relations.begin(); it != relations.end();) {
            Relationship& r = it->second;
            r.grievance *= gF;
            if (r.grievance < 0.005f) r.grievance = 0.f;
            r.balance *= bF;
            const float mag = std::fabs(r.affinity);
            if (mag > kAffinityRegressAbove) {
                const float excess = (mag - kAffinityRegressAbove) * aF;
                r.affinity = std::copysign(kAffinityRegressAbove + excess, r.affinity);
            }
            // Forget dead/unknown acquaintances that left no mark.
            const bool gone = std::as_const(sim).agent(it->first) == nullptr;
            if (gone && std::fabs(r.affinity) < 0.05f && r.grievance <= 0.f && r.attraction < 0.05f)
                it = relations.erase(it);
            else
                ++it;
        }
    }
}

}  // namespace heur
