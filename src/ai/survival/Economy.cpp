// Heuristica — economy: Farm (plant / tend / harvest / collect), Craft (tools), Deposit, Withdraw.
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ai/Morality.h"
#include "ai/survival/SurvivalCommon.h"
#include "sim/Simulation.h"
#include "social/Social.h"

namespace heur::survival {

namespace {
// ---- Farm ---------------------------------------------------------------------------------------
constexpr int kFarmRadius = 25;
constexpr float kHarvestBase = 0.45f;       // ripe crop: base + urgency(Food) × a + winterPrep × b (0.45..0.85)
constexpr float kHarvestHunger = 0.25f;
constexpr float kHarvestPrep = 0.15f;
constexpr float kCollectBase = 0.26f;       // fetch the harvest pile left at the field
constexpr float kCollectHunger = 0.3f;
constexpr int kCollectMinRoom = 3;
constexpr float kPlantBase = 0.28f;         // sow: (base + Int × a + Farming × b) × season (0.2..0.6)
constexpr float kPlantInt = 0.2f;
constexpr float kPlantSkill = 0.1f;
constexpr float kTendBelow = 0.6f;          // tend when `tended` drops below this
constexpr float kTendBase = 0.16f;          // (base + Ind × a + Farming × b + Int × c) × (1 − tended) × season
constexpr float kTendInd = 0.15f;
constexpr float kTendSkill = 0.1f;
constexpr float kTendInt = 0.08f;
constexpr std::array<float, 4> kFarmWorkSeason = {1.0f, 1.0f, 0.7f, 0.15f};  // spring..winter
constexpr int kPlantTicks = 8;
constexpr int kHarvestTicks = 12;
constexpr int kTendMaxTicks = 30;
constexpr float kTendRate = 0.03f;          // tended per tick × (1 + Farming)
constexpr float kYieldBase = 8.f;           // harvest = base + fertility × a + Farming × b food
constexpr float kYieldFertility = 10.f;
constexpr float kYieldSkill = 6.f;
constexpr float kToolYield = 1.1f;
constexpr int kHarvestSeeds = 3;            // a harvest also returns seed for the next sowing
constexpr float kPracticeHarvest = 3.f;
constexpr float kPlantTendedStart = 0.5f;   // freshly sown soil counts as half-tended

// ---- Craft ----------------------------------------------------------------------------------------
constexpr int kWorkshopRange = 20;
constexpr float kCraftWork = 36.f;          // ticks at Crafting 0 at a workshop
constexpr float kNoWorkshopSlowdown = 2.f;  // crafting without a workshop is allowed at half speed
constexpr float kWorkshopBonus = 0.05f;
constexpr float kNoWorkshopFactor = 0.85f;
constexpr float kSpareToolScore = 0.1f;     // industrious makers near a workshop craft spares (gifts / trade)
constexpr float kSpareToolMinInd = 0.6f;
constexpr int kSpareToolNear = 6;
constexpr int kMaxTools = 2;
constexpr float kPracticeCraft = 2.f;

// ---- Deposit -----------------------------------------------------------------------------------------
constexpr float kKeepFoodDays = 1.2f;       // carry about a day's food
constexpr int kKeepFoodMin = 3;
constexpr int kKeepHerbs = 2;
constexpr int kKeepFuel = 3;
constexpr float kKeepFuelCold = 0.4f;
constexpr int kMinDepositUnits = 3;         // don't walk to a store for less (unless the basket is full)
constexpr float kDepositBase = 0.08f;       // score = base + full × a + winterPrep × Int × b + Int × c + surplus × d
constexpr float kDepositFull = 0.3f;
constexpr float kDepositFullFrom = 0.55f;   // basket fullness where "full" starts to count...
constexpr float kDepositFullSpan = 0.4f;    // ...reaching 1 at From + Span
constexpr float kDepositPrep = 0.15f;
constexpr float kDepositInt = 0.06f;
constexpr float kDepositSurplus = 0.1f;
constexpr float kPrivateStoreBonus = 0.05f; // keeping it in my own store feels safest...
constexpr float kPrivateGreed = 0.06f;      // ...especially to the greedy
constexpr float kCommunalJudgeWeight = 0.5f;
constexpr float kDepositBroadcastChance = 0.15f;
constexpr int kDepositBroadcastMin = 3;
constexpr float kDepositBenefitPerUnit = 0.1f;

// ---- Withdraw -------------------------------------------------------------------------------------------
constexpr float kWithdrawBonus = 0.05f;     // stores beat gathering when close
constexpr float kCommunalTakeRestraint = 0.15f;  // × Integrity: hesitate to use common stock for private ends

bool canFarm(const Agent& a, const Structure& s, const Simulation& sim) {
    return s.type == StructureType::Farm && s.complete && (s.isCommunal() || isHousehold(a, s.owner, sim));
}

// =====================================================================================================
// Farm
// =====================================================================================================
enum class FarmTask { Harvest, Collect, Plant, Tend };

class FarmAction final : public Action {
public:
    FarmAction(StructureId farm, FarmTask task, std::string label) : farm_(farm), task_(task), label_(std::move(label)) {}
    const char* kind() const override { return kinds::Farm; }
    std::string label() const override { return label_; }
    void onAbort(Agent& self, Simulation& /*sim*/) override { clearPath(self); }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        World& w = sim.world();
        Structure* s = w.structure(farm_);
        if (!s || !canFarm(self, *s, sim)) return finish(self, ActionStatus::Failed);
        const MoveResult mr = stepToward(self, sim, s->pos, 1);
        if (mr == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
        if (mr == MoveResult::Moving) {
            if (++travel_ > tune::kMaxTravelTicks) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        const std::string where = structPhrase(self, *s, sim);

        // Re-evaluate on site: the field may have changed while I walked.
        FarmTask now = task_;
        if (s->harvestable()) now = FarmTask::Harvest;
        else if (now == FarmTask::Harvest) now = s->planted ? FarmTask::Tend : FarmTask::Plant;
        if (now == FarmTask::Plant && (s->planted || !self.inventory.has(Item::Seeds, tune::kSeedsPerPlanting)))
            now = s->planted ? FarmTask::Tend : FarmTask::Collect;
        if (now == FarmTask::Tend && !s->planted) now = FarmTask::Collect;
        if (now != current_) {
            current_ = now;
            work_ = 0;
        }
        ++work_;

        switch (now) {
            case FarmTask::Harvest:
                label_ = "Harvesting " + where;
                if (work_ < kHarvestTicks) return ActionStatus::Running;
                harvest(self, sim, *s);
                return finish(self, ActionStatus::Done);
            case FarmTask::Collect: {
                label_ = "Gathering up the harvest at " + where;
                const int n = std::min(self.freeCarry(), s->stock.get(Item::Food));
                if (n <= 0) return finish(self, ActionStatus::Failed);
                s->stock.take(Item::Food, n);
                self.inventory.add(Item::Food, n);
                return finish(self, ActionStatus::Done);
            }
            case FarmTask::Plant:
                label_ = "Sowing seeds in " + where;
                self.practice(Skill::Farming, tune::kPracticeWork);
                if (work_ < kPlantTicks) return ActionStatus::Running;
                self.inventory.take(Item::Seeds, tune::kSeedsPerPlanting);
                s->planted = true;
                s->growth = 0.f;
                s->tended = std::max(s->tended, kPlantTendedStart);
                self.feel(Emotion::Joy, 0.05f);
                return finish(self, ActionStatus::Done);
            case FarmTask::Tend:
                label_ = "Tending the crops in " + where;
                s->tended = std::min(1.f, s->tended + kTendRate * (1.f + self.skills.get(Skill::Farming)));
                self.practice(Skill::Farming, tune::kPracticeWork);
                if (s->tended >= 1.f || work_ >= kTendMaxTicks) return finish(self, ActionStatus::Done);
                return ActionStatus::Running;
        }
        return finish(self, ActionStatus::Failed);
    }

private:
    static void harvest(Agent& self, Simulation& sim, Structure& s) {
        const World& w = sim.world();
        const float fert = w.inBounds(s.pos) ? w.at(s.pos).fertility : 0.5f;
        float y = kYieldBase + kYieldFertility * fert + kYieldSkill * self.skills.get(Skill::Farming);
        if (ownsTools(self)) y *= kToolYield;
        const int yield = std::max(1, int(std::lround(y)));
        s.planted = false;
        s.growth = 0.f;
        s.tended = 0.f;
        // The crop goes onto the field's pile; I carry what I can, the rest waits for collection.
        s.stock.add(Item::Food, yield);
        const int carry = std::min(self.freeCarry(), s.stock.get(Item::Food));
        s.stock.take(Item::Food, carry);
        self.inventory.add(Item::Food, carry);
        const int seedsCarry = std::min(kHarvestSeeds, self.freeCarry());
        self.inventory.add(Item::Seeds, seedsCarry);
        s.stock.add(Item::Seeds, kHarvestSeeds - seedsCarry);
        self.stats.foodGathered += yield;
        self.practice(Skill::Farming, kPracticeHarvest);
        self.feel(Emotion::Joy, 0.15f);
        self.feel(Emotion::Pride, 0.1f);
        sim.log(EventKind::Misc, self.name + " harvested " + std::to_string(yield) + " food from " + structPhraseWorld(s, sim),
                self.id);
    }

    StructureId farm_;
    FarmTask task_;
    FarmTask current_ = FarmTask::Collect;
    std::string label_;
    int travel_ = 0;
    int work_ = -1;
};

class FarmBehavior final : public Behavior {
public:
    const char* name() const override { return "farm"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (isChild(self, sim)) return;
        const World& w = sim.world();
        const float season = kFarmWorkSeason[std::size_t(w.season())];
        const float urg = self.needs.urgency(Need::Food);
        const float night = nightWorkFactor(w, urg);
        const float intel = self.trait(Trait::Intelligence);
        const float skill = self.skills.get(Skill::Farming);

        float best = 0.f;
        StructureId bestId = kNoStructure;
        FarmTask bestTask = FarmTask::Tend;
        std::string bestReason;
        for (StructureId id : w.structuresNear(self.pos, kFarmRadius, StructureType::Farm)) {
            const Structure* s = w.structure(id);
            if (!s || !canFarm(self, *s, sim)) continue;
            float score = 0.f;
            FarmTask task = FarmTask::Tend;
            std::string reason;
            if (s->harvestable()) {
                if (self.freeCarry() <= 0) continue;
                score = kHarvestBase + kHarvestHunger * urg + kHarvestPrep * winterPrep(w);
                task = FarmTask::Harvest;
                reason = "the crop is ripe";
            } else if (!s->planted && self.inventory.has(Item::Seeds, tune::kSeedsPerPlanting) && season > 0.2f) {
                score = (kPlantBase + kPlantInt * intel + kPlantSkill * skill) * season;
                task = FarmTask::Plant;
                reason = "the field lies bare and I have seeds";
            } else if (s->planted && s->tended < kTendBelow) {
                score = (kTendBase + kTendInd * self.trait(Trait::Industriousness) + kTendSkill * skill + kTendInt * intel) *
                        (1.f - s->tended) * season;
                task = FarmTask::Tend;
                reason = "the crops need tending (" + std::to_string(int(s->growth * 100.f)) + "% grown)";
            }
            if (s->stock.get(Item::Food) > 0 && self.freeCarry() >= kCollectMinRoom) {
                const float c = kCollectBase + kCollectHunger * urg;
                if (c > score) {
                    score = c;
                    task = FarmTask::Collect;
                    reason = std::to_string(s->stock.get(Item::Food)) + " food waiting at the field";
                }
            }
            score *= distanceFactor(chebyshev(self.pos, s->pos), tune::kStructHalfDist) * night;
            if (score > best) {
                best = score;
                bestId = id;
                bestTask = task;
                bestReason = std::move(reason);
            }
        }
        if (bestId == kNoStructure) return;
        const std::string where = structPhrase(self, *w.structure(bestId), sim);
        std::string label;
        switch (bestTask) {
            case FarmTask::Harvest: label = "Harvesting " + where; break;
            case FarmTask::Collect: label = "Gathering up the harvest at " + where; break;
            case FarmTask::Plant: label = "Sowing seeds in " + where; break;
            case FarmTask::Tend: label = "Tending the crops in " + where; break;
        }
        out.push_back(Proposal{best, std::make_unique<FarmAction>(bestId, bestTask, label), bestReason});
    }
};

// =====================================================================================================
// Craft: 2 wood + 1 stone -> 1 tools (half speed without a workshop)
// =====================================================================================================
class CraftAction final : public Action {
public:
    CraftAction(StructureId workshop, std::string label) : workshop_(workshop), label_(std::move(label)) {}
    const char* kind() const override { return kinds::Craft; }
    std::string label() const override { return label_; }
    void onAbort(Agent& self, Simulation& /*sim*/) override { clearPath(self); }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        if (!self.inventory.has(Item::Wood, tune::kToolWood) || !self.inventory.has(Item::Stone, tune::kToolStone))
            return finish(self, ActionStatus::Failed);
        float slowdown = kNoWorkshopSlowdown;
        if (workshop_ != kNoStructure) {
            const Structure* s = sim.world().structure(workshop_);
            if (s && s->complete) {
                const MoveResult mr = stepToward(self, sim, s->pos, 1);
                if (mr == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
                if (mr == MoveResult::Moving) {
                    if (++travel_ > tune::kMaxTravelTicks) return finish(self, ActionStatus::Failed);
                    return ActionStatus::Running;
                }
                slowdown = 1.f;
            } else {
                workshop_ = kNoStructure;  // gone: carry on by hand
                label_ = "Knapping a stone axe by hand";
            }
        }
        progress_ += (1.f + self.skills.get(Skill::Crafting)) / (kCraftWork * slowdown);
        self.practice(Skill::Crafting, tune::kPracticeWork);
        if (progress_ < 1.f) return ActionStatus::Running;
        self.inventory.take(Item::Wood, tune::kToolWood);
        self.inventory.take(Item::Stone, tune::kToolStone);
        self.inventory.add(Item::Tools, 1);
        self.practice(Skill::Crafting, kPracticeCraft);
        self.feel(Emotion::Pride, 0.1f);
        self.needs.add(Need::Fun, 0.05f);
        return finish(self, ActionStatus::Done);
    }

private:
    StructureId workshop_;
    std::string label_;
    int travel_ = 0;
    float progress_ = 0.f;
};

class CraftBehavior final : public Behavior {
public:
    const char* name() const override { return "craft"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (isChild(self, sim)) return;
        if (!self.inventory.has(Item::Wood, tune::kToolWood) || !self.inventory.has(Item::Stone, tune::kToolStone)) return;
        const World& w = sim.world();
        const StructureId ws = w.nearestStructure(self.pos, kWorkshopRange, StructureType::Workshop,
                                                  [](const Structure& s) { return s.complete; });
        const int dws = ws != kNoStructure ? chebyshev(self.pos, w.structure(ws)->pos) : 0;
        float desire = craftDesire(self);
        std::string reason = "I have no tools";
        if (desire <= 0.f) {
            // A spare tool to give away or trade: only for the industrious, at a workshop.
            if (ws == kNoStructure || dws > kSpareToolNear || self.inventory.get(Item::Tools) >= kMaxTools ||
                self.trait(Trait::Industriousness) < kSpareToolMinInd)
                return;
            desire = kSpareToolScore * self.trait(Trait::Industriousness);
            reason = "a spare tool could help someone";
        }
        const float night = nightWorkFactor(w, 0.f);
        float score;
        std::string label;
        if (ws != kNoStructure) {
            score = (desire + kWorkshopBonus) * distanceFactor(dws, tune::kStructHalfDist);
            label = "Crafting stone tools at the workshop";
            reason += ", the workshop is " + std::to_string(dws) + " tiles away";
        } else {
            score = desire * kNoWorkshopFactor;
            label = "Knapping a stone axe by hand";
        }
        out.push_back(Proposal{score * night, std::make_unique<CraftAction>(ws, label), reason});
    }
};

// =====================================================================================================
// Deposit
// =====================================================================================================
// How many units of `item` the agent wants to keep on its person (consistent with materialDemand so
// deposits and withdrawals never ping-pong).
int keepAmount(const Agent& self, const Simulation& sim, Item item) {
    const World& w = sim.world();
    int keep = 0;
    if (const Structure* p = activeProject(self, sim)) keep += p->materialsMissing(item);
    switch (item) {
        case Item::Food: return std::max(kKeepFoodMin, int(std::ceil(dailyFoodUnits(self) * kKeepFoodDays)));
        case Item::Tools: return 1 << 20;  // never stored
        case Item::Wood: {
            if (craftDesire(self) > 0.f) keep += tune::kToolWood;
            const float cold = std::max(coldSeason(w), (1.f - w.temperature()) * 0.8f);
            if (cold > kKeepFuelCold) keep += kKeepFuel;
            return keep;
        }
        case Item::Stone:
            if (craftDesire(self) > 0.f) keep += tune::kToolStone;
            return keep;
        case Item::Seeds: {
            bool farm = false;
            for (StructureId id : w.structuresNear(self.pos, tune::kStoreRadius, StructureType::Farm)) {
                const Structure* f = w.structure(id);
                if (f && (f->isCommunal() || isHousehold(self, f->owner, sim))) farm = true;
            }
            return keep + (farm ? 2 : 1) * tune::kSeedsPerPlanting;
        }
        case Item::Herbs: return keep + kKeepHerbs;
        default: return keep;
    }
}

ItemCounts surplusPlan(const Agent& self, const Simulation& sim, int& total) {
    ItemCounts plan{};
    total = 0;
    for (int i = 0; i < kItemCount; ++i) {
        const Item it = Item(i);
        const int extra = self.inventory.get(it) - keepAmount(self, sim, it);
        if (extra > 0) {
            plan[std::size_t(i)] = extra;
            total += extra;
        }
    }
    return plan;
}

std::string planSummary(const ItemCounts& plan) {
    std::string s;
    for (int i = 0; i < kItemCount; ++i) {
        if (plan[std::size_t(i)] <= 0) continue;
        if (!s.empty()) s += ", ";
        s += std::to_string(plan[std::size_t(i)]) + " " + itemName(Item(i));
    }
    return s;
}

DeedContext depositContext(const Agent& self, const Structure& s, int units, const Simulation& sim) {
    DeedContext ctx;
    ctx.deed = Deed::Help;
    ctx.target = nullptr;
    ctx.targetStructure = &s;
    ctx.benefit = clampf(float(units) * kDepositBenefitPerUnit, 0.1f, 1.f);
    ctx.witnesses = countWitnesses(sim, s.pos, self.id);
    return ctx;
}

class DepositAction final : public Action {
public:
    DepositAction(StructureId store, ItemCounts plan, std::string label)
        : store_(store), plan_(plan), label_(std::move(label)) {}
    const char* kind() const override { return kinds::Deposit; }
    std::string label() const override { return label_; }
    void onAbort(Agent& self, Simulation& /*sim*/) override { clearPath(self); }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        Structure* s = sim.world().structure(store_);
        if (!s || !canUseStore(self, *s, sim) || s->stockFree() <= 0) return finish(self, ActionStatus::Failed);
        const MoveResult mr = stepToward(self, sim, s->pos, 1);
        if (mr == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
        if (mr == MoveResult::Moving) {
            if (++travel_ > tune::kMaxTravelTicks) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        if (++ticks_ < 2) return ActionStatus::Running;  // unpacking
        int moved = 0, food = 0;
        for (int i = 0; i < kItemCount; ++i) {
            const Item it = Item(i);
            const int n = std::min({plan_[std::size_t(i)], self.inventory.get(it), s->stockFree()});
            if (n <= 0) continue;
            self.inventory.take(it, n);
            s->stock.add(it, n);
            moved += n;
            if (it == Item::Food) food = n;
        }
        if (moved <= 0) return finish(self, ActionStatus::Failed);
        if (s->isCommunal()) {
            const DeedContext ctx = depositContext(self, *s, moved, sim);
            const Verdict v = judge(self, ctx, sim);
            self.feel(Emotion::Pride, v.pride);
            if (moved >= kDepositBroadcastMin && sim.rng().chance(kDepositBroadcastChance)) {
                sim.log(EventKind::Social,
                        self.name + " stocked " + structPhraseWorld(*s, sim) + " with " +
                            (food > 0 ? std::to_string(food) + " food" : std::to_string(moved) + " goods"),
                        self.id);
                broadcastDeed(sim, self, ctx, s->pos);
            }
        } else {
            self.feel(Emotion::Joy, 0.03f);
        }
        return finish(self, ActionStatus::Done);
    }

private:
    StructureId store_;
    ItemCounts plan_;
    std::string label_;
    int travel_ = 0;
    int ticks_ = 0;
};

class DepositBehavior final : public Behavior {
public:
    const char* name() const override { return "deposit"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (isChild(self, sim)) return;
        int total = 0;
        const ItemCounts plan = surplusPlan(self, sim, total);
        if (total <= 0) return;
        const int cap = std::max(1, self.carryCapacity());
        const float fullness = float(self.inventory.total()) / float(cap);
        if (total < kMinDepositUnits && fullness < kDepositFullFrom + kDepositFullSpan) return;
        const World& w = sim.world();
        const float intel = self.trait(Trait::Intelligence);
        const float base = kDepositBase + kDepositFull * clamp01((fullness - kDepositFullFrom) / kDepositFullSpan) +
                           kDepositPrep * winterPrep(w) * intel + kDepositInt * intel +
                           kDepositSurplus * float(total) / float(cap);

        const Structure* best = nullptr;
        float bestScore = 0.f;
        std::string bestWhy;
        for (StructureId id : w.structuresNear(self.pos, tune::kStoreRadius)) {
            const Structure* s = w.structure(id);
            if (!s || (s->type != StructureType::Storage && s->type != StructureType::Hut)) continue;
            if (s->stockFree() <= 0 || !canUseStore(self, *s, sim)) continue;
            float score = base;
            std::string why;
            if (s->isCommunal()) {
                const Verdict v = judge(self, depositContext(self, *s, std::min(total, s->stockFree()), sim), sim);
                score += kCommunalJudgeWeight * v.utility;
                why = v.rationale;
            } else {
                score += kPrivateStoreBonus + kPrivateGreed * self.trait(Trait::Greed);
                why = "safe in " + structPhrase(self, *s, sim);
            }
            score *= distanceFactor(chebyshev(self.pos, s->pos), tune::kStructHalfDist);
            if (score > bestScore) {
                bestScore = score;
                best = s;
                bestWhy = std::move(why);
            }
        }
        if (!best) return;
        const std::string where = structPhrase(self, *best, sim);
        std::string reason = fullness > kDepositFullFrom ? "my arms are full" : "putting surplus away";
        reason += " (" + planSummary(plan) + ")";
        if (!bestWhy.empty()) reason += ": " + bestWhy;
        out.push_back(Proposal{bestScore * nightWorkFactor(w, 0.f),
                               std::make_unique<DepositAction>(best->id, plan, "Stowing goods in " + where), reason});
    }
};

// =====================================================================================================
// Withdraw (materials for my project / tools / seeds, from my own or communal stores)
// =====================================================================================================
class WithdrawAction final : public Action {
public:
    WithdrawAction(StructureId store, Item item, int units, std::string label)
        : store_(store), item_(item), units_(units), label_(std::move(label)) {}
    const char* kind() const override { return kinds::Withdraw; }
    std::string label() const override { return label_; }
    void onAbort(Agent& self, Simulation& /*sim*/) override { clearPath(self); }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        Structure* s = sim.world().structure(store_);
        if (!s || !canUseStore(self, *s, sim) || !s->stock.has(item_)) return finish(self, ActionStatus::Failed);
        const MoveResult mr = stepToward(self, sim, s->pos, 1);
        if (mr == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
        if (mr == MoveResult::Moving) {
            if (++travel_ > tune::kMaxTravelTicks) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        const int n = s->stock.take(item_, std::min(units_, self.freeCarry()));
        if (n <= 0) return finish(self, ActionStatus::Failed);
        self.inventory.add(item_, n);
        return finish(self, ActionStatus::Done);
    }

private:
    StructureId store_;
    Item item_;
    int units_;
    std::string label_;
    int travel_ = 0;
};

class WithdrawBehavior final : public Behavior {
public:
    const char* name() const override { return "withdraw"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (isChild(self, sim) || self.freeCarry() <= 0) return;
        const World& w = sim.world();
        const float night = nightWorkFactor(w, 0.f);
        const Item items[] = {Item::Wood, Item::Stone, Item::Seeds, Item::Herbs};
        for (Item item : items) {
            const Demand d = materialDemand(self, sim, item);
            if (d.score <= 0.f) continue;
            const Structure* best = nullptr;
            float bestScore = 0.f;
            for (StructureId id : w.structuresNear(self.pos, tune::kStoreRadius)) {
                const Structure* s = w.structure(id);
                if (!s || !s->stock.has(item) || !canUseStore(self, *s, sim)) continue;
                float score = (d.score + kWithdrawBonus) * distanceFactor(chebyshev(self.pos, s->pos), tune::kStructHalfDist);
                if (s->isCommunal()) score *= 1.f - kCommunalTakeRestraint * self.trait(Trait::Integrity);
                if (score > bestScore) {
                    bestScore = score;
                    best = s;
                }
            }
            if (!best) continue;
            const int units = std::min({d.units, best->stock.get(item), self.freeCarry()});
            const std::string where = structPhrase(self, *best, sim);
            out.push_back(Proposal{bestScore * night,
                                   std::make_unique<WithdrawAction>(best->id, item, units,
                                                                    std::string("Fetching ") + itemName(item) + " from " +
                                                                        where + " " + d.forWhat),
                                   d.reason + ", " + std::to_string(best->stock.get(item)) + " in " + where});
        }
    }
};

}  // namespace

void registerEconomyBehaviors(BehaviorList& out) {
    out.push_back(std::make_unique<FarmBehavior>());
    out.push_back(std::make_unique<CraftBehavior>());
    out.push_back(std::make_unique<DepositBehavior>());
    out.push_back(std::make_unique<WithdrawBehavior>());
}

}  // namespace heur::survival
