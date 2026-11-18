// Heuristica — social dynamics: witnessing, reputation via gossip, norm learning, attraction.
//
// broadcastDeed : every awake onlooker appraises a deed (ai/Morality appraise()) and updates its
//                 relationship with the actor, its emotions, its episodic memory and — most
//                 importantly for culture — its Norms (social learning). The target reacts too.
// gossip        : opinions about third parties flow between talkers, weighted by trust, so
//                 reputations ("Ivo is a thief") spread beyond the original witnesses.
// attraction / acceptsAdvance : heuristic mate choice (health, mood, status, genetic distance,
//                 affinity, age; loyalty to an existing partner).
#include "social/Social.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>
#include <vector>

#include "agent/Agent.h"
#include "ai/Action.h"
#include "ai/ActionHelpers.h"
#include "core/Config.h"
#include "sim/Simulation.h"
#include "social/SocialInternal.h"
#include "world/World.h"

namespace heur {

namespace {

// ===========================================================================
// Tunables
// ===========================================================================
// ---- Witnessing -------------------------------------------------------------
constexpr float kOutrageApproval = -0.5f;    // approval below this = outraged onlooker
constexpr float kTellApproval = -0.25f;      // onlookers this disapproving may tell the victim who did it
constexpr float kTellChanceBase = 0.45f;     // P(tell) = base + scale * |approval|
constexpr float kTellChanceScale = 0.5f;
constexpr float kTellMinVictimAffinity = -0.2f;  // nobody tips off someone they dislike
constexpr int kCrowdLogSize = 4;             // outraged onlookers needed to log "the crowd turns on X"
constexpr float kRethinkFear = 0.25f;        // onlookers this scared/angry re-think immediately
constexpr float kRethinkAnger = 0.30f;
constexpr float kAdmireJoy = 0.12f;          // joy per +1 approval when watching a kind deed
constexpr float kWatchEnvy = 0.05f;          // × Greed: envy when watching someone else receive
constexpr float kWitnessMemBase = 0.30f;     // witness memory intensity = base + scale*|approval|
constexpr float kWitnessMemScale = 0.50f;
// ---- Target reactions ---------------------------------------------------------
constexpr float kTheftSadness = 0.10f;       // + kTheftSadnessHarm * harm
constexpr float kTheftSadnessHarm = 0.20f;
constexpr float kUnknownThiefAnger = 0.12f;  // + kUnknownThiefAngerHarm * harm
constexpr float kUnknownThiefAngerHarm = 0.20f;
constexpr float kVandalSadness = 0.12f;      // + 0.2 * harm
constexpr float kGiftJoy = 0.10f;            // + kGiftJoyBenefit * benefit
constexpr float kGiftJoyBenefit = 0.30f;
constexpr float kComfortJoy = 0.10f;
constexpr float kForgivenGuiltRelief = 0.20f;
constexpr float kForgivenJoy = 0.10f;
constexpr float kHelpGratitude = 0.05f;      // reciprocity ledger credit when someone helps my project
constexpr float kBalanceCap = 2.0f;
constexpr float kJiltedSadness = 0.35f;
constexpr float kJiltedMemory = 0.7f;
// ---- Norm learning (EMA step sizes toward 0 or 1) ------------------------------
constexpr float kNormNormalise = 0.02f;      // a transgression that doesn't outrage makes it seem normal
constexpr float kNormReinforce = 0.03f;      // one that outrages reinforces the taboo
constexpr float kNormKillReinforce = 0.05f;
constexpr float kNormPunishSeen = 0.05f;     // seeing punishment: punishmentExpectation up
constexpr float kNormPunishTheft = 0.03f;    //                    theftTolerance down
constexpr float kNormSharingSeen = 0.02f;    // seeing generosity: sharingExpectation -> 0.55 + 0.4 × approval
constexpr float kNormSharingCeil = 0.95f;
constexpr float kNormTheftSharing = 0.01f;   // seeing theft: people evidently don't share (-> 0)
constexpr float kNormHelpSeen = 0.01f;
constexpr float kNormComfortSeen = 0.01f;
constexpr float kNormForgiveSeen = 0.01f;    // forgiveness: punishmentExpectation down slightly
constexpr float kNormVandalSeen = 0.01f;
constexpr float kNormVictim = 0.05f;         // victims harden against what was done to them
constexpr float kNormReceiver = 0.03f;       // receiving generosity: sharingExpectation -> kNormReceiverTarget
constexpr float kNormReceiverTarget = 0.85f;
constexpr float kNormHelpTarget = 0.75f;
constexpr float kNormComfortTarget = 0.70f;
constexpr float kNormPunished = 0.06f;       // being punished teaches (scaled by guilt)
constexpr float kNormUnpunished = 0.02f;     // an unknown thief got away: punishmentExpectation down

// ---- Gossip -----------------------------------------------------------------------
constexpr int kGossipTopics = 2;
constexpr float kGossipMinStrength = 0.20f;  // opinions weaker than this aren't worth mentioning
constexpr float kGossipBlend = 0.30f;        // max fraction of the speaker's opinion adopted (× trust)
constexpr float kGossipHostileSpeaker = 0.3f;// × when the listener dislikes the speaker
constexpr float kGossipHostileBelow = -0.3f;
constexpr float kGossipFirstHand = 0.08f;    // weight /= 1 + this × listener's own interactions
constexpr float kGossipLoyalty = 0.5f;       // × when bad-mouthing someone the listener is close to
constexpr float kGossipLoyalAbove = 0.5f;
constexpr float kGossipKinGrievance = 0.30f; // grievance passed on between family members
constexpr float kGossipBondAffinity = 0.02f; // × (0.5 + Sociability) mutual bump
constexpr float kGossipBondTrust = 0.01f;
constexpr float kNormConvergeListener = 0.03f;  // × trust
constexpr float kNormConvergeSpeaker = 0.01f;   // × trust

// ---- Attraction -----------------------------------------------------------------------
constexpr float kAttrHealth = 0.25f;         // weights of the heuristic terms (sum to 1)
constexpr float kAttrMood = 0.15f;
constexpr float kAttrStatus = 0.15f;
constexpr float kAttrGenetic = 0.20f;
constexpr float kAttrAffinity = 0.25f;
constexpr float kAttrHeuristicShare = 0.75f; // final = share*heuristic + stored*kAttrStoredShare
constexpr float kAttrStoredShare = 0.35f;    // stored Relationship::attraction (built by courting)
constexpr float kTypicalGeneDistance = 0.20f;// founders' typical mean gene difference
constexpr float kGeneticFloor = 0.40f;       // genetic factor at zero distance (clone-like)
constexpr float kStatusFoodDays = 6.f;       // food owned worth this many days = max food status
constexpr float kStatusBuilt = 4.f;          // structures built for max building status
constexpr float kAgeGapHalfDays = 40.f;      // age-gap factor halves at this many days apart
constexpr float kElderFactor = 0.6f;
constexpr float kKinFactor = 0.03f;          // incest avoidance
constexpr float kSameSexFactor = 0.10f;
constexpr float kAttrGrievance = 0.7f;       // × (1 - this × grievance)

// ---- Accepting advances -------------------------------------------------------------
constexpr float kAccMinAttraction = 0.15f;
constexpr float kAccTrust = 0.5f;            // × (trust - 0.5)
constexpr float kAccMood = 0.15f;
constexpr float kAccLibido = 0.30f;          // × (Libido - 0.5)
constexpr float kAccIntimacy = 0.25f;        // × urgency(Intimacy)
constexpr float kAccGrievance = 0.8f;
constexpr float kLoyaltyBase = 0.25f;        // cost of leaving a living partner ...
constexpr float kLoyaltyIntegrity = 0.45f;   // ... + Integrity × this
constexpr float kLoyaltyLove = 0.35f;        // ... + affinity for partner × this
constexpr float kHomewreckIntegrity = 0.25f; // suitor has a partner: × Integrity
constexpr float kAcceptMid = 0.5f;
constexpr float kAcceptSteep = 7.f;

// ===========================================================================
// Helpers
// ===========================================================================
float emaToward(float v, float target, float rate) { return clamp01(v + rate * (target - v)); }

bool asleep(const Agent& a) { return a.actionKind == kinds::Sleep; }
bool isTheftDeed(Deed d) { return d == Deed::StealFromAgent || d == Deed::StealFromStorage; }
bool isViolentDeed(Deed d) { return d == Deed::Attack || d == Deed::Kill; }

bool bloodRelated(const Agent& a, const Agent& b) {
    if (a.id == b.id) return true;
    if (a.mother == b.id || a.father == b.id || b.mother == a.id || b.father == a.id) return true;
    if (a.mother != kNoAgent && a.mother == b.mother) return true;
    if (a.father != kNoAgent && a.father == b.father) return true;
    return false;
}

const char* deedPhrase(Deed d) {
    switch (d) {
        case Deed::StealFromAgent:
        case Deed::StealFromStorage: return "the theft";
        case Deed::Attack: return "the assault";
        case Deed::Kill: return "the killing";
        case Deed::Vandalize: return "the vandalism";
        case Deed::Adultery: return "the affair";
        default: return "what they did";
    }
}

void applyAppraisal(Agent& o, const Agent& actor, const Appraisal& ap, Tick now) {
    Relationship& r = o.rel(actor.id);
    r.affinity = clampf(r.affinity + ap.affinityDelta, -1.f, 1.f);
    r.trust = clamp01(r.trust + ap.trustDelta);
    r.grievance = clamp01(r.grievance + ap.grievance);
    ++r.interactions;
    r.lastSeen = now;
    if (ap.anger > 0.f) o.feel(Emotion::Anger, ap.anger);
    if (ap.fear > 0.f) o.feel(Emotion::Fear, ap.fear);
    if (ap.approval > 0.2f) o.feel(Emotion::Joy, ap.approval * kAdmireJoy);
}

// Third-party onlooker: episodic memory of what they saw.
void rememberWitnessed(Agent& o, const Agent& actor, const DeedContext& ctx, AgentId victim, const Appraisal& ap,
                       Tick now) {
    const float intensity = clamp01(kWitnessMemBase + kWitnessMemScale * std::fabs(ap.approval));
    if (isTheftDeed(ctx.deed)) {
        o.remember(MemoryType::WitnessedTheft, actor.id, now, intensity, victim);
    } else if (ctx.deed == Deed::Attack) {
        o.remember(MemoryType::WitnessedAttack, actor.id, now, intensity, victim);
    } else if (ctx.deed == Deed::Kill) {
        o.remember(MemoryType::WitnessedAttack, actor.id, now, 1.f, victim);
    }
}

// Social learning from watching others. EMA steps toward 0 or 1.
void learnFromWatching(Agent& o, Deed deed, const Appraisal& ap) {
    Norms& n = o.norms;
    const bool outraged = ap.approval <= kOutrageApproval;
    switch (deed) {
        case Deed::StealFromAgent:
        case Deed::StealFromStorage:
            n.theftTolerance = outraged ? emaToward(n.theftTolerance, 0.f, kNormReinforce)
                                        : emaToward(n.theftTolerance, 1.f, kNormNormalise);
            n.sharingExpectation = emaToward(n.sharingExpectation, 0.f, kNormTheftSharing);
            break;
        case Deed::Attack:
            n.violenceTolerance = outraged ? emaToward(n.violenceTolerance, 0.f, kNormReinforce)
                                           : emaToward(n.violenceTolerance, 1.f, kNormNormalise);
            break;
        case Deed::Kill:
            n.violenceTolerance = outraged ? emaToward(n.violenceTolerance, 0.f, kNormKillReinforce)
                                           : emaToward(n.violenceTolerance, 1.f, kNormNormalise);
            break;
        case Deed::Vandalize:
            n.violenceTolerance = outraged ? emaToward(n.violenceTolerance, 0.f, kNormVandalSeen)
                                           : emaToward(n.violenceTolerance, 1.f, kNormVandalSeen);
            break;
        case Deed::Punish:
            n.punishmentExpectation = emaToward(n.punishmentExpectation, 1.f, kNormPunishSeen);
            n.theftTolerance = emaToward(n.theftTolerance, 0.f, kNormPunishTheft);
            break;
        case Deed::Share:
        case Deed::Gift:
            n.sharingExpectation = emaToward(n.sharingExpectation,
                                             clampf(0.55f + 0.4f * ap.approval, 0.f, kNormSharingCeil), kNormSharingSeen);
            break;
        case Deed::Help:
            n.sharingExpectation = emaToward(n.sharingExpectation, kNormHelpTarget, kNormHelpSeen);
            break;
        case Deed::Comfort:
            n.sharingExpectation = emaToward(n.sharingExpectation, kNormComfortTarget, kNormComfortSeen);
            break;
        case Deed::Forgive:
            n.punishmentExpectation = emaToward(n.punishmentExpectation, 0.f, kNormForgiveSeen);
            break;
        default: break;
    }
}

// The target knows who did it.
void reactAsTarget(Simulation& sim, Agent& target, Agent& actor, const DeedContext& ctx) {
    const Tick now = sim.now();
    const Appraisal ap = appraise(target, actor, ctx, sim);
    applyAppraisal(target, actor, ap, now);
    const float harm = clamp01(ctx.harm);
    const float benefit = clamp01(ctx.benefit);
    Norms& n = target.norms;
    switch (ctx.deed) {
        case Deed::StealFromAgent:
        case Deed::StealFromStorage:
            target.remember(MemoryType::StolenFrom, actor.id, now, 0.4f + 0.6f * harm);
            target.feel(Emotion::Sadness, kTheftSadness + kTheftSadnessHarm * harm);
            n.theftTolerance = emaToward(n.theftTolerance, 0.f, kNormVictim);
            break;
        case Deed::Attack:
            target.remember(MemoryType::Attacked, actor.id, now, 0.6f + 0.4f * harm);
            target.wantsRethink = true;
            n.violenceTolerance = emaToward(n.violenceTolerance, 0.f, kNormVictim);
            break;
        case Deed::Vandalize:
            target.feel(Emotion::Sadness, kVandalSadness + 0.2f * harm);
            break;
        case Deed::Share:
        case Deed::Gift:
            target.remember(MemoryType::ReceivedGift, actor.id, now, 0.3f + 0.5f * benefit);
            target.feel(Emotion::Joy, kGiftJoy + kGiftJoyBenefit * benefit);
            n.sharingExpectation = emaToward(n.sharingExpectation, kNormReceiverTarget, kNormReceiver);
            break;
        case Deed::Comfort:
            target.remember(MemoryType::Comforted, actor.id, now, 0.3f + 0.5f * benefit);
            target.feel(Emotion::Joy, kComfortJoy);
            break;
        case Deed::Punish: {
            target.remember(MemoryType::Punished, actor.id, now, 0.4f + 0.5f * harm);
            // The lesson sticks in proportion to how guilty the wrongdoer feels.
            const float lesson = clamp01(0.3f + target.emotions.get(Emotion::Guilt));
            n.punishmentExpectation = emaToward(n.punishmentExpectation, 1.f, kNormPunished * lesson);
            n.theftTolerance = emaToward(n.theftTolerance, 0.f, kNormPunished * 0.6f * lesson);
            break;
        }
        case Deed::Forgive:
            target.emotions.add(Emotion::Guilt, -kForgivenGuiltRelief);
            target.feel(Emotion::Joy, kForgivenJoy);
            break;
        case Deed::Help: {
            Relationship& r = target.rel(actor.id);
            r.balance = clampf(r.balance + kHelpGratitude, -kBalanceCap, kBalanceCap);
            target.feel(Emotion::Joy, 0.05f);
            break;
        }
        default: break;  // Kill: the target is dead. Adultery: the lover; betrayed partners handled separately.
    }
}

// Theft / vandalism noticed, culprit unknown.
void reactToUnknownCulprit(Simulation& sim, Agent& target, const DeedContext& ctx) {
    const float harm = clamp01(ctx.harm);
    if (isTheftDeed(ctx.deed)) {
        target.remember(MemoryType::StolenFrom, kNoAgent, sim.now(), 0.3f + 0.5f * harm);
        target.feel(Emotion::Sadness, kTheftSadness + kTheftSadnessHarm * harm);
        target.feel(Emotion::Anger, kUnknownThiefAnger + kUnknownThiefAngerHarm * harm);
        target.norms.theftTolerance = emaToward(target.norms.theftTolerance, 0.f, kNormVictim * 0.6f);
        target.norms.punishmentExpectation = emaToward(target.norms.punishmentExpectation, 0.f, kNormUnpunished);
    } else if (ctx.deed == Deed::Vandalize) {
        target.feel(Emotion::Sadness, kVandalSadness + 0.2f * harm);
        target.feel(Emotion::Anger, kUnknownThiefAnger + kUnknownThiefAngerHarm * harm);
    }
}

// A partner betrayed by an affair learns of it (they are left / they see it).
void reactAsBetrayed(Simulation& sim, Agent& betrayed, Agent& actor, const DeedContext& ctx) {
    const Tick now = sim.now();
    const Appraisal ap = appraise(betrayed, actor, ctx, sim);
    applyAppraisal(betrayed, actor, ap, now);
    betrayed.remember(MemoryType::Rejected, actor.id, now, kJiltedMemory);
    betrayed.feel(Emotion::Sadness, kJiltedSadness);
    betrayed.wantsRethink = true;
}

void convergeNorms(Norms& n, const Norms& toward, float rate) {
    n.theftTolerance = emaToward(n.theftTolerance, toward.theftTolerance, rate);
    n.violenceTolerance = emaToward(n.violenceTolerance, toward.violenceTolerance, rate);
    n.sharingExpectation = emaToward(n.sharingExpectation, toward.sharingExpectation, rate);
    n.punishmentExpectation = emaToward(n.punishmentExpectation, toward.punishmentExpectation, rate);
}

}  // namespace

// ===========================================================================
// Witnessing
// ===========================================================================
void broadcastDeedEx(Simulation& sim, Agent& actor, const DeedContext& ctx, Vec2i where, TargetAwareness awareness) {
    const Tick now = sim.now();
    // Mutable handle on the target (nullptr if none or dead — e.g. after a Kill).
    Agent* target = ctx.target ? sim.agent(ctx.target->id) : nullptr;
    if (target && (!target->alive || target == &actor)) target = nullptr;
    const AgentId victimId = ctx.target ? ctx.target->id
                                        : (ctx.targetStructure ? ctx.targetStructure->owner : kNoAgent);

    // Partners betrayed by an affair (computed before anyone reacts; partner links are unchanged here).
    Agent* betrayedA = nullptr;
    Agent* betrayedB = nullptr;
    if (ctx.deed == Deed::Adultery) {
        if (actor.partner != kNoAgent && (!ctx.target || actor.partner != ctx.target->id)) betrayedA = sim.agent(actor.partner);
        if (ctx.target && ctx.target->partner != kNoAgent && ctx.target->partner != actor.id)
            betrayedB = sim.agent(ctx.target->partner);
        if (betrayedB == betrayedA) betrayedB = nullptr;
    }

    int outraged = 0;
    bool told = false;  // a disapproving onlooker tells the target who did it
    for (Agent* o : sim.agentsNear(where, cfg::kWitnessRadius, actor.id)) {
        if (!o || !o->alive || o == &actor || o == target || o == betrayedA || o == betrayedB) continue;
        if (asleep(*o)) continue;
        const Appraisal ap = appraise(*o, actor, ctx, sim);
        applyAppraisal(*o, actor, ap, now);
        rememberWitnessed(*o, actor, ctx, victimId, ap, now);
        learnFromWatching(*o, ctx.deed, ap);
        if ((ctx.deed == Deed::Share || ctx.deed == Deed::Gift) && ap.approval > 0.f)
            o->feel(Emotion::Envy, kWatchEnvy * o->trait(Trait::Greed));
        if (ap.approval <= kOutrageApproval) ++outraged;
        if (target && awareness != TargetAwareness::Identifies && !told && ap.approval <= kTellApproval &&
            o->affinityFor(target->id) > kTellMinVictimAffinity &&
            sim.rng().chance(kTellChanceBase + kTellChanceScale * std::fabs(ap.approval)))
            told = true;
        bool rethink = ap.fear >= kRethinkFear || ap.anger >= kRethinkAnger;
        if (isViolentDeed(ctx.deed) && ctx.target && o->isFamily(*ctx.target)) rethink = true;  // defend kin
        if (rethink) o->wantsRethink = true;
    }

    // Betrayed partners: if they saw it, or the affair is out in the open / someone told them.
    for (Agent* b : {betrayedA, betrayedB}) {
        if (!b || !b->alive) continue;
        const bool saw = !asleep(*b) && chebyshev(b->pos, where) <= cfg::kWitnessRadius;
        if (awareness == TargetAwareness::Identifies || saw || told) reactAsBetrayed(sim, *b, actor, ctx);
    }

    if (target) {
        const TargetAwareness eff = told ? TargetAwareness::Identifies : awareness;
        if (eff == TargetAwareness::Identifies) reactAsTarget(sim, *target, actor, ctx);
        else if (eff == TargetAwareness::NoticesLoss) reactToUnknownCulprit(sim, *target, ctx);
    }

    // Rare reaction worth a log line: a crowd turning on a wrongdoer (assaults already log as fights).
    if (isTransgression(ctx.deed) && ctx.deed != Deed::Attack && outraged >= kCrowdLogSize) {
        sim.log(EventKind::Justice,
                std::to_string(outraged) + " onlookers turn against " + actor.name + " over " + deedPhrase(ctx.deed),
                actor.id, victimId);
    }
}

void broadcastDeed(Simulation& sim, Agent& actor, const DeedContext& ctx, Vec2i where) {
    broadcastDeedEx(sim, actor, ctx, where, TargetAwareness::Identifies);
}

// ===========================================================================
// Gossip
// ===========================================================================
void gossip(Simulation& sim, Agent& speaker, Agent& listener) {
    if (&speaker == &listener || !speaker.alive || !listener.alive) return;
    const Tick now = sim.now();

    // 1. Pick up to kGossipTopics third parties the speaker feels strongly about.
    struct Topic {
        AgentId id;
        float strength;
    };
    std::vector<Topic> topics;
    for (const auto& [id, r] : speaker.relations) {
        if (id == listener.id || id == speaker.id) continue;
        float strength = std::max(std::fabs(r.affinity), r.grievance) + std::fabs(r.trust - 0.5f);
        if (strength < kGossipMinStrength) continue;
        if (!sim.agent(id)) continue;  // no point gossiping about the dead
        strength *= sim.rng().range(0.7f, 1.3f);
        topics.push_back({id, strength});
    }
    const std::size_t nTopics = std::min<std::size_t>(topics.size(), std::size_t(kGossipTopics));
    std::partial_sort(topics.begin(), topics.begin() + std::ptrdiff_t(nTopics), topics.end(),
                      [](const Topic& a, const Topic& b) { return a.strength > b.strength; });

    // 2. The listener blends the speaker's opinions in, weighted by trust in the speaker.
    float baseW = kGossipBlend * listener.trustIn(speaker.id);
    if (listener.affinityFor(speaker.id) < kGossipHostileBelow) baseW *= kGossipHostileSpeaker;
    const bool kinSpeaker = listener.isFamily(speaker);
    for (std::size_t i = 0; i < nTopics; ++i) {
        const auto sit = speaker.relations.find(topics[i].id);
        if (sit == speaker.relations.end()) continue;
        const Relationship sr = sit->second;  // copy: listener.rel() below may touch other maps only
        Relationship& lr = listener.rel(topics[i].id);
        float w = baseW / (1.f + kGossipFirstHand * float(lr.interactions));
        if (sr.affinity < lr.affinity && lr.affinity > kGossipLoyalAbove) w *= kGossipLoyalty;
        lr.affinity = clampf(lr.affinity + w * (sr.affinity - lr.affinity), -1.f, 1.f);
        lr.trust = clamp01(lr.trust + w * (sr.trust - lr.trust));
        if (kinSpeaker && sr.grievance > lr.grievance)
            lr.grievance = clamp01(lr.grievance + w * kGossipKinGrievance * (sr.grievance - lr.grievance));
    }

    // 3. Talking bonds the two.
    {
        Relationship& a = speaker.rel(listener.id);
        a.affinity = clampf(a.affinity + kGossipBondAffinity * (0.5f + speaker.trait(Trait::Sociability)), -1.f, 1.f);
        a.trust = clamp01(a.trust + kGossipBondTrust);
        ++a.interactions;
        a.lastSeen = now;
    }
    {
        Relationship& b = listener.rel(speaker.id);
        b.affinity = clampf(b.affinity + kGossipBondAffinity * (0.5f + listener.trait(Trait::Sociability)), -1.f, 1.f);
        b.trust = clamp01(b.trust + kGossipBondTrust);
        ++b.interactions;
        b.lastSeen = now;
    }

    // 4. Partial norm convergence (culture spreads through conversation).
    const Norms speakerNorms = speaker.norms;
    convergeNorms(listener.norms, speakerNorms, kNormConvergeListener * listener.trustIn(speaker.id));
    convergeNorms(speaker.norms, listener.norms, kNormConvergeSpeaker * speaker.trustIn(listener.id));
}

// ===========================================================================
// Attraction & mate choice
// ===========================================================================
float attraction(const Agent& a, const Agent& b, const Simulation& sim) {
    if (a.id == b.id || !a.alive || !b.alive) return 0.f;
    const Tick now = sim.now();
    if (!a.isAdult(now) || !b.isAdult(now)) return 0.f;

    const float health = clamp01(b.health);
    const float mood = clamp01((b.mood() + 1.f) * 0.5f);
    const float gd = a.genome.distance(b.genome);
    const float genetic = clamp01(kGeneticFloor + (1.f - kGeneticFloor) * gd / kTypicalGeneDistance);
    const float foodStatus = clamp01(float(foodOwned(b, sim)) / std::max(1.f, dailyFoodUnits(b) * kStatusFoodDays));
    const float builtStatus = clamp01(float(b.stats.structuresBuilt) / kStatusBuilt);
    float bestSkill = 0.f;
    for (float s : b.skills.v) bestSkill = std::max(bestSkill, s);
    const float status = 0.5f * foodStatus + 0.3f * builtStatus + 0.2f * bestSkill;
    const Relationship* r = a.findRel(b.id);
    const float aff = clamp01(((r ? r->affinity : 0.f) + 1.f) * 0.5f);
    const float stored = r ? clamp01(r->attraction) : 0.f;
    const float grievance = r ? r->grievance : 0.f;

    const float heuristic = kAttrHealth * health + kAttrMood * mood + kAttrStatus * status + kAttrGenetic * genetic +
                            kAttrAffinity * aff;
    float v = clamp01(kAttrHeuristicShare * heuristic + kAttrStoredShare * stored);

    const float gap = float(std::abs(a.ageDays(now) - b.ageDays(now)));
    v *= 1.f / (1.f + gap / kAgeGapHalfDays);
    if (b.stage(now) == LifeStage::Elder) v *= kElderFactor;
    if (bloodRelated(a, b)) v *= kKinFactor;
    if (a.sex == b.sex) v *= kSameSexFactor;
    v *= 1.f - kAttrGrievance * clamp01(grievance);
    return clamp01(v);
}

bool acceptsAdvance(const Agent& chooser, const Agent& suitor, Simulation& sim) {
    if (chooser.id == suitor.id || !chooser.alive || !suitor.alive) return false;
    const Tick now = sim.now();
    if (!chooser.isAdult(now) || !suitor.isAdult(now)) return false;
    const float att = attraction(chooser, suitor, sim);
    if (att < kAccMinAttraction) return false;

    const Relationship* r = chooser.findRel(suitor.id);
    float x = att + kAccTrust * (chooser.trustIn(suitor.id) - 0.5f) + kAccMood * chooser.mood() +
              kAccLibido * (chooser.trait(Trait::Libido) - 0.5f) +
              kAccIntimacy * chooser.needs.urgency(Need::Intimacy) - kAccGrievance * (r ? r->grievance : 0.f);

    // Loyalty to a living partner.
    if (chooser.partner != kNoAgent && chooser.partner != suitor.id) {
        if (const Agent* p = sim.agent(chooser.partner)) {
            x -= kLoyaltyBase + kLoyaltyIntegrity * chooser.trait(Trait::Integrity) +
                 kLoyaltyLove * std::max(0.f, chooser.affinityFor(p->id));
        }
    }
    // Reluctance to break up someone else's bond.
    if (suitor.partner != kNoAgent && suitor.partner != chooser.id && sim.agent(suitor.partner))
        x -= kHomewreckIntegrity * chooser.trait(Trait::Integrity);

    return sim.rng().chance(logistic(x, kAcceptMid, kAcceptSteep));
}

float reputation(const Agent& a, const Simulation& sim) {
    float sum = 0.f;
    int n = 0;
    for (const auto& up : sim.agents()) {
        const Agent* o = up.get();
        if (!o || !o->alive || o->id == a.id) continue;
        if (const Relationship* r = o->findRel(a.id)) {
            sum += r->trust;
            ++n;
        }
    }
    return n > 0 ? sum / float(n) : 0.5f;
}

}  // namespace heur
