// Heuristica — conversational & caring behaviours: Chat (with gossip), Comfort, Teach, Mourn, and
// children following their parents.
#include <algorithm>
#include <cmath>
#include <initializer_list>
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
// ---- Chat ----------------------------------------------------------------------
constexpr float kChatBase = 0.08f;            // sated agent of average sociability
constexpr float kChatSocialWeight = 0.50f;    // × urgency(Social)
constexpr float kChatFunWeight = 0.10f;       // × urgency(Fun)
constexpr float kChatSociabilityLo = 0.6f;    // multiplier = lo + span × Sociability
constexpr float kChatSociabilitySpan = 0.8f;
constexpr float kChatFriendBonus = 0.6f;      // × positive affinity
constexpr float kChatPartnerBonus = 0.4f;
constexpr float kChatFamilyBonus = 0.3f;
constexpr float kChatDislikePenalty = 0.9f;   // × negative affinity
constexpr float kChatGrievancePenalty = 0.7f; // × grievance
constexpr float kChatStrangerCuriosity = 0.3f;// × Curiosity, never met
constexpr float kChatReplyBonus = 0.15f;      // flat: they're already talking to me
constexpr float kChatRepeatPenalty = 0.5f;    // × if we chatted within kChatRepeatTicks
constexpr Tick kChatRepeatTicks = 60;
constexpr float kChatDistHalf = 6.f;
constexpr int kChatMinTicks = 6;
constexpr int kChatMaxTicks = 14;
constexpr int kChatGiveUpTicks = 40;          // couldn't catch up with them
constexpr int kChatTalkRange = 2;             // once talking, a step apart is still a conversation
constexpr float kChatSocialPerTick = 0.030f;  // × (0.6 + 0.8 Sociability) × (1 + 0.5 skill)
constexpr float kChatFunPerTick = 0.008f;
constexpr float kChatPracticePerTick = 0.08f; // Socializing (≈ 1.0 per conversation)
constexpr float kChatJoy = 0.06f;
constexpr float kChatAffinity = 0.03f;        // per finished conversation, × (1 + skill)
constexpr float kChatTrust = 0.01f;
constexpr float kChatMemory = 0.3f;

// ---- Comfort -------------------------------------------------------------------
constexpr float kComfortMinDistress = 0.35f;
constexpr Tick kComfortAnyoneTicks = 90;      // someone else comforted them very recently
constexpr float kComfortFriendAffinity = 0.25f;  // non-family must be at least this liked
constexpr float kComfortBase = 0.10f;
constexpr float kComfortNeedWeight = 0.55f;   // × distress × (0.4 + Empathy)
constexpr float kComfortFamilyMult = 1.3f;
constexpr float kComfortMoralWeight = 0.6f;   // × judge(Comfort).utility
constexpr float kComfortDistHalf = 8.f;
constexpr Tick kComfortRepeatTicks = 120;     // don't re-comfort the same person within this
constexpr int kComfortTicks = 6;
constexpr int kComfortGiveUpTicks = 45;
constexpr float kComfortStressPerTick = 0.015f;  // × (1 + Socializing)
constexpr float kComfortSadnessPerTick = 0.025f; // × (1 + Socializing)
constexpr float kComfortFearPerTick = 0.015f;
constexpr float kComfortSocialPerTick = 0.015f;
constexpr float kComfortPractice = 1.0f;         // Socializing per session
constexpr float kComfortAffinity = 0.03f;        // comforter -> comforted

// ---- Teach ---------------------------------------------------------------------
constexpr float kTeachMinSkill = 0.35f;
constexpr float kTeachMinGap = 0.15f;
constexpr float kTeachBase = 0.06f;
constexpr float kTeachGapWeight = 0.35f;      // × gap
constexpr float kTeachChildMult = 1.4f;       // teaching my own child
constexpr float kTeachFunWeight = 0.08f;      // × urgency(Fun)
constexpr float kTeachMinAffinity = 0.15f;    // non-family learner must be liked
constexpr Tick kTeachRepeatTicks = Tick(cfg::kTicksPerDay / 2);  // learner taught by me recently
constexpr float kTeachDistHalf = 6.f;
constexpr int kTeachMinTicks = 8;
constexpr int kTeachMaxTicks = 12;
constexpr int kTeachGiveUpTicks = 40;
constexpr float kTeachLessonPractice = 4.0f;  // learner practice per lesson × (teacher skill / 0.5)
constexpr float kTeachTeacherPractice = 0.5f; // Socializing for the teacher
constexpr float kTeachFunPerTick = 0.004f;
constexpr float kTeachSocialPerTick = 0.004f;
constexpr float kTeachPride = 0.10f;
constexpr float kTeachAffinity = 0.03f;       // learner -> teacher
constexpr float kTeachTrust = 0.02f;

// ---- Mourn ---------------------------------------------------------------------
constexpr Tick kMournWindow = Tick(cfg::kTicksPerDay) * 2;
constexpr float kMournMinIntensity = 0.25f;
constexpr float kMournMinSadness = 0.15f;
constexpr float kMournBase = 0.10f;
constexpr float kMournSadnessWeight = 0.55f;  // × Sadness × memory intensity
constexpr float kMournStabilityDamp = 0.4f;   // × (1.2 - this × Stability)
constexpr int kMournMinTicks = 20;
constexpr int kMournMaxTicks = 40;
constexpr float kMournSadnessRelief = 0.005f; // per tick
constexpr float kMournStressRelief = 0.002f;  // per tick: grief worked through
constexpr float kMournMemoryFade = 0.985f;    // memory intensity × this per tick of mourning
constexpr float kMournTogetherSocial = 0.004f;// per tick, family mourning together

// ---- FollowParent --------------------------------------------------------------
constexpr int kFollowFarTiles = 6;
constexpr int kFollowArriveTiles = 3;
constexpr float kFollowBase = 0.20f;
constexpr float kFollowDistWeight = 0.30f;    // × (dist - far) / 10, capped at 1
constexpr float kFollowFearWeight = 0.40f;    // × Fear
constexpr float kFollowNightBonus = 0.15f;
constexpr float kFollowMax = 0.80f;
constexpr int kFollowSearch = 60;             // children don't track parents beyond this
constexpr int kFollowGiveUpTicks = 60;
constexpr float kFollowArriveSocial = 0.05f;

float sociabilityMult(const Agent& a) { return kChatSociabilityLo + kChatSociabilitySpan * a.trait(Trait::Sociability); }

// ===========================================================================
// Chat
// ===========================================================================
class ChatAction final : public Action {
public:
    ChatAction(AgentId target, std::string targetName, int duration)
        : target_(target), targetName_(std::move(targetName)), duration_(duration) {}
    const char* kind() const override { return kinds::Chat; }
    std::string label() const override { return "Chatting with " + targetName_; }
    AgentId target() const { return target_; }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        ++elapsed_;
        Agent* t = living(sim, target_);
        if (!t) return finish(self, ActionStatus::Failed);
        if (isAsleep(*t) || attackTargetOf(*t) == self.id)
            return finish(self, talked_ > 0 ? ActionStatus::Done : ActionStatus::Failed);
        const int dist = chebyshev(self.pos, t->pos);
        if (dist > (talked_ > 0 ? kChatTalkRange : 1)) {
            if (elapsed_ > kChatGiveUpTicks) return finish(self, talked_ > 0 ? ActionStatus::Done : ActionStatus::Failed);
            if (stepToward(self, sim, t->pos, 1) == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        talk(self, *t, sim);
        if (talked_ >= duration_) {
            wrapUp(self, *t, sim);
            return finish(self, ActionStatus::Done);
        }
        return ActionStatus::Running;
    }

private:
    // If they are chatting with me too, their action handles their side.
    bool mutual(const Agent& self, const Agent& t) const { return chatTargetOf(t) == self.id; }

    void talk(Agent& self, Agent& t, Simulation& sim) {
        const Tick now = sim.now();
        const bool both = !mutual(self, t);
        if (talked_ == 0) {
            self.rel(t.id).lastSeen = now;
            t.rel(self.id).lastSeen = now;  // light "we're talking" flag; their action is untouched
        }
        ++talked_;
        const float skill = self.skills.get(Skill::Socializing);
        self.needs.add(Need::Social, kChatSocialPerTick * sociabilityMult(self) * (1.f + 0.5f * skill));
        self.needs.add(Need::Fun, kChatFunPerTick);
        self.practice(Skill::Socializing, kChatPracticePerTick);
        if (both) {
            t.needs.add(Need::Social, kChatSocialPerTick * sociabilityMult(t) * (1.f + 0.5f * skill));
            t.needs.add(Need::Fun, kChatFunPerTick);
        }
        // Midway the talk turns to other people: reputations spread.
        if (!gossiped_ && talked_ >= duration_ / 2) {
            gossiped_ = true;
            gossip(sim, self, t);
            if (both) gossip(sim, t, self);
        }
    }

    void wrapUp(Agent& self, Agent& t, Simulation& sim) {
        const Tick now = sim.now();
        const bool both = !mutual(self, t);
        const float skill = self.skills.get(Skill::Socializing);
        nudgeRelation(self, t.id, kChatAffinity * (1.f + skill), kChatTrust, now);
        self.remember(MemoryType::Chatted, t.id, now, kChatMemory);
        self.feel(Emotion::Joy, kChatJoy);
        if (both) {
            nudgeRelation(t, self.id, kChatAffinity * (1.f + skill), kChatTrust, now);
            t.remember(MemoryType::Chatted, self.id, now, kChatMemory);
            t.feel(Emotion::Joy, kChatJoy);
        }
    }

    AgentId target_;
    std::string targetName_;
    int duration_;
    int elapsed_ = 0;
    int talked_ = 0;
    bool gossiped_ = false;
};

class ChatBehavior final : public Behavior {
public:
    const char* name() const override { return "chat"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        const Tick now = sim.now();
        const float uSocial = self.needs.urgency(Need::Social);
        const float base = (kChatBase + kChatSocialWeight * uSocial + kChatFunWeight * self.needs.urgency(Need::Fun)) *
                           sociabilityMult(self) * needsDamp(self);
        const Agent* best = nullptr;
        float bestScore = 0.f;
        bool bestReply = false;
        for (Agent* o : sim.agentsNear(self.pos, cfg::kSightRadius, self.id)) {
            if (!o || !o->alive || o->id == self.id || isAsleep(*o) || isFighting(*o) || isRampaging(*o)) continue;
            const Relationship* r = self.findRel(o->id);
            const float aff = r ? r->affinity : 0.f;
            float pref = 1.f + kChatFriendBonus * std::max(0.f, aff) - kChatDislikePenalty * std::max(0.f, -aff) -
                         kChatGrievancePenalty * (r ? r->grievance : 0.f);
            if (self.partner == o->id) pref += kChatPartnerBonus;
            else if (self.isFamily(*o)) pref += kChatFamilyBonus;
            if (!r) pref += kChatStrangerCuriosity * self.trait(Trait::Curiosity);
            if (recentMemory(self, MemoryType::Chatted, o->id, now, kChatRepeatTicks)) pref *= kChatRepeatPenalty;
            if (pref <= 0.f) continue;
            const bool reply = chatTargetOf(*o) == self.id;
            const float score = base * pref * distanceFactor(chebyshev(self.pos, o->pos), kChatDistHalf) +
                                (reply ? kChatReplyBonus : 0.f);
            if (score > bestScore) {
                bestScore = score;
                best = o;
                bestReply = reply;
            }
        }
        if (!best) return;
        std::string why;
        if (bestReply) why = best->name + " is talking to me";
        else if (uSocial > 0.5f) why = "lonely (social " + pct(self.needs.get(Need::Social)) + ")";
        else why = "feel like talking";
        const float aff = self.affinityFor(best->id);
        if (self.partner == best->id) why += "; " + best->name + " is my partner";
        else if (self.isFamily(*best)) why += "; " + best->name + " is family";
        else if (aff > 0.4f) why += "; " + best->name + " is a friend";
        else if (!self.findRel(best->id)) why += "; haven't met " + best->name;
        pushProposal(out, bestScore,
                     std::make_unique<ChatAction>(best->id, best->name, sim.rng().rangeInt(kChatMinTicks, kChatMaxTicks)),
                     why);
    }
};

// ===========================================================================
// Comfort
// ===========================================================================
float distressOf(const Agent& a) {
    float d = std::max({a.emotions.get(Emotion::Sadness), 0.8f * a.stress, 0.7f * a.emotions.get(Emotion::Fear)});
    if (a.inBreakdown() && a.breakType == BreakType::Despair) d = std::max(d, 0.9f);
    return clamp01(d);
}

class ComfortAction final : public Action {
public:
    ComfortAction(AgentId target, std::string targetName) : target_(target), targetName_(std::move(targetName)) {}
    const char* kind() const override { return kinds::Comfort; }
    std::string label() const override { return "Comforting " + targetName_; }
    AgentId target() const { return target_; }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        ++elapsed_;
        Agent* t = living(sim, target_);
        if (!t || attackTargetOf(*t) == self.id) return finish(self, ActionStatus::Failed);
        const int dist = chebyshev(self.pos, t->pos);
        if (dist > (ticks_ > 0 ? 2 : 1)) {
            if (elapsed_ > kComfortGiveUpTicks) return finish(self, ActionStatus::Failed);
            if (stepToward(self, sim, t->pos, 1) == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        if (ticks_ == 0) startDistress_ = distressOf(*t);
        ++ticks_;
        const float skill = 1.f + self.skills.get(Skill::Socializing);
        t->stress = clamp01(t->stress - kComfortStressPerTick * skill);
        t->emotions.add(Emotion::Sadness, -kComfortSadnessPerTick * skill);
        t->emotions.add(Emotion::Fear, -kComfortFearPerTick * skill);
        t->needs.add(Need::Social, kComfortSocialPerTick);
        self.needs.add(Need::Social, kComfortSocialPerTick);
        if (ticks_ < kComfortTicks) return ActionStatus::Running;

        // Session complete: the deed happened.
        const Tick now = sim.now();
        const DeedContext ctx = makeDeed(Deed::Comfort, t, 0.f, 0.f, startDistress_,
                                         countWitnesses(sim, self.pos, self.id, t->id));
        const Verdict v = judge(self, ctx, sim);
        self.feel(Emotion::Guilt, v.guilt);
        self.feel(Emotion::Pride, v.pride);
        self.practice(Skill::Socializing, kComfortPractice);
        nudgeRelation(self, t->id, kComfortAffinity, 0.f, now);
        if (t->inBreakdown() && t->breakType == BreakType::Despair)
            sim.log(EventKind::Social, self.name + " consoles " + t->name + " in " + hisHer(*t) + " despair", self.id,
                    t->id);
        broadcastDeed(sim, self, ctx, self.pos);  // t remembers Comforted, warms to me
        return finish(self, ActionStatus::Done);
    }

private:
    AgentId target_;
    std::string targetName_;
    int elapsed_ = 0;
    int ticks_ = 0;
    float startDistress_ = 0.f;
};

bool beingComforted(Simulation& sim, const Agent& who, AgentId except) {
    for (const Agent* a : std::as_const(sim).agentsNear(who.pos, cfg::kSightRadius, who.id)) {
        if (!a || !a->alive || a->id == except) continue;
        if (const auto* c = dynamic_cast<const ComfortAction*>(a->action.get()); c && c->target() == who.id) return true;
    }
    return false;
}

class ComfortBehavior final : public Behavior {
public:
    const char* name() const override { return "comfort"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (!self.isAdult(sim.now())) return;
        const Tick now = sim.now();
        const float empathy = self.trait(Trait::Empathy);
        const Agent* best = nullptr;
        float bestScore = 0.f;
        float bestDistress = 0.f;
        Verdict bestV;
        for (Agent* o : sim.agentsNear(self.pos, cfg::kSightRadius, self.id)) {
            if (!o || !o->alive || o->id == self.id || isAsleep(*o) || isFighting(*o) || isRampaging(*o)) continue;
            const bool family = self.isFamily(*o);
            if (!family && self.affinityFor(o->id) < kComfortFriendAffinity) continue;
            const float distress = distressOf(*o);
            if (distress < kComfortMinDistress) continue;
            if (recentMemory(*o, MemoryType::Comforted, self.id, now, kComfortRepeatTicks)) continue;
            if (recentMemory(*o, MemoryType::Comforted, kNoAgent, now, kComfortAnyoneTicks)) continue;
            if (beingComforted(sim, *o, self.id)) continue;  // someone is already at their side
            const DeedContext ctx = makeDeed(Deed::Comfort, o, 0.f, 0.f, distress,
                                             countWitnesses(sim, o->pos, self.id, o->id));
            const Verdict v = judge(self, ctx, sim);
            const float score = (kComfortBase + kComfortNeedWeight * distress * (0.4f + empathy)) *
                                    (family ? kComfortFamilyMult : 1.f) * needsDamp(self) *
                                    distanceFactor(chebyshev(self.pos, o->pos), kComfortDistHalf) +
                                kComfortMoralWeight * v.utility;
            if (score > bestScore) {
                bestScore = score;
                best = o;
                bestDistress = distress;
                bestV = v;
            }
        }
        if (!best) return;
        const char* state = (best->inBreakdown() && best->breakType == BreakType::Despair) ? "in despair"
                            : best->emotions.get(Emotion::Sadness) >= 0.8f * best->stress ? "sad"
                                                                                         : "stressed";
        pushProposal(out, bestScore, std::make_unique<ComfortAction>(best->id, best->name),
                     withWhy(best->name + " is " + state + " (" + pct(bestDistress) + ")", bestV));
    }
};

// ===========================================================================
// Teach
// ===========================================================================
class TeachAction final : public Action {
public:
    TeachAction(AgentId learner, std::string learnerName, Skill skill, int duration)
        : learner_(learner), learnerName_(std::move(learnerName)), skill_(skill), duration_(duration) {}
    const char* kind() const override { return kinds::Teach; }
    std::string label() const override { return "Teaching " + learnerName_ + " " + skillName(skill_); }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        ++elapsed_;
        Agent* l = living(sim, learner_);
        if (!l || isAsleep(*l) || isFighting(*l)) return finish(self, ActionStatus::Failed);
        const int dist = chebyshev(self.pos, l->pos);
        if (dist > (ticks_ > 0 ? 2 : 1)) {
            if (elapsed_ > kTeachGiveUpTicks) return finish(self, ActionStatus::Failed);
            if (stepToward(self, sim, l->pos, 1) == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        ++ticks_;
        self.needs.add(Need::Fun, kTeachFunPerTick);
        self.needs.add(Need::Social, kTeachSocialPerTick);
        l->needs.add(Need::Fun, kTeachFunPerTick);
        l->needs.add(Need::Social, kTeachSocialPerTick);
        if (ticks_ < duration_) return ActionStatus::Running;

        // Lesson complete. Transfer scales with how much better the teacher is.
        const Tick now = sim.now();
        const float mine = self.skills.get(skill_);
        const float theirs = l->skills.get(skill_);
        const float quality = clamp01((mine - theirs) / 0.5f) * (0.6f + 0.4f * self.skills.get(Skill::Socializing));
        l->practice(skill_, kTeachLessonPractice * (mine / 0.5f) * (0.5f + quality));
        self.practice(Skill::Socializing, kTeachTeacherPractice);
        l->remember(MemoryType::Taught, self.id, now, 0.4f);
        nudgeRelation(*l, self.id, kTeachAffinity, kTeachTrust, now);
        nudgeRelation(self, l->id, kTeachAffinity * 0.5f, 0.f, now);
        self.feel(Emotion::Pride, kTeachPride);
        self.feel(Emotion::Joy, kTeachPride * 0.5f);
        return finish(self, ActionStatus::Done);
    }

private:
    AgentId learner_;
    std::string learnerName_;
    Skill skill_;
    int duration_;
    int elapsed_ = 0;
    int ticks_ = 0;
};

class TeachBehavior final : public Behavior {
public:
    const char* name() const override { return "teach"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        const Tick now = sim.now();
        if (!self.isAdult(now)) return;
        float topSkill = 0.f;
        for (float s : self.skills.v) topSkill = std::max(topSkill, s);
        if (topSkill < kTeachMinSkill) return;
        const float temperament = 0.5f + 0.25f * (self.trait(Trait::Empathy) + self.trait(Trait::Sociability));
        const Agent* best = nullptr;
        Skill bestSkill = Skill::Foraging;
        float bestScore = 0.f;
        float bestGap = 0.f;
        for (Agent* o : sim.agentsNear(self.pos, cfg::kSightRadius, self.id)) {
            if (!o || !o->alive || o->id == self.id || isAsleep(*o) || isFighting(*o) || o->inBreakdown()) continue;
            const bool myChild = isParentOf(self, *o) && isChildAgent(*o, sim);
            if (!self.isFamily(*o) && self.affinityFor(o->id) < kTeachMinAffinity) continue;
            if (recentMemory(*o, MemoryType::Taught, self.id, now, kTeachRepeatTicks)) continue;
            for (int i = 0; i < kSkillCount; ++i) {
                const Skill s = Skill(i);
                const float mine = self.skills.get(s);
                const float gap = mine - o->skills.get(s);
                if (mine < kTeachMinSkill || gap < kTeachMinGap) continue;
                const float score = (kTeachBase + kTeachGapWeight * gap + kTeachFunWeight * self.needs.urgency(Need::Fun)) *
                                    temperament * (myChild ? kTeachChildMult : 1.f) * needsDamp(self) *
                                    distanceFactor(chebyshev(self.pos, o->pos), kTeachDistHalf);
                if (score > bestScore) {
                    bestScore = score;
                    best = o;
                    bestSkill = s;
                    bestGap = gap;
                }
            }
        }
        if (!best) return;
        const bool myChild = isParentOf(self, *best);
        pushProposal(out, bestScore,
                     std::make_unique<TeachAction>(best->id, best->name, bestSkill,
                                                   sim.rng().rangeInt(kTeachMinTicks, kTeachMaxTicks)),
                     std::string("I'm good at ") + skillName(bestSkill) + " and " + (myChild ? "my child " : "") +
                         best->name + " could learn (" + pct(bestGap) + " behind)");
    }
};

// ===========================================================================
// Mourn
// ===========================================================================
class MournAction final : public Action {
public:
    MournAction(AgentId deceased, std::string deceasedName, Tick memoryWhen, int duration)
        : deceased_(deceased), deceasedName_(std::move(deceasedName)), memoryWhen_(memoryWhen), duration_(duration) {}
    const char* kind() const override { return kinds::Mourn; }
    std::string label() const override { return "Mourning " + deceasedName_; }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        ++ticks_;
        // Sit with the grief: sadness is slowly worked through into relief.
        self.emotions.add(Emotion::Sadness, -kMournSadnessRelief);
        self.stress = clamp01(self.stress - kMournStressRelief);
        for (Memory& m : self.memories) {
            if (m.type == MemoryType::LovedOneDied && m.other == deceased_ && m.when == memoryWhen_) {
                m.intensity *= kMournMemoryFade;
                break;
            }
        }
        // Family grieving the same person nearby: shared grief is company.
        for (Agent* o : sim.agentsNear(self.pos, 2, self.id)) {
            if (o && o->alive && o->actionKind == kinds::Mourn && self.isFamily(*o)) {
                self.needs.add(Need::Social, kMournTogetherSocial);
                break;
            }
        }
        return ticks_ >= duration_ ? finish(self, ActionStatus::Done) : ActionStatus::Running;
    }

private:
    AgentId deceased_;
    std::string deceasedName_;
    Tick memoryWhen_;
    int duration_;
    int ticks_ = 0;
};

class MournBehavior final : public Behavior {
public:
    const char* name() const override { return "mourn"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        const Tick now = sim.now();
        const float sadness = self.emotions.get(Emotion::Sadness);
        if (sadness < kMournMinSadness) return;
        const Memory* grief = nullptr;
        for (auto it = self.memories.rbegin(); it != self.memories.rend(); ++it) {
            if (it->when + kMournWindow < now) break;
            if (it->type == MemoryType::LovedOneDied && it->intensity >= kMournMinIntensity &&
                (!grief || it->intensity > grief->intensity))
                grief = &*it;
        }
        if (!grief) return;
        const float score = (kMournBase + kMournSadnessWeight * sadness) * grief->intensity *
                            (1.2f - kMournStabilityDamp * self.trait(Trait::Stability));
        const std::string who = sim.nameOf(grief->other);
        pushProposal(out, score,
                     std::make_unique<MournAction>(grief->other, who, grief->when,
                                                   sim.rng().rangeInt(kMournMinTicks, kMournMaxTicks)),
                     "grieving for " + who + " (sadness " + pct(sadness) + ")");
    }
};

// ===========================================================================
// FollowParent (children)
// ===========================================================================
class FollowParentAction final : public Action {
public:
    FollowParentAction(AgentId parent, std::string label) : parent_(parent), label_(std::move(label)) {}
    const char* kind() const override { return kinds::FollowParent; }
    std::string label() const override { return label_; }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        ++elapsed_;
        Agent* p = living(sim, parent_);
        if (!p) return finish(self, ActionStatus::Failed);
        if (chebyshev(self.pos, p->pos) <= kFollowArriveTiles) {
            self.needs.add(Need::Social, kFollowArriveSocial);
            self.emotions.add(Emotion::Fear, -0.05f);
            return finish(self, ActionStatus::Done);
        }
        if (elapsed_ > kFollowGiveUpTicks) return finish(self, ActionStatus::Failed);
        if (stepToward(self, sim, p->pos, kFollowArriveTiles - 1) == MoveResult::Blocked)
            return finish(self, ActionStatus::Failed);
        return ActionStatus::Running;
    }

private:
    AgentId parent_;
    std::string label_;
    int elapsed_ = 0;
};

class FollowParentBehavior final : public Behavior {
public:
    const char* name() const override { return "follow_parent"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (!isChildAgent(self, sim)) return;
        const Agent* best = nullptr;
        int bestDist = 0;
        for (AgentId pid : {self.mother, self.father}) {
            const Agent* p = living(std::as_const(sim), pid);
            if (!p) continue;
            const int d = chebyshev(self.pos, p->pos);
            if (d > kFollowSearch) continue;
            if (!best || d < bestDist) {
                best = p;
                bestDist = d;
            }
        }
        if (!best || bestDist <= kFollowFarTiles) return;  // the nearest parent is close enough
        const float fear = self.emotions.get(Emotion::Fear);
        const bool night = sim.world().isNight();
        const float score = std::min(kFollowMax, kFollowBase +
                                                     kFollowDistWeight * std::min(1.f, float(bestDist - kFollowFarTiles) / 10.f) +
                                                     kFollowFearWeight * fear + (night ? kFollowNightBonus : 0.f));
        const char* role = best->id == self.mother ? "mother" : "father";
        std::string why = std::string("my ") + role + " " + best->name + " is " + std::to_string(bestDist) + " tiles away";
        if (fear > 0.3f) why += " and I'm scared";
        else if (night) why += " and it's dark";
        pushProposal(out, score,
                     std::make_unique<FollowParentAction>(best->id, std::string("Following ") + role + " " + best->name),
                     why);
    }
};

}  // namespace

// ===========================================================================
// Cross-file query & registration
// ===========================================================================
AgentId chatTargetOf(const Agent& a) {
    if (const auto* c = dynamic_cast<const ChatAction*>(a.action.get())) return c->target();
    return kNoAgent;
}

void addConversationBehaviors(BehaviorList& out) {
    out.push_back(std::make_unique<ChatBehavior>());
    out.push_back(std::make_unique<ComfortBehavior>());
    out.push_back(std::make_unique<TeachBehavior>());
    out.push_back(std::make_unique<MournBehavior>());
    out.push_back(std::make_unique<FollowParentBehavior>());
}

}  // namespace heur::social
