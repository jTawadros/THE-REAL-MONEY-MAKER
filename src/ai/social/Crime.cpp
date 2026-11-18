// Heuristica — property crime: pickpocketing (Steal), raiding private stores (StealStorage) and
// Vandalize. Whether any of it happens is the conscience's call (judge()); these behaviours only supply
// the motive (hunger, greed, kleptomania, grudges, rage) and the practical risk of getting caught.
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ai/social/SocialCommon.h"

namespace heur::social {

namespace {

// ===========================================================================
// Tunables
// ===========================================================================
// ---- Motives (shared by Steal / StealStorage) -------------------------------------
constexpr float kHaveFoodDamp = 0.3f;         // food motive × this if I already carry food
constexpr int kOwnStoreFood = 2;              // ... or own this much stored food
constexpr float kGreedWeight = 0.30f;         // luxury motive = Greed × this × (0.5 + Envy)
constexpr float kKleptoBase = 0.30f;          // Kleptomania: positive base so Brain's ×3 bites
constexpr float kKleptoConscience = 0.4f;     // compulsion mutes the conscience
constexpr float kMinFoodMotive = 0.15f;
constexpr float kMinGreedMotive = 0.08f;
constexpr float kChildMinHunger = 0.60f;      // children only pickpocket food when this hungry
constexpr float kChildScoreMult = 0.8f;

// ---- Steal (pickpocket) ----------------------------------------------------------------
constexpr int kStealMaxFood = 4;
constexpr float kStealWeight = 0.90f;         // × motive × distanceFactor
constexpr float kStealDistHalf = 8.f;
constexpr float kStealAsleepBonus = 0.10f;    // easy pickings
constexpr float kCatchRiskWeight = 0.35f;     // × P(caught) × (1.2 - Bravery)
constexpr int kStealGiveUpTicks = 50;
// P(caught) = base + victim Intelligence × w - my stealth × w (stealth = Intelligence + experience)
constexpr float kCatchBase = 0.30f;
constexpr float kCatchVictimInt = 0.50f;
constexpr float kCatchStealth = 0.50f;
constexpr float kCatchExpPerTheft = 0.03f;    // experience: up to kCatchExpMax
constexpr float kCatchExpMax = 0.30f;
constexpr float kCatchChild = 0.10f;          // clumsy kids
constexpr float kCatchAsleep = 0.08f;         // the sleeper might stir
constexpr float kCatchMin = 0.05f;
constexpr float kCatchMax = 0.85f;
// When caught awake, the victim may snatch it back (power difference).
constexpr float kThwartBase = 0.30f;
constexpr float kThwartPower = 0.40f;
constexpr float kAttemptHarm = 0.3f;          // harm of a foiled attempt (relative)

// ---- StealStorage ------------------------------------------------------------------------
constexpr int kStoreSearch = 16;
constexpr int kStoreMaxFood = 5;
constexpr float kStoreWeight = 0.85f;
constexpr float kStoreDistHalf = 10.f;
constexpr float kStoreHarmScale = 0.8f;
constexpr float kOwnerPresentRisk = 0.25f;    // × (1.1 - Bravery) when the owner is watching
constexpr int kStoreGiveUpTicks = 60;

// ---- Vandalize -----------------------------------------------------------------------------
constexpr int kVandalSearch = 14;
constexpr float kVandalMinGrievance = 0.5f;
constexpr float kVandalWeight = 0.8f;         // × grievance × Vengefulness × (0.4 + Aggression)
constexpr float kRampageVandalBase = 0.22f;   // + kRampageGrudge × grievance (Brain ×3)
constexpr float kRampageGrudge = 0.3f;
constexpr float kRampageConscience = 0.3f;
constexpr float kVandalDistHalf = 8.f;
constexpr int kVandalTicks = 8;
constexpr int kVandalGiveUpTicks = 60;
constexpr float kVandalDamagePerTick = 0.035f;// × (0.6 + 0.8 Strength) × (tools ? 1.4 : 1)
constexpr float kVandalToolsMult = 1.4f;
constexpr float kVandalStressRelief = 0.05f;
constexpr float kVandalGrievanceKeep = 0.7f;  // vengeance (partly) satisfied

// ===========================================================================
// Shared helpers
// ===========================================================================
struct Motive {
    float food = 0.f;
    float greed = 0.f;
    bool klepto = false;
};

Motive stealMotive(const Agent& self, const Simulation& sim, bool child) {
    Motive m;
    const float hunger = self.needs.urgency(Need::Food);
    m.food = hunger;
    if (self.inventory.get(Item::Food) > 0 || foodOwned(self, sim) >= kOwnStoreFood) m.food *= kHaveFoodDamp;
    m.greed = child ? 0.f : self.trait(Trait::Greed) * kGreedWeight * (0.5f + self.emotions.get(Emotion::Envy));
    m.klepto = self.inBreakdown() && self.breakType == BreakType::Kleptomania;
    if (child && hunger < kChildMinHunger) m.food = 0.f;
    return m;
}

struct Loot {
    Item item = Item::Food;
    int amount = 0;
    float motive = 0.f;
};

// What to take from `inv` given my motives.
Loot chooseLoot(const Agent& self, const Inventory& inv, const Motive& m, bool child, int maxFood) {
    Loot best;
    const int room = self.freeCarry();
    if (room < 1) return best;
    if (m.food > 0.f && inv.get(Item::Food) >= (child ? 1 : 2)) {
        const int n = std::min({inv.get(Item::Food), std::max(1, foodToSate(self) + 1), maxFood, room});
        best = {Item::Food, n, m.food};
    }
    if (child) return best;
    auto consider = [&](Item i, int n, float motive) {
        if (m.klepto) motive = std::max(motive, kKleptoBase);
        n = std::min({n, inv.get(i), room});
        if (n >= 1 && motive > best.motive) best = {i, n, motive};
    };
    if (inv.has(Item::Tools) && !self.inventory.has(Item::Tools)) consider(Item::Tools, 1, m.greed);
    if (inv.has(Item::Herbs)) consider(Item::Herbs, 1, m.greed * 0.6f + (self.health < 0.6f ? 0.5f * (1.f - self.health) : 0.f));
    if (inv.get(Item::Food) >= 4) consider(Item::Food, 3, std::max(m.greed * 0.7f, best.item == Item::Food ? best.motive : 0.f));
    if (m.klepto && best.amount == 0) {
        for (int i = 0; i < kItemCount; ++i)
            if (inv.has(Item(i))) {
                consider(Item(i), 2, kKleptoBase);
                break;
            }
    }
    return best;
}

float stealthOf(const Agent& self) {
    return 0.5f * self.trait(Trait::Intelligence) + std::min(kCatchExpMax, kCatchExpPerTheft * float(self.stats.thefts));
}

float catchChance(const Agent& thief, const Agent& victim, const Simulation& sim) {
    if (isAsleep(victim)) return kCatchAsleep;
    float p = kCatchBase + kCatchVictimInt * victim.trait(Trait::Intelligence) - kCatchStealth * stealthOf(thief);
    if (isChildAgent(thief, sim)) p += kCatchChild;
    return clampf(p, kCatchMin, kCatchMax);
}

float theftHarm(const Inventory& inv, Item item, int amount, const Agent* victim, float scale) {
    const float share = float(amount) / float(std::max(1, inv.get(item)));
    const float need = victim ? victim->needs.urgency(Need::Food) : 0.f;
    const float itemWeight = item == Item::Food ? 1.f : 0.7f;
    return clamp01(share * (0.35f + 0.65f * need) * itemWeight * scale + 0.05f * float(amount) * itemValue(item) * 0.1f);
}

std::string lootPhrase(const Loot& l, const Motive& m, const Agent& self) {
    if (l.item == Item::Food && l.motive == m.food && m.food > 0.f)
        return self.needs.urgency(Need::Food) > 0.7f ? "starving" : "hungry";
    if (m.klepto) return "can't stop myself";
    return std::string("I want ") + itemName(l.item);
}

// ===========================================================================
// Steal (from an agent)
// ===========================================================================
class StealAction final : public Action {
public:
    StealAction(AgentId victim, std::string victimName, Item item, int amount)
        : victim_(victim), victimName_(std::move(victimName)), item_(item), amount_(amount) {}
    const char* kind() const override { return kinds::Steal; }
    std::string label() const override { return std::string("Stealing ") + itemName(item_) + " from " + victimName_; }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        ++elapsed_;
        Agent* v = living(sim, victim_);
        if (!v || v->inventory.get(item_) < 1 || self.freeCarry() < 1 || elapsed_ > kStealGiveUpTicks)
            return finish(self, ActionStatus::Failed);
        if (chebyshev(self.pos, v->pos) > 1) {
            if (stepToward(self, sim, v->pos, 1) == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        return finish(self, attempt(self, *v, sim));
    }

private:
    ActionStatus attempt(Agent& self, Agent& v, Simulation& sim) {
        const Tick now = sim.now();
        const int amount = std::min({amount_, v.inventory.get(item_), self.freeCarry()});
        const bool victimAsleep = isAsleep(v);
        const int witnesses = countWitnesses(sim, v.pos, self.id, v.id);
        const float selfGain = item_ == Item::Food ? self.needs.urgency(Need::Food) : 0.25f * self.trait(Trait::Greed);
        DeedContext ctx = makeDeed(Deed::StealFromAgent, &v, selfGain, theftHarm(v.inventory, item_, amount, &v, 1.f), 0.f,
                                   witnesses);
        const Verdict verdict = judge(self, ctx, sim);
        const bool caught = sim.rng().chance(catchChance(self, v, sim));

        if (caught && !victimAsleep) {
            const float pd = combatPower(v, sim) - combatPower(self, sim);
            if (sim.rng().chance(clampf(kThwartBase + kThwartPower * pd, 0.1f, 0.7f))) {
                // Foiled: grabbed by the wrist.
                ctx.harm *= kAttemptHarm;
                self.feel(Emotion::Fear, 0.15f);
                self.feel(Emotion::Guilt, verdict.guilt * 0.5f);
                self.wantsRethink = true;
                v.wantsRethink = true;
                sim.log(EventKind::Theft,
                        self.name + " tried to steal " + itemName(item_) + " from " + v.name + " but was caught", self.id,
                        v.id);
                broadcastDeedEx(sim, self, ctx, self.pos, TargetAwareness::Identifies);
                return ActionStatus::Failed;
            }
        }

        const int taken = v.inventory.take(item_, amount);
        if (taken <= 0) return ActionStatus::Failed;
        self.inventory.add(item_, taken);
        self.feel(Emotion::Guilt, verdict.guilt);
        self.feel(Emotion::Pride, verdict.pride);
        self.remember(MemoryType::Stole, v.id, now, 0.4f + 0.6f * ctx.harm);
        ++self.stats.thefts;
        ++v.stats.timesStolenFrom;
        ++sim.stats().thefts;
        std::string text = self.name + " stole " + describeItems(item_, taken) + " from " + v.name;
        if (victimAsleep) text += caught ? std::string(", who woke up and saw ") + himHer(self) : std::string(" while ") + heShe(v) + " slept";
        else if (caught) text += " and was caught in the act";
        sim.log(EventKind::Theft, text, self.id, v.id);
        if (caught) {
            self.wantsRethink = true;
            self.feel(Emotion::Fear, 0.1f);
            v.wantsRethink = true;
        }
        broadcastDeedEx(sim, self, ctx, self.pos, caught ? TargetAwareness::Identifies : TargetAwareness::NoticesLoss);
        return ActionStatus::Done;
    }

    AgentId victim_;
    std::string victimName_;
    Item item_;
    int amount_;
    int elapsed_ = 0;
};

class StealBehavior final : public Behavior {
public:
    const char* name() const override { return "steal"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (self.freeCarry() < 1) return;
        const bool child = isChildAgent(self, sim);
        const Motive m = stealMotive(self, sim, child);
        if (m.food < kMinFoodMotive && m.greed < kMinGreedMotive && !m.klepto) return;
        const float bravery = self.trait(Trait::Bravery);

        const Agent* best = nullptr;
        float bestScore = 0.f;
        Loot bestLoot;
        Verdict bestV;
        int bestWitnesses = 0;
        for (Agent* o : sim.agentsNear(self.pos, cfg::kSightRadius, self.id)) {
            if (!o || !o->alive || o->id == self.id || o->id == self.partner) continue;  // partners share a pot
            if (isFighting(*o)) continue;
            Loot loot = chooseLoot(self, o->inventory, m, child, kStealMaxFood);
            if (m.klepto) loot.motive = std::max(loot.motive, loot.amount > 0 ? kKleptoBase : 0.f);
            if (loot.amount < 1 || loot.motive <= 0.f) continue;
            const int witnesses = countWitnesses(sim, o->pos, self.id, o->id);
            const float selfGain = loot.item == Item::Food ? self.needs.urgency(Need::Food) : 0.25f * self.trait(Trait::Greed);
            const DeedContext ctx = makeDeed(Deed::StealFromAgent, o, selfGain,
                                             theftHarm(o->inventory, loot.item, loot.amount, o, 1.f), 0.f, witnesses);
            const Verdict v = judge(self, ctx, sim);
            const float moral = m.klepto ? kKleptoConscience * v.utility : v.utility;
            const float risk = kCatchRiskWeight * catchChance(self, *o, sim) * (1.2f - bravery);
            float score = kStealWeight * loot.motive * distanceFactor(chebyshev(self.pos, o->pos), kStealDistHalf) + moral -
                          risk + (isAsleep(*o) ? kStealAsleepBonus : 0.f);
            if (child) score *= kChildScoreMult;
            if (score > bestScore) {
                bestScore = score;
                best = o;
                bestLoot = loot;
                bestV = v;
                bestWitnesses = witnesses;
            }
        }
        if (!best) return;
        std::string why = lootPhrase(bestLoot, m, self) + ", " + best->name + " carries " +
                          describeItems(bestLoot.item, best->inventory.get(bestLoot.item)) +
                          (isAsleep(*best) ? " and is asleep" : "") + ", " + watchers(bestWitnesses);
        pushProposal(out, bestScore,
                     std::make_unique<StealAction>(best->id, best->name, bestLoot.item, bestLoot.amount),
                     withWhy(why, bestV));
    }
};

// ===========================================================================
// StealStorage
// ===========================================================================
// A private store whose owner isn't me, my partner or my family. Returns the living owner or nullptr.
const Agent* foreignOwner(const Agent& self, const Structure& s, const Simulation& sim) {
    if (!s.complete || s.isCommunal() || s.owner == self.id || s.owner == self.partner || s.id == self.home) return nullptr;
    if (s.type != StructureType::Storage && s.type != StructureType::Hut) return nullptr;
    const Agent* owner = living(sim, s.owner);
    if (!owner || self.isFamily(*owner)) return nullptr;  // dead owners' stores become communal/inherited
    return owner;
}

bool ownerWatching(const Agent& owner, Vec2i where) {
    return !isAsleep(owner) && chebyshev(owner.pos, where) <= cfg::kWitnessRadius;
}

class StealStorageAction final : public Action {
public:
    StealStorageAction(StructureId store, std::string label, Item item, int amount)
        : store_(store), label_(std::move(label)), item_(item), amount_(amount) {}
    const char* kind() const override { return kinds::StealStorage; }
    std::string label() const override { return label_; }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        ++elapsed_;
        Structure* s = sim.world().structure(store_);
        if (!s || elapsed_ > kStoreGiveUpTicks || self.freeCarry() < 1 || s->stock.get(item_) < 1)
            return finish(self, ActionStatus::Failed);
        Agent* owner = const_cast<Agent*>(foreignOwner(self, *s, sim));
        if (!owner) return finish(self, ActionStatus::Failed);
        if (chebyshev(self.pos, s->pos) > 1) {
            if (stepToward(self, sim, s->pos, 1) == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        const Tick now = sim.now();
        const int amount = std::min({amount_, s->stock.get(item_), self.freeCarry()});
        const float selfGain = item_ == Item::Food ? self.needs.urgency(Need::Food) : 0.25f * self.trait(Trait::Greed);
        const DeedContext ctx = makeDeed(Deed::StealFromStorage, owner, selfGain,
                                         theftHarm(s->stock, item_, amount, owner, kStoreHarmScale), 0.f,
                                         countWitnesses(sim, s->pos, self.id, owner->id), s);
        const Verdict verdict = judge(self, ctx, sim);
        const int taken = s->stock.take(item_, amount);
        if (taken <= 0) return finish(self, ActionStatus::Failed);
        self.inventory.add(item_, taken);
        self.feel(Emotion::Guilt, verdict.guilt);
        self.feel(Emotion::Pride, verdict.pride);
        self.remember(MemoryType::Stole, owner->id, now, 0.3f + 0.6f * ctx.harm);
        ++self.stats.thefts;
        ++owner->stats.timesStolenFrom;
        ++sim.stats().thefts;
        const bool seen = ownerWatching(*owner, s->pos);
        sim.log(EventKind::Theft,
                self.name + " stole " + describeItems(item_, taken) + " from " + owner->name + "'s " +
                    structureNoun(s->type) + (seen ? " under " + std::string(hisHer(*owner)) + " nose" : ""),
                self.id, owner->id);
        if (seen) self.wantsRethink = true;
        // The owner only knows who did it if they saw it (or an onlooker tells them).
        broadcastDeedEx(sim, self, ctx, s->pos, seen ? TargetAwareness::Identifies : TargetAwareness::NoticesLoss);
        return finish(self, ActionStatus::Done);
    }

private:
    StructureId store_;
    std::string label_;
    Item item_;
    int amount_;
    int elapsed_ = 0;
};

class StealStorageBehavior final : public Behavior {
public:
    const char* name() const override { return "steal_storage"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (!self.isAdult(sim.now()) || self.freeCarry() < 1) return;
        const Motive m = stealMotive(self, sim, false);
        if (m.food < kMinFoodMotive && m.greed < kMinGreedMotive && !m.klepto) return;
        const float bravery = self.trait(Trait::Bravery);
        const World& world = sim.world();

        const Structure* best = nullptr;
        const Agent* bestOwner = nullptr;
        float bestScore = 0.f;
        Loot bestLoot;
        Verdict bestV;
        int bestWitnesses = 0;
        for (StructureId sid : world.structuresNear(self.pos, kStoreSearch)) {
            const Structure* s = world.structure(sid);
            if (!s) continue;
            const Agent* owner = foreignOwner(self, *s, sim);
            if (!owner) continue;
            Loot loot = chooseLoot(self, s->stock, m, false, kStoreMaxFood);
            if (m.klepto) loot.motive = std::max(loot.motive, loot.amount > 0 ? kKleptoBase : 0.f);
            if (loot.amount < 1 || loot.motive <= 0.f) continue;
            const int witnesses = countWitnesses(sim, s->pos, self.id, owner->id);
            const float selfGain = loot.item == Item::Food ? self.needs.urgency(Need::Food) : 0.25f * self.trait(Trait::Greed);
            const DeedContext ctx = makeDeed(Deed::StealFromStorage, owner, selfGain,
                                             theftHarm(s->stock, loot.item, loot.amount, owner, kStoreHarmScale), 0.f,
                                             witnesses, s);
            const Verdict v = judge(self, ctx, sim);
            const float moral = m.klepto ? kKleptoConscience * v.utility : v.utility;
            const float risk = ownerWatching(*owner, s->pos) ? kOwnerPresentRisk * (1.1f - bravery) : 0.f;
            const float score =
                kStoreWeight * loot.motive * distanceFactor(chebyshev(self.pos, s->pos), kStoreDistHalf) + moral - risk;
            if (score > bestScore) {
                bestScore = score;
                best = s;
                bestOwner = owner;
                bestLoot = loot;
                bestV = v;
                bestWitnesses = witnesses;
            }
        }
        if (!best) return;
        const std::string place = bestOwner->name + "'s " + structureNoun(best->type);
        std::string why = lootPhrase(bestLoot, m, self) + ", " + place + " holds " +
                          describeItems(bestLoot.item, best->stock.get(bestLoot.item)) + ", " +
                          (ownerWatching(*bestOwner, best->pos) ? bestOwner->name + " is nearby" : bestOwner->name + " is away") +
                          ", " + watchers(bestWitnesses);
        pushProposal(out, bestScore,
                     std::make_unique<StealStorageAction>(best->id, "Stealing from " + place, bestLoot.item, bestLoot.amount),
                     withWhy(why, bestV));
    }
};

// ===========================================================================
// Vandalize
// ===========================================================================
float vandalHarm(const Structure& s) {
    switch (s.type) {
        case StructureType::Hut:
        case StructureType::Storage:
        case StructureType::Farm: return 0.5f;
        case StructureType::Monument:
        case StructureType::Workshop: return 0.4f;
        default: return 0.3f;
    }
}

class VandalizeAction final : public Action {
public:
    VandalizeAction(StructureId target, std::string label) : target_(target), label_(std::move(label)) {}
    const char* kind() const override { return kinds::Vandalize; }
    std::string label() const override { return label_; }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        ++elapsed_;
        World& world = sim.world();
        Structure* s = world.structure(target_);
        if (!s) return finish(self, smashed_ > 0 ? ActionStatus::Done : ActionStatus::Failed);
        if (chebyshev(self.pos, s->pos) > 1) {
            if (elapsed_ > kVandalGiveUpTicks) return finish(self, ActionStatus::Failed);
            if (stepToward(self, sim, s->pos, 1) == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        if (smashed_ == 0) startCondition_ = s->condition;
        ++smashed_;
        const float dmg = kVandalDamagePerTick * (0.6f + 0.8f * self.trait(Trait::Strength)) *
                          (self.inventory.has(Item::Tools) ? kVandalToolsMult : 1.f);
        s->condition = std::max(0.f, s->condition - dmg);
        if (smashed_ < kVandalTicks && s->condition > 0.f) return ActionStatus::Running;
        conclude(self, *s, sim);
        return finish(self, ActionStatus::Done);
    }

private:
    void conclude(Agent& self, Structure& s, Simulation& sim) {
        Agent* owner = (s.owner != kNoAgent && s.owner != self.id) ? living(sim, s.owner) : nullptr;
        const bool destroyed = s.condition <= 0.f;
        const float harm = clamp01(vandalHarm(s) * (destroyed ? 1.f : 0.5f + (startCondition_ - s.condition)));
        const AgentId ownerId = owner ? owner->id : kNoAgent;
        const float grudge = owner ? (self.findRel(ownerId) ? self.findRel(ownerId)->grievance : 0.f) : 0.f;
        const DeedContext ctx = makeDeed(Deed::Vandalize, owner, 0.3f * grudge, harm, 0.f,
                                         countWitnesses(sim, s.pos, self.id, ownerId), &s);
        const Verdict v = judge(self, ctx, sim);
        self.feel(Emotion::Guilt, v.guilt);
        self.feel(Emotion::Pride, v.pride);
        self.stress = clamp01(self.stress - kVandalStressRelief);
        self.emotions.add(Emotion::Anger, -0.1f);
        if (owner) {
            Relationship& r = self.rel(ownerId);
            r.grievance *= kVandalGrievanceKeep;
        }
        const std::string what = owner ? owner->name + "'s " + structureNoun(s.type)
                                       : std::string(s.isCommunal() ? "the communal " : "a ") + structureNoun(s.type);
        sim.log(EventKind::Misc,
                destroyed ? self.name + " destroyed " + what
                          : self.name + " vandalized " + what + " (condition " + pct(s.condition) + ")",
                self.id, ownerId);
        const bool seen = owner && ownerWatching(*owner, s.pos);
        broadcastDeedEx(sim, self, ctx, s.pos, seen ? TargetAwareness::Identifies : TargetAwareness::NoticesLoss);
        if (destroyed) {
            const StructureId sid = s.id;
            for (const auto& up : sim.agents()) {
                Agent* a = up.get();
                if (!a) continue;
                if (a->home == sid) a->home = kNoStructure;
                if (a->project == sid) a->project = kNoStructure;
            }
            sim.world().destroyStructure(sid);  // `s` is dangling after this
        }
    }

    StructureId target_;
    std::string label_;
    int elapsed_ = 0;
    int smashed_ = 0;
    float startCondition_ = 1.f;
};

class VandalizeBehavior final : public Behavior {
public:
    const char* name() const override { return "vandalize"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (!self.isAdult(sim.now())) return;
        const bool rampage = isRampaging(self);
        const float V = self.trait(Trait::Vengefulness);
        const float A = self.trait(Trait::Aggression);
        if (!rampage && V < 0.4f) return;
        const World& world = sim.world();

        const Structure* best = nullptr;
        const Agent* bestOwner = nullptr;
        float bestScore = 0.f;
        float bestGrudge = 0.f;
        Verdict bestV;
        for (StructureId sid : world.structuresNear(self.pos, kVandalSearch)) {
            const Structure* s = world.structure(sid);
            if (!s || s->condition <= 0.f || s->owner == self.id || s->id == self.home) continue;
            const Agent* owner = s->isCommunal() ? nullptr : living(std::as_const(sim), s->owner);
            const Relationship* r = owner ? self.findRel(owner->id) : nullptr;
            const float grudge = r ? r->grievance : 0.f;
            float motive = 0.f;
            if (rampage) {
                if (owner && cares(self, *owner)) continue;
                motive = kRampageVandalBase + kRampageGrudge * grudge;
            } else {
                if (!owner || cares(self, *owner) || grudge < kVandalMinGrievance) continue;
                motive = kVandalWeight * grudge * V * (0.4f + A);
            }
            const DeedContext ctx = makeDeed(Deed::Vandalize, owner, 0.3f * grudge, vandalHarm(*s), 0.f,
                                             countWitnesses(sim, s->pos, self.id, owner ? owner->id : kNoAgent), s);
            const Verdict v = judge(self, ctx, sim);
            const float score = motive * distanceFactor(chebyshev(self.pos, s->pos), kVandalDistHalf) +
                                (rampage ? kRampageConscience : 1.f) * v.utility;
            if (score > bestScore) {
                bestScore = score;
                best = s;
                bestOwner = owner;
                bestGrudge = grudge;
                bestV = v;
            }
        }
        if (!best) return;
        const std::string what = bestOwner ? bestOwner->name + "'s " + structureNoun(best->type)
                                           : std::string("the ") + structureNoun(best->type);
        std::string why = rampage ? "in a blind rage" : "I hate " + bestOwner->name + " (grievance " + pct(bestGrudge) + ")";
        why += "; smash " + what;
        pushProposal(out, bestScore, std::make_unique<VandalizeAction>(best->id, "Vandalizing " + what), withWhy(why, bestV));
    }
};

}  // namespace

void addCrimeBehaviors(BehaviorList& out) {
    out.push_back(std::make_unique<StealBehavior>());
    out.push_back(std::make_unique<StealStorageBehavior>());
    out.push_back(std::make_unique<VandalizeBehavior>());
}

}  // namespace heur::social
