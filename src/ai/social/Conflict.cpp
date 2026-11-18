// Heuristica — conflict & justice: Attack (revenge, rage, robbery, defending kin, self-defence), Flee,
// Confront (punish / demand restitution, may escalate), Apologize (make amends) and Forgive.
#include <algorithm>
#include <cmath>
#include <cstdint>
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
// ---- Attack: motives ------------------------------------------------------------------
constexpr float kMinHealthToStart = 0.35f;     // don't pick fights when hurt (unless raging)
constexpr float kSelfDefBase = 0.55f;          // + Aggression/Bravery/power terms
constexpr float kSelfDefAggression = 0.30f;
constexpr float kSelfDefBravery = 0.30f;
constexpr float kSelfDefPower = 0.40f;         // × (my power - theirs)
constexpr float kSelfDefConscience = 0.3f;     // × judge utility
constexpr float kDefendBase = 0.45f;
constexpr float kDefendBravery = 0.35f;
constexpr float kDefendEmpathy = 0.25f;
constexpr float kDefendFamily = 0.20f;
constexpr float kDefendMyChild = 0.20f;
constexpr float kDefendPower = 0.30f;
constexpr float kDefendConscience = 0.4f;
constexpr float kRevengeMinGrievance = 0.25f;
constexpr float kRevengeWeight = 1.2f;         // × grievance × Vengefulness × (0.5 + Aggression)
constexpr float kRevengeAnger = 0.3f;          // + × Anger × grievance
constexpr Tick kRevengeCooldown = Tick(cfg::kTicksPerDay / 2);  // "I already got mine"
constexpr float kRevengeRecentMult = 0.3f;     // revenge motive × this if I hit them within the cooldown
constexpr float kDefendCounterMult = 0.4f;     // × when the attacker is itself defending someone
constexpr float kDefendPunishMult = 0.6f;      // × when the attacker is punishing a wrongdoer
constexpr float kDefendHelpedMult = 0.5f;      // × when someone else is already fighting the attacker
constexpr float kRampageBase = 0.30f;          // Brain multiplies by 3
constexpr float kRampageHate = 0.20f;          // × max(0, -affinity)
constexpr float kRampageAdjacent = 0.10f;
constexpr float kRampageConscience = 0.3f;
constexpr Tick kRampageFatigueTicks = 60;      // a rager who just beat someone moves on ...
constexpr float kRampageFatigueMult = 0.4f;    // ... (motive × this for that target)
constexpr float kRobMinHunger = 0.70f;
constexpr int kRobMinVictimFood = 3;
constexpr float kRobPowerEdge = 1.1f;          // my power must exceed theirs × this
constexpr float kRobWeight = 0.6f;
constexpr float kWeakPenalty = 1.5f;           // m × clamp(1 - (their - my power)(1 - Bravery) × this)
constexpr float kAttackDistHalf = 6.f;
constexpr float kAttackHarm = 0.45f;           // expected harm for judge()
// ---- Attack: execution ---------------------------------------------------------------
constexpr int kSwingInterval = 2;              // ticks between swings
constexpr int kAttackMaxTicks = 60;
constexpr int kMaxChaseTicks = 25;
constexpr float kQuitHealth = 0.25f;           // self-preservation
constexpr float kRampageQuitHealth = 0.12f;
constexpr int kRampageMaxHits = 4;
constexpr float kRevengeRelief = 0.6f;         // grievance × this per landed hit ...
constexpr float kRevengeReliefFlat = 0.05f;    // ... minus this
constexpr float kRevengeSated = 0.10f;
constexpr float kAngerGone = 0.05f;
constexpr Tick kAttackLogQuiet = 40;           // don't re-log a fight continuing against the same target
// The killing blow is its own decision: below kLethalHealth the attacker judges Deed::Kill and spares the
// victim unless drive + conscience × verdict > 0. Drive = base(motive) + Aggression/Anger terms.
constexpr float kLethalHealth = 0.15f;
constexpr float kKillDriveSelfDefense = 0.40f;
constexpr float kKillDriveDefend = 0.20f;
constexpr float kKillDriveRampage = 0.05f;
constexpr float kKillDriveRevenge = 0.0f;
constexpr float kKillDriveOther = -0.1f;       // robbery, punishment
constexpr float kKillAggression = 0.20f;
constexpr float kKillAnger = 0.10f;
constexpr float kKillRampageConscience = 0.8f; // even in a rage, killing gives pause
constexpr int kRobHits = 2;
constexpr float kRobHealth = 0.6f;
constexpr int kRobMaxFood = 4;

// ---- Flee ----------------------------------------------------------------------------
constexpr Tick kRecentAttackTicks = 30;
constexpr int kHostileApproachTiles = 5;
constexpr float kHostileAffinity = -0.5f;
constexpr float kHostileAggression = 0.6f;
constexpr float kHostileAnger = 0.4f;
constexpr int kRampagerTiles = 6;
constexpr float kDangerAttacked = 1.0f;        // being hit right now
constexpr float kDangerRecent = 0.8f;          // attacked recently, attacker nearby
constexpr float kDangerRampager = 0.6f;
constexpr float kDangerHostile = 0.45f;
constexpr float kDangerPanic = 0.4f;           // Panic: anyone nearby (Brain ×3)
constexpr float kFleeBase = 0.9f;              // × danger
constexpr float kFleeCowardice = 0.5f;         // × danger × (1 - Bravery)
constexpr float kFleePower = 0.3f;             // × (their power - mine)
constexpr float kFleeHurt = 0.3f;              // when my health < kFleeHurtBelow
constexpr float kFleeHurtBelow = 0.4f;
constexpr float kFleeChild = 0.3f;
constexpr int kSafeDistance = 11;
constexpr int kFleeMaxTicks = 40;
constexpr int kPanicMaxTicks = 20;
constexpr int kCorneredTicks = 3;

// ---- Confront ----------------------------------------------------------------------------
constexpr Tick kWitnessWindow = Tick(cfg::kTicksPerDay) * 2;
constexpr Tick kRecentPunishTicks = Tick(cfg::kTicksPerDay) * 2;  // one confrontation per wrongdoer per 2 days
constexpr float kConfrontPersonal = 0.30f;     // grievance × (this + 0.4 Vengefulness)
constexpr float kConfrontPersonalV = 0.40f;
constexpr float kConfrontCivicI = 0.20f;       // witnessed: intensity × (I×a + V×b + pe×c) × care
constexpr float kConfrontCivicV = 0.15f;
constexpr float kConfrontCivicP = 0.15f;
constexpr float kConfrontKinVictim = 1.5f;
constexpr float kConfrontMinMotive = 0.10f;
constexpr float kConfrontMoralWeight = 0.7f;
constexpr float kConfrontDistHalf = 8.f;
constexpr float kConfrontHarm = 0.20f;         // judge: a demand, not a beating
constexpr int kArgueTicks = 3;
constexpr int kConfrontGiveUpTicks = 50;
constexpr int kConfrontMaxTicks = 90;
// The wrongdoer's decision: comply = logistic(score, mid, steep)
constexpr float kComplyGuilt = 0.45f;
constexpr float kComplyFear = 0.35f;
constexpr float kComplyMeek = 0.25f;           // × (1 - Aggression)
constexpr float kComplyIntegrity = 0.20f;
constexpr float kComplyNorm = 0.15f;           // × punishmentExpectation
constexpr float kComplyGreed = 0.30f;
constexpr float kComplyHunger = 0.25f;
constexpr float kComplyFeud = 0.30f;           // × their grievance toward me
constexpr float kComplyMid = 0.50f;
constexpr float kComplySteep = 8.f;
constexpr float kRestitutionBase = 2.f;        // food-equivalents demanded + grievance × scale
constexpr float kRestitutionScale = 4.f;
constexpr float kComplyGrievanceKeep = 0.3f;
constexpr float kForgiveBelow = 0.12f;         // after restitution, a grievance this small is forgiven
// Escalation = logistic(...)
constexpr float kEscAggression = 0.4f;
constexpr float kEscAnger = 0.4f;
constexpr float kEscRevenge = 0.4f;            // × grievance × Vengefulness
constexpr float kEscPower = 0.3f;
constexpr float kEscNorm = 0.3f;               // × violenceTolerance
constexpr float kEscEmpathy = 0.3f;
constexpr float kEscMid = 0.55f;
constexpr float kEscSteep = 8.f;
constexpr int kPunishHits = 3;
constexpr float kPunishFightHarm = 0.4f;
constexpr float kPunishShameHarm = 0.1f;
constexpr float kPunishGrievanceKeep = 0.4f;   // after a beating, justice feels served

// ---- Apologize ----------------------------------------------------------------------------
constexpr float kApologizeMinGuilt = 0.25f;
constexpr Tick kApologizeWindow = Tick(cfg::kTicksPerDay) * 3;
constexpr float kApologizeMinMemory = 0.2f;
constexpr int kApologizeSearch = 24;
constexpr float kApologizeBase = 0.10f;
constexpr float kApologizeGuilt = 0.60f;       // × Guilt
constexpr float kApologizeDistHalf = 12.f;
constexpr int kApologizeTalkTicks = 2;
constexpr int kApologizeGiveUpTicks = 80;
constexpr int kApologizeMaxFood = 3;
constexpr float kApologizeGuiltKeep = 0.35f;
constexpr float kApologizeMemoryKeep = 0.25f;  // the deed weighs less once amends are made
// Victim's forgiveness = logistic(...)
constexpr float kForgiveEmpathy = 0.45f;
constexpr float kForgiveMercy = 0.35f;         // × (1 - Vengefulness)
constexpr float kForgiveRestitution = 0.25f;   // × min(1, value / 3)
constexpr float kForgiveFamily = 0.25f;
constexpr float kForgiveTrust = 0.15f;
constexpr float kForgiveGrievance = 0.45f;
constexpr float kForgiveAnger = 0.20f;
constexpr float kForgiveMid = 0.35f;
constexpr float kForgiveSteep = 8.f;

// ---- Forgive -----------------------------------------------------------------------------
constexpr float kForgiveMinGrievance = 0.15f;
constexpr Tick kAmendsWindow = Tick(cfg::kTicksPerDay) * 2;
constexpr float kForgiveFamilyMult = 1.5f;
constexpr float kForgiveAmendsMult = 1.5f;
constexpr float kForgiveStressWeight = 0.10f;  // grudges are stressful
constexpr float kForgiveAngerCost = 0.5f;
constexpr float kForgiveWeight = 0.8f;
constexpr float kForgiveMoralWeight = 0.5f;
constexpr float kForgiveDistHalf = 8.f;
constexpr int kForgiveGiveUpTicks = 40;
constexpr float kForgiveGrievanceKeep = 0.1f;
constexpr float kForgiveAffinity = 0.10f;
constexpr float kForgiveTrustGain = 0.05f;
constexpr float kForgiveStressRelief = 0.05f;

// ===========================================================================
// Helpers
// ===========================================================================
enum class Motive : std::uint8_t { SelfDefense, Defend, Revenge, Rampage, Robbery };

const char* motiveText(Motive m) {
    switch (m) {
        case Motive::SelfDefense: return "self-defence";
        case Motive::Defend: return "defending kin";
        case Motive::Revenge: return "revenge";
        case Motive::Rampage: return "in a rage";
        case Motive::Robbery: return "robbery";
    }
    return "?";
}

float grievanceToward(const Agent& self, AgentId other) {
    const Relationship* r = self.findRel(other);
    return r ? r->grievance : 0.f;
}

// Would `self` deliver a probably-lethal blow to `t`? (Deed::Kill judged on the spot.)
bool willKill(const Agent& self, const Agent& t, Motive m, bool punishment, const Simulation& sim) {
    const DeedContext ctx = makeDeed(Deed::Kill, &t, 0.f, 1.f, 0.f, countWitnesses(sim, self.pos, self.id, t.id));
    const Verdict v = judge(self, ctx, sim);
    float drive = kKillDriveOther;
    if (!punishment) {
        switch (m) {
            case Motive::SelfDefense: drive = kKillDriveSelfDefense; break;
            case Motive::Defend: drive = kKillDriveDefend; break;
            case Motive::Rampage: drive = kKillDriveRampage; break;
            case Motive::Revenge: drive = kKillDriveRevenge; break;
            default: break;
        }
    }
    // (Revenge is already weighed inside judge(): "he wronged me first".)
    drive += kKillAggression * self.trait(Trait::Aggression) + kKillAnger * self.emotions.get(Emotion::Anger);
    const float conscience = isRampaging(self) ? kKillRampageConscience : 1.f;
    return drive + conscience * v.utility > 0.f;
}

// Shared by Attack and Confront when a blow kills.
void onKill(Agent& killer, Agent& victim, Simulation& sim, const std::string& why) {
    ++killer.stats.kills;
    const DeedContext ctx = makeDeed(Deed::Kill, &victim, 0.f, 1.f, 0.f, countWitnesses(sim, killer.pos, killer.id, victim.id));
    const Verdict v = judge(killer, ctx, sim);
    killer.feel(Emotion::Guilt, v.guilt);
    killer.feel(Emotion::Pride, v.pride);
    killer.remember(MemoryType::AttackedSomeone, victim.id, sim.now(), 1.f);
    sim.log(EventKind::Fight, killer.name + " killed " + victim.name + " (" + why + ")", killer.id, victim.id);
    broadcastDeed(sim, killer, ctx, killer.pos);
}

// ===========================================================================
// Attack
// ===========================================================================
class AttackAction final : public Action {
public:
    AttackAction(AgentId target, std::string targetName, Motive motive, AgentId protectee)
        : target_(target), targetName_(std::move(targetName)), motive_(motive), protectee_(protectee) {}
    const char* kind() const override { return kinds::Attack; }
    std::string label() const override {
        switch (motive_) {
            case Motive::SelfDefense: return "Fighting off " + targetName_;
            case Motive::Defend: return "Defending kin from " + targetName_;
            default: return "Attacking " + targetName_ + " (" + motiveText(motive_) + ")";
        }
    }
    AgentId target() const { return target_; }
    Motive motive() const { return motive_; }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        ++elapsed_;
        Agent* t = living(sim, target_);
        if (!t) return finish(self, started_ ? ActionStatus::Done : ActionStatus::Failed);
        const float quit = motive_ == Motive::Rampage ? kRampageQuitHealth : kQuitHealth;
        if (self.health < quit || elapsed_ > kAttackMaxTicks) return finish(self, ActionStatus::Failed);
        // Protective fights end as soon as the threat stops (even before the first blow).
        const bool protective = motive_ == Motive::Defend || motive_ == Motive::SelfDefense;
        if ((started_ || protective) && satisfied(self, *t))
            return finish(self, started_ ? ActionStatus::Done : ActionStatus::Failed);

        if (chebyshev(self.pos, t->pos) > 1) {
            if (chebyshev(self.pos, t->pos) > cfg::kSightRadius) return finish(self, started_ ? ActionStatus::Done : ActionStatus::Failed);
            if (started_ && ++chase_ > kMaxChaseTicks) return finish(self, ActionStatus::Done);  // they got away
            if (stepToward(self, sim, t->pos, 1) == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        if (cooldown_ > 0) {
            --cooldown_;
            return ActionStatus::Running;
        }
        cooldown_ = kSwingInterval - 1;
        if (!started_) begin(self, *t, sim);
        if (t->health <= kLethalHealth && !willKill(self, *t, motive_, false, sim))
            return finish(self, ActionStatus::Done);  // they're down; I spare them
        const StrikeResult r = strike(self, *t, sim);
        if (r != StrikeResult::Missed) {
            ++hits_;
            if (motive_ == Motive::Revenge) {
                Relationship& rel = self.rel(t->id);
                rel.grievance = std::max(0.f, rel.grievance * kRevengeRelief - kRevengeReliefFlat);
            }
        }
        if (r == StrikeResult::Killed) {
            onKill(self, *t, sim, motiveText(motive_));
            return finish(self, ActionStatus::Done);
        }
        if (motive_ == Motive::Robbery && hits_ >= kRobHits && (t->health < kRobHealth || hits_ > kRobHits)) {
            rob(self, *t, sim);
            return finish(self, ActionStatus::Done);
        }
        return ActionStatus::Running;
    }

private:
    bool satisfied(const Agent& self, const Agent& t) const {
        switch (motive_) {
            case Motive::Revenge:
                return grievanceToward(self, t.id) < kRevengeSated || self.emotions.get(Emotion::Anger) < kAngerGone;
            case Motive::Rampage: return !self.inBreakdown() || hits_ >= kRampageMaxHits;
            case Motive::Defend: return attackTargetOf(t) != protectee_;
            case Motive::SelfDefense: return attackTargetOf(t) != self.id;
            case Motive::Robbery: return false;
        }
        return true;
    }

    void begin(Agent& self, Agent& t, Simulation& sim) {
        started_ = true;
        const Tick now = sim.now();
        const float selfGain = motive_ == Motive::Robbery ? self.needs.urgency(Need::Food)
                               : (motive_ == Motive::SelfDefense || motive_ == Motive::Defend) ? 0.5f
                               : motive_ == Motive::Revenge ? 0.3f * grievanceToward(self, t.id)
                                                            : 0.2f;
        const DeedContext ctx = makeDeed(Deed::Attack, &t, selfGain, kAttackHarm, 0.f,
                                         countWitnesses(sim, self.pos, self.id, t.id));
        const Verdict v = judge(self, ctx, sim);
        self.feel(Emotion::Guilt, v.guilt);
        self.feel(Emotion::Pride, v.pride);
        ++self.stats.attacks;
        ++sim.stats().fights;
        const bool continuing = recentMemory(self, MemoryType::AttackedSomeone, t.id, now, kAttackLogQuiet) != nullptr;
        self.remember(MemoryType::AttackedSomeone, t.id, now, 0.7f);
        if (!continuing) {
            std::string text = self.name + " attacks " + t.name + " (" + motiveText(motive_);
            if (motive_ == Motive::Defend) text += " " + sim.nameOf(protectee_);
            text += ")";
            sim.log(EventKind::Fight, text, self.id, t.id);
        }
        broadcastDeed(sim, self, ctx, self.pos);  // t remembers Attacked, rethinks (fight back / flee)
    }

    void rob(Agent& self, Agent& t, Simulation& sim) {
        const int amount = std::min({t.inventory.get(Item::Food), std::max(2, foodToSate(self)), kRobMaxFood});
        const int taken = giveItems(t, self, Item::Food, amount);
        if (taken <= 0) return;
        const DeedContext ctx = makeDeed(Deed::StealFromAgent, &t, self.needs.urgency(Need::Food), 0.6f, 0.f,
                                         countWitnesses(sim, self.pos, self.id, t.id));
        const Verdict v = judge(self, ctx, sim);
        self.feel(Emotion::Guilt, v.guilt);
        self.remember(MemoryType::Stole, t.id, sim.now(), 0.8f);
        ++self.stats.thefts;
        ++t.stats.timesStolenFrom;
        ++sim.stats().thefts;
        sim.log(EventKind::Theft, self.name + " beat " + t.name + " and took " + describeItems(Item::Food, taken), self.id,
                t.id);
        broadcastDeed(sim, self, ctx, self.pos);
    }

    AgentId target_;
    std::string targetName_;
    Motive motive_;
    AgentId protectee_;
    int elapsed_ = 0;
    int chase_ = 0;
    int hits_ = 0;
    int cooldown_ = 0;
    bool started_ = false;
};

// Is anyone other than `ex1`/`ex2` already fighting or confronting `who`?
bool othersFighting(Simulation& sim, const Agent& who, AgentId ex1, AgentId ex2);

class AttackBehavior final : public Behavior {
public:
    const char* name() const override { return "attack"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        const Tick now = sim.now();
        if (!self.isAdult(now)) return;
        const bool rampage = isRampaging(self);
        if (!rampage && self.health < kMinHealthToStart) return;
        const float A = self.trait(Trait::Aggression);
        const float B = self.trait(Trait::Bravery);
        const float E = self.trait(Trait::Empathy);
        const float V = self.trait(Trait::Vengefulness);
        const float anger = self.emotions.get(Emotion::Anger);
        const float hunger = self.needs.urgency(Need::Food);
        const float myP = combatPower(self, sim);

        const Agent* best = nullptr;
        float bestScore = 0.f;
        Motive bestMotive = Motive::Revenge;
        AgentId bestProtectee = kNoAgent;
        Verdict bestV;
        for (Agent* o : sim.agentsNear(self.pos, cfg::kSightRadius, self.id)) {
            if (!o || !o->alive || o->id == self.id || isChildAgent(*o, sim)) continue;  // adults don't fight kids
            const float theirP = combatPower(*o, sim);
            const float edge = myP - theirP;
            const int dist = chebyshev(self.pos, o->pos);
            float m = 0.f;
            Motive mot = Motive::Revenge;
            AgentId protectee = kNoAgent;
            auto offer = [&](float v, Motive mm, AgentId p) {
                if (v > m) {
                    m = v;
                    mot = mm;
                    protectee = p;
                }
            };
            const AgentId victimOfO = attackTargetOf(*o);
            if (victimOfO == self.id) offer(kSelfDefBase + kSelfDefAggression * A + kSelfDefBravery * B + kSelfDefPower * edge,
                                            Motive::SelfDefense, kNoAgent);
            if (victimOfO != kNoAgent && victimOfO != self.id) {
                if (const Agent* p = living(std::as_const(sim), victimOfO); p && cares(self, *p)) {
                    // Damp brawl cascades: counter-defenders, punishers, and already-helped victims.
                    float damp = 1.f;
                    if (const auto* aa = dynamic_cast<const AttackAction*>(o->action.get())) {
                        if (aa->motive() == Motive::Defend || aa->motive() == Motive::SelfDefense) damp *= kDefendCounterMult;
                    } else {
                        damp *= kDefendPunishMult;  // Confront's fight phase
                    }
                    if (othersFighting(sim, *o, self.id, p->id)) damp *= kDefendHelpedMult;
                    offer(damp * (kDefendBase + kDefendBravery * B + kDefendEmpathy * E +
                                  (self.isFamily(*p) ? kDefendFamily : 0.f) + (isParentOf(self, *p) ? kDefendMyChild : 0.f) +
                                  kDefendPower * edge),
                          Motive::Defend, p->id);
                }
            }
            const float g = grievanceToward(self, o->id);
            if (g >= kRevengeMinGrievance) {
                float rev = kRevengeWeight * g * V * (0.5f + A) + kRevengeAnger * anger * g;
                if (recentMemory(self, MemoryType::AttackedSomeone, o->id, now, kRevengeCooldown)) rev *= kRevengeRecentMult;
                offer(rev, Motive::Revenge, kNoAgent);
            }
            if (rampage) {
                float rm = kRampageBase + kRampageHate * std::max(0.f, -self.affinityFor(o->id)) +
                           (dist <= 1 ? kRampageAdjacent : 0.f);
                if (recentMemory(self, MemoryType::AttackedSomeone, o->id, now, kRampageFatigueTicks)) rm *= kRampageFatigueMult;
                offer(rm, Motive::Rampage, kNoAgent);
            }
            if (hunger >= kRobMinHunger && self.inventory.get(Item::Food) == 0 &&
                o->inventory.get(Item::Food) >= kRobMinVictimFood && myP > theirP * kRobPowerEdge)
                offer(hunger * kRobWeight * (0.5f + edge) * (1.f - 0.5f * E), Motive::Robbery, kNoAgent);
            if (m <= 0.f) continue;
            if (edge < 0.f) m *= clampf(1.f + edge * (1.f - B) * kWeakPenalty, 0.2f, 1.f);

            const float selfGain = mot == Motive::Robbery ? hunger
                                   : (mot == Motive::SelfDefense || mot == Motive::Defend) ? 0.5f
                                   : mot == Motive::Revenge ? 0.3f * g
                                                            : 0.2f;
            const DeedContext ctx = makeDeed(Deed::Attack, o, selfGain, kAttackHarm, 0.f,
                                             countWitnesses(sim, o->pos, self.id, o->id));
            const Verdict v = judge(self, ctx, sim);
            const float conscience = mot == Motive::SelfDefense ? kSelfDefConscience
                                     : mot == Motive::Defend    ? kDefendConscience
                                     : mot == Motive::Rampage   ? kRampageConscience
                                                                : 1.f;
            const float score = m * distanceFactor(dist, kAttackDistHalf) + conscience * v.utility;
            if (score > bestScore) {
                bestScore = score;
                best = o;
                bestMotive = mot;
                bestProtectee = protectee;
                bestV = v;
            }
        }
        if (!best) return;
        std::string why;
        switch (bestMotive) {
            case Motive::SelfDefense: why = best->name + " is attacking me"; break;
            case Motive::Defend: why = best->name + " is attacking " + sim.nameOf(bestProtectee); break;
            case Motive::Revenge:
                why = "revenge on " + best->name + " (grievance " + pct(grievanceToward(self, best->id)) + ")";
                break;
            case Motive::Rampage: why = "seeing red"; break;
            case Motive::Robbery:
                why = "starving; " + best->name + " carries " + std::to_string(best->inventory.get(Item::Food)) +
                      " food and is weaker";
                break;
        }
        pushProposal(out, bestScore, std::make_unique<AttackAction>(best->id, best->name, bestMotive, bestProtectee),
                     withWhy(why, bestV));
    }
};

// ===========================================================================
// Flee
// ===========================================================================
class FleeAction final : public Action {
public:
    FleeAction(AgentId threat, std::string threatName, bool panic)
        : threat_(threat), threatName_(std::move(threatName)), panic_(panic) {}
    const char* kind() const override { return kinds::Flee; }
    std::string label() const override { return panic_ ? "Fleeing in panic" : "Fleeing from " + threatName_; }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        ++elapsed_;
        if (elapsed_ > (panic_ ? kPanicMaxTicks : kFleeMaxTicks)) return finish(self, ActionStatus::Done);
        const Agent* threat = living(std::as_const(sim), threat_);
        if (panic_) {  // flee whoever is closest
            int bestD = 1 << 20;
            for (const Agent* o : std::as_const(sim).agentsNear(self.pos, kRampagerTiles, self.id)) {
                if (!o || !o->alive) continue;
                const int d = chebyshev(self.pos, o->pos);
                if (d < bestD) {
                    bestD = d;
                    threat = o;
                }
            }
            if (!threat) return finish(self, self.inBreakdown() ? ActionStatus::Running : ActionStatus::Done);
        }
        if (!threat || chebyshev(self.pos, threat->pos) >= kSafeDistance) return finish(self, ActionStatus::Done);
        if (!stepAway(self, sim, threat->pos)) {
            if (++cornered_ >= kCorneredTicks) return finish(self, ActionStatus::Failed);  // cornered: rethink (fight?)
        } else {
            cornered_ = 0;
        }
        return ActionStatus::Running;
    }

private:
    AgentId threat_;
    std::string threatName_;
    bool panic_;
    int elapsed_ = 0;
    int cornered_ = 0;
};

class FleeBehavior final : public Behavior {
public:
    const char* name() const override { return "flee"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        const Tick now = sim.now();
        const bool child = isChildAgent(self, sim);
        const bool panic = self.inBreakdown() && self.breakType == BreakType::Panic;
        const Agent* threat = nullptr;
        float danger = 0.f;
        const char* what = "";
        for (Agent* o : sim.agentsNear(self.pos, cfg::kSightRadius, self.id)) {
            if (!o || !o->alive || o->id == self.id) continue;
            const int dist = chebyshev(self.pos, o->pos);
            float d = 0.f;
            const char* w = "";
            if (attackTargetOf(*o) == self.id) {
                d = kDangerAttacked;
                w = "is attacking me";
            } else if (recentMemory(self, MemoryType::Attacked, o->id, now, kRecentAttackTicks)) {
                d = kDangerRecent;
                w = "just attacked me";
            } else if (isRampaging(*o) && dist <= kRampagerTiles) {
                d = kDangerRampager;
                w = "is on a rampage";
            } else if (o->isAdult(now) && dist <= kHostileApproachTiles && self.affinityFor(o->id) < kHostileAffinity &&
                       (o->trait(Trait::Aggression) > kHostileAggression || o->emotions.get(Emotion::Anger) > kHostileAnger)) {
                d = kDangerHostile;
                w = "hates me and looks dangerous";
            }
            if (d <= 0.f) continue;
            d *= 1.f / (1.f + 0.05f * float(dist));  // nearer threats first
            if (d > danger) {
                danger = d;
                threat = o;
                what = w;
            }
        }
        if (!threat && !panic) return;

        const float B = self.trait(Trait::Bravery);
        float score;
        std::string why;
        if (threat) {
            const float gap = combatPower(*threat, sim) - combatPower(self, sim);
            score = danger * (kFleeBase + kFleeCowardice * (1.f - B)) + kFleePower * gap +
                    (self.health < kFleeHurtBelow ? kFleeHurt : 0.f) + (child ? kFleeChild : 0.f);
            why = threat->name + " " + what;
            if (gap > 0.1f) why += " and is stronger";
            if (self.health < kFleeHurtBelow) why += "; I'm badly hurt";
        } else {
            score = kDangerPanic * (kFleeBase + kFleeCowardice * (1.f - B));
            why = "panicking";
        }
        pushProposal(out, score,
                     std::make_unique<FleeAction>(threat ? threat->id : kNoAgent, threat ? threat->name : std::string(),
                                                  panic && !threat),
                     why);
    }
};

// ===========================================================================
// Confront (punish / demand restitution)
// ===========================================================================
class ConfrontAction final : public Action {
public:
    ConfrontAction(AgentId target, std::string targetName, std::string offense, float demand)
        : target_(target), targetName_(std::move(targetName)), offense_(std::move(offense)), demand_(demand) {}
    const char* kind() const override { return kinds::Confront; }
    std::string label() const override {
        return fighting_ ? "Punishing " + targetName_ : "Confronting " + targetName_ + " over " + offense_;
    }
    AgentId fightTarget() const { return fighting_ ? target_ : kNoAgent; }
    AgentId target() const { return target_; }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        ++elapsed_;
        Agent* t = living(sim, target_);
        if (!t) return finish(self, fighting_ ? ActionStatus::Done : ActionStatus::Failed);
        if (self.health < kQuitHealth || elapsed_ > kConfrontMaxTicks) return finish(self, ActionStatus::Failed);
        const int dist = chebyshev(self.pos, t->pos);
        if (fighting_) return fight(self, *t, sim, dist);
        if (dist > 1) {
            if (elapsed_ > kConfrontGiveUpTicks) return finish(self, ActionStatus::Failed);
            if (stepToward(self, sim, t->pos, 1) == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        t->rel(self.id).lastSeen = sim.now();  // they know they're being called out
        if (++argued_ < kArgueTicks) return ActionStatus::Running;
        return resolve(self, *t, sim);
    }

private:
    ActionStatus resolve(Agent& self, Agent& t, Simulation& sim) {
        const Tick now = sim.now();
        const int witnesses = countWitnesses(sim, self.pos, self.id, t.id);
        const float myP = combatPower(self, sim);
        const float theirP = combatPower(t, sim);
        const float grievance = grievanceToward(self, t.id);

        ++sim.stats().punishments;
        ++self.stats.punishmentsGiven;
        self.remember(MemoryType::PunishedSomeone, t.id, now, 0.6f);

        // The wrongdoer weighs guilt and fear against greed and pride.
        const float fear = clamp01(0.5f + (myP - theirP)) * (1.f - 0.5f * t.trait(Trait::Bravery));
        const float comply = kComplyGuilt * t.emotions.get(Emotion::Guilt) + kComplyFear * fear +
                             kComplyMeek * (1.f - t.trait(Trait::Aggression)) + kComplyIntegrity * t.trait(Trait::Integrity) +
                             kComplyNorm * t.norms.punishmentExpectation - kComplyGreed * t.trait(Trait::Greed) -
                             kComplyHunger * t.needs.urgency(Need::Food) - kComplyFeud * grievanceToward(t, self.id);
        if (sim.rng().chance(logistic(comply, kComplyMid, kComplySteep))) {
            // Restitution: hand over goods worth `demand_`, food first.
            ItemCounts moved{};
            float remaining = demand_;
            for (Item it : {Item::Food, Item::Tools, Item::Herbs, Item::Stone, Item::Wood, Item::Seeds}) {
                while (remaining > 0.25f && giveItems(t, self, it, 1) == 1) {
                    ++moved[std::size_t(it)];
                    remaining -= itemValue(it);
                }
            }
            const float returned = demand_ - std::max(0.f, remaining);
            t.emotions.set(Emotion::Guilt, t.emotions.get(Emotion::Guilt) * 0.5f);
            t.feel(Emotion::Sadness, 0.05f);
            Relationship& r = self.rel(t.id);
            r.grievance *= kComplyGrievanceKeep;
            r.trust = clamp01(r.trust + 0.05f);
            const bool forgave = r.grievance < kForgiveBelow;
            if (forgave) {
                r.grievance = 0.f;
                r.affinity = clampf(r.affinity + 0.05f, -1.f, 1.f);
                self.remember(MemoryType::Forgave, t.id, now, 0.5f);
                ++sim.stats().forgivenesses;
            }
            const DeedContext ctx = makeDeed(Deed::Punish, &t, 0.2f * grievance, kConfrontHarm, 0.f, witnesses);
            const Verdict v = judge(self, ctx, sim);
            self.feel(Emotion::Guilt, v.guilt);
            self.feel(Emotion::Pride, v.pride);
            sim.log(EventKind::Justice,
                    self.name + " confronted " + t.name + " over " + offense_ + "; " + heShe(t) +
                        (returned > 0.f ? " handed over " + describeItems(moved) : std::string(" apologized")) +
                        (forgave ? " and was forgiven" : ""),
                    self.id, t.id);
            broadcastDeed(sim, self, ctx, self.pos);
            return finish(self, ActionStatus::Done);
        }

        // Refused. Shout it out, or make them pay?
        self.feel(Emotion::Anger, 0.15f);
        const float esc = kEscAggression * self.trait(Trait::Aggression) + kEscAnger * self.emotions.get(Emotion::Anger) +
                          kEscRevenge * grievance * self.trait(Trait::Vengefulness) + kEscPower * (myP - theirP) +
                          kEscNorm * self.norms.violenceTolerance - kEscEmpathy * self.trait(Trait::Empathy);
        if (!isChildAgent(t, sim) && sim.rng().chance(logistic(esc, kEscMid, kEscSteep))) {
            fighting_ = true;
            ++sim.stats().fights;
            ++self.stats.attacks;
            const DeedContext ctx = makeDeed(Deed::Punish, &t, 0.2f * grievance, kPunishFightHarm, 0.f, witnesses);
            const Verdict v = judge(self, ctx, sim);
            self.feel(Emotion::Guilt, v.guilt);
            self.feel(Emotion::Pride, v.pride);
            sim.log(EventKind::Justice, t.name + " refused to make amends to " + self.name + " for " + offense_ + "; " +
                                            self.name + " means to teach " + himHer(t) + " a lesson", self.id, t.id);
            sim.log(EventKind::Fight, self.name + " attacks " + t.name + " (punishment)", self.id, t.id);
            broadcastDeed(sim, self, ctx, self.pos);
            t.wantsRethink = true;
            return ActionStatus::Running;
        }
        const DeedContext ctx = makeDeed(Deed::Punish, &t, 0.1f * grievance, kPunishShameHarm, 0.f, witnesses);
        const Verdict v = judge(self, ctx, sim);
        self.feel(Emotion::Pride, v.pride * 0.5f);
        sim.log(EventKind::Justice, t.name + " refused to make amends to " + self.name + " for " + offense_, self.id, t.id);
        broadcastDeed(sim, self, ctx, self.pos);  // public shaming
        return finish(self, ActionStatus::Done);
    }

    ActionStatus fight(Agent& self, Agent& t, Simulation& sim, int dist) {
        if (dist > 3) return endFight(self, t, ActionStatus::Done);  // they ran off: point made
        if (dist > 1) {
            if (stepToward(self, sim, t.pos, 1) == MoveResult::Blocked) return endFight(self, t, ActionStatus::Done);
            return ActionStatus::Running;
        }
        if (cooldown_ > 0) {
            --cooldown_;
            return ActionStatus::Running;
        }
        cooldown_ = kSwingInterval - 1;
        if (t.health <= kLethalHealth && !willKill(self, t, Motive::Revenge, true, sim))
            return endFight(self, t, ActionStatus::Done);  // lesson taught
        const StrikeResult r = strike(self, t, sim);
        if (r != StrikeResult::Missed) ++hits_;
        if (r == StrikeResult::Killed) {
            onKill(self, t, sim, "punishment gone too far");
            return finish(self, ActionStatus::Done);
        }
        if (hits_ >= kPunishHits) return endFight(self, t, ActionStatus::Done);
        return ActionStatus::Running;
    }

    ActionStatus endFight(Agent& self, const Agent& t, ActionStatus s) {
        Relationship& r = self.rel(t.id);
        r.grievance *= kPunishGrievanceKeep;
        self.emotions.add(Emotion::Anger, -0.15f);
        return finish(self, s);
    }

    AgentId target_;
    std::string targetName_;
    std::string offense_;
    float demand_;
    int elapsed_ = 0;
    int argued_ = 0;
    int hits_ = 0;
    int cooldown_ = 0;
    bool fighting_ = false;
};

bool othersFighting(Simulation& sim, const Agent& who, AgentId ex1, AgentId ex2) {
    for (const Agent* a : std::as_const(sim).agentsNear(who.pos, cfg::kSightRadius, who.id)) {
        if (!a || !a->alive || a->id == ex1 || a->id == ex2 || !a->action) continue;
        if (attackTargetOf(*a) == who.id) return true;
        if (const auto* c = dynamic_cast<const ConfrontAction*>(a->action.get()); c && c->target() == who.id) return true;
    }
    return false;
}

// What did `o` do (that I know of)? Fills offense text; returns civic motive weight (witnessed deeds).
struct KnownOffense {
    std::string text;     // first person, for the inspector: "stealing from me"
    std::string logText;  // third person, for the log/label: "the theft", "attacking Lia"
    float witnessed = 0.f;  // intensity × care of the strongest recent witnessed transgression
};

KnownOffense knownOffense(const Agent& self, const Agent& o, const Simulation& sim, Tick now) {
    KnownOffense k;
    for (auto it = self.memories.rbegin(); it != self.memories.rend(); ++it) {
        if (it->other != o.id) continue;
        const bool recent = it->when + kWitnessWindow >= now;
        switch (it->type) {
            case MemoryType::StolenFrom:
                if (k.text.empty()) {
                    k.text = "stealing from me";
                    k.logText = "the theft";
                }
                break;
            case MemoryType::Attacked:
                if (k.text.empty()) {
                    k.text = "attacking me";
                    k.logText = "the assault";
                }
                break;
            case MemoryType::Punished:
                if (k.text.empty()) {
                    k.text = "punishing me";
                    k.logText = "the punishment";
                }
                break;
            case MemoryType::WitnessedTheft:
            case MemoryType::WitnessedAttack: {
                if (!recent) break;
                const Agent* victim = living(sim, it->subject);
                float care = 1.f;
                if (victim && victim->id != self.id && cares(self, *victim)) care = kConfrontKinVictim;
                const float w = it->intensity * care;
                if (w > k.witnessed) {
                    k.witnessed = w;
                    const bool theft = it->type == MemoryType::WitnessedTheft;
                    const std::string vname = it->subject == kNoAgent ? std::string("the commons") : sim.nameOf(it->subject);
                    if (k.text.empty()) {
                        k.text = (theft ? "stealing from " : "attacking ") + vname;
                        k.logText = k.text;
                    }
                }
                break;
            }
            default: break;
        }
    }
    if (k.text.empty()) {
        k.text = "wronging me";
        k.logText = "past wrongs";
    }
    return k;
}

class ConfrontBehavior final : public Behavior {
public:
    const char* name() const override { return "confront"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        const Tick now = sim.now();
        if (!self.isAdult(now) || self.health < kMinHealthToStart) return;
        const float I = self.trait(Trait::Integrity);
        const float V = self.trait(Trait::Vengefulness);
        const float B = self.trait(Trait::Bravery);
        const float pe = self.norms.punishmentExpectation;
        const float myP = combatPower(self, sim);

        const Agent* best = nullptr;
        float bestScore = 0.f;
        float bestGrievance = 0.f;
        KnownOffense bestOffense;
        Verdict bestV;
        for (Agent* o : sim.agentsNear(self.pos, cfg::kSightRadius, self.id)) {
            if (!o || !o->alive || o->id == self.id || isAsleep(*o) || isFighting(*o)) continue;
            if (recentMemory(self, MemoryType::PunishedSomeone, o->id, now, kRecentPunishTicks)) continue;
            if (othersFighting(sim, *o, self.id, kNoAgent)) continue;  // no pile-ons: someone is already on it
            const float g = grievanceToward(self, o->id);
            const KnownOffense k = knownOffense(self, *o, sim, now);
            const float personal = g * (kConfrontPersonal + kConfrontPersonalV * V);
            const float civic = k.witnessed * (kConfrontCivicI * I + kConfrontCivicV * V + kConfrontCivicP * pe);
            float m = std::max(personal, civic);
            if (m < kConfrontMinMotive) continue;
            m *= clampf(0.5f + 0.5f * B + 0.5f * (myP - combatPower(*o, sim)), 0.2f, 1.3f);  // courage
            const DeedContext ctx = makeDeed(Deed::Punish, o, 0.2f * g, kConfrontHarm, 0.f,
                                             countWitnesses(sim, o->pos, self.id, o->id));
            const Verdict v = judge(self, ctx, sim);
            const float score = m * distanceFactor(chebyshev(self.pos, o->pos), kConfrontDistHalf) +
                                kConfrontMoralWeight * v.utility;
            if (score > bestScore) {
                bestScore = score;
                best = o;
                bestGrievance = g;
                bestOffense = k;
                bestV = v;
            }
        }
        if (!best) return;
        const float demand = kRestitutionBase + std::round(bestGrievance * kRestitutionScale);
        std::string why = best->name + " has been " + bestOffense.text;
        if (bestGrievance > 0.05f) why += " (grievance " + pct(bestGrievance) + ")";
        pushProposal(out, bestScore, std::make_unique<ConfrontAction>(best->id, best->name, bestOffense.logText, demand),
                     withWhy(why, bestV));
    }
};

// ===========================================================================
// Apologize
// ===========================================================================
class ApologizeAction final : public Action {
public:
    ApologizeAction(AgentId victim, std::string victimName, MemoryType deed, Tick deedWhen)
        : victim_(victim), victimName_(std::move(victimName)), deed_(deed), deedWhen_(deedWhen) {}
    const char* kind() const override { return kinds::Apologize; }
    std::string label() const override { return "Apologizing to " + victimName_; }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        ++elapsed_;
        Agent* v = living(sim, victim_);
        if (!v || isAsleep(*v) || attackTargetOf(*v) == self.id) return finish(self, ActionStatus::Failed);
        if (chebyshev(self.pos, v->pos) > 1) {
            if (elapsed_ > kApologizeGiveUpTicks) return finish(self, ActionStatus::Failed);
            if (stepToward(self, sim, v->pos, 1) == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        if (++talked_ < kApologizeTalkTicks) return ActionStatus::Running;
        makeAmends(self, *v, sim);
        return finish(self, ActionStatus::Done);
    }

private:
    void makeAmends(Agent& self, Agent& v, Simulation& sim) {
        const Tick now = sim.now();
        // Restitution: food back for a theft; herbs (or food) for an assault.
        int given = 0;
        Item item = Item::Food;
        if (deed_ == MemoryType::AttackedSomeone && self.inventory.has(Item::Herbs)) {
            item = Item::Herbs;
            given = giveItems(self, v, Item::Herbs, 1);
        }
        if (given == 0) {
            item = Item::Food;
            given = feedFood(self, v, std::min(self.inventory.get(Item::Food), kApologizeMaxFood));
        }
        const float value = float(given) * itemValue(item);

        self.emotions.set(Emotion::Guilt, self.emotions.get(Emotion::Guilt) * kApologizeGuiltKeep);
        for (Memory& m : self.memories)
            if (m.type == deed_ && m.other == v.id && m.when == deedWhen_) m.intensity *= kApologizeMemoryKeep;
        nudgeRelation(self, v.id, 0.03f, 0.03f, now);

        // The victim decides whether to forgive.
        Relationship& r = v.rel(self.id);
        r.trust = clamp01(r.trust + 0.06f);
        r.affinity = clampf(r.affinity + 0.04f + 0.03f * value, -1.f, 1.f);
        const float x = kForgiveEmpathy * v.trait(Trait::Empathy) + kForgiveMercy * (1.f - v.trait(Trait::Vengefulness)) +
                        kForgiveRestitution * std::min(1.f, value / 3.f) + (v.isFamily(self) ? kForgiveFamily : 0.f) +
                        kForgiveTrust * r.trust - kForgiveGrievance * r.grievance -
                        kForgiveAnger * v.emotions.get(Emotion::Anger);
        const std::string gift = given > 0 ? " and returned " + describeItems(item, given) : std::string();
        if (sim.rng().chance(logistic(x, kForgiveMid, kForgiveSteep))) {
            r.grievance *= 0.15f;
            v.remember(MemoryType::Forgave, self.id, now, 0.5f);
            ++sim.stats().forgivenesses;
            sim.log(EventKind::Justice, self.name + " apologized to " + v.name + gift + "; " + heShe(v) + " forgave " + himHer(self),
                    self.id, v.id);
            const DeedContext ctx = makeDeed(Deed::Forgive, &self, 0.f, 0.f, 0.5f, countWitnesses(sim, v.pos, v.id, self.id));
            const Verdict fv = judge(v, ctx, sim);
            v.feel(Emotion::Pride, fv.pride);
            broadcastDeed(sim, v, ctx, v.pos);  // forgiver is the actor; I feel relief
        } else {
            r.grievance *= 0.7f;
            sim.log(EventKind::Justice,
                    self.name + " apologized to " + v.name + gift + ", but " + heShe(v) + " would not forgive " + himHer(self),
                    self.id, v.id);
        }
    }

    AgentId victim_;
    std::string victimName_;
    MemoryType deed_;
    Tick deedWhen_;
    int elapsed_ = 0;
    int talked_ = 0;
};

class ApologizeBehavior final : public Behavior {
public:
    const char* name() const override { return "apologize"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        const Tick now = sim.now();
        if (!self.isAdult(now)) return;
        const float guilt = self.emotions.get(Emotion::Guilt);
        if (guilt < kApologizeMinGuilt) return;
        const Memory* bestM = nullptr;
        const Agent* victim = nullptr;
        float bestW = 0.f;
        for (auto it = self.memories.rbegin(); it != self.memories.rend(); ++it) {
            if (it->when + kApologizeWindow < now) break;
            if (it->type != MemoryType::Stole && it->type != MemoryType::AttackedSomeone) continue;
            if (it->intensity < kApologizeMinMemory || it->other == kNoAgent) continue;
            const Agent* v = living(std::as_const(sim), it->other);
            if (!v || chebyshev(self.pos, v->pos) > kApologizeSearch) continue;
            const float w = it->intensity * distanceFactor(chebyshev(self.pos, v->pos), kApologizeDistHalf);
            if (w > bestW) {
                bestW = w;
                bestM = &*it;
                victim = v;
            }
        }
        if (!bestM) return;
        const float temperament = 0.35f + 0.45f * self.trait(Trait::Integrity) + 0.3f * self.trait(Trait::Empathy);
        const float score = (kApologizeBase + kApologizeGuilt * guilt) * temperament * bestW;
        pushProposal(out, score, std::make_unique<ApologizeAction>(victim->id, victim->name, bestM->type, bestM->when),
                     std::string("guilty about ") + (bestM->type == MemoryType::Stole ? "stealing from " : "attacking ") +
                         victim->name + " (guilt " + pct(guilt) + "); I want to make amends");
    }
};

// ===========================================================================
// Forgive
// ===========================================================================
class ForgiveAction final : public Action {
public:
    ForgiveAction(AgentId target, std::string targetName) : target_(target), targetName_(std::move(targetName)) {}
    const char* kind() const override { return kinds::Forgive; }
    std::string label() const override { return "Forgiving " + targetName_; }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        ++elapsed_;
        Agent* t = living(sim, target_);
        if (!t || attackTargetOf(*t) == self.id) return finish(self, ActionStatus::Failed);
        if (chebyshev(self.pos, t->pos) > 2) {
            if (elapsed_ > kForgiveGiveUpTicks) return finish(self, ActionStatus::Failed);
            if (stepToward(self, sim, t->pos, 2) == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        const Tick now = sim.now();
        const DeedContext ctx = makeDeed(Deed::Forgive, t, 0.f, 0.f, grievanceToward(self, t->id),
                                         countWitnesses(sim, self.pos, self.id, t->id));
        const Verdict v = judge(self, ctx, sim);
        self.feel(Emotion::Pride, v.pride);
        Relationship& r = self.rel(t->id);
        r.grievance *= kForgiveGrievanceKeep;
        r.affinity = clampf(r.affinity + kForgiveAffinity, -1.f, 1.f);
        r.trust = clamp01(r.trust + kForgiveTrustGain);
        self.stress = clamp01(self.stress - kForgiveStressRelief);
        self.emotions.set(Emotion::Anger, self.emotions.get(Emotion::Anger) * 0.5f);
        self.feel(Emotion::Joy, 0.1f);
        self.remember(MemoryType::Forgave, t->id, now, 0.5f);
        ++sim.stats().forgivenesses;
        sim.log(EventKind::Justice, self.name + " forgave " + t->name, self.id, t->id);
        broadcastDeed(sim, self, ctx, self.pos);
        return finish(self, ActionStatus::Done);
    }

private:
    AgentId target_;
    std::string targetName_;
    int elapsed_ = 0;
};

class ForgiveBehavior final : public Behavior {
public:
    const char* name() const override { return "forgive"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        const Tick now = sim.now();
        if (!self.isAdult(now)) return;
        const float disposition = 0.6f * (1.f - self.trait(Trait::Vengefulness)) + 0.4f * self.trait(Trait::Empathy);
        const float anger = self.emotions.get(Emotion::Anger);
        const Agent* best = nullptr;
        float bestScore = 0.f;
        float bestG = 0.f;
        bool bestAmends = false;
        Verdict bestV;
        for (const auto& [id, r] : self.relations) {
            if (r.grievance < kForgiveMinGrievance) continue;
            const Agent* t = living(std::as_const(sim), id);
            if (!t || attackTargetOf(*t) == self.id) continue;
            const int dist = chebyshev(self.pos, t->pos);
            if (dist > cfg::kSightRadius) continue;
            const bool family = self.isFamily(*t);
            const bool amends = recentMemory(self, MemoryType::ReceivedGift, id, now, kAmendsWindow) ||
                                recentMemory(self, MemoryType::Comforted, id, now, kAmendsWindow);
            const float m = r.grievance * disposition * (family ? kForgiveFamilyMult : 1.f) *
                                (amends ? kForgiveAmendsMult : 1.f) +
                            kForgiveStressWeight * self.stress - kForgiveAngerCost * anger;
            const DeedContext ctx = makeDeed(Deed::Forgive, t, 0.f, 0.f, r.grievance, countWitnesses(sim, t->pos, self.id, id));
            const Verdict v = judge(self, ctx, sim);
            const float score = kForgiveWeight * m * distanceFactor(dist, kForgiveDistHalf) + kForgiveMoralWeight * v.utility;
            if (score > bestScore) {
                bestScore = score;
                best = t;
                bestG = r.grievance;
                bestAmends = amends;
                bestV = v;
            }
        }
        if (!best) return;
        std::string why = "letting go of my grudge against " + best->name + " (" + pct(bestG) + ")";
        if (self.isFamily(*best)) why += "; " + best->name + " is family";
        else if (bestAmends) why += "; " + best->name + " has made amends";
        pushProposal(out, bestScore, std::make_unique<ForgiveAction>(best->id, best->name), withWhy(why, bestV));
    }
};

}  // namespace

// ===========================================================================
// Cross-file query & registration
// ===========================================================================
AgentId attackTargetOf(const Agent& a) {
    const Action* act = a.action.get();
    if (!act) return kNoAgent;
    if (const auto* at = dynamic_cast<const AttackAction*>(act)) return at->target();
    if (const auto* c = dynamic_cast<const ConfrontAction*>(act)) return c->fightTarget();
    return kNoAgent;
}

void addConflictBehaviors(BehaviorList& out) {
    out.push_back(std::make_unique<AttackBehavior>());
    out.push_back(std::make_unique<FleeBehavior>());
    out.push_back(std::make_unique<ConfrontBehavior>());
    out.push_back(std::make_unique<ApologizeBehavior>());
    out.push_back(std::make_unique<ForgiveBehavior>());
}

}  // namespace heur::social
