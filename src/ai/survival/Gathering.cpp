// Heuristica — gathering: Forage (berries, wild grain), Fish, Chop, Mine, GatherHerbs.
//
// Food gathering is driven by foodDrive() (hunger + reserve + winter prep + greed/industry surplus);
// materials by materialDemand() (my project, tools, firewood, seeds, herbs) — so "need wood for my hut"
// shows up as a Chop proposal with that reason.
#include <memory>
#include <string>
#include <vector>

#include "ai/survival/SurvivalCommon.h"
#include "sim/Simulation.h"

namespace heur::survival {

namespace {
// ---- Harvest rates: units per tick at skill 0, without tools ----------------------------------
constexpr float kBerryRate = 0.20f;         // ~5 ticks a handful
constexpr float kGrainRate = 0.16f;
constexpr float kFishRate = 0.07f;          // slow, but needs nothing and works all winter
constexpr float kChopRate = 0.12f;
constexpr float kMineRate = 0.08f;
constexpr float kHerbRate = 0.15f;
constexpr float kSkillRateBonus = 1.0f;     // rate × (1 + skill × this)
constexpr float kStrengthRateBase = 0.85f;  // chop/mine: × (base + Strength × scale)
constexpr float kStrengthRateScale = 0.3f;
constexpr float kToolHarvestBonus = 1.5f;   // owning at least one tool
constexpr float kToolWearPerUnit = 0.01f;   // chance a tool breaks per unit harvested (~100 units per tool)
constexpr float kElderRate = 0.85f;
constexpr float kChildRate = 0.7f;
constexpr float kGrainFoodChance = 0.5f;    // wild grain also yields a little food
constexpr int kRetargetRadius = 5;          // when a bush/tree runs out, try another this close
constexpr float kPracticePerUnit = 0.5f;    // extra Agent::practice per unit harvested

// ---- Proposal shaping ------------------------------------------------------------------------------
constexpr float kGrainFoodValue = 0.5f;     // grain is a poor food source next to berries
constexpr float kFishFactor = 0.75f;        // fishing is slow: scores × this

Item gatherItem(ResourceKind k) {
    switch (k) {
        case ResourceKind::BerryBush:
        case ResourceKind::FishShoal: return Item::Food;
        case ResourceKind::Tree: return Item::Wood;
        case ResourceKind::Rock: return Item::Stone;
        case ResourceKind::HerbPatch: return Item::Herbs;
        case ResourceKind::WildGrain: return Item::Seeds;
        default: return Item::Food;
    }
}

// =====================================================================================================
// GatherAction: walk next to a resource tile, harvest at a skill/tool-scaled rate until `want` units,
// a full basket, or the patch (and its close neighbours) run dry.
// =====================================================================================================
class GatherAction final : public Action {
public:
    GatherAction(const char* kind, ResourceKind res, Vec2i target, int want, Skill skill, float baseRate,
                 std::string label)
        : kind_(kind), res_(res), target_(target), want_(std::max(1, want)), skill_(skill), baseRate_(baseRate),
          label_(std::move(label)) {}

    const char* kind() const override { return kind_; }
    std::string label() const override { return label_; }
    void onAbort(Agent& self, Simulation& /*sim*/) override { clearPath(self); }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        World& w = sim.world();
        ++ticks_;
        if (self.freeCarry() <= 0) return done(self);
        if (!patchOk(w, target_) && !retarget(w)) return done(self);

        const MoveResult mr = stepToward(self, sim, target_, 1);
        if (mr == MoveResult::Blocked) return finish(self, gathered_ > 0 ? ActionStatus::Done : ActionStatus::Failed);
        if (mr == MoveResult::Moving) {
            if (ticks_ > tune::kMaxTravelTicks && gathered_ == 0) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }

        progress_ += rate(self, sim);
        self.practice(skill_, tune::kPracticeWork);
        while (progress_ >= 1.f) {
            progress_ -= 1.f;
            if (w.harvest(target_, 1) <= 0) {
                if (!retarget(w)) return done(self);
                return ActionStatus::Running;  // walk to the neighbouring patch
            }
            collect(self, sim);
            if (gathered_ >= want_ || self.freeCarry() <= 0) return done(self);
        }
        return ActionStatus::Running;
    }

private:
    bool patchOk(const World& w, Vec2i p) const {
        if (!w.inBounds(p)) return false;
        const Tile& t = w.at(p);
        return t.resource == res_ && t.amount >= 1.f;
    }

    bool retarget(const World& w) {
        const std::optional<Vec2i> next = w.findNearestResource(target_, res_, kRetargetRadius, 1.f);
        if (!next) return false;
        target_ = *next;
        return true;
    }

    float rate(const Agent& self, const Simulation& sim) const {
        float r = baseRate_ * (1.f + self.skills.get(skill_) * kSkillRateBonus);
        if (res_ == ResourceKind::Tree || res_ == ResourceKind::Rock)
            r *= kStrengthRateBase + self.trait(Trait::Strength) * kStrengthRateScale;
        if (ownsTools(self)) r *= kToolHarvestBonus;
        if (isElder(self, sim)) r *= kElderRate;
        if (isChild(self, sim)) r *= kChildRate;
        return r;
    }

    void collect(Agent& self, Simulation& sim) {
        const Item item = gatherItem(res_);
        self.inventory.add(item, 1);
        if (item == Item::Food) self.stats.foodGathered++;
        if (res_ == ResourceKind::WildGrain && self.freeCarry() > 0 && sim.rng().chance(kGrainFoodChance)) {
            self.inventory.add(Item::Food, 1);
            self.stats.foodGathered++;
        }
        ++gathered_;
        self.practice(skill_, kPracticePerUnit);
        wearTools(self, sim, kToolWearPerUnit);
    }

    ActionStatus done(Agent& self) const {
        return finish(self, gathered_ > 0 ? ActionStatus::Done : ActionStatus::Failed);
    }

    const char* kind_;
    ResourceKind res_;
    Vec2i target_;
    int want_;
    Skill skill_;
    float baseRate_;
    std::string label_;
    float progress_ = 0.f;
    int gathered_ = 0;
    int ticks_ = 0;
};

std::string foodLabel(ResourceKind k, FoodPurpose p) {
    switch (k) {
        case ResourceKind::BerryBush:
            switch (p) {
                case FoodPurpose::Hunger: return "Picking berries to fill my belly";
                case FoodPurpose::Winter: return "Picking berries to store for winter";
                case FoodPurpose::Reserve: return "Picking berries for the larder";
                case FoodPurpose::Surplus: return "Picking berries";
            }
            break;
        case ResourceKind::WildGrain:
            switch (p) {
                case FoodPurpose::Hunger: return "Gleaning wild grain, I'm hungry";
                case FoodPurpose::Winter: return "Gathering wild grain for the winter";
                default: return "Gathering wild grain";
            }
        case ResourceKind::FishShoal:
            switch (p) {
                case FoodPurpose::Hunger: return "Fishing for my supper";
                case FoodPurpose::Winter: return "Fishing to stock up for winter";
                default: return "Fishing at the shore";
            }
        default: break;
    }
    return "Gathering food";
}

std::string tilesAway(const char* what, int d) { return std::string(what) + " " + std::to_string(d) + " tiles away"; }

// =====================================================================================================
// Behaviours
// =====================================================================================================
class ForageBehavior final : public Behavior {
public:
    const char* name() const override { return "forage"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (self.freeCarry() <= 0) return;
        const World& w = sim.world();
        const int radius = searchRadius(self, sim);
        const std::optional<Vec2i> grain = nearestResource(self, sim, ResourceKind::WildGrain, radius);

        const FoodDrive fd = foodDrive(self, sim);
        if (fd.score > 0.f) {
            const float night = nightWorkFactor(w, self.needs.urgency(Need::Food));
            float best = 0.f;
            ResourceKind kind = ResourceKind::None;
            Vec2i at{};
            int dist = 0;
            if (const auto berry = nearestResource(self, sim, ResourceKind::BerryBush, radius)) {
                const int d = chebyshev(self.pos, *berry);
                best = fd.score * distanceFactor(d, tune::kGatherHalfDist) * night;
                kind = ResourceKind::BerryBush;
                at = *berry;
                dist = d;
            }
            if (grain) {
                const int d = chebyshev(self.pos, *grain);
                const float s = fd.score * kGrainFoodValue * distanceFactor(d, tune::kGatherHalfDist) * night;
                if (s > best) {
                    best = s;
                    kind = ResourceKind::WildGrain;
                    at = *grain;
                    dist = d;
                }
            }
            if (kind != ResourceKind::None && best > 0.f) {
                const bool berries = kind == ResourceKind::BerryBush;
                out.push_back(Proposal{
                    best,
                    std::make_unique<GatherAction>(kinds::Forage, kind, at, fd.want, Skill::Foraging,
                                                   berries ? kBerryRate : kGrainRate, foodLabel(kind, fd.purpose)),
                    fd.reason + ", " + tilesAway(berries ? "berries" : "wild grain", dist)});
            }
        }

        // Seeds for farming come from wild grain.
        if (grain) {
            const Demand seeds = materialDemand(self, sim, Item::Seeds);
            if (seeds.score > 0.f) {
                const int d = chebyshev(self.pos, *grain);
                const float s = seeds.score * distanceFactor(d, tune::kGatherHalfDist) * nightWorkFactor(w, 0.f);
                out.push_back(Proposal{s,
                                       std::make_unique<GatherAction>(kinds::Forage, ResourceKind::WildGrain, *grain,
                                                                      seeds.units, Skill::Foraging, kGrainRate,
                                                                      "Gathering seeds " + seeds.forWhat),
                                       seeds.reason + ", " + tilesAway("wild grain", d)});
            }
        }
    }
};

class FishBehavior final : public Behavior {
public:
    const char* name() const override { return "fish"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (isChild(self, sim) || self.freeCarry() <= 0) return;
        const FoodDrive fd = foodDrive(self, sim);
        if (fd.score <= 0.f) return;
        const auto shoal = nearestResource(self, sim, ResourceKind::FishShoal, searchRadius(self, sim));
        if (!shoal) return;
        const World& w = sim.world();
        const int d = chebyshev(self.pos, *shoal);
        const float s = fd.score * kFishFactor * distanceFactor(d, tune::kGatherHalfDist) *
                        nightWorkFactor(w, self.needs.urgency(Need::Food));
        out.push_back(Proposal{s,
                               std::make_unique<GatherAction>(kinds::Fish, ResourceKind::FishShoal, *shoal, fd.want,
                                                              Skill::Foraging, kFishRate,
                                                              foodLabel(ResourceKind::FishShoal, fd.purpose)),
                               fd.reason + ", " + tilesAway("fish", d)});
    }
};

// Generic "I need item X; go harvest resource R" behaviour (Chop / Mine / GatherHerbs).
class MaterialBehavior final : public Behavior {
public:
    MaterialBehavior(const char* behaviorName, const char* kind, Item item, ResourceKind res, Skill skill, float rate,
                     const char* verb, const char* noun)
        : name_(behaviorName), kind_(kind), item_(item), res_(res), skill_(skill), rate_(rate), verb_(verb), noun_(noun) {}

    const char* name() const override { return name_; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (isChild(self, sim) || self.freeCarry() <= 0) return;
        const Demand d = materialDemand(self, sim, item_);
        if (d.score <= 0.f) return;
        const auto at = nearestResource(self, sim, res_, searchRadius(self, sim));
        if (!at) return;
        const World& w = sim.world();
        const int dist = chebyshev(self.pos, *at);
        const float urg = item_ == Item::Herbs ? clamp01(1.f - self.health) : 0.f;
        const float s = d.score * distanceFactor(dist, tune::kGatherHalfDist) * nightWorkFactor(w, urg);
        out.push_back(Proposal{s,
                               std::make_unique<GatherAction>(kind_, res_, *at, d.units, skill_, rate_,
                                                              std::string(verb_) + " " + d.forWhat),
                               d.reason + ", " + tilesAway(noun_, dist)});
    }

private:
    const char* name_;
    const char* kind_;
    Item item_;
    ResourceKind res_;
    Skill skill_;
    float rate_;
    const char* verb_;
    const char* noun_;
};

}  // namespace

void registerGatherBehaviors(BehaviorList& out) {
    out.push_back(std::make_unique<ForageBehavior>());
    out.push_back(std::make_unique<FishBehavior>());
    out.push_back(std::make_unique<MaterialBehavior>("chop", kinds::Chop, Item::Wood, ResourceKind::Tree,
                                                     Skill::Woodcutting, kChopRate, "Chopping a tree", "a tree"));
    out.push_back(std::make_unique<MaterialBehavior>("mine", kinds::Mine, Item::Stone, ResourceKind::Rock, Skill::Mining,
                                                     kMineRate, "Quarrying stone", "rocks"));
    out.push_back(std::make_unique<MaterialBehavior>("gather_herbs", kinds::GatherHerbs, Item::Herbs,
                                                     ResourceKind::HerbPatch, Skill::Foraging, kHerbRate,
                                                     "Gathering herbs", "herbs"));
}

}  // namespace heur::survival
