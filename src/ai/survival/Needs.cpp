// Heuristica — bodily needs: Eat, EatFromStorage, Sleep, WarmUp (+ feeding campfires), Heal.
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "ai/survival/SurvivalCommon.h"
#include "sim/Simulation.h"

namespace heur::survival {

namespace {
// ---- Eat (own inventory) ----------------------------------------------------------------------
constexpr float kEatProposeBelow = 0.72f;   // don't bother eating when fuller than this
constexpr float kEatWeight = 1.4f;          // score = urgency(Food) × weight (+ starving bonus): 0.1..1.6
constexpr float kEatStarvingBelow = 0.25f;
constexpr float kEatStarvingBonus = 0.2f;
constexpr int kEatMaxUnits = 4;

// ---- EatFromStorage ------------------------------------------------------------------------------
constexpr float kStoreEatProposeBelow = 0.7f;
constexpr float kStoreEatWeight = 1.3f;     // × distanceFactor: slightly below eating what you carry
constexpr float kStoreEatHalfDist = 14.f;
constexpr float kStoreEatStarvingBonus = 0.2f;
constexpr float kStoreEatCarryingFactor = 0.5f;  // carrying food? eat that first
constexpr float kCommunalRestraint = 0.2f;  // Empathy × this × (1 − urgency): leave communal food to the needy
constexpr int kStoreEatMaxUnits = 4;
constexpr float kStoreEatDesperateBelow = 0.25f;  // this hungry, distance matters less...
constexpr float kStoreEatDesperateFloor = 0.6f;   // ...distanceFactor never below this

// ---- Sleep ------------------------------------------------------------------------------------------
constexpr float kSleepRateHut = 1.f / (0.3f * float(cfg::kTicksPerDay));  // empty -> full in ~0.3 day sheltered
constexpr float kSleepOutdoorFactor = 0.6f; // sleeping rough is less restful...
constexpr float kSleepFireFactor = 0.8f;    // ...unless by a fire
constexpr float kSleepColdFloor = 0.5f;     // rate × (floor + (1 − floor) × Warmth): cold sleep is poor sleep
constexpr float kSleepNightWeight = 1.1f;   // urgency(Rest) weight at night
constexpr float kSleepDayWeight = 0.9f;     // ...and by day (naps)
constexpr float kNightSleepBonus = 0.45f;   // night strongly favours sleep: 0.45..1.55 at night
constexpr float kEveningSleepBonus = 0.2f;  // ramps in during the evening
constexpr float kDayNapBelow = 0.55f;       // daytime naps only when Rest is below this
constexpr float kEveningSleepBelow = 0.8f;  // evening: turn in early only if not well rested
constexpr float kWakeRested = 0.97f;        // fully rested: sleep turns light (interruptible)
constexpr float kWakeMorning = 0.8f;        // by day, wake once Rest is above this
constexpr float kWakeNeedUrgency = 0.45f;   // a rested night sleeper wakes when hunger/cold urgency exceeds this
constexpr float kWakeStarving = 0.1f;       // Food level that wakes anyone with Rest above kWakeMinRest
constexpr float kWakeFreezing = 0.15f;      // Warmth level that wakes an exposed sleeper
constexpr float kWakeMinRest = 0.3f;
constexpr int kMaxSleepTicks = 220;
constexpr int kSleepHomeRange = 30;         // walk home to sleep if it is this close...
constexpr int kSleepHomeRangeCold = 45;     // ...farther when sleeping rough would mean freezing
constexpr int kShelterRange = 8;            // otherwise bed down beside any hut this close
constexpr int kSleepFireRange = 15;         // ...or by a lit fire when cold
constexpr float kColdSleepTemp = 0.4f;

// ---- WarmUp ------------------------------------------------------------------------------------------
constexpr float kWarmSeekBelow = 0.7f;      // seek warmth when Warmth below this...
constexpr float kWarmSeekTemp = 0.5f;       // ...and it's cold out (or Warmth below kWarmAnyTemp)
constexpr float kWarmAnyTemp = 0.4f;
constexpr float kWarmWeight = 1.5f;         // score = urgency(Warmth) × weight × distance: 0.15..1.7
constexpr float kWarmEmergencyBelow = 0.3f;
constexpr float kWarmEmergencyBonus = 0.25f;
constexpr float kWarmHalfDist = 15.f;
constexpr float kWarmDesperateBelow = 0.25f;  // this cold, distance matters less...
constexpr float kWarmDesperateFloor = 0.5f;   // ...distanceFactor never below this
constexpr int kFireSeekRange = 20;
constexpr int kHomeWarmRange = 45;
constexpr int kUnlitRange = 15;
constexpr int kUnlitPenalty = 4;            // an unlit fire needs lighting: counts as this much farther
constexpr float kWarmEnough = 0.92f;
constexpr float kWarmOutsideTemp = 0.6f;    // warm weather ends the action early
constexpr int kMaxWarmTicks = 160;
constexpr float kFeedBelow = 150.f;         // fire fuel (ticks) below which it gets fed
constexpr float kFuelTarget = 400.f;        // feed up to this
constexpr int kFeedRange = 12;
constexpr float kFeedBase = 0.16f;          // feed score = base + cold × a + Empathy × b + crowd × c: 0.15..0.6
constexpr float kFeedCold = 0.25f;
constexpr float kFeedEmpathy = 0.08f;
constexpr float kFeedCrowd = 0.03f;         // per agent near the fire (max 5)
constexpr float kFeedHalfDist = 8.f;
constexpr float kFeedMinCold = 0.35f;       // only feed when it's this cold, at night, or people gather
constexpr float kLightBonus = 1.15f;        // lighting a dead fire feels more urgent
constexpr int kFeedTicksPerWood = 2;

// ---- Heal ---------------------------------------------------------------------------------------------
constexpr float kHealBelow = 0.7f;
constexpr float kHealBase = 0.35f;          // score = base + (0.7 − health) × scale: 0.35..1.4
constexpr float kHealScale = 1.5f;
constexpr float kHealUntil = 0.85f;
constexpr int kHealMaxHerbs = 3;
constexpr float kHerbStressRelief = 0.05f;
constexpr float kCalmStressAbove = 0.75f;   // herbs also calm frayed nerves (low score)
constexpr float kCalmBase = 0.12f;
constexpr int kHealTicksPerHerb = 4;

// =====================================================================================================
// Eat
// =====================================================================================================
class EatAction final : public Action {
public:
    explicit EatAction(std::string label) : label_(std::move(label)) {}
    const char* kind() const override { return kinds::Eat; }
    std::string label() const override { return label_; }
    ActionStatus tick(Agent& self, Simulation& /*sim*/) override {
        ++ticks_;
        if (ticks_ % tune::kTicksPerBite != 0) return ActionStatus::Running;
        if (self.needs.get(Need::Food) >= tune::kEatUntil || self.eatFromInventory(1) <= 0)
            return finish(self, eaten_ > 0 ? ActionStatus::Done : ActionStatus::Failed);
        ++eaten_;
        if (self.needs.get(Need::Food) >= tune::kEatUntil || !self.inventory.has(Item::Food) || eaten_ >= kEatMaxUnits)
            return finish(self, ActionStatus::Done);
        return ActionStatus::Running;
    }

private:
    std::string label_;
    int ticks_ = 0;
    int eaten_ = 0;
};

class EatBehavior final : public Behavior {
public:
    const char* name() const override { return "eat"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (!self.inventory.has(Item::Food)) return;
        const float level = self.needs.get(Need::Food);
        if (level >= kEatProposeBelow) return;
        float score = self.needs.urgency(Need::Food) * kEatWeight;
        if (level < kEatStarvingBelow) score += kEatStarvingBonus;
        const std::string label = level < kEatStarvingBelow ? "Wolfing down food from my pack" : "Eating a meal from my pack";
        out.push_back(Proposal{score, std::make_unique<EatAction>(label),
                               "hungry (food " + std::to_string(int(level * 100.f)) + "%), carrying " +
                                   std::to_string(self.inventory.get(Item::Food)) + " food"});
    }
};

// =====================================================================================================
// EatFromStorage
// =====================================================================================================
class EatFromStorageAction final : public Action {
public:
    EatFromStorageAction(StructureId store, std::string label) : store_(store), label_(std::move(label)) {}
    const char* kind() const override { return kinds::EatFromStorage; }
    std::string label() const override { return label_; }
    void onAbort(Agent& self, Simulation& /*sim*/) override { clearPath(self); }
    ActionStatus tick(Agent& self, Simulation& sim) override {
        Structure* s = sim.world().structure(store_);
        if (!s || !canUseStore(self, *s, sim) || s->stock.get(Item::Food) <= 0)
            return finish(self, eaten_ > 0 ? ActionStatus::Done : ActionStatus::Failed);
        const MoveResult mr = stepToward(self, sim, s->pos, 1);
        if (mr == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
        if (mr == MoveResult::Moving) {
            if (++travel_ > tune::kMaxTravelTicks) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        if (++ticks_ % tune::kTicksPerBite != 0) return ActionStatus::Running;
        if (self.needs.get(Need::Food) >= tune::kEatUntil) return finish(self, ActionStatus::Done);
        if (s->stock.take(Item::Food, 1) <= 0) return finish(self, eaten_ > 0 ? ActionStatus::Done : ActionStatus::Failed);
        self.eatUnits(1);
        ++eaten_;
        if (self.needs.get(Need::Food) >= tune::kEatUntil || eaten_ >= kStoreEatMaxUnits) return finish(self, ActionStatus::Done);
        return ActionStatus::Running;
    }

private:
    StructureId store_;
    std::string label_;
    int travel_ = 0;
    int ticks_ = 0;
    int eaten_ = 0;
};

class EatFromStorageBehavior final : public Behavior {
public:
    const char* name() const override { return "eat_storage"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        const float level = self.needs.get(Need::Food);
        if (level >= kStoreEatProposeBelow) return;
        const World& w = sim.world();
        const float urg = self.needs.urgency(Need::Food);
        const Structure* best = nullptr;
        float bestScore = 0.f;
        for (StructureId id : w.structuresNear(self.pos, tune::kStoreRadius)) {
            const Structure* s = w.structure(id);
            if (!s || s->stock.get(Item::Food) <= 0 || !canUseStore(self, *s, sim)) continue;
            float df = distanceFactor(chebyshev(self.pos, s->pos), kStoreEatHalfDist);
            if (level < kStoreEatDesperateBelow) df = std::max(df, kStoreEatDesperateFloor);
            float score = urg * kStoreEatWeight * df;
            if (s->isCommunal()) score *= 1.f - kCommunalRestraint * self.trait(Trait::Empathy) * (1.f - urg);
            if (score > bestScore) {
                bestScore = score;
                best = s;
            }
        }
        if (!best) return;
        if (level < kEatStarvingBelow) bestScore += kStoreEatStarvingBonus;
        if (self.inventory.has(Item::Food)) bestScore *= kStoreEatCarryingFactor;
        const std::string where = structPhrase(self, *best, sim);
        out.push_back(Proposal{bestScore, std::make_unique<EatFromStorageAction>(best->id, "Eating from " + where),
                               "hungry (food " + std::to_string(int(level * 100.f)) + "%), " +
                                   std::to_string(best->stock.get(Item::Food)) + " food in " + where});
    }
};

// =====================================================================================================
// Sleep
// =====================================================================================================
enum class SleepPlace { Hut, Fire, Rough };

// Is there somewhere warm I could actually get to (lit fire, home, any hut, or a pit I can light)?
bool warmPlaceReachable(const Agent& self, const Simulation& sim) {
    const World& w = sim.world();
    if (w.nearestStructure(self.pos, kFireSeekRange, StructureType::Campfire, [](const Structure& s) { return s.isLit(); }) !=
        kNoStructure)
        return true;
    if (const Structure* home = effectiveHome(self, sim)) {
        if (chebyshev(self.pos, home->pos) <= kHomeWarmRange) return true;
    }
    if (w.nearestStructure(self.pos, kShelterRange, StructureType::Hut, [](const Structure& s) { return s.complete; }) !=
        kNoStructure)
        return true;
    return self.inventory.has(Item::Wood) &&
           w.nearestStructure(self.pos, kUnlitRange, StructureType::Campfire, [](const Structure& s) { return s.complete; }) !=
               kNoStructure;
}

class SleepAction final : public Action {
public:
    SleepAction(StructureId place, SleepPlace where, std::string travelLabel, std::string sleepLabel)
        : place_(place), where_(where), label_(std::move(travelLabel)), sleepLabel_(std::move(sleepLabel)) {}
    const char* kind() const override { return kinds::Sleep; }
    std::string label() const override { return label_; }
    // Deep sleep can't be preempted (the brain still wakes us on wantsRethink); light sleep can.
    bool interruptible() const override { return !asleep_ || light_; }
    void onAbort(Agent& self, Simulation& /*sim*/) override { clearPath(self); }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        const World& w = sim.world();
        if (!asleep_) {
            if (place_ != kNoStructure && where_ != SleepPlace::Rough) {
                const Structure* s = w.structure(place_);
                const bool ok = s && s->complete && (where_ == SleepPlace::Hut ? s->type == StructureType::Hut : s->isLit());
                if (ok) {
                    const MoveResult mr = stepToward(self, sim, s->pos, where_ == SleepPlace::Hut ? 1 : 2);
                    if (mr == MoveResult::Moving && ++travel_ <= tune::kMaxTravelTicks) return ActionStatus::Running;
                    if (mr == MoveResult::Arrived) settleIn(self, *s);
                }
            }
            asleep_ = true;
            label_ = sleepLabel_;
            clearPath(self);
        }

        ++ticks_;
        const bool sheltered = w.isSheltered(self.pos);
        const bool fire = w.nearWarmth(self.pos);
        float rate = kSleepRateHut;
        if (!sheltered) rate *= fire ? kSleepFireFactor : kSleepOutdoorFactor;
        rate *= kSleepColdFloor + (1.f - kSleepColdFloor) * self.needs.get(Need::Warmth);
        self.needs.add(Need::Rest, rate);  // (Agent::updateBody pauses Rest decay while asleep)

        const float rest = self.needs.get(Need::Rest);
        const bool night = w.isNight();
        light_ = rest >= kWakeRested;
        const bool exposed = !sheltered && !fire;
        if (!night && rest >= kWakeMorning) return wake(self);
        if (light_ && (self.needs.urgency(Need::Food) > kWakeNeedUrgency ||
                       (exposed && self.needs.urgency(Need::Warmth) > kWakeNeedUrgency)))
            return wake(self);
        if (rest > kWakeMinRest && self.needs.get(Need::Food) < kWakeStarving) return wake(self);
        // Freezing wakes an exposed sleeper only if warmth is within reach (else waking just thrashes).
        if (rest > kWakeMinRest && exposed && self.needs.get(Need::Warmth) < kWakeFreezing && ticks_ % 10 == 1 &&
            warmPlaceReachable(self, sim))
            return wake(self);
        if (ticks_ > kMaxSleepTicks) return wake(self);
        return ActionStatus::Running;
    }

private:
    // Moving in: sleeping in a partner's hut makes it home.
    void settleIn(Agent& self, const Structure& s) {
        if (where_ != SleepPlace::Hut || self.home != kNoStructure) return;
        if (s.owner != kNoAgent && s.owner == self.partner) self.home = s.id;
    }
    static ActionStatus wake(Agent& self) { return finish(self, ActionStatus::Done); }

    StructureId place_;
    SleepPlace where_;
    std::string label_;
    std::string sleepLabel_;
    bool asleep_ = false;
    bool light_ = false;
    int travel_ = 0;
    int ticks_ = 0;
};

class SleepBehavior final : public Behavior {
public:
    const char* name() const override { return "sleep"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        const World& w = sim.world();
        const bool asleepNow = self.actionKind == kinds::Sleep;
        const float rest = self.needs.get(Need::Rest);
        const float u = self.needs.urgency(Need::Rest);
        const bool night = w.isNight();
        const float eve = eveningness(w);
        if (!asleepNow && !night) {
            if (eve > 0.f ? rest > kEveningSleepBelow : rest > kDayNapBelow) return;
        }
        float score = u * (night ? kSleepNightWeight : kSleepDayWeight);
        score += night ? kNightSleepBonus : eve * kEveningSleepBonus;

        std::string reason;
        if (u > 0.6f) reason = "exhausted (rest " + std::to_string(int(rest * 100.f)) + "%)";
        else if (night) reason = "it's night";
        else if (eve > 0.f) reason = "the day is ending";
        else reason = "tired (rest " + std::to_string(int(rest * 100.f)) + "%)";

        // Where to sleep: home, else beside any hut, else by a fire when cold, else on the spot.
        const float temp = w.temperature();
        const bool cold = temp < kColdSleepTemp || w.season() == Season::Winter;
        const Structure* home = effectiveHome(self, sim);
        if (home && chebyshev(self.pos, home->pos) <= (cold ? kSleepHomeRangeCold : kSleepHomeRange)) {
            const bool mine = home->owner == self.id;
            const std::string bed = mine ? "Sleeping in my hut" : "Sleeping at home";
            const std::string travel = chebyshev(self.pos, home->pos) > 1 ? "Heading home to sleep" : bed;
            out.push_back(Proposal{score, std::make_unique<SleepAction>(home->id, SleepPlace::Hut, travel, bed), reason});
            return;
        }
        const StructureId hut = w.nearestStructure(self.pos, kShelterRange, StructureType::Hut,
                                                   [](const Structure& s) { return s.complete; });
        if (hut != kNoStructure) {
            const Structure* s = w.structure(hut);
            const std::string bed = "Sleeping in the lee of " + structPhrase(self, *s, sim);
            out.push_back(Proposal{score, std::make_unique<SleepAction>(hut, SleepPlace::Hut, "Looking for shelter to sleep", bed),
                                   reason + ", no home of my own nearby"});
            return;
        }
        if (cold) {
            const StructureId fire = w.nearestStructure(self.pos, kSleepFireRange, StructureType::Campfire,
                                                        [](const Structure& s) { return s.isLit(); });
            if (fire != kNoStructure) {
                out.push_back(Proposal{score,
                                       std::make_unique<SleepAction>(fire, SleepPlace::Fire, "Heading to the fire to sleep",
                                                                     "Sleeping curled up by the campfire"),
                                       reason + ", it's cold"});
                return;
            }
        }
        std::string bed;
        if (!night) bed = "Napping in the grass";
        else if (cold) bed = "Shivering through the night outdoors";
        else bed = "Sleeping under the stars";
        out.push_back(Proposal{score, std::make_unique<SleepAction>(kNoStructure, SleepPlace::Rough, bed, bed), reason});
    }
};

// =====================================================================================================
// WarmUp (seek a fire / shelter) and feeding campfires
// =====================================================================================================
class WarmUpAction final : public Action {
public:
    WarmUpAction(StructureId place, bool feedOnly, std::string label)
        : place_(place), feedOnly_(feedOnly), label_(std::move(label)) {}
    const char* kind() const override { return kinds::WarmUp; }
    std::string label() const override { return label_; }
    void onAbort(Agent& self, Simulation& /*sim*/) override { clearPath(self); }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        World& w = sim.world();
        ++ticks_;
        if (place_ != kNoStructure) {
            Structure* s = w.structure(place_);
            if (!s || !s->complete) return finish(self, ActionStatus::Failed);
            const bool isFire = s->type == StructureType::Campfire;
            const bool hasWood = self.inventory.has(Item::Wood);
            if (feedOnly_ && (!hasWood || s->fuel >= kFuelTarget)) return finish(self, fed_ > 0 ? ActionStatus::Done : ActionStatus::Failed);
            if (isFire && !s->isLit() && !hasWood && !feedOnly_) return finish(self, ActionStatus::Failed);  // dead and I can't light it
            if (!arrived_) {
                const MoveResult mr = stepToward(self, sim, s->pos, isFire ? 2 : 1);
                if (mr == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
                if (mr == MoveResult::Moving) {
                    if (ticks_ > tune::kMaxTravelTicks) return finish(self, ActionStatus::Failed);
                    return ActionStatus::Running;
                }
                arrived_ = true;
                clearPath(self);
                if (!feedOnly_) label_ = isFire ? "Warming my hands by the campfire" : "Sheltering by " + structPhrase(self, *s, sim);
            }
            const bool wantFeed = isFire && hasWood && s->fuel < (feedOnly_ ? kFuelTarget : kFeedBelow);
            if (wantFeed) {
                if (ticks_ % kFeedTicksPerWood == 0) {
                    const bool wasLit = s->isLit();
                    self.inventory.take(Item::Wood, 1);
                    s->fuel += tune::kFuelPerWood;
                    ++fed_;
                    label_ = wasLit ? "Feeding the campfire" : "Lighting the campfire";
                    if (!wasLit) self.feel(Emotion::Joy, 0.05f);
                    self.feel(Emotion::Pride, 0.02f);
                }
                return ActionStatus::Running;
            }
            if (feedOnly_) return finish(self, fed_ > 0 ? ActionStatus::Done : ActionStatus::Failed);
        } else if (!w.nearWarmth(self.pos) && !w.isSheltered(self.pos)) {
            return finish(self, ActionStatus::Failed);  // the fire I was huddling by went out
        }
        const float warmth = self.needs.get(Need::Warmth);
        if (warmth >= kWarmEnough) return finish(self, ActionStatus::Done);
        if (w.temperature() > kWarmOutsideTemp && ticks_ > 10) return finish(self, ActionStatus::Done);
        if (ticks_ > kMaxWarmTicks) return finish(self, ActionStatus::Done);
        return ActionStatus::Running;
    }

private:
    StructureId place_;
    bool feedOnly_;
    std::string label_;
    bool arrived_ = false;
    int ticks_ = 0;
    int fed_ = 0;
};

class WarmUpBehavior final : public Behavior {
public:
    const char* name() const override { return "warm_up"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        proposeSeekWarmth(self, sim, out);
        proposeFeedFire(self, sim, out);
    }

private:
    static void proposeSeekWarmth(const Agent& self, Simulation& sim, std::vector<Proposal>& out) {
        const World& w = sim.world();
        const float warmth = self.needs.get(Need::Warmth);
        const float temp = w.temperature();
        if (warmth >= kWarmSeekBelow || (temp >= kWarmSeekTemp && warmth >= kWarmAnyTemp)) return;

        StructureId target = kNoStructure;
        int bestD = 1 << 20;
        std::string label;
        const bool warmHere = w.nearWarmth(self.pos) || w.isSheltered(self.pos);
        if (warmHere) {
            bestD = 0;
            label = w.nearWarmth(self.pos) ? "Warming up by the fire" : "Huddling out of the wind by a hut";
        } else {
            const StructureId fire = w.nearestStructure(self.pos, kFireSeekRange, StructureType::Campfire,
                                                        [](const Structure& s) { return s.isLit(); });
            if (fire != kNoStructure) {
                const int d = chebyshev(self.pos, w.structure(fire)->pos);
                if (d < bestD) {
                    bestD = d;
                    target = fire;
                    label = "Heading to the campfire to warm up";
                }
            }
            if (const Structure* home = effectiveHome(self, sim)) {
                const int d = chebyshev(self.pos, home->pos);
                if (d <= kHomeWarmRange && d < bestD) {
                    bestD = d;
                    target = home->id;
                    label = "Going home to get out of the cold";
                }
            }
            const StructureId hut = w.nearestStructure(self.pos, kShelterRange, StructureType::Hut,
                                                       [](const Structure& s) { return s.complete; });
            if (hut != kNoStructure) {
                const int d = chebyshev(self.pos, w.structure(hut)->pos);
                if (d < bestD) {
                    bestD = d;
                    target = hut;
                    label = "Sheltering beside " + structPhrase(self, *w.structure(hut), sim);
                }
            }
            if (self.inventory.has(Item::Wood)) {
                const StructureId pit = w.nearestStructure(self.pos, kUnlitRange, StructureType::Campfire,
                                                           [](const Structure& s) { return s.complete && !s.isLit(); });
                if (pit != kNoStructure) {
                    const int d = chebyshev(self.pos, w.structure(pit)->pos) + kUnlitPenalty;
                    if (d < bestD) {
                        bestD = d;
                        target = pit;
                        label = "Going to light the campfire";
                    }
                }
            }
            if (target == kNoStructure) return;  // nowhere warm: building a fire/hut is the long-term answer
        }

        float score = self.needs.urgency(Need::Warmth) * kWarmWeight;
        if (warmth < kWarmEmergencyBelow) score += kWarmEmergencyBonus;
        float df = distanceFactor(bestD, kWarmHalfDist);
        if (warmth < kWarmDesperateBelow) df = std::max(df, kWarmDesperateFloor);
        score *= df;
        out.push_back(Proposal{score, std::make_unique<WarmUpAction>(target, false, label),
                               "cold (warmth " + std::to_string(int(warmth * 100.f)) + "%, it's " +
                                   (temp < 0.25f ? "freezing" : "chilly") + ")"});
    }

    static void proposeFeedFire(const Agent& self, Simulation& sim, std::vector<Proposal>& out) {
        if (!self.inventory.has(Item::Wood)) return;
        const World& w = sim.world();
        const StructureId fid = w.nearestStructure(self.pos, kFeedRange, StructureType::Campfire,
                                                   [](const Structure& s) { return s.complete && s.fuel < kFeedBelow; });
        if (fid == kNoStructure) return;
        const Structure* f = w.structure(fid);
        const float cold = std::max(coldSeason(w), 1.f - w.temperature());
        const int crowd = int(sim.agentsNear(f->pos, 4, self.id).size());
        if (cold < kFeedMinCold && !w.isNight() && crowd < 2) return;
        float score = kFeedBase + kFeedCold * cold + kFeedEmpathy * self.trait(Trait::Empathy) +
                      kFeedCrowd * float(std::min(crowd, 5));
        score *= distanceFactor(chebyshev(self.pos, f->pos), kFeedHalfDist);
        const bool dead = f->fuel <= 0.f;
        if (dead) score *= kLightBonus;
        out.push_back(Proposal{score,
                               std::make_unique<WarmUpAction>(fid, true, dead ? "Lighting the campfire" : "Feeding the campfire"),
                               dead ? "the fire has gone out and I have wood" : "the fire is burning low"});
    }
};

// =====================================================================================================
// Heal (herbs)
// =====================================================================================================
class HealAction final : public Action {
public:
    HealAction(bool calmOnly, std::string label) : calmOnly_(calmOnly), label_(std::move(label)) {}
    const char* kind() const override { return kinds::Heal; }
    std::string label() const override { return label_; }
    ActionStatus tick(Agent& self, Simulation& /*sim*/) override {
        if (++ticks_ % kHealTicksPerHerb != 0) return ActionStatus::Running;
        if (self.inventory.take(Item::Herbs, 1) <= 0) return finish(self, used_ > 0 ? ActionStatus::Done : ActionStatus::Failed);
        ++used_;
        self.health = std::min(1.f, self.health + cfg::kHerbHealAmount);
        self.stress = clamp01(self.stress - kHerbStressRelief);
        const bool enough = calmOnly_ || self.health >= kHealUntil;
        if (enough || used_ >= kHealMaxHerbs || !self.inventory.has(Item::Herbs)) return finish(self, ActionStatus::Done);
        return ActionStatus::Running;
    }

private:
    bool calmOnly_;
    std::string label_;
    int ticks_ = 0;
    int used_ = 0;
};

class HealBehavior final : public Behavior {
public:
    const char* name() const override { return "heal"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (!self.inventory.has(Item::Herbs) || isChild(self, sim)) return;
        if (self.health < kHealBelow) {
            const float score = kHealBase + (kHealBelow - self.health) * kHealScale;
            out.push_back(Proposal{score, std::make_unique<HealAction>(false, "Chewing healing herbs"),
                                   "hurt (health " + std::to_string(int(self.health * 100.f)) + "%), I have herbs"});
        } else if (self.stress > kCalmStressAbove) {
            out.push_back(Proposal{kCalmBase + (self.stress - kCalmStressAbove),
                                   std::make_unique<HealAction>(true, "Chewing calming herbs"), "my nerves are frayed"});
        }
    }
};

}  // namespace

void registerNeedBehaviors(BehaviorList& out) {
    out.push_back(std::make_unique<EatBehavior>());
    out.push_back(std::make_unique<EatFromStorageBehavior>());
    out.push_back(std::make_unique<SleepBehavior>());
    out.push_back(std::make_unique<WarmUpBehavior>());
    out.push_back(std::make_unique<HealBehavior>());
}

}  // namespace heur::survival
