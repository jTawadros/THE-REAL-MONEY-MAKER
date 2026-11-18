// Heuristica — free time & low-key states: Idle, Wander, Explore, GoHome, Mope, Play, MakeArt.
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "ai/survival/SurvivalCommon.h"
#include "sim/Simulation.h"

namespace heur::survival {

namespace {
// ---- Idle / Wander -------------------------------------------------------------------------------
constexpr float kIdleScore = 0.05f;         // the floor every other option must beat
constexpr int kIdleMinTicks = 6;
constexpr int kIdleMaxTicks = 14;
constexpr float kWanderBase = 0.055f;       // 0.055..0.105 with Curiosity
constexpr float kWanderCuriosity = 0.05f;
constexpr int kWanderRadius = 6;
constexpr int kWanderMaxTicks = 25;
constexpr float kWanderFun = 0.0005f;       // per tick

// ---- Explore ---------------------------------------------------------------------------------------
constexpr float kExploreCuriosity = 0.16f;  // score = Curiosity × this × (base + urgency(Fun)) × comfort (0..0.2)
constexpr float kExploreFunBase = 0.5f;
constexpr float kExploreColdDamp = 0.8f;    // × (1 − damp × coldSeason): nobody sightsees in winter
constexpr int kExploreLeash = 24;           // explore goals stay within this of home
constexpr int kExploreTries = 6;
constexpr float kExploreDepleted = 0.3f;    // nothing to eat within reach: + base + hunger × scale
constexpr float kExploreDepletedHunger = 0.4f;
constexpr float kExploreDepletedMinHunger = 0.15f;
constexpr int kExploreMinDist = 15;
constexpr int kExploreMaxDist = 25;
constexpr int kExploreMaxTicks = 90;
constexpr float kExploreFun = 0.0015f;      // per tick × (0.5 + Curiosity)
constexpr float kExploreArriveFun = 0.04f;

// ---- GoHome --------------------------------------------------------------------------------------------
constexpr float kGoHomeBase = 0.12f;        // evening/night drift home: 0.12..0.35
constexpr float kGoHomeEvening = 0.15f;
constexpr float kGoHomeFamily = 0.08f;      // × Sociability
constexpr int kGoHomeMinDist = 5;
constexpr float kGoHomeMinRest = 0.55f;     // sleepier agents just go to sleep (Sleep walks home too)
constexpr float kGoHomeKeepFoodDays = 1.2f; // on arrival, stow food beyond this in the hut
constexpr int kGoHomeKeepFoodMin = 3;

// ---- Mope ------------------------------------------------------------------------------------------------
constexpr float kGriefDays = 2.5f;          // a loved one's death weighs on me this long
constexpr float kMopeBase = 0.1f;           // score = base + grief × a + Sadness × b (0.1..0.65)
constexpr float kMopeGrief = 0.35f;
constexpr float kMopeSadness = 0.2f;
constexpr float kMopeMinGrief = 0.15f;
constexpr float kMopeMinSadness = 0.5f;
constexpr float kMopeDespair = 0.25f;       // Despair breakdown floor (brain multiplies Mope ×3 -> 0.75,
                                            // so a starving agent's food options still break through)
constexpr int kMopeTicks = 40;
constexpr int kMopeStepEvery = 5;           // shuffles slowly
constexpr float kMopeStressRelief = 0.0006f;
constexpr float kMopeSadnessRelief = 0.002f;

// ---- Play -----------------------------------------------------------------------------------------------
constexpr float kPlayChildBase = 0.22f;     // children: base + urgency(Fun) × a (0.22..0.72)
constexpr float kPlayChildFun = 0.5f;
constexpr float kPlayAdultBase = 0.03f;     // adults: base + urgency(Fun) × a + Sociability × b
constexpr float kPlayAdultFun = 0.3f;
constexpr float kPlayAdultSoc = 0.03f;
constexpr int kPlayTicks = 30;
constexpr int kPlayHopEvery = 3;
constexpr float kPlayFunPerTick = 0.012f;
constexpr float kPlayStressRelief = 0.0004f;
constexpr int kPlaymateRadius = 5;

// ---- MakeArt -------------------------------------------------------------------------------------------
constexpr float kArtFun = 0.45f;            // score = urgency(Fun) × a + Creativity × b + Creativity × stress × c
constexpr float kArtCreativity = 0.14f;     // typical 0.07..0.45
constexpr float kArtStress = 0.1f;
constexpr int kArtTicks = 40;
constexpr float kArtFunPerTick = 0.01f;     // × (0.6 + Creativity)
constexpr float kArtStressRelief = 0.0007f;
constexpr float kArtPrideBase = 0.12f;
constexpr float kArtPrideCreativity = 0.15f;

// ---- Comfort gate for leisure ------------------------------------------------------------------------
constexpr float kComfortFood = 0.35f;
constexpr float kComfortRest = 0.25f;
constexpr float kComfortWarmth = 0.35f;
constexpr float kUncomfortable = 0.3f;      // leisure × this when a basic need is low

float comfort(const Agent& a) {
    const bool ok = a.needs.get(Need::Food) > kComfortFood && a.needs.get(Need::Rest) > kComfortRest &&
                    a.needs.get(Need::Warmth) > kComfortWarmth;
    return ok ? 1.f : kUncomfortable;
}

const char* pickLabel(Simulation& sim, std::initializer_list<const char*> options) {
    const int n = int(options.size());
    return *(options.begin() + sim.rng().rangeInt(0, n - 1));
}

std::string compass(Vec2i d) {
    std::string s;
    if (d.y < -std::abs(d.x) / 2) s = "north";
    else if (d.y > std::abs(d.x) / 2) s = "south";
    if (d.x > std::abs(d.y) / 2) s += s.empty() ? "east" : "-east";
    else if (d.x < -std::abs(d.y) / 2) s += s.empty() ? "west" : "-west";
    return s.empty() ? "nearby" : s;
}

// Walk to a goal (range 0..1); returns Running while moving, Done on arrival, Failed when blocked/too long.
ActionStatus walkTo(Agent& self, Simulation& sim, Vec2i goal, int range, int& ticks, int maxTicks) {
    const MoveResult mr = stepToward(self, sim, goal, range);
    if (mr == MoveResult::Blocked) return ActionStatus::Failed;
    if (mr == MoveResult::Arrived) return ActionStatus::Done;
    return ++ticks > maxTicks ? ActionStatus::Failed : ActionStatus::Running;
}

// =====================================================================================================
// Idle
// =====================================================================================================
class IdleAction final : public Action {
public:
    IdleAction(int ticks, std::string label) : left_(ticks), label_(std::move(label)) {}
    const char* kind() const override { return kinds::Idle; }
    std::string label() const override { return label_; }
    ActionStatus tick(Agent& self, Simulation& /*sim*/) override {
        return --left_ <= 0 ? finish(self, ActionStatus::Done) : ActionStatus::Running;
    }

private:
    int left_;
    std::string label_;
};

class IdleBehavior final : public Behavior {
public:
    const char* name() const override { return "idle"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        const char* label = sim.world().isNight()
                                ? pickLabel(sim, {"Staring at the stars", "Listening to the night"})
                                : pickLabel(sim, {"Sitting on a rock", "Watching the clouds", "Idling", "Whittling a stick"});
        out.push_back(Proposal{kIdleScore,
                               std::make_unique<IdleAction>(sim.rng().rangeInt(kIdleMinTicks, kIdleMaxTicks), label),
                               "nothing pressing"});
    }
};

// =====================================================================================================
// Wander
// =====================================================================================================
class WanderAction final : public Action {
public:
    WanderAction(Vec2i goal, std::string label) : goal_(goal), label_(std::move(label)) {}
    const char* kind() const override { return kinds::Wander; }
    std::string label() const override { return label_; }
    void onAbort(Agent& self, Simulation& /*sim*/) override { clearPath(self); }
    ActionStatus tick(Agent& self, Simulation& sim) override {
        self.needs.add(Need::Fun, kWanderFun);
        const ActionStatus st = walkTo(self, sim, goal_, 0, ticks_, kWanderMaxTicks);
        if (st == ActionStatus::Running) return st;
        return finish(self, st == ActionStatus::Failed && ticks_ > kWanderMaxTicks ? ActionStatus::Done : st);
    }

private:
    Vec2i goal_;
    std::string label_;
    int ticks_ = 0;
};

class WanderBehavior final : public Behavior {
public:
    const char* name() const override { return "wander"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        const auto goal = sim.world().randomWalkableNear(self.pos, kWanderRadius, sim.rng());
        if (!goal || *goal == self.pos) return;
        const float score = kWanderBase + kWanderCuriosity * self.trait(Trait::Curiosity);
        const char* label = isChild(self, sim) ? pickLabel(sim, {"Toddling about", "Poking around in the grass"})
                                               : pickLabel(sim, {"Wandering about", "Strolling", "Taking a little walk"});
        out.push_back(Proposal{score, std::make_unique<WanderAction>(*goal, label), "stretching my legs"});
    }
};

// =====================================================================================================
// Explore
// =====================================================================================================
class ExploreAction final : public Action {
public:
    ExploreAction(Vec2i goal, std::string label) : goal_(goal), label_(std::move(label)) {}
    const char* kind() const override { return kinds::Explore; }
    std::string label() const override { return label_; }
    void onAbort(Agent& self, Simulation& /*sim*/) override { clearPath(self); }
    ActionStatus tick(Agent& self, Simulation& sim) override {
        self.needs.add(Need::Fun, kExploreFun * (0.5f + self.trait(Trait::Curiosity)));
        const ActionStatus st = walkTo(self, sim, goal_, 1, ticks_, kExploreMaxTicks);
        if (st == ActionStatus::Done) {
            self.needs.add(Need::Fun, kExploreArriveFun);
            self.feel(Emotion::Joy, 0.05f);
        }
        if (st == ActionStatus::Running) return st;
        // Running out of time far from home still counts as having explored.
        return finish(self, ticks_ > kExploreMaxTicks ? ActionStatus::Done : st);
    }

private:
    Vec2i goal_;
    std::string label_;
    int ticks_ = 0;
};

class ExploreBehavior final : public Behavior {
public:
    const char* name() const override { return "explore"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (isChild(self, sim)) return;
        const World& w = sim.world();
        if (eveningness(w) > 0.f) return;  // not setting off at dusk
        float score = kExploreCuriosity * self.trait(Trait::Curiosity) *
                      (kExploreFunBase + self.needs.urgency(Need::Fun)) * comfort(self) * nightLeisureFactor(w) *
                      (1.f - kExploreColdDamp * coldSeason(w));
        std::string reason = "curious what lies beyond";
        std::string verb = "Exploring to the ";

        // Local food depleted: go look further afield.
        const FoodDrive fd = foodDrive(self, sim);
        if (fd.hunger > kExploreDepletedMinHunger) {
            const int r = searchRadius(self, sim);
            const bool none = !nearestResource(self, sim, ResourceKind::BerryBush, r) &&
                              !nearestResource(self, sim, ResourceKind::WildGrain, r) &&
                              !nearestResource(self, sim, ResourceKind::FishShoal, r);
            if (none) {
                score = kExploreDepleted + kExploreDepletedHunger * self.needs.urgency(Need::Food);
                reason = "no food left around here";
                verb = "Searching for food to the ";
            }
        }
        if (score <= 0.f) return;

        // Pick a direction; stay within a leash of home unless food has run out around here.
        const Structure* home = effectiveHome(self, sim);
        const bool leashed = home && verb.rfind("Exploring", 0) == 0;
        for (int tries = 0; tries < kExploreTries; ++tries) {
            const float ang = sim.rng().range(0.f, 6.2831853f);
            const int dist = sim.rng().rangeInt(kExploreMinDist, kExploreMaxDist);
            Vec2i goal{self.pos.x + int(std::lround(std::cos(ang) * float(dist))),
                       self.pos.y + int(std::lround(std::sin(ang) * float(dist)))};
            goal.x = std::clamp(goal.x, 0, w.width() - 1);
            goal.y = std::clamp(goal.y, 0, w.height() - 1);
            const auto walkable = w.nearestWalkable(goal, 6);
            if (!walkable || chebyshev(*walkable, self.pos) < kExploreMinDist / 2) continue;
            if (leashed && chebyshev(*walkable, home->pos) > kExploreLeash) continue;
            out.push_back(Proposal{score, std::make_unique<ExploreAction>(*walkable, verb + compass(*walkable - self.pos)), reason});
            return;
        }
    }
};

// =====================================================================================================
// GoHome
// =====================================================================================================
class GoHomeAction final : public Action {
public:
    GoHomeAction(StructureId home, std::string label) : home_(home), label_(std::move(label)) {}
    const char* kind() const override { return kinds::GoHome; }
    std::string label() const override { return label_; }
    void onAbort(Agent& self, Simulation& /*sim*/) override { clearPath(self); }
    ActionStatus tick(Agent& self, Simulation& sim) override {
        Structure* h = sim.world().structure(home_);
        if (!h || !h->complete) return finish(self, ActionStatus::Failed);
        const ActionStatus st = walkTo(self, sim, h->pos, 1, ticks_, tune::kMaxTravelTicks);
        if (st == ActionStatus::Running) return st;
        if (st == ActionStatus::Done && canUseStore(self, *h, sim)) {
            // Stow the day's surplus food in the hut.
            const int keep = std::max(kGoHomeKeepFoodMin, int(std::ceil(dailyFoodUnits(self) * kGoHomeKeepFoodDays)));
            const int n = std::min(self.inventory.get(Item::Food) - keep, h->stockFree());
            if (n > 0) {
                self.inventory.take(Item::Food, n);
                h->stock.add(Item::Food, n);
            }
        }
        return finish(self, st);
    }

private:
    StructureId home_;
    std::string label_;
    int ticks_ = 0;
};

class GoHomeBehavior final : public Behavior {
public:
    const char* name() const override { return "go_home"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (isChild(self, sim)) return;  // children follow their parents (social module) or Sleep walks them home
        const World& w = sim.world();
        const float eve = eveningness(w);
        if (eve <= 0.f && !w.isNight()) return;
        if (self.needs.get(Need::Rest) < kGoHomeMinRest) return;
        const Structure* home = effectiveHome(self, sim);
        if (!home) return;
        const int d = chebyshev(self.pos, home->pos);
        if (d < kGoHomeMinDist) return;
        const float score = kGoHomeBase + kGoHomeEvening * (w.isNight() ? 1.f : eve) +
                            kGoHomeFamily * self.trait(Trait::Sociability);
        out.push_back(Proposal{score, std::make_unique<GoHomeAction>(home->id, "Heading home for the evening"),
                               w.isNight() ? "it's dark; home is " + std::to_string(d) + " tiles away"
                                           : "the day is ending"});
    }
};

// =====================================================================================================
// Mope (grief / despair)
// =====================================================================================================
class MopeAction final : public Action {
public:
    explicit MopeAction(std::string label) : label_(std::move(label)) {}
    const char* kind() const override { return kinds::Mope; }
    std::string label() const override { return label_; }
    void onAbort(Agent& self, Simulation& /*sim*/) override { clearPath(self); }
    ActionStatus tick(Agent& self, Simulation& sim) override {
        ++ticks_;
        self.stress = clamp01(self.stress - kMopeStressRelief);
        self.emotions.add(Emotion::Sadness, -kMopeSadnessRelief);
        if (ticks_ % kMopeStepEvery == 0) {
            // Drift away from company, slowly.
            const auto near = sim.agentsNear(self.pos, 3, self.id);
            if (!near.empty()) {
                stepAway(self, sim, near.front()->pos);
            } else if (const auto p = sim.world().randomWalkableNear(self.pos, 1, sim.rng())) {
                if (chebyshev(*p, self.pos) == 1) stepToward(self, sim, *p, 0);
            }
        }
        return ticks_ >= kMopeTicks ? finish(self, ActionStatus::Done) : ActionStatus::Running;
    }

private:
    std::string label_;
    int ticks_ = 0;
};

class MopeBehavior final : public Behavior {
public:
    const char* name() const override { return "mope"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        const Tick now = sim.now();
        float grief = 0.f;
        AgentId lost = kNoAgent;
        for (const Memory& m : self.memories) {
            if (m.type != MemoryType::LovedOneDied) continue;
            const float ageDays = float(double(now - m.when) / double(cfg::kTicksPerDay));
            if (ageDays > kGriefDays) continue;
            const float g = m.intensity * (1.f - ageDays / kGriefDays);
            if (g > grief) {
                grief = g;
                lost = m.other;
            }
        }
        const float sad = self.emotions.get(Emotion::Sadness);
        const bool despair = self.inBreakdown() && self.breakType == BreakType::Despair;
        if (!despair && grief < kMopeMinGrief && sad < kMopeMinSadness) return;
        float score = kMopeBase + kMopeGrief * grief + kMopeSadness * sad;
        if (despair) score = std::max(score, kMopeDespair);
        std::string label, reason;
        if (lost != kNoAgent && grief >= kMopeMinGrief) {
            label = "Moping alone, missing " + sim.nameOf(lost);
            reason = "grieving " + sim.nameOf(lost);
        } else if (despair) {
            label = "Sitting alone in despair";
            reason = "everything feels hopeless";
        } else {
            label = "Brooding by myself";
            reason = "feeling low";
        }
        out.push_back(Proposal{score, std::make_unique<MopeAction>(label), reason});
    }
};

// =====================================================================================================
// Play
// =====================================================================================================
class PlayAction final : public Action {
public:
    explicit PlayAction(std::string label) : label_(std::move(label)) {}
    const char* kind() const override { return kinds::Play; }
    std::string label() const override { return label_; }
    void onAbort(Agent& self, Simulation& /*sim*/) override { clearPath(self); }
    ActionStatus tick(Agent& self, Simulation& sim) override {
        ++ticks_;
        self.needs.add(Need::Fun, kPlayFunPerTick);
        self.stress = clamp01(self.stress - kPlayStressRelief);
        if (ticks_ % kPlayHopEvery == 0) {
            if (const auto p = sim.world().randomWalkableNear(self.pos, 1, sim.rng())) {
                if (chebyshev(*p, self.pos) == 1) stepToward(self, sim, *p, 0);
            }
        }
        if (ticks_ >= kPlayTicks || self.needs.get(Need::Fun) >= 0.99f) {
            self.feel(Emotion::Joy, 0.08f);
            return finish(self, ActionStatus::Done);
        }
        return ActionStatus::Running;
    }

private:
    std::string label_;
    int ticks_ = 0;
};

class PlayBehavior final : public Behavior {
public:
    const char* name() const override { return "play"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        const World& w = sim.world();
        const float u = self.needs.urgency(Need::Fun);
        const bool child = isChild(self, sim);
        float score = child ? kPlayChildBase + kPlayChildFun * u
                            : kPlayAdultBase + kPlayAdultFun * u + kPlayAdultSoc * self.trait(Trait::Sociability);
        score *= comfort(self) * nightLeisureFactor(w);
        std::string label;
        if (child) {
            const Agent* mate = nullptr;
            for (const Agent* o : sim.agentsNear(self.pos, kPlaymateRadius, self.id)) {
                if (o->alive && isChild(*o, sim)) {
                    mate = o;
                    break;
                }
            }
            label = mate ? "Playing tag with " + mate->name
                         : std::string(pickLabel(sim, {"Chasing butterflies", "Building a mud castle", "Hopping between stones"}));
        } else {
            label = pickLabel(sim, {"Skipping stones", "Dancing a little jig", "Juggling pebbles"});
        }
        out.push_back(Proposal{score, std::make_unique<PlayAction>(label),
                               u > 0.4f ? "bored (fun " + std::to_string(int(self.needs.get(Need::Fun) * 100.f)) + "%)"
                                        : "just for fun"});
    }
};

// =====================================================================================================
// MakeArt
// =====================================================================================================
class MakeArtAction final : public Action {
public:
    explicit MakeArtAction(std::string label) : label_(std::move(label)) {}
    const char* kind() const override { return kinds::MakeArt; }
    std::string label() const override { return label_; }
    ActionStatus tick(Agent& self, Simulation& /*sim*/) override {
        ++ticks_;
        const float cre = self.trait(Trait::Creativity);
        self.needs.add(Need::Fun, kArtFunPerTick * (0.6f + cre));
        self.stress = clamp01(self.stress - kArtStressRelief);
        self.practice(Skill::Crafting, tune::kPracticeWork * 0.3f);
        if (ticks_ >= kArtTicks) {
            self.feel(Emotion::Pride, kArtPrideBase + kArtPrideCreativity * cre);
            self.feel(Emotion::Joy, 0.1f);
            return finish(self, ActionStatus::Done);
        }
        return ActionStatus::Running;
    }

private:
    std::string label_;
    int ticks_ = 0;
};

class MakeArtBehavior final : public Behavior {
public:
    const char* name() const override { return "make_art"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (isChild(self, sim)) return;
        const float cre = self.trait(Trait::Creativity);
        const float u = self.needs.urgency(Need::Fun);
        const float score = (kArtFun * u + kArtCreativity * cre + kArtStress * cre * self.stress) * comfort(self) *
                            nightLeisureFactor(sim.world());
        const char* label = pickLabel(sim, {"Carving a little wooden figure", "Painting the rocks with ochre",
                                            "Weaving a grass bracelet", "Composing a song", "Shaping a clay pot"});
        std::string reason = cre > 0.6f ? "I need to create" : "something to pass the time";
        if (self.stress > 0.5f) reason += ", and it calms me";
        out.push_back(Proposal{score, std::make_unique<MakeArtAction>(label), reason});
    }
};

}  // namespace

void registerLeisureBehaviors(BehaviorList& out) {
    out.push_back(std::make_unique<IdleBehavior>());
    out.push_back(std::make_unique<WanderBehavior>());
    out.push_back(std::make_unique<ExploreBehavior>());
    out.push_back(std::make_unique<GoHomeBehavior>());
    out.push_back(std::make_unique<MopeBehavior>());
    out.push_back(std::make_unique<PlayBehavior>());
    out.push_back(std::make_unique<MakeArtBehavior>());
}

}  // namespace heur::survival
