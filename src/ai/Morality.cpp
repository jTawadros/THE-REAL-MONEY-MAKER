// Heuristica — the heuristic conscience.
//
// judge():    how does an agent feel about committing a deed right now? A weighted sum of smooth terms
//             (harm, rule, necessity, kinship, reciprocity/revenge, fairness/envy, norms, witness risk,
//             emotions, prosocial reward). The rationale is the single biggest term that pushed the
//             verdict in its final direction, phrased in the first person.
// appraise(): how does an observer (bystander, victim, kin of victim, beneficiary) judge someone else's
//             deed, from the observer's own traits, norms, relationships and memories?
#include "ai/Morality.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string_view>

#include "agent/Agent.h"
#include "ai/Action.h"
#include "ai/ActionHelpers.h"
#include "core/Config.h"
#include "sim/Simulation.h"
#include "world/World.h"

namespace heur {

namespace {

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------
// Taboo strength of each deed (rule term = -Integrity * taboo).
constexpr float kTabooSteal = 0.60f;
constexpr float kTabooStealStorage = 0.55f;
constexpr float kTabooAttack = 0.80f;
constexpr float kTabooKill = 1.00f;
constexpr float kTabooVandalize = 0.50f;
constexpr float kTabooAdultery = 0.50f;

// Necessity: desperation = urgency(Food) * exp(-kSecurityDecay * days of food owned).
constexpr float kSecurityDecay = 0.9f;
constexpr float kNecessityMid = 0.55f;
constexpr float kNecessitySteep = 8.f;
constexpr float kNecessityRuleShrink = 0.65f;   // rule term *= 1 - this * necessity
constexpr float kNecessityHarmShrink = 0.45f;   // harm term *= 1 - this * necessity
constexpr float kNecessityGain = 0.40f;         // "I have to eat"

constexpr float kHarmWeight = 1.00f;
constexpr float kFamilyHarmMult = 2.0f;         // harm doubled for family
constexpr float kClanHarmMult = 1.3f;
constexpr float kFamilyLoyalty = 0.30f;         // flat kin aversion on top of doubled harm
constexpr float kRuleNormRelief = 0.80f;        // rule *= 1 - this * tolerance
constexpr float kNormTolGain = 0.35f;           // "everyone does it" (x taboo)
constexpr float kGreedGain = 0.35f;
constexpr float kRevengeGain = 0.80f;
constexpr float kDefenseGain = 0.70f;
constexpr float kDislikeGain = 0.30f;
constexpr float kFriendCost = 0.45f;
constexpr float kGratitudeCost = 0.35f;
constexpr float kNeedMoreCost = 0.50f;
constexpr float kWitnessWeight = 0.60f;
constexpr float kRetaliationCost = 0.25f;
constexpr float kGuiltCost = 0.40f;
constexpr float kAngerGain = 0.45f;
constexpr float kBreakdownPush = 0.35f;
constexpr float kFairnessBase = 0.10f;

// Prosocial reward.
constexpr float kCompassion = 0.90f;
constexpr float kKinGive = 0.30f;
constexpr float kFriendGive = 0.30f;
constexpr float kDutyGive = 0.30f;
constexpr float kOweGive = 0.30f;
constexpr float kUnrequitedCost = 0.25f;
constexpr float kRepGain = 0.10f;
constexpr float kJoyGive = 0.20f;
constexpr float kGreedKeep = 0.35f;
constexpr float kScarcityKeep = 0.55f;

constexpr float kMinUtility = -1.5f;
constexpr float kMaxUtility = 1.0f;

// ---------------------------------------------------------------------------
// Term bookkeeping: keep each contribution with a phrase template so the verdict can explain itself.
// ---------------------------------------------------------------------------
enum class Cat : std::uint8_t {
    Moral,    // conscience: harm, rules, kinship, loyalty  -> guilt/pride
    Justify,  // reasons that make it feel right: necessity, revenge, fairness, norms
    Desire,   // wants & emotions: greed, envy, anger, lust
    Risk,     // fear of being seen / of retaliation
    Social,   // reputation, affinity
};

struct Term {
    float v = 0.f;
    const char* why = "";
    Cat cat = Cat::Moral;
};

struct Terms {
    std::array<Term, 32> t{};
    int n = 0;
    void add(float v, const char* why, Cat c) {
        if (n < int(t.size()) && std::fabs(v) > 1e-4f) t[std::size_t(n++)] = Term{v, why, c};
    }
    float sum() const {
        float s = 0.f;
        for (int i = 0; i < n; ++i) s += t[std::size_t(i)].v;
        return s;
    }
    float sumIf(Cat c, bool positive) const {
        float s = 0.f;
        for (int i = 0; i < n; ++i) {
            const Term& x = t[std::size_t(i)];
            if (x.cat == c && (positive ? x.v > 0.f : x.v < 0.f)) s += x.v;
        }
        return s;
    }
    // Biggest term pushing in the direction of `sign` (or overall biggest if none).
    const Term* dominant(float sign) const {
        const Term* best = nullptr;
        for (int i = 0; i < n; ++i) {
            const Term& x = t[std::size_t(i)];
            if (x.v * sign <= 0.f) continue;
            if (!best || std::fabs(x.v) > std::fabs(best->v)) best = &x;
        }
        if (!best)
            for (int i = 0; i < n; ++i)
                if (!best || std::fabs(t[std::size_t(i)].v) > std::fabs(best->v)) best = &t[std::size_t(i)];
        return best;
    }
};

// Fills a phrase template with pronouns for `who` (null => they). Tokens:
// {he} {him} {his} {is} {has} {'s} {name} {x}
std::string fill(const char* tmpl, const Agent* who, std::string_view x = {}) {
    const bool they = who == nullptr;
    const bool female = who && who->sex == Sex::Female;
    std::string out;
    out.reserve(48);
    for (const char* p = tmpl; *p;) {
        if (*p != '{') {
            out += *p++;
            continue;
        }
        const char* end = std::strchr(p, '}');
        if (!end) {
            out += p;
            break;
        }
        const std::string_view tok(p + 1, std::size_t(end - p - 1));
        if (tok == "he") out += they ? "they" : (female ? "she" : "he");
        else if (tok == "him") out += they ? "them" : (female ? "her" : "him");
        else if (tok == "his") out += they ? "their" : (female ? "her" : "his");
        else if (tok == "is") out += they ? "are" : "is";
        else if (tok == "has") out += they ? "have" : "has";
        else if (tok == "'s") out += they ? "'re" : "'s";
        else if (tok == "name") out += who ? who->name : std::string("they");
        else if (tok == "x") out += x;
        p = end + 1;
    }
    return out;
}

float taboo(Deed d) {
    switch (d) {
        case Deed::StealFromAgent: return kTabooSteal;
        case Deed::StealFromStorage: return kTabooStealStorage;
        case Deed::Attack: return kTabooAttack;
        case Deed::Kill: return kTabooKill;
        case Deed::Vandalize: return kTabooVandalize;
        case Deed::Adultery: return kTabooAdultery;
        default: return 0.f;
    }
}

bool isTheft(Deed d) { return d == Deed::StealFromAgent || d == Deed::StealFromStorage; }
bool isViolence(Deed d) { return d == Deed::Attack || d == Deed::Kill; }
bool isProsocial(Deed d) { return d == Deed::Share || d == Deed::Gift || d == Deed::Comfort || d == Deed::Help; }

// The relevant norm tolerance for a transgression (0..1).
float tolerance(const Norms& n, Deed d) {
    if (isTheft(d)) return n.theftTolerance;
    if (isViolence(d)) return n.violenceTolerance;
    if (d == Deed::Vandalize) return 0.5f * (n.theftTolerance + n.violenceTolerance);
    return 0.f;
}

float severity(Deed d) {
    switch (d) {
        case Deed::StealFromAgent: return 0.8f;
        case Deed::StealFromStorage: return 0.75f;
        case Deed::Attack: return 1.0f;
        case Deed::Kill: return 1.3f;
        case Deed::Vandalize: return 0.7f;
        case Deed::Adultery: return 0.6f;
        case Deed::Punish: return 0.3f;
        default: return 0.f;
    }
}

bool asleep(const Agent& a) { return a.actionKind == kinds::Sleep; }

// Days of food an agent controls (inventory + owned storage/home).
float ownedFoodDays(const Agent& a, const Simulation& sim) {
    const float perDay = cfg::kFoodDecay * float(cfg::kTicksPerDay);
    return float(foodOwned(a, sim)) * cfg::kFoodPerUnit / perDay;
}

// 0..1: how much more food `owner` (and/or the structure) holds than a comfortable 3-day buffer.
float hoardOf(const Agent* owner, const Structure* st, const Simulation& sim) {
    float owned = 0.f;
    float need = 7.f;
    if (owner) {
        owned = float(foodOwned(*owner, sim));
        need = std::max(1.f, dailyFoodUnits(*owner) * 3.f);
    }
    if (st) {
        const float here = float(st->stock.get(Item::Food)) + (owner ? float(owner->inventory.get(Item::Food)) : 0.f);
        owned = std::max(owned, here);
    }
    return clamp01((owned - need) / (2.f * need + 4.f));
}

// What `who` knows about wrongs `culprit` committed (from episodic memory). 0..1.
struct Offense {
    float level = 0.f;
    bool againstMe = false;
    bool againstFamily = false;
    bool theft = false;
    bool violence = false;
};

Offense offenseBy(const Agent& who, AgentId culprit, const Simulation& sim) {
    Offense o;
    if (culprit == kNoAgent) return o;
    float notGuilty = 1.f;
    float alreadyPunished = 0.f;
    for (const Memory& m : who.memories) {
        if (m.other != culprit) continue;
        float w = 0.f;
        bool witnessed = false;
        switch (m.type) {
            case MemoryType::StolenFrom: w = 0.8f; o.againstMe = true; o.theft = true; break;
            case MemoryType::Attacked: w = 1.0f; o.againstMe = true; o.violence = true; break;
            case MemoryType::WitnessedTheft: w = 0.45f; witnessed = true; o.theft = true; break;
            case MemoryType::WitnessedAttack: w = 0.6f; witnessed = true; o.violence = true; break;
            case MemoryType::PunishedSomeone: alreadyPunished = std::max(alreadyPunished, m.intensity); continue;
            default: continue;
        }
        if (witnessed) {
            if (m.subject == who.id) {
                w *= 1.8f;
                o.againstMe = true;
            } else if (m.subject == kNoAgent) {
                w *= 0.8f;  // communal / unowned
            } else if (const Agent* v = sim.agent(m.subject)) {
                if (who.isFamily(*v)) {
                    w *= 1.6f;
                    o.againstFamily = true;
                } else {
                    w *= 1.f + 0.5f * std::max(0.f, who.affinityFor(v->id));
                }
            } else {
                w *= 1.2f;  // victim has since died
            }
        }
        notGuilty *= 1.f - clamp01(m.intensity * w);
    }
    o.level = clamp01((1.f - notGuilty) * (1.f - 0.7f * alreadyPunished));
    return o;
}

// Most recent strong Attacked memory by `attacker` within `window` ticks, scaled by recency. 0..1.
float recentAttackBy(const Agent& who, AgentId attacker, Tick now, Tick window) {
    float best = 0.f;
    for (auto it = who.memories.rbegin(); it != who.memories.rend(); ++it) {
        const Memory& m = *it;
        if (m.type != MemoryType::Attacked || m.other != attacker) continue;
        const Tick age = now >= m.when ? now - m.when : 0;
        if (age > window) break;  // memories are chronological
        best = std::max(best, m.intensity * (1.f - float(age) / float(window)));
    }
    return clamp01(0.4f + best) * (best > 0.f ? 1.f : 0.f);
}

// The phrase for "they wronged me", specialised by what they actually did. `first` frames it as
// retaliation ("...first") for transgressions; plain for punishment.
const char* grievancePhrase(const Agent& self, AgentId other, bool first = true) {
    for (auto it = self.memories.rbegin(); it != self.memories.rend(); ++it) {
        if (it->other != other) continue;
        if (it->type == MemoryType::StolenFrom) return first ? "{he} stole from me first" : "{he} stole from me";
        if (it->type == MemoryType::Attacked) return first ? "{he} hurt me first" : "{he} hurt me";
        if (it->type == MemoryType::Rejected) return "{he} humiliated me";
        if (it->type == MemoryType::Punished) return "{he} punished me";
    }
    return first ? "{he} wronged me first" : "{he} wronged me";
}

// How dangerous `t` would be to cross for `self` (0..1).
float dangerOf(const Agent& self, const Agent& t, Tick now) {
    float d = 0.40f * t.trait(Trait::Strength) + 0.35f * t.trait(Trait::Aggression) +
              0.25f * t.trait(Trait::Vengefulness) +
              0.30f * (t.trait(Trait::Strength) - self.trait(Trait::Strength)) +
              0.20f * (t.skills.get(Skill::Fighting) - self.skills.get(Skill::Fighting));
    d *= 0.4f + 0.6f * t.health;
    if (t.stage(now) == LifeStage::Child) d *= 0.3f;
    if (self.stage(now) == LifeStage::Child) d *= 1.5f;
    if (asleep(t)) d *= 0.25f;
    return clamp01(d);
}

// Shared per-judgement context.
struct View {
    const Agent& self;
    const DeedContext& d;
    const Simulation& sim;
    const Agent* T = nullptr;  // target, or the owner of the target structure
    Tick now = 0;
    float E = 0, I = 0, G = 0, A = 0, V = 0, B = 0, Soc = 0;
    float uFood = 0, ownDays = 0, desperation = 0, necessity = 0, poor = 0;
    // How far desperation excuses THIS deed: only deeds that actually relieve my need (selfGain) are
    // excused by hunger, and killing barely at all. (Starving people steal; they don't murder for nothing.)
    float excuse = 0;
    bool family = false, clan = false;
    float aff = 0, trust = 0.5f, balance = 0, grievance = 0, attraction = 0;
    float tNeed = 0;
    float seen = 0;  // 0..1 chance of being seen

    View(const Agent& s, const DeedContext& c, const Simulation& sm) : self(s), d(c), sim(sm) {
        now = sim.now();
        T = d.target;
        if (!T && d.targetStructure && d.targetStructure->owner != kNoAgent) T = sim.agent(d.targetStructure->owner);
        if (T == &self) T = nullptr;
        E = s.trait(Trait::Empathy);
        I = s.trait(Trait::Integrity);
        G = s.trait(Trait::Greed);
        A = s.trait(Trait::Aggression);
        V = s.trait(Trait::Vengefulness);
        B = s.trait(Trait::Bravery);
        Soc = s.trait(Trait::Sociability);
        uFood = s.needs.urgency(Need::Food);
        ownDays = ownedFoodDays(s, sim);
        desperation = uFood * std::exp(-kSecurityDecay * ownDays);
        necessity = logistic(desperation, kNecessityMid, kNecessitySteep);
        excuse = necessity * clamp01(1.6f * clamp01(d.selfGain)) * (d.deed == Deed::Kill ? 0.4f : 1.f);
        poor = 1.f - clamp01(ownDays / 4.f);
        if (T) {
            family = s.isFamily(*T);
            clan = !family && T->clan == s.clan;
            if (const Relationship* r = s.findRel(T->id)) {
                aff = r->affinity;
                trust = r->trust;
                balance = r->balance;
                grievance = r->grievance;
                attraction = r->attraction;
            }
            tNeed = T->needs.urgency(Need::Food);
        }
        seen = 1.f - std::exp(-0.5f * float(std::max(0, d.witnesses)));
    }
};

// Terms common to wronging someone: harm, kinship, friendship, gratitude, revenge, dislike, witnesses, guilt.
void addWrongingTerms(const View& w, Terms& t, float harmScale, bool physical) {
    const Agent& s = w.self;
    const float harm = clamp01(w.d.harm);
    if (w.T) {
        const float affMul = 1.f + 0.8f * std::max(0.f, w.aff) - 0.4f * std::max(0.f, -w.aff);
        const float base = -kHarmWeight * w.E * harm * harmScale * (1.f + w.tNeed) * (w.clan ? kClanHarmMult : 1.f) *
                           affMul * (1.f - kNecessityHarmShrink * w.excuse);
        t.add(base, w.tNeed > 0.4f ? "{he}{'s} hungry too" : (physical ? "I don't want to hurt {him}" : "it would hurt {him}"),
              Cat::Moral);
        if (w.family) t.add(base * (kFamilyHarmMult - 1.f) - kFamilyLoyalty, "{he}{'s} family", Cat::Moral);
        if (w.aff > 0.f) t.add(-kFriendCost * w.aff * (0.5f + 0.5f * w.E), "{he}{'s} my friend", Cat::Moral);
        if (w.aff < 0.f) t.add(kDislikeGain * -w.aff * (1.f - 0.5f * w.E), "I can't stand {him}", Cat::Desire);
        if (w.balance > 0.f)
            t.add(-kGratitudeCost * std::min(1.f, w.balance) * (0.4f + 0.6f * w.I), "{he} {has} been good to me",
                  Cat::Moral);
        if (w.grievance > 0.f)
            t.add(kRevengeGain * w.grievance * (0.3f + 0.7f * w.V), grievancePhrase(s, w.T->id), Cat::Justify);
        if (w.trust < 0.5f) t.add(0.25f * (0.5f - w.trust) * (1.f - 0.5f * w.I), "{he} would do the same to me", Cat::Justify);
    }
    // Witnesses: risk grows with eyes, shrinks with bravery, scaled by how often wrongs get punished here.
    const float sev = severity(w.d.deed);
    t.add(-kWitnessWeight * w.seen * (1.1f - w.B) * (0.3f + 0.7f * s.norms.punishmentExpectation) * sev *
              (0.8f + 0.4f * w.Soc),
          "too many eyes around", Cat::Risk);
    // Guilt already weighing on me.
    t.add(-kGuiltCost * s.emotions.get(Emotion::Guilt) * (0.5f + w.I), "my conscience is already heavy", Cat::Moral);
}

void addRuleTerms(const View& w, Terms& t, const char* phrase) {
    const Deed deed = w.d.deed;
    const float tab = taboo(deed);
    const float tol = tolerance(w.self.norms, deed);
    const float rule = -w.I * tab * (1.f - kNecessityRuleShrink * w.excuse) * (1.f - kRuleNormRelief * tol);
    t.add(rule, phrase, Cat::Moral);
    if (tol > 0.f) {
        const char* why = isTheft(deed) ? "everyone takes what they need here"
                                        : (isViolence(deed) ? "fights happen all the time here" : "nobody cares about that here");
        t.add(kNormTolGain * tol * tab, why, Cat::Justify);
    }
}

// ---------------------------------------------------------------------------
// judge(): per deed family
// ---------------------------------------------------------------------------
void judgeTheft(const View& w, Terms& t) {
    const Agent& s = w.self;
    const bool storage = w.d.deed == Deed::StealFromStorage;
    const float gain = clamp01(w.d.selfGain);
    const float hoard = hoardOf(w.T, storage ? w.d.targetStructure : nullptr, w.sim);

    // Necessity — "I have to eat" (combined phrase when the target is rich).
    t.add(kNecessityGain * w.necessity * (0.4f + 0.6f * gain),
          hoard > 0.3f ? "I'm starving and {he} {has} plenty" : "I have to eat", Cat::Justify);
    // Greed — wanting more than I need (muted when the gain is genuinely needed).
    t.add(kGreedGain * w.G * gain * (1.f - 0.7f * w.necessity), "I want what {he} {has}", Cat::Desire);
    // Fairness / envy — they hold far more than they need.
    const float envy = s.emotions.get(Emotion::Envy);
    t.add(hoard * (kFairnessBase + 0.3f * w.G + 0.25f * envy) * (0.3f + 0.7f * w.poor),
          storage ? "{he}{'s} hoarding food" : "{he} {has} more than enough", Cat::Justify);
    // They need it more than I do.
    if (w.T) {
        const float myNeed = w.uFood;
        t.add(-kNeedMoreCost * w.E * std::max(0.f, w.tNeed - myNeed), "{his} need is greater than mine", Cat::Moral);
    }
    addRuleTerms(w, t, "stealing is wrong");
    addWrongingTerms(w, t, storage ? 0.7f : 1.f, false);
    // The victim might notice and fight back (stealing from a pocket, not a storehouse).
    if (w.T && !storage)
        t.add(-kRetaliationCost * dangerOf(s, *w.T, w.now) * (1.1f - w.B), "{he} might fight back", Cat::Risk);
    if (s.inBreakdown() && s.breakType == BreakType::Kleptomania) t.add(kBreakdownPush, "I can't stop myself", Cat::Desire);
}

void judgeViolence(const View& w, Terms& t) {
    const Agent& s = w.self;
    const bool kill = w.d.deed == Deed::Kill;
    const float gain = clamp01(w.d.selfGain);
    if (w.T) {
        const float defend = recentAttackBy(s, w.T->id, w.now, Tick(cfg::kTicksPerDay / 4));
        t.add(kDefenseGain * defend * (0.6f + 0.4f * w.B), "I'm defending myself", Cat::Justify);
    }
    if (gain > 0.f) t.add(kNecessityGain * w.necessity * gain, "I have to eat", Cat::Justify);
    const float anger = s.emotions.get(Emotion::Anger);
    t.add(kAngerGain * anger * (0.4f + 0.6f * w.A), "I'm furious", Cat::Desire);
    t.add(0.25f * w.A * w.A, "I settle things with my fists", Cat::Desire);
    addRuleTerms(w, t, kill ? "killing is unforgivable" : "violence is wrong");
    addWrongingTerms(w, t, kill ? 1.5f : 1.f, true);
    if (w.T) {
        // Fear of losing the fight / of the target's revenge.
        t.add(-0.45f * dangerOf(s, *w.T, w.now) * (1.1f - w.B), "{he}{'s} stronger than me", Cat::Risk);
    }
    t.add(-0.35f * s.emotions.get(Emotion::Fear) * (1.f - w.B), "I'm too scared", Cat::Risk);
    if (s.inBreakdown() && s.breakType == BreakType::Rampage) t.add(kBreakdownPush, "I see red", Cat::Desire);
}

void judgeVandalize(const View& w, Terms& t) {
    const Agent& s = w.self;
    const Structure* st = w.d.targetStructure;
    const bool communal = st ? st->isCommunal() : (w.T == nullptr);
    const bool mine = st && (st->owner == s.id || s.home == st->id);
    addRuleTerms(w, t, "destroying things is wrong");
    if (w.T) {
        addWrongingTerms(w, t, 0.6f, false);
        t.add(-0.15f * w.E * clamp01(w.d.harm), "it belongs to {him}", Cat::Moral);
    } else {
        t.add(-kWitnessWeight * w.seen * (1.1f - w.B) * (0.3f + 0.7f * s.norms.punishmentExpectation) * severity(w.d.deed),
              "too many eyes around", Cat::Risk);
    }
    if (communal) {
        const bool contributed =
            st && std::find(st->contributors.begin(), st->contributors.end(), s.id) != st->contributors.end();
        t.add(-(0.15f + 0.35f * w.E) * (0.5f + s.norms.sharingExpectation) * clamp01(w.d.harm + 0.2f) *
                  (contributed ? 1.5f : 1.f),
              contributed ? "I helped build it" : "it belongs to everyone", Cat::Moral);
    }
    if (mine) t.add(-0.6f, "it's mine", Cat::Moral);
    const float envy = s.emotions.get(Emotion::Envy);
    if (w.T) t.add((0.3f * envy + 0.15f * w.G) * (0.4f + 0.6f * w.poor), "why should {he} have it all?", Cat::Desire);
    t.add(kAngerGain * s.emotions.get(Emotion::Anger) * (0.4f + 0.6f * w.A), "I'm furious", Cat::Desire);
    if (s.inBreakdown() && s.breakType == BreakType::Rampage) t.add(kBreakdownPush, "I see red", Cat::Desire);
}

// Returns false when nobody is betrayed (no partners involved).
bool judgeAdultery(const View& w, Terms& t, std::string& partnerName) {
    const Agent& s = w.self;
    const Agent* myPartner = (s.partner != kNoAgent && (!w.T || s.partner != w.T->id)) ? w.sim.agent(s.partner) : nullptr;
    const Agent* theirPartner =
        (w.T && w.T->partner != kNoAgent && w.T->partner != s.id) ? w.sim.agent(w.T->partner) : nullptr;
    if (!myPartner && !theirPartner) return false;

    if (myPartner) {
        partnerName = myPartner->name;
        const float affP = s.affinityFor(myPartner->id);
        const float grvP = s.findRel(myPartner->id) ? s.findRel(myPartner->id)->grievance : 0.f;
        t.add(-w.I * kTabooAdultery * 1.2f, "I'd betray {x}", Cat::Moral);
        if (affP > 0.f) t.add(-affP * (0.4f + 0.4f * w.I), "I love {x}", Cat::Moral);
        if (affP < 0.f) t.add(-affP * 0.3f, "{x} and I are through", Cat::Justify);
        if (grvP > 0.f) t.add(grvP * (0.2f + 0.4f * w.V), "{x} wronged me", Cat::Justify);
        t.add(-0.3f * w.E * (0.5f + std::max(0.f, affP)), "it would break {x}'s heart", Cat::Moral);
    } else {
        t.add(-w.I * kTabooAdultery * 0.8f, "cheating is wrong", Cat::Moral);
    }
    if (theirPartner) {
        if (partnerName.empty()) partnerName = theirPartner->name;
        const float affTP = s.affinityFor(theirPartner->id);
        t.add(-0.25f * w.E * (1.f + std::max(0.f, affTP)) - 0.15f * w.I, "{he}{'s} with {x}", Cat::Moral);
        // Crossing a jealous partner is dangerous.
        t.add(-0.2f * dangerOf(s, *theirPartner, w.now) * (1.1f - w.B), "{x} would come after me", Cat::Risk);
    }
    t.add(0.3f * s.trait(Trait::Libido) * (0.5f + s.needs.urgency(Need::Intimacy)), "I need someone", Cat::Desire);
    if (w.T) t.add(0.35f * w.attraction, "I want {him}", Cat::Desire);
    t.add(-kWitnessWeight * w.seen * (1.1f - w.B) * (0.3f + 0.7f * s.norms.punishmentExpectation) * severity(w.d.deed),
          "too many eyes around", Cat::Risk);
    t.add(-kGuiltCost * s.emotions.get(Emotion::Guilt) * (0.5f + w.I), "my conscience is already heavy", Cat::Moral);
    return true;
}

void judgeProsocial(const View& w, Terms& t) {
    const Agent& s = w.self;
    const Deed deed = w.d.deed;
    const float benefit = clamp01(w.d.benefit);
    const bool givesFood = deed == Deed::Share || deed == Deed::Gift;

    // How much the target needs this.
    float need = 0.3f;
    const char* compassionWhy = "it would help {him}";
    if (w.T) {
        switch (deed) {
            case Deed::Share:
                need = w.tNeed;
                compassionWhy = w.tNeed > w.uFood + 0.1f ? "{his} need is greater than mine" : "{he}{'s} hungry";
                break;
            case Deed::Gift:
                need = std::max({0.2f, 0.7f * w.tNeed, 0.5f * w.T->emotions.get(Emotion::Sadness)});
                compassionWhy = w.tNeed > 0.5f ? "{he}{'s} hungry" : "it would make {him} happy";
                break;
            case Deed::Comfort:
                need = std::max({w.T->stress, w.T->emotions.get(Emotion::Sadness), 0.8f * w.T->emotions.get(Emotion::Fear)});
                compassionWhy = "{he}{'s} hurting";
                break;
            case Deed::Help:
                need = 0.3f;
                compassionWhy = "many hands make light work";
                break;
            default: break;
        }
    } else if (deed == Deed::Help) {
        compassionWhy = "many hands make light work";
    }
    t.add(kCompassion * w.E * benefit * (0.3f + need), compassionWhy, Cat::Moral);

    // Kinship & friendship.
    if (w.T) {
        if (w.family) t.add(kKinGive * (0.5f + w.E), "{he}{'s} family", Cat::Moral);
        else if (w.clan) t.add(0.08f, "{he}{'s} one of us", Cat::Moral);
        if (w.aff > 0.f) t.add(kFriendGive * w.aff, "{he}{'s} my friend", Cat::Social);
        if (w.aff < 0.f) t.add(0.4f * w.aff * (1.f - 0.4f * w.E), "I don't like {him}", Cat::Social);
        // Reciprocity ledger: >0 they gave me more (I owe them), <0 they owe me.
        if (w.balance > 0.f) t.add(kOweGive * std::min(1.f, w.balance) * (0.5f + 0.5f * w.I), "I owe {him}", Cat::Moral);
        if (w.balance < 0.f)
            t.add(-kUnrequitedCost * std::min(1.f, -w.balance) * (0.5f + w.G), "{he} never paid me back", Cat::Justify);
        if (w.grievance > 0.f) t.add(-w.grievance * (0.3f + 0.5f * w.V), "{he} wronged me", Cat::Justify);
        if (deed == Deed::Gift) t.add(0.3f * w.attraction, "I'm sweet on {him}", Cat::Desire);
        // Making amends to someone I wronged.
        for (auto it = s.memories.rbegin(); it != s.memories.rend(); ++it) {
            if (it->other == w.T->id && (it->type == MemoryType::Stole || it->type == MemoryType::AttackedSomeone)) {
                t.add(0.35f * it->intensity * (0.4f + 0.6f * w.I) + 0.2f * s.emotions.get(Emotion::Guilt),
                      "I owe {him} for what I did", Cat::Moral);
                break;
            }
        }
    }
    // Culture of sharing.
    const float dutyScale = deed == Deed::Gift ? 0.5f : (deed == Deed::Comfort ? 0.6f : 1.f);
    t.add(kDutyGive * s.norms.sharingExpectation * dutyScale * (0.5f + 0.5f * w.I), "it's what we do here", Cat::Moral);
    if (deed == Deed::Help) {
        t.add(0.15f * s.trait(Trait::Industriousness), "I like to work", Cat::Desire);
        if (w.d.targetStructure && w.d.targetStructure->isCommunal())
            t.add(0.2f * s.norms.sharingExpectation + 0.05f, "it's for everyone", Cat::Moral);
    }
    // Reputation and mood.
    t.add(kRepGain * w.seen * (0.5f + w.Soc), "people will think well of me", Cat::Social);
    t.add(kJoyGive * s.emotions.get(Emotion::Joy), "I feel generous", Cat::Desire);
    if (s.emotions.get(Emotion::Guilt) > 0.f && deed != Deed::Help)
        t.add(0.15f * s.emotions.get(Emotion::Guilt), "I want to make amends", Cat::Moral);

    // Costs: greed and my own scarcity.
    if (givesFood) {
        t.add(-kGreedKeep * w.G, "I want to keep it", Cat::Desire);
        const float insecurity = 1.f - clamp01(w.ownDays / 3.f);
        t.add(-kScarcityKeep * (0.6f * w.uFood + 0.4f * insecurity), "I barely have enough myself", Cat::Risk);
    } else {
        t.add(-0.15f * w.G, deed == Deed::Help ? "I'd rather work for myself" : "what's in it for me?", Cat::Desire);
        t.add(-0.3f * std::max(w.uFood, s.needs.urgency(Need::Warmth)), "I have my own troubles", Cat::Risk);
    }
    if (s.inBreakdown() && s.breakType == BreakType::Despair) t.add(-0.3f, "I can't bring myself to care", Cat::Desire);
}

void judgeForgive(const View& w, Terms& t) {
    const Agent& s = w.self;
    if (!w.T || w.grievance < 0.03f) {
        t.add(-0.2f, "there's nothing to forgive", Cat::Moral);
        return;
    }
    t.add(0.30f * w.E, "everyone makes mistakes", Cat::Moral);
    t.add(0.35f * (1.f - w.V), "holding grudges only hurts me", Cat::Justify);
    if (w.family) t.add(0.25f + 0.15f * w.E, "{he}{'s} family", Cat::Moral);
    if (w.aff > 0.f) t.add(0.3f * w.aff, "{he}{'s} my friend", Cat::Social);
    t.add(0.10f * w.Soc + 0.08f * s.trait(Trait::Stability), "I want peace", Cat::Social);
    t.add(-w.grievance * (0.2f + 0.6f * w.V), "{he} {has}n't earned it", Cat::Justify);
    if (w.trust < 0.5f) t.add(-0.4f * (0.5f - w.trust), "I can't trust {him}", Cat::Risk);
    t.add(-0.3f * s.emotions.get(Emotion::Anger), "I'm still furious", Cat::Desire);
    // Still being wronged? Hard to forgive an ongoing attacker.
    t.add(-0.6f * recentAttackBy(s, w.T->id, w.now, Tick(cfg::kTicksPerDay / 4)), "{he} just attacked me", Cat::Risk);
}

void judgePunish(const View& w, Terms& t, Offense& off) {
    const Agent& s = w.self;
    if (!w.T) {
        t.add(-0.3f, "there's no one to punish", Cat::Moral);
        return;
    }
    off = offenseBy(s, w.T->id, w.sim);
    const float harm = clamp01(w.d.harm);
    const float justice = std::max(off.level, 0.6f * w.grievance);
    const char* justWhy = off.againstFamily ? "{he} hurt my family"
                          : off.againstMe   ? "{he} must answer for what {he} did to me"
                          : off.theft       ? "thieves must answer for it"
                          : off.violence    ? "{he}{'s} violent"
                                            : "{he} must answer for it";
    t.add(justice * (0.25f + 0.45f * w.I + 0.15f * (off.againstFamily ? 1.f : 0.f)), justWhy, Cat::Justify);
    if (w.grievance > 0.f) t.add(w.grievance * (0.2f + 0.6f * w.V), grievancePhrase(s, w.T->id, false), Cat::Justify);
    t.add(justice * 0.15f * s.norms.punishmentExpectation, "that's how justice works here", Cat::Justify);
    t.add(-0.45f * (1.f - logistic(justice, 0.2f, 12.f)) * (0.5f + 0.5f * w.I), "{he} {has}n't done anything",
          Cat::Moral);
    // Cost of hurting someone.
    t.add(-w.E * harm * 0.5f * (1.f + w.tNeed) * (1.f - 0.5f * justice), "I don't want to hurt {him}", Cat::Moral);
    if (harm > 0.3f)
        t.add(-w.I * 0.2f * harm * (1.f - w.self.norms.violenceTolerance) * (1.f - justice), "violence is wrong",
              Cat::Moral);
    if (w.family) t.add(-(0.2f + 0.3f * w.E) * (1.f - 0.4f * justice), "{he}{'s} family", Cat::Moral);
    if (w.aff > 0.f) t.add(-0.35f * w.aff, "{he}{'s} my friend", Cat::Moral);
    t.add(0.2f * s.emotions.get(Emotion::Anger) * (0.5f + w.A), "I'm furious", Cat::Desire);
    t.add(-0.35f * dangerOf(s, *w.T, w.now) * (1.1f - w.B), "{he}{'s} dangerous", Cat::Risk);
    // Public justice is reputable when deserved.
    t.add(0.12f * w.seen * (justice - 0.3f) * (0.5f + w.Soc), "people will see justice done", Cat::Social);
}

}  // namespace

// ============================================================================
// Public API
// ============================================================================
const char* deedName(Deed d) {
    switch (d) {
        case Deed::StealFromAgent: return "steal";
        case Deed::StealFromStorage: return "steal from storage";
        case Deed::Attack: return "attack";
        case Deed::Kill: return "kill";
        case Deed::Vandalize: return "vandalize";
        case Deed::Share: return "share";
        case Deed::Gift: return "gift";
        case Deed::Comfort: return "comfort";
        case Deed::Punish: return "punish";
        case Deed::Forgive: return "forgive";
        case Deed::Help: return "help";
        case Deed::Adultery: return "adultery";
        default: return "?";
    }
}

bool isTransgression(Deed d) {
    return d == Deed::StealFromAgent || d == Deed::StealFromStorage || d == Deed::Attack || d == Deed::Kill ||
           d == Deed::Vandalize || d == Deed::Adultery;
}

Verdict judge(const Agent& self, const DeedContext& ctx, const Simulation& sim) {
    const View w(self, ctx, sim);
    Terms t;
    Verdict v;
    std::string extra;
    Offense off;

    switch (ctx.deed) {
        case Deed::StealFromAgent:
        case Deed::StealFromStorage: judgeTheft(w, t); break;
        case Deed::Attack:
        case Deed::Kill: judgeViolence(w, t); break;
        case Deed::Vandalize: judgeVandalize(w, t); break;
        case Deed::Adultery:
            if (!judgeAdultery(w, t, extra)) {
                v.rationale = "no one is betrayed";
                return v;
            }
            break;
        case Deed::Share:
        case Deed::Gift:
        case Deed::Comfort:
        case Deed::Help: judgeProsocial(w, t); break;
        case Deed::Forgive: judgeForgive(w, t); break;
        case Deed::Punish: judgePunish(w, t, off); break;
        default: break;
    }

    v.utility = clampf(t.sum(), kMinUtility, kMaxUtility);

    // ---- Guilt / pride / risk ----
    const float conscience = 0.5f + 0.5f * std::max(w.E, w.I);
    const float moralCost = -t.sumIf(Cat::Moral, false);
    const float moralGood = t.sumIf(Cat::Moral, true);
    const float justify = t.sumIf(Cat::Justify, true);
    if (isTransgression(ctx.deed)) {
        v.guilt = clamp01(std::max(0.f, moralCost - 0.6f * justify) * conscience * 1.3f);
        // Righteous revenge / self-defence can feel like vindication.
        v.pride = clamp01(std::max(0.f, justify - moralCost) * 0.4f * (0.3f + 0.7f * w.V));
    } else if (ctx.deed == Deed::Punish) {
        const float justice = std::max(off.level, 0.6f * w.grievance);
        v.guilt = clamp01(std::max(0.f, moralCost - 0.5f * justify) * conscience);
        v.pride = clamp01(justice * (0.2f + 0.5f * w.I) * (1.f - 0.5f * clamp01(moralCost)));
    } else if (ctx.deed == Deed::Forgive) {
        v.pride = w.grievance >= 0.03f ? clamp01((0.15f + 0.35f * w.E + 0.2f * (1.f - w.V)) * (0.5f + w.grievance)) : 0.f;
    } else if (isProsocial(ctx.deed)) {
        const float social = t.sumIf(Cat::Social, true);
        v.pride = clamp01((moralGood * (0.5f + 0.6f * w.E) + 0.5f * social) * 0.8f);
    }

    if (isTransgression(ctx.deed) || ctx.deed == Deed::Punish) {
        float danger = 0.f;
        if (w.T && (ctx.deed == Deed::StealFromAgent || isViolence(ctx.deed) || ctx.deed == Deed::Punish))
            danger = dangerOf(self, *w.T, w.now);
        v.risk = clamp01(w.seen * (0.3f + 0.7f * self.norms.punishmentExpectation) * severity(ctx.deed) + 0.3f * danger);
    }

    // ---- Rationale: the biggest term pushing the verdict in its final direction ----
    const float sign = v.utility >= 0.f ? 1.f : -1.f;
    if (const Term* top = t.dominant(sign)) v.rationale = fill(top->why, w.T, extra);
    return v;
}

Appraisal appraise(const Agent& observer, const Agent& actor, const DeedContext& ctx, const Simulation& sim) {
    Appraisal a;
    const Deed deed = ctx.deed;
    const Agent& O = observer;
    if (O.id == actor.id) return a;

    const Tick now = sim.now();
    const Agent* T = ctx.target;
    if (!T && ctx.targetStructure && ctx.targetStructure->owner != kNoAgent) T = sim.agent(ctx.targetStructure->owner);
    const bool isTarget = T && T->id == O.id;
    const bool victimKin = T && !isTarget && O.isFamily(*T);
    const bool actorKin = O.isFamily(actor);

    const float E = O.trait(Trait::Empathy);
    const float I = O.trait(Trait::Integrity);
    const float V = O.trait(Trait::Vengefulness);
    const float Ag = O.trait(Trait::Aggression);
    const float B = O.trait(Trait::Bravery);
    const float G = O.trait(Trait::Greed);
    const float affA = O.affinityFor(actor.id);
    const float affT = (T && !isTarget) ? O.affinityFor(T->id) : 0.f;
    const Relationship* relT = (T && !isTarget) ? O.findRel(T->id) : nullptr;
    const float grvT = relT ? relT->grievance : 0.f;
    const float trustT = (T && !isTarget) ? O.trustIn(T->id) : 0.5f;
    const float harm = clamp01(ctx.harm);
    const float benefit = clamp01(ctx.benefit);
    const float tNeed = T ? T->needs.urgency(Need::Food) : 0.f;
    // Friends and kin get the benefit of the doubt.
    const float partisan = 0.35f * affA + (actorKin ? 0.25f : 0.f);

    // Structure deeds: the observer's own home/storage or a communal thing they helped build counts as theirs.
    bool ownStructure = false, contributed = false;
    if (const Structure* st = ctx.targetStructure) {
        ownStructure = st->owner == O.id || O.home == st->id;
        contributed = std::find(st->contributors.begin(), st->contributors.end(), O.id) != st->contributors.end();
    }
    const bool victim = isTarget || ((deed == Deed::StealFromStorage || deed == Deed::Vandalize) && ownStructure);

    if (deed == Deed::Adultery) {
        const bool actorBetrays = actor.partner != kNoAgent && (!T || actor.partner != T->id);
        const bool targetBetrays = T && T->partner != kNoAgent && T->partner != actor.id;
        if (!actorBetrays && !targetBetrays) return a;
        const bool iAmBetrayed = (actorBetrays && actor.partner == O.id) || (targetBetrays && T->partner == O.id);
        if (isTarget) {
            a.approval = 0.1f;
            a.affinityDelta = 0.05f;
            return a;
        }
        if (iAmBetrayed) {
            const bool myPartnerCheated = actor.partner == O.id;
            a.approval = myPartnerCheated ? -0.9f : -0.7f;
            a.anger = clamp01((0.5f + 0.3f * Ag + 0.2f * V) * (myPartnerCheated ? 1.f : 0.9f));
            a.grievance = clamp01(0.45f + 0.4f * V);
            a.affinityDelta = myPartnerCheated ? -0.45f : -0.35f;
            a.trustDelta = myPartnerCheated ? -0.4f : -0.25f;
            return a;
        }
        const Agent* betrayed = actorBetrays ? sim.agent(actor.partner) : (targetBetrays ? sim.agent(T->partner) : nullptr);
        const bool kinOfBetrayed = betrayed && O.isFamily(*betrayed);
        const float affB = betrayed ? O.affinityFor(betrayed->id) : 0.f;
        a.approval = clampf(-I * 0.5f - (kinOfBetrayed ? 0.3f : 0.f) - 0.2f * std::max(0.f, affB) + 0.5f * partisan +
                                0.1f * O.norms.theftTolerance,
                            -1.f, 1.f);
        a.anger = clamp01(kinOfBetrayed ? 0.25f + 0.2f * V : std::max(0.f, -a.approval) * 0.2f);
        a.grievance = kinOfBetrayed ? clamp01(0.15f + 0.2f * V) : 0.f;
        a.affinityDelta = clampf(a.approval * 0.15f, -0.5f, 0.3f);
        a.trustDelta = clampf(-0.1f * I - (kinOfBetrayed ? 0.1f : 0.f), -0.4f, 0.2f);
        return a;
    }

    if (isTransgression(deed)) {  // theft, violence, vandalism
        const float tab = taboo(deed);
        const float tol = tolerance(O.norms, deed);
        const bool kill = deed == Deed::Kill;
        const bool theft = isTheft(deed);
        const bool violent = isViolence(deed);

        // How bad does it look to me?
        const float care = clampf(1.f + 1.2f * std::max(0.f, affT) - 0.9f * std::max(0.f, -affT) - 0.6f * grvT +
                                      (victimKin ? 1.f : 0.f),
                                  0.f, 3.f);
        float ruleBad = tab * (0.35f + 0.65f * I) * (1.f - 0.7f * tol);
        float harmBad = E * harm * (1.f + tNeed) * care * 0.8f;
        if (deed == Deed::Vandalize && ctx.targetStructure && ctx.targetStructure->isCommunal())
            harmBad += 0.25f * (0.3f + harm) * (0.5f + O.norms.sharingExpectation) * (contributed ? 1.6f : 1.f);

        // Excuses I might see.
        float excuse = 0.f;
        const float actorNeed = actor.needs.urgency(Need::Food);  // starving people look it
        excuse += E * logistic(actorNeed, 0.6f, 8.f) * 0.45f * (theft ? 1.f : 0.3f);
        if (theft) {
            const float hoard = hoardOf(T, deed == Deed::StealFromStorage ? ctx.targetStructure : nullptr, sim);
            excuse += hoard * (0.1f + 0.25f * O.norms.theftTolerance + 0.15f * G) * (1.f - 0.5f * I);
        }
        if (T && !isTarget) {
            // Victim had it coming: I know they wronged the actor, I resent / hate / distrust them.
            for (const Memory& m : O.memories) {
                if (m.other == T->id && m.subject == actor.id &&
                    (m.type == MemoryType::WitnessedTheft || m.type == MemoryType::WitnessedAttack)) {
                    excuse += 0.5f * m.intensity * (0.4f + 0.6f * V);
                    break;
                }
            }
            if (violent) {
                // Self-defence: the "victim" attacked the actor first, recently, in front of me.
                for (auto it = O.memories.rbegin(); it != O.memories.rend(); ++it) {
                    if (it->type == MemoryType::WitnessedAttack && it->other == T->id && it->subject == actor.id &&
                        now - it->when < Tick(cfg::kTicksPerDay / 4)) {
                        excuse += 0.6f * it->intensity + 0.2f;
                        break;
                    }
                }
            }
            excuse += 0.4f * grvT + 0.5f * std::max(0.f, -affT) + 0.3f * std::max(0.f, 0.5f - trustT);
        }
        if (kill) excuse *= 0.5f;

        float approval = -(ruleBad + harmBad) + excuse + partisan;
        if (victim) {
            approval = std::min(approval, 0.f) - (0.35f + 0.4f * harm + 0.2f * tab) + 0.25f * std::max(0.f, affA);
        }
        a.approval = clampf(approval, -1.f, 1.f);

        // Feelings, grievances, relationship change.
        if (victim) {
            // Did I have it coming? Knowing I recently wronged the actor (and feeling guilty about it)
            // blunts the grudge — this is what lets blood feuds burn out instead of escalating forever.
            // Tit-for-tat ("I hit him first") is recognised as fair by almost everyone; having robbed
            // them is recognised mostly by the principled.
            float stole = 0.f, struck = 0.f;
            for (auto it = O.memories.rbegin(); it != O.memories.rend(); ++it) {
                if (now - it->when > Tick(3 * cfg::kTicksPerDay)) break;
                if (it->other != actor.id) continue;
                if (it->type == MemoryType::Stole) stole += it->intensity;
                if (it->type == MemoryType::AttackedSomeone) struck += it->intensity;
            }
            const float deserved = clamp01(clamp01(stole) * (0.4f + 0.6f * I) + clamp01(struck) * (0.75f + 0.25f * I) +
                                           0.3f * O.emotions.get(Emotion::Guilt));
            const float softened = (1.f - 0.4f * std::max(0.f, affA) - (actorKin ? 0.2f : 0.f)) * (1.f - 0.75f * deserved);
            a.anger = clamp01((0.35f + 0.5f * harm + 0.15f * tab) * (0.6f + 0.4f * Ag + 0.3f * V) * softened);
            a.grievance = clamp01((0.25f + 0.6f * harm + 0.15f * tab) * (0.5f + V) * softened);
            a.fear = violent ? clamp01(harm * (1.2f - B) * 0.7f) : clamp01(0.05f + 0.1f * harm * (1.f - B));
            a.affinityDelta = -(0.15f + 0.25f * harm + 0.1f * tab) * softened;
            a.trustDelta = -(0.2f + 0.2f * harm) * (0.7f + 0.3f * softened);
        } else {
            const float outrage = std::max(0.f, -a.approval);
            a.anger = clamp01(outrage * (0.25f + 0.35f * Ag + 0.2f * I) + (victimKin ? 0.25f + 0.5f * harm * tab : 0.f) +
                              (kill && victimKin ? 0.5f : 0.f));
            // Third-party grudges shrink when I can see the actor had a reason (feud damping).
            const float understood = 1.f - (kill ? 0.3f : 0.65f) * clamp01(excuse);
            if (victimKin)
                a.grievance = understood * (kill ? clamp01(0.7f + 0.3f * V) : clamp01((0.15f + 0.5f * harm) * tab * (0.5f + V)));
            else if (affT > 0.5f)
                a.grievance = understood * clamp01(0.5f * (0.15f + 0.5f * harm) * tab * V * (kill ? 2.f : 1.f));
            if (violent) a.fear = clamp01(harm * (1.f - B) * 0.5f * (kill ? 1.6f : 1.f) * (1.f - 0.5f * std::max(0.f, affA)));
            a.affinityDelta = a.approval * 0.25f - (victimKin ? 0.15f * tab : 0.f);
            a.trustDelta = -0.05f - 0.15f * tab * (1.f - 0.5f * clamp01(excuse)) + 0.1f * std::max(0.f, a.approval) -
                           (victimKin ? 0.1f : 0.f);
        }
        if (kill) {
            a.affinityDelta -= 0.1f;
            a.trustDelta -= 0.1f;
        }
        a.affinityDelta = clampf(a.affinityDelta, -0.5f, 0.3f);
        a.trustDelta = clampf(a.trustDelta, -0.4f, 0.2f);
        return a;
    }

    switch (deed) {
        case Deed::Share:
        case Deed::Gift:
        case Deed::Comfort:
        case Deed::Help: {
            float need = tNeed;
            if (deed == Deed::Comfort && T) need = std::max(T->stress, T->emotions.get(Emotion::Sadness));
            if (deed == Deed::Help) need = 0.3f;
            if (isTarget) {
                const float gratitude = 0.6f + 0.4f * O.trait(Trait::Sociability);
                a.approval = clampf((0.4f + 0.5f * benefit * (0.5f + need)) * gratitude, -1.f, 1.f);
                a.affinityDelta = clampf((0.08f + 0.2f * benefit * (0.5f + need)) * gratitude, -0.5f, 0.3f);
                a.trustDelta = clampf(0.05f + 0.15f * benefit, -0.4f, 0.2f);
                return a;
            }
            float approval = 0.1f + E * benefit * (0.3f + need) * 0.6f + 0.25f * O.norms.sharingExpectation +
                             0.2f * std::max(0.f, affT) - 0.35f * std::max(0.f, -affT) + 0.3f * partisan;
            if (victimKin) approval += 0.2f;  // kindness to my family
            if (deed == Deed::Share) approval -= 0.15f * G * (1.f - E);
            if (deed == Deed::Help && ctx.targetStructure && ctx.targetStructure->isCommunal())
                approval += 0.15f + (contributed ? 0.1f : 0.f);
            // Jealousy: someone courting my partner, or my partner courting someone else.
            if (deed == Deed::Gift && T) {
                if (O.partner != kNoAgent && T->id == O.partner) {
                    approval -= 0.4f + 0.3f * std::max(0.f, T->affinityFor(actor.id));
                    a.anger = clamp01(0.25f * (0.5f + Ag));
                } else if (O.partner != kNoAgent && actor.id == O.partner) {
                    approval -= 0.3f;
                    a.anger = clamp01(0.2f * (0.5f + Ag));
                }
            }
            a.approval = clampf(approval, -1.f, 1.f);
            a.affinityDelta = clampf(a.approval * 0.12f, -0.5f, 0.3f);
            a.trustDelta = clampf(a.approval * 0.08f, -0.4f, 0.2f);
            return a;
        }
        case Deed::Punish: {
            // Did the punished deserve it, from what I know?
            const AgentId punished = T ? T->id : kNoAgent;
            const Offense off = offenseBy(O, punished, sim);
            if (isTarget) {
                const float guilt = O.emotions.get(Emotion::Guilt);
                const float accept = clamp01(I * std::min(1.f, guilt * 2.f));  // "I had it coming"
                a.approval = clampf(-0.2f - 0.4f * harm + 0.5f * accept, -1.f, 1.f);
                a.anger = clamp01((0.2f + 0.4f * harm) * (0.5f + 0.5f * Ag) * (1.f - 0.5f * accept));
                a.fear = clamp01(harm * (1.f - B) * 0.4f);
                a.grievance = clamp01((0.1f + 0.4f * harm) * (0.5f + V) * (1.f - 0.6f * accept));
                a.affinityDelta = clampf(-(0.1f + 0.25f * harm) * (1.f - 0.5f * accept), -0.5f, 0.3f);
                a.trustDelta = clampf(-0.05f - 0.1f * harm + 0.1f * accept, -0.4f, 0.2f);
                return a;
            }
            // What I know about the punished, plus benefit of the doubt for a punisher I trust.
            const float deserved = clamp01(1.5f * off.level + 0.8f * grvT + 0.6f * std::max(0.f, 0.5f - trustT) +
                                           0.2f * std::max(0.f, -affT) + 0.25f * O.trustIn(actor.id));
            float approval = deserved * (0.25f + 0.4f * I + 0.25f * O.norms.punishmentExpectation) -
                             (1.f - deserved) * (0.3f + 0.4f * E) - E * harm * 0.25f * (1.f - 0.5f * deserved) +
                             0.5f * partisan - 0.4f * std::max(0.f, affT) * (1.f - deserved);
            if (off.againstMe) approval += 0.3f;  // someone stood up for me
            if (victimKin) approval -= 0.2f + 0.3f * harm - 0.3f * deserved * I;
            a.approval = clampf(approval, -1.f, 1.f);
            if (victimKin) {
                a.anger = clamp01((0.15f + 0.4f * harm) * (1.f - deserved) * (0.5f + 0.5f * Ag));
                a.grievance = clamp01((0.1f + 0.3f * harm) * (1.f - deserved) * (0.5f + V));
            } else {
                a.anger = clamp01(std::max(0.f, -a.approval) * 0.25f * (0.5f + Ag));
            }
            a.fear = clamp01(harm * (1.f - B) * 0.2f);
            a.affinityDelta = clampf(a.approval * 0.2f + (off.againstMe ? 0.05f : 0.f), -0.5f, 0.3f);
            a.trustDelta = clampf(a.approval * 0.15f, -0.4f, 0.2f);
            return a;
        }
        case Deed::Forgive: {
            if (isTarget) {
                const float guilt = O.emotions.get(Emotion::Guilt);
                a.approval = clampf(0.4f + 0.3f * guilt, -1.f, 1.f);
                a.affinityDelta = clampf(0.15f + 0.1f * guilt, -0.5f, 0.3f);
                a.trustDelta = clampf(0.08f, -0.4f, 0.2f);
                return a;
            }
            const float approval = 0.1f + 0.3f * E - 0.25f * V + 0.2f * std::max(0.f, affT) - 0.3f * grvT +
                                   0.3f * partisan;
            a.approval = clampf(approval, -1.f, 1.f);
            a.affinityDelta = clampf(a.approval * 0.1f, -0.5f, 0.3f);
            a.trustDelta = clampf(a.approval * 0.05f, -0.4f, 0.2f);
            return a;
        }
        default: break;
    }
    return a;
}

}  // namespace heur
