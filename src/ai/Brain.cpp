// Heuristica — the utility-AI decision maker.
//
// Every think cycle:
//   1. every registered Behavior proposes scored, concrete actions;
//   2. mental-break modifiers reshape the scores (a raging agent sees violence everywhere);
//   3. commitment hysteresis keeps agents from dithering between near-equal options;
//   4. bounded rationality: a softmax over the top options, with a temperature set by personality
//      (curious / unintelligent / stressed agents are noisier deciders);
//   5. the top options and the winning rationale are recorded for the inspector UI.
#include "ai/Brain.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "agent/Agent.h"
#include "ai/ActionHelpers.h"
#include "core/Config.h"
#include "sim/Simulation.h"

namespace heur {

namespace {
constexpr int kTopK = 3;                 // options considered by the softmax
constexpr float kTopWindow = 0.30f;      // only options within this of the best are candidates
constexpr int kInspectorOptions = 5;     // options recorded for the UI
constexpr int kThinkJitter = 4;          // de-synchronises agents' think ticks
constexpr int kIdleRethink = 4;          // ticks before an idle agent re-thinks

bool is(const char* kind, const char* k) { return std::strcmp(kind, k) == 0; }

bool isSurvivalCore(const char* k) {
    return is(k, kinds::Eat) || is(k, kinds::EatFromStorage) || is(k, kinds::Sleep) || is(k, kinds::Flee) ||
           is(k, kinds::WarmUp) || is(k, kinds::Heal);
}

bool isProsocial(const char* k) {
    return is(k, kinds::Share) || is(k, kinds::Gift) || is(k, kinds::Comfort) || is(k, kinds::HelpBuild) ||
           is(k, kinds::Teach) || is(k, kinds::Chat) || is(k, kinds::Court) || is(k, kinds::Mate) ||
           is(k, kinds::Trade) || is(k, kinds::Apologize) || is(k, kinds::Forgive);
}

bool isWork(const char* k) {
    return is(k, kinds::Build) || is(k, kinds::Farm) || is(k, kinds::Craft) || is(k, kinds::Chop) ||
           is(k, kinds::Mine) || is(k, kinds::Deposit) || is(k, kinds::Withdraw) || is(k, kinds::Forage) ||
           is(k, kinds::Fish) || is(k, kinds::GatherHerbs) || is(k, kinds::MakeArt);
}

// How a mental break distorts the value of each kind of action.
float breakdownMultiplier(BreakType b, const char* k) {
    switch (b) {
        case BreakType::Rampage:
            if (is(k, kinds::Attack) || is(k, kinds::Vandalize)) return 3.0f;
            if (is(k, kinds::Confront)) return 1.8f;
            if (isProsocial(k)) return 0.15f;
            if (isSurvivalCore(k)) return 0.8f;
            return 0.4f;
        case BreakType::Despair:
            if (is(k, kinds::Mope) || is(k, kinds::Mourn)) return 3.0f;
            if (is(k, kinds::Sleep)) return 1.4f;
            if (isSurvivalCore(k)) return 0.7f;
            if (isWork(k)) return 0.15f;
            return 0.3f;
        case BreakType::Panic:
            if (is(k, kinds::Flee)) return 3.0f;
            if (is(k, kinds::GoHome)) return 2.0f;
            if (is(k, kinds::Eat)) return 0.8f;
            return 0.3f;
        case BreakType::Kleptomania:
            if (is(k, kinds::Steal) || is(k, kinds::StealStorage)) return 3.0f;
            if (isSurvivalCore(k)) return 0.9f;
            if (is(k, kinds::Share) || is(k, kinds::Gift)) return 0.1f;
            return 0.45f;
        case BreakType::None:
        default: return 1.0f;
    }
}

// Decision noise: curious, less intelligent, emotionally unstable and stressed agents choose less optimally.
float decisionTemperature(const Agent& a) {
    return 0.025f + 0.07f * a.trait(Trait::Curiosity) + 0.05f * (1.f - a.trait(Trait::Intelligence)) +
           0.04f * (1.f - a.trait(Trait::Stability)) + 0.06f * a.stress;
}
}  // namespace

Brain::Brain() {
    registerSurvivalBehaviors(behaviors_);
    registerSocialBehaviors(behaviors_);
}

void Brain::think(Agent& self, Simulation& sim) {
    if (!self.alive) return;
    const Tick now = sim.now();
    const bool hasAction = static_cast<bool>(self.action);
    if (hasAction && !self.wantsRethink && now < self.nextThink) return;
    if (hasAction && !self.wantsRethink && !self.action->interruptible()) {
        self.nextThink = now + Tick(cfg::kThinkInterval);
        return;
    }

    // 1. Gather proposals.
    scratch_.clear();
    for (auto& b : behaviors_) b->propose(self, sim, scratch_);
    scratch_.erase(std::remove_if(scratch_.begin(), scratch_.end(),
                                  [](const Proposal& p) { return !p.action || !(p.score > 0.f); }),
                   scratch_.end());

    // 2 + 3. Breakdown distortion and commitment hysteresis.
    for (auto& p : scratch_) {
        const char* k = p.action->kind();
        if (self.inBreakdown()) p.score *= breakdownMultiplier(self.breakType, k);
        if (hasAction && self.actionKind == k) p.score += cfg::kCommitmentBonus;
    }

    self.wantsRethink = false;
    if (scratch_.empty()) {
        self.nextThink = now + Tick(kIdleRethink);
        if (!hasAction) self.thought = "Nothing seems worth doing.";
        return;
    }

    std::sort(scratch_.begin(), scratch_.end(),
              [](const Proposal& a, const Proposal& b) { return a.score > b.score; });

    // 5. Inspector record (before selection so the UI shows the real ranking).
    self.considered.clear();
    for (std::size_t i = 0; i < scratch_.size() && int(i) < kInspectorOptions; ++i) {
        self.considered.push_back({scratch_[i].action->label(), scratch_[i].score, scratch_[i].reason});
    }

    // 4. Bounded rationality: softmax over the top candidates.
    const float best = scratch_.front().score;
    const float temp = decisionTemperature(self);
    std::vector<float> weights;
    for (std::size_t i = 0; i < scratch_.size() && int(i) < kTopK; ++i) {
        if (best - scratch_[i].score > kTopWindow) break;
        weights.push_back(std::exp((scratch_[i].score - best) / temp));
    }
    int pick = sim.rng().weightedIndex(weights);
    if (pick < 0) pick = 0;
    Proposal& chosen = scratch_[std::size_t(pick)];

    self.nextThink = now + Tick(cfg::kThinkInterval + sim.rng().rangeInt(0, kThinkJitter));
    self.thought = chosen.reason;

    // Same kind as what we're doing: keep the existing action object so progress isn't lost.
    if (hasAction && self.actionKind == chosen.action->kind()) {
        self.actionScore = chosen.score;
        return;
    }

    if (hasAction) {
        self.action->onAbort(self, sim);
        clearPath(self);
    }
    self.action = std::move(chosen.action);
    self.actionKind = self.action->kind();
    self.actionLabel = self.action->label();
    self.actionScore = chosen.score;
    self.actionStarted = now;
    scratch_.clear();
}

void Brain::act(Agent& self, Simulation& sim) {
    if (!self.alive || !self.action) return;
    const ActionStatus st = self.action->tick(self, sim);
    if (!self.action) return;  // defensive: an action must never reset itself, but stay safe
    if (st == ActionStatus::Running) {
        self.actionLabel = self.action->label();
        return;
    }
    self.action.reset();
    self.actionKind.clear();
    self.actionLabel.clear();
    self.actionScore = 0.f;
    clearPath(self);
    self.nextThink = sim.now() + 1;  // decide something new next tick
}

void Brain::abort(Agent& self, Simulation& sim) {
    if (!self.action) return;
    self.action->onAbort(self, sim);
    self.action.reset();
    self.actionKind.clear();
    self.actionLabel.clear();
    self.actionScore = 0.f;
    clearPath(self);
}

}  // namespace heur
