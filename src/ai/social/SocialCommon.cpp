// Heuristica — shared helpers for the social behaviours.
#include "ai/social/SocialCommon.h"

#include <algorithm>
#include <cmath>

namespace heur::social {

namespace {
// ---- Tunables -------------------------------------------------------------------
// Combat power = (Strength, Fighting skill, tools) weights, scaled by health & life stage.
constexpr float kPowStrength = 0.45f;
constexpr float kPowSkill = 0.35f;
constexpr float kPowTools = 0.20f;
constexpr float kPowHealthFloor = 0.4f;  // power × (floor + (1 - floor) × health)
constexpr float kPowChild = 0.4f;
constexpr float kPowElder = 0.7f;
// Hit chance = base + diff × (my power - theirs) (+ sleeping victim), clamped.
constexpr float kHitBase = 0.60f;
constexpr float kHitPowerDiff = 0.50f;
constexpr float kHitAsleepBonus = 0.30f;
constexpr float kHitMin = 0.20f;
constexpr float kHitMax = 0.95f;
// Damage per hit: lerp(min, max, power) × jitter.
constexpr float kDmgMin = 0.04f;
constexpr float kDmgMax = 0.10f;
constexpr float kDmgJitterLo = 0.8f;
constexpr float kDmgJitterHi = 1.2f;
constexpr float kFightPractice = 0.50f;    // attacker per swing (practice(s,1) ≈ +0.02 skill)
constexpr float kDefendPractice = 0.25f;   // victim per swing taken
// Food keeping: keep (base + greed × Greed + hungry × urgency) days of food before giving any away.
constexpr float kKeepDaysBase = 1.0f;
constexpr float kKeepDaysGreed = 2.0f;
constexpr float kKeepDaysHungry = 1.0f;
constexpr float kSateLevel = 0.95f;
// Reciprocity ledger: balance change per food-equivalent given, and cap.
constexpr float kBalancePerValue = 0.08f;
constexpr float kBalanceCap = 2.0f;
// Close-friend threshold for cares().
constexpr float kCaresAffinity = 0.5f;
// needsDamp = clamp(1 - weight × max(urgency(Food), urgency(Warmth)), floor, 1)
constexpr float kNeedsDampWeight = 0.7f;
constexpr float kNeedsDampFloor = 0.2f;
}  // namespace

Agent* living(Simulation& sim, AgentId id) {
    if (id == kNoAgent) return nullptr;
    Agent* a = sim.agent(id);
    return (a && a->alive) ? a : nullptr;
}

const Agent* living(const Simulation& sim, AgentId id) {
    if (id == kNoAgent) return nullptr;
    const Agent* a = sim.agent(id);
    return (a && a->alive) ? a : nullptr;
}

bool bloodKin(const Agent& a, const Agent& b) {
    if (a.id == b.id) return true;
    if (a.mother == b.id || a.father == b.id || b.mother == a.id || b.father == a.id) return true;
    if (a.mother != kNoAgent && a.mother == b.mother) return true;
    if (a.father != kNoAgent && a.father == b.father) return true;
    return false;
}

bool cares(const Agent& self, const Agent& other) {
    return self.isFamily(other) || self.affinityFor(other.id) > kCaresAffinity;
}

float needsDamp(const Agent& a) {
    const float u = std::max(a.needs.urgency(Need::Food), a.needs.urgency(Need::Warmth));
    return clampf(1.f - kNeedsDampWeight * u, kNeedsDampFloor, 1.f);
}

int foodToSate(const Agent& a) {
    const float deficit = kSateLevel - a.needs.get(Need::Food);
    if (deficit <= 0.f) return 0;
    return int(std::ceil(deficit / cfg::kFoodPerUnit));
}

int spareFood(const Agent& a) {
    const float days = kKeepDaysBase + kKeepDaysGreed * a.trait(Trait::Greed) +
                       kKeepDaysHungry * a.needs.urgency(Need::Food);
    const int keep = int(std::ceil(dailyFoodUnits(a) * days));
    return std::max(0, a.inventory.get(Item::Food) - keep);
}

float itemValue(Item i) {
    switch (i) {
        case Item::Food: return 1.0f;
        case Item::Wood: return 0.5f;
        case Item::Stone: return 0.6f;
        case Item::Seeds: return 0.4f;
        case Item::Herbs: return 1.2f;
        case Item::Tools: return 3.0f;
        default: return 0.5f;
    }
}

int giveItems(Agent& from, Agent& to, Item item, int n) {
    const int k = std::min({n, from.inventory.get(item), to.freeCarry()});
    if (k <= 0) return 0;
    const int taken = from.inventory.take(item, k);
    to.inventory.add(item, taken);
    return taken;
}

int feedFood(Agent& from, Agent& to, int n) {
    const int avail = std::min(n, from.inventory.get(Item::Food));
    if (avail <= 0) return 0;
    const int eat = std::min(avail, foodToSate(to));
    if (eat > 0) {
        from.inventory.take(Item::Food, eat);
        to.eatUnits(eat);
    }
    return eat + giveItems(from, to, Item::Food, avail - eat);
}

std::string describeItems(Item i, int n) { return std::to_string(n) + " " + itemName(i); }

std::string describeItems(const ItemCounts& c) {
    std::vector<std::string> parts;
    for (int i = 0; i < kItemCount; ++i)
        if (c[std::size_t(i)] > 0) parts.push_back(describeItems(Item(i), c[std::size_t(i)]));
    if (parts.empty()) return "nothing";
    std::string s = parts[0];
    for (std::size_t i = 1; i < parts.size(); ++i) s += (i + 1 == parts.size() ? " and " : ", ") + parts[i];
    return s;
}

void nudgeRelation(Agent& a, AgentId other, float dAffinity, float dTrust, Tick now) {
    if (other == kNoAgent || other == a.id) return;
    Relationship& r = a.rel(other);
    r.affinity = clampf(r.affinity + dAffinity, -1.f, 1.f);
    r.trust = clamp01(r.trust + dTrust);
    ++r.interactions;
    r.lastSeen = now;
}

void addBalance(Agent& giver, Agent& receiver, float value) {
    const float d = value * kBalancePerValue;
    Relationship& owes = receiver.rel(giver.id);
    owes.balance = clampf(owes.balance + d, -kBalanceCap, kBalanceCap);
    Relationship& owed = giver.rel(receiver.id);
    owed.balance = clampf(owed.balance - d, -kBalanceCap, kBalanceCap);
}

const Memory* recentMemory(const Agent& a, MemoryType t, AgentId other, Tick now, Tick within) {
    for (auto it = a.memories.rbegin(); it != a.memories.rend(); ++it) {
        if (it->when + within < now) break;  // memories are chronological
        if (it->type == t && (other == kNoAgent || it->other == other)) return &*it;
    }
    return nullptr;
}

Memory* recentMemory(Agent& a, MemoryType t, AgentId other, Tick now, Tick within) {
    return const_cast<Memory*>(recentMemory(std::as_const(a), t, other, now, within));
}

float combatPower(const Agent& a, const Simulation& sim) {
    float p = kPowStrength * a.trait(Trait::Strength) + kPowSkill * a.skills.get(Skill::Fighting) +
              kPowTools * (a.inventory.has(Item::Tools) ? 1.f : 0.f);
    p *= kPowHealthFloor + (1.f - kPowHealthFloor) * clamp01(a.health);
    switch (a.stage(sim.now())) {
        case LifeStage::Child: p *= kPowChild; break;
        case LifeStage::Elder: p *= kPowElder; break;
        default: break;
    }
    return clamp01(p);
}

StrikeResult strike(Agent& attacker, Agent& victim, Simulation& sim) {
    const float pa = combatPower(attacker, sim);
    const float pv = combatPower(victim, sim);
    float hitP = kHitBase + kHitPowerDiff * (pa - pv);
    if (isAsleep(victim)) hitP += kHitAsleepBonus;
    hitP = clampf(hitP, kHitMin, kHitMax);
    attacker.practice(Skill::Fighting, kFightPractice);
    victim.practice(Skill::Fighting, kDefendPractice);
    victim.wantsRethink = true;
    if (!sim.rng().chance(hitP)) return StrikeResult::Missed;
    const float dmg = lerpf(kDmgMin, kDmgMax, pa) * sim.rng().range(kDmgJitterLo, kDmgJitterHi);
    victim.damage(dmg, "killed by " + attacker.name, attacker.id);  // also feeds fear/anger
    return victim.alive ? StrikeResult::Hit : StrikeResult::Killed;
}

DeedContext makeDeed(Deed d, const Agent* target, float selfGain, float harm, float benefit, int witnesses,
                     const Structure* st) {
    DeedContext c;
    c.deed = d;
    c.target = target;
    c.targetStructure = st;
    c.selfGain = clamp01(selfGain);
    c.harm = clamp01(harm);
    c.benefit = clamp01(benefit);
    c.witnesses = std::max(0, witnesses);
    return c;
}

std::string pct(float v01) { return std::to_string(int(std::lround(clamp01(v01) * 100.f))) + "%"; }

std::string watchers(int witnesses) {
    if (witnesses <= 0) return "nobody watching";
    if (witnesses == 1) return "1 onlooker";
    return std::to_string(witnesses) + " onlookers";
}

std::string withWhy(const std::string& situation, const Verdict& v) {
    if (v.rationale.empty()) return situation;
    return situation + "; \"" + v.rationale + "\"";
}

const char* structureNoun(StructureType t) {
    switch (t) {
        case StructureType::Campfire: return "campfire";
        case StructureType::Hut: return "hut";
        case StructureType::Storage: return "storage";
        case StructureType::Farm: return "farm";
        case StructureType::Wall: return "wall";
        case StructureType::Workshop: return "workshop";
        case StructureType::Monument: return "monument";
        default: return "structure";
    }
}

}  // namespace heur::social
