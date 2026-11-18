// Heuristica — romance: Court (build attraction, propose partnership; affairs are judged as Adultery)
// and Mate (with a mutual partner; may conceive — parents "plan" heuristically around food & kids).
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
// ---- Court ---------------------------------------------------------------------------
constexpr float kCourtMinAttraction = 0.30f;
constexpr Tick kRejectCooldown = Tick(cfg::kTicksPerDay) * 2;  // after a rejection by them
constexpr float kCourtNeedBase = 0.25f;       // desire = (base + w × urgency(Intimacy)) × (lo + Libido) × attraction
constexpr float kCourtNeedWeight = 0.75f;
constexpr float kCourtLibidoLo = 0.4f;
constexpr float kCourtWeight = 0.9f;
constexpr float kCourtElderMult = 0.6f;
constexpr float kCourtDistHalf = 8.f;
constexpr float kAffairUpgrade = 0.5f;        // with a partner: × clamp(att(new) - att(partner) + this)
constexpr int kCourtMinTicks = 6;
constexpr int kCourtMaxTicks = 10;
constexpr int kCourtGiveUpTicks = 50;
constexpr float kCourtAttractionPerTick = 0.015f;  // target's attraction to me, × (1 + 0.5 Socializing)
constexpr float kCourtSocialPerTick = 0.02f;
constexpr float kCourtFunPerTick = 0.01f;
constexpr float kCourtPracticePerTick = 0.1f;
// Outcome
constexpr float kBondAffinity = 0.15f;
constexpr float kBondTrust = 0.08f;
constexpr float kBondAttraction = 0.15f;
constexpr float kBondJoy = 0.35f;
constexpr float kRejectedSadness = 0.2f;      // × (0.5 + Libido)
constexpr float kRejectedAffinity = -0.03f;
constexpr float kRejectedAttractionKeep = 0.8f;
constexpr float kLeftBehindAffinity = -0.30f; // jilted partner of the one who accepted -> their ex
constexpr float kLeftBehindGrievance = 0.30f;

// ---- Mate ----------------------------------------------------------------------------
constexpr int kMateSearch = 15;
constexpr Tick kMateCooldown = Tick(cfg::kTicksPerDay / 2);
constexpr float kMateMinNeed = 0.05f;
constexpr float kMateBase = 0.10f;            // desire = (base + w × urgency) × (0.5 + Libido) × (lo + span × partner urgency)
constexpr float kMateNeedWeight = 0.70f;
constexpr float kMatePartnerLo = 0.6f;
constexpr float kMatePartnerSpan = 0.4f;
constexpr float kMateWeight = 0.9f;
constexpr float kMateComingBonus = 0.15f;     // partner already heading to me
constexpr float kMateDistHalf = 8.f;
constexpr int kMateTicks = 6;
constexpr int kMateGiveUpTicks = 60;
constexpr float kMateJoy = 0.25f;
constexpr float kMateStressRelief = 0.05f;
constexpr float kMateAffinity = 0.03f;
// Conception: p = base × (lo + span × avg Libido) × food × kids × season
constexpr float kConceiveBase = 0.40f;
constexpr float kConceiveLibidoLo = 0.7f;
constexpr float kConceiveLibidoSpan = 0.6f;
constexpr float kConceiveFoodLo = 0.35f;      // food factor = lo + span × clamp(days / kConceiveFoodDays) - hunger penalty
constexpr float kConceiveFoodSpan = 0.65f;
constexpr float kConceiveFoodDays = 3.f;
constexpr float kConceiveHungerPenalty = 0.4f;
constexpr float kConceiveKidPenalty = 0.35f;  // ÷ (1 + this × young children)
constexpr float kConceiveWinter = 0.6f;
constexpr float kConceiveAutumn = 0.8f;       // a baby born into winter is a risk
constexpr int kCrowdRadius = 12;              // "how many mouths around here?"
constexpr float kCrowdFree = 18.f;            // neighbours tolerated before crowding lowers fertility
constexpr float kCrowdScale = 8.f;            // ÷ (1 + (neighbours - free) / scale)

// ===========================================================================
// Court
// ===========================================================================
class CourtAction final : public Action {
public:
    CourtAction(AgentId target, std::string targetName, int duration)
        : target_(target), targetName_(std::move(targetName)), duration_(duration) {}
    const char* kind() const override { return kinds::Court; }
    std::string label() const override { return "Courting " + targetName_; }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        ++elapsed_;
        Agent* t = living(sim, target_);
        if (!t || isAsleep(*t) || isFighting(*t) || self.partner == t->id) return finish(self, ActionStatus::Failed);
        if (chebyshev(self.pos, t->pos) > (ticks_ > 0 ? 2 : 1)) {
            if (elapsed_ > kCourtGiveUpTicks) return finish(self, ActionStatus::Failed);
            if (stepToward(self, sim, t->pos, 1) == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        ++ticks_;
        Relationship& theirs = t->rel(self.id);
        theirs.attraction = clamp01(theirs.attraction + kCourtAttractionPerTick *
                                                           (1.f + 0.5f * self.skills.get(Skill::Socializing)));
        theirs.lastSeen = sim.now();
        self.needs.add(Need::Social, kCourtSocialPerTick);
        self.needs.add(Need::Fun, kCourtFunPerTick);
        t->needs.add(Need::Social, kCourtSocialPerTick);
        self.practice(Skill::Socializing, kCourtPracticePerTick);
        if (ticks_ < duration_) return ActionStatus::Running;

        if (acceptsAdvance(*t, self, sim)) bond(self, *t, sim);
        else rejected(self, *t, sim);
        return finish(self, ActionStatus::Done);
    }

private:
    void bond(Agent& self, Agent& t, Simulation& sim) {
        const Tick now = sim.now();
        Agent* myEx = (self.partner != t.id) ? living(sim, self.partner) : nullptr;
        Agent* theirEx = (t.partner != self.id) ? living(sim, t.partner) : nullptr;

        if (myEx || theirEx) {
            // An affair out in the open. Broadcast BEFORE relinking: appraise() reads the partner fields to
            // find who is betrayed. Betrayed partners always learn of it (they're being left).
            const DeedContext ctx = makeDeed(Deed::Adultery, &t, 0.5f * self.needs.urgency(Need::Intimacy), 0.6f, 0.f,
                                             countWitnesses(sim, self.pos, self.id, t.id));
            const Verdict v = judge(self, ctx, sim);
            self.feel(Emotion::Guilt, v.guilt);
            self.feel(Emotion::Pride, v.pride);
            broadcastDeed(sim, self, ctx, self.pos);
            if (theirEx) {
                // ... and they resent the partner who walked out on them, too.
                Relationship& r = theirEx->rel(t.id);
                r.affinity = clampf(r.affinity + kLeftBehindAffinity, -1.f, 1.f);
                r.grievance = clamp01(r.grievance + kLeftBehindGrievance);
                t.feel(Emotion::Guilt, 0.2f * t.trait(Trait::Integrity));
            }
        }
        if (myEx && myEx->partner == self.id) myEx->partner = kNoAgent;
        if (theirEx && theirEx->partner == t.id) theirEx->partner = kNoAgent;
        self.partner = t.id;
        t.partner = self.id;
        for (auto [a, b] : {std::pair<Agent*, Agent*>{&self, &t}, std::pair<Agent*, Agent*>{&t, &self}}) {
            Relationship& r = a->rel(b->id);
            r.affinity = clampf(r.affinity + kBondAffinity, -1.f, 1.f);
            r.trust = clamp01(r.trust + kBondTrust);
            r.attraction = clamp01(r.attraction + kBondAttraction);
            ++r.interactions;
            r.lastSeen = now;
            a->feel(Emotion::Joy, kBondJoy);
        }
        std::string text = self.name + " and " + t.name + " became partners";
        if (myEx) text += "; " + self.name + " left " + myEx->name;
        if (theirEx) text += "; " + t.name + " left " + theirEx->name;
        sim.log(EventKind::Romance, text, self.id, t.id);
    }

    void rejected(Agent& self, Agent& t, Simulation& sim) {
        self.remember(MemoryType::Rejected, t.id, sim.now(), 0.6f);
        self.feel(Emotion::Sadness, kRejectedSadness * (0.5f + self.trait(Trait::Libido)));
        Relationship& r = self.rel(t.id);
        r.affinity = clampf(r.affinity + kRejectedAffinity, -1.f, 1.f);
        r.attraction *= kRejectedAttractionKeep;
    }

    AgentId target_;
    std::string targetName_;
    int duration_;
    int elapsed_ = 0;
    int ticks_ = 0;
};

class CourtBehavior final : public Behavior {
public:
    const char* name() const override { return "court"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        const Tick now = sim.now();
        if (!self.isAdult(now)) return;
        const Agent* myPartner = living(std::as_const(sim), self.partner);
        float need = (kCourtNeedBase + kCourtNeedWeight * self.needs.urgency(Need::Intimacy)) *
                     (kCourtLibidoLo + self.trait(Trait::Libido)) * kCourtWeight;
        if (self.stage(now) == LifeStage::Elder) need *= kCourtElderMult;
        need *= needsDamp(self);
        const float partnerAtt = myPartner ? attraction(self, *myPartner, sim) : 0.f;

        const Agent* best = nullptr;
        float bestScore = 0.f;
        float bestAtt = 0.f;
        bool bestAffair = false;
        Verdict bestV;
        for (Agent* o : sim.agentsNear(self.pos, cfg::kSightRadius, self.id)) {
            if (!o || !o->alive || o->id == self.id || o->id == self.partner) continue;
            if (isAsleep(*o) || isFighting(*o) || o->inBreakdown() || !o->isAdult(now) || bloodKin(self, *o)) continue;
            if (recentMemory(self, MemoryType::Rejected, o->id, now, kRejectCooldown)) continue;
            const float att = attraction(self, *o, sim);
            if (att < kCourtMinAttraction) continue;
            float desire = need * att;
            const bool affair = myPartner || living(std::as_const(sim), o->partner);
            float moral = 0.f;
            Verdict v;
            if (affair) {
                if (myPartner) desire *= clamp01(att - partnerAtt + kAffairUpgrade);  // only for someone better
                const DeedContext ctx = makeDeed(Deed::Adultery, o, 0.5f * self.needs.urgency(Need::Intimacy) + 0.3f * att,
                                                 0.6f, 0.f, countWitnesses(sim, o->pos, self.id, o->id));
                v = judge(self, ctx, sim);
                moral = v.utility;
            }
            const float score = desire * distanceFactor(chebyshev(self.pos, o->pos), kCourtDistHalf) + moral;
            if (score > bestScore) {
                bestScore = score;
                best = o;
                bestAtt = att;
                bestAffair = affair;
                bestV = v;
            }
        }
        if (!best) return;
        std::string why = "drawn to " + best->name + " (attraction " + pct(bestAtt) + ")";
        if (self.needs.urgency(Need::Intimacy) > 0.4f) why += ", longing for intimacy";
        if (bestAffair) {
            const Agent* theirP = living(std::as_const(sim), best->partner);
            why += myPartner ? " despite being with " + myPartner->name
                             : (theirP ? " though " + std::string(heShe(*best)) + "'s with " + theirP->name : std::string());
            why = withWhy(why, bestV);
        }
        pushProposal(out, bestScore,
                     std::make_unique<CourtAction>(best->id, best->name, sim.rng().rangeInt(kCourtMinTicks, kCourtMaxTicks)),
                     why);
    }
};

// ===========================================================================
// Mate
// ===========================================================================
class MateAction final : public Action {
public:
    MateAction(AgentId partner, std::string partnerName) : partner_(partner), partnerName_(std::move(partnerName)) {}
    const char* kind() const override { return kinds::Mate; }
    std::string label() const override { return "Spending intimate time with " + partnerName_; }
    AgentId partner() const { return partner_; }
    void consume() { consumed_ = true; }  // the partner's action completed our time together

    ActionStatus tick(Agent& self, Simulation& sim) override {
        if (consumed_) return finish(self, ActionStatus::Done);
        ++elapsed_;
        Agent* p = living(sim, partner_);
        if (!p || p->partner != self.id || self.partner != p->id || isAsleep(*p) || isFighting(*p))
            return finish(self, ActionStatus::Failed);
        if (chebyshev(self.pos, p->pos) > 1) {
            if (elapsed_ > kMateGiveUpTicks) return finish(self, ActionStatus::Failed);
            if (stepToward(self, sim, p->pos, 1) == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        if (++together_ < kMateTicks) return ActionStatus::Running;
        complete(self, *p, sim);
        return finish(self, ActionStatus::Done);
    }

private:
    static int youngChildren(const Agent& mother, const Simulation& sim) {
        int n = 0;
        for (AgentId c : mother.children)
            if (const Agent* k = living(sim, c); k && isChildAgent(*k, sim)) ++n;
        return n;
    }

    void complete(Agent& self, Agent& p, Simulation& sim) {
        const Tick now = sim.now();
        if (auto* theirs = dynamic_cast<MateAction*>(p.action.get()); theirs && theirs->partner() == self.id)
            theirs->consume();  // one encounter, counted once
        for (Agent* a : {&self, &p}) {
            Agent* b = a == &self ? &p : &self;
            a->needs.set(Need::Intimacy, 1.f);
            a->feel(Emotion::Joy, kMateJoy);
            a->stress = clamp01(a->stress - kMateStressRelief);
            nudgeRelation(*a, b->id, kMateAffinity, 0.01f, now);
            a->remember(MemoryType::Mated, b->id, now, 0.5f);
        }
        ++sim.stats().matings;

        Agent* mother = self.sex == Sex::Female ? &self : &p;
        Agent* father = self.sex == Sex::Male ? &self : &p;
        if (mother == father || mother->sex != Sex::Female || father->sex != Sex::Male) return;
        if (mother->pregnant || !mother->isFertile(now) || !father->isFertile(now)) return;
        // Heuristic family planning: food security, children already at foot, season.
        const float libido = 0.5f * (mother->trait(Trait::Libido) + father->trait(Trait::Libido));
        const float daily = dailyFoodUnits(*mother) + dailyFoodUnits(*father);
        const float days = float(foodOwned(*mother, sim) + foodOwned(*father, sim)) / std::max(0.5f, daily);
        const float hunger = std::max(mother->needs.urgency(Need::Food), father->needs.urgency(Need::Food));
        const float food = clampf(kConceiveFoodLo + kConceiveFoodSpan * clamp01(days / kConceiveFoodDays) -
                                      kConceiveHungerPenalty * hunger,
                                  0.1f, 1.f);
        const float kids = 1.f / (1.f + kConceiveKidPenalty * float(youngChildren(*mother, sim)));
        const Season se = sim.world().season();
        const float season = se == Season::Winter ? kConceiveWinter : (se == Season::Autumn ? kConceiveAutumn : 1.f);
        // Crowding: many mouths nearby means thinner foraging — a density-dependent brake on growth.
        const float neighbours = float(sim.agentsNear(mother->pos, kCrowdRadius, mother->id).size());
        const float crowd = 1.f / (1.f + std::max(0.f, neighbours - kCrowdFree) / kCrowdScale);
        const float pConceive =
            kConceiveBase * (kConceiveLibidoLo + kConceiveLibidoSpan * libido) * food * kids * season * crowd;
        if (sim.rng().chance(pConceive)) sim.conceive(*mother, *father);  // conceive() logs the news
    }

    AgentId partner_;
    std::string partnerName_;
    int elapsed_ = 0;
    int together_ = 0;
    bool consumed_ = false;
};

AgentId mateTargetOf(const Agent& a) {
    if (const auto* m = dynamic_cast<const MateAction*>(a.action.get())) return m->partner();
    return kNoAgent;
}

class MateBehavior final : public Behavior {
public:
    const char* name() const override { return "mate"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        const Tick now = sim.now();
        if (!self.isAdult(now)) return;
        const Agent* p = living(std::as_const(sim), self.partner);
        if (!p || p->partner != self.id || !p->isAdult(now) || isAsleep(*p) || isFighting(*p) || p->inBreakdown()) return;
        const int dist = chebyshev(self.pos, p->pos);
        if (dist > kMateSearch) return;
        if (recentMemory(self, MemoryType::Mated, p->id, now, kMateCooldown)) return;
        const float need = self.needs.urgency(Need::Intimacy);
        if (need < kMateMinNeed) return;
        const float theirNeed = p->needs.urgency(Need::Intimacy);
        float desire = (kMateBase + kMateNeedWeight * need) * (0.5f + self.trait(Trait::Libido)) *
                       (kMatePartnerLo + kMatePartnerSpan * theirNeed) * kMateWeight;
        desire *= needsDamp(self);
        const bool coming = mateTargetOf(*p) == self.id;
        const float score = desire * distanceFactor(dist, kMateDistHalf) + (coming ? kMateComingBonus : 0.f);
        pushProposal(out, score, std::make_unique<MateAction>(p->id, p->name),
                     coming ? p->name + " wants to be with me"
                            : "longing for " + p->name + " (intimacy " + pct(self.needs.get(Need::Intimacy)) + ")");
    }
};

}  // namespace

void addRomanceBehaviors(BehaviorList& out) {
    out.push_back(std::make_unique<CourtBehavior>());
    out.push_back(std::make_unique<MateBehavior>());
}

}  // namespace heur::social
