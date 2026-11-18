// Heuristica — exchange behaviours: Share (food to the hungry), Gift (friends, partner, debts,
// courtship), Trade (barter), FeedChild (parents), Beg (hungry children / desperate humble adults).
#include <algorithm>
#include <cmath>
#include <cstdint>
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
// ---- Giving (shared by Share / Gift / FeedChild) --------------------------------------
constexpr int kGiveGiveUpTicks = 60;          // couldn't reach the recipient
constexpr float kCourtGiftAttraction = 0.12f; // recipient's attraction to a suitor who brings gifts
constexpr float kGiveJoy = 0.05f;

// ---- Share ----------------------------------------------------------------------------
constexpr float kShareMinNeed = 0.30f;        // recipient urgency(Food) to consider sharing
constexpr int kShareMaxRecipientFood = 1;     // recipients already carrying more food can feed themselves
constexpr int kShareMaxUnits = 4;
constexpr float kShareBase = 0.10f;
constexpr float kShareNeedWeight = 0.35f;     // × recipient urgency(Food)
constexpr float kShareDistHalf = 8.f;
constexpr float kShareSacrificeEmpathy = 0.6f;// above this, share even without surplus...
constexpr float kShareSacrificeMaxHunger = 0.3f;  // ...as long as I'm not hungry myself
constexpr float kShareSacrificeKeepDays = 0.5f;

// ---- Gift -----------------------------------------------------------------------------
constexpr Tick kGiftCooldown = Tick(cfg::kTicksPerDay);  // after any gift/share I've given
constexpr float kGiftMinBenefit = 0.30f;      // friend/partner gifts must actually help (repay/court exempt)
constexpr float kGiftRepayMin = 0.10f;        // balance (I owe them) needed to want to repay
constexpr float kGiftRepay = 0.45f;           // × min(1, balance) × conscientiousness
constexpr float kGiftFriendMin = 0.35f;       // affinity
constexpr float kGiftFriend = 0.40f;          // × (affinity - min) × (0.4 + Empathy)
constexpr float kGiftPartner = 0.08f;         // × (0.5 + Empathy)
constexpr float kGiftCourtMinAttraction = 0.35f;
constexpr float kGiftCourt = 0.50f;           // × attraction × urgency(Intimacy) × (0.5 + Libido)
constexpr float kGiftMoralWeight = 0.5f;      // × judge(Gift).utility
constexpr float kGiftDistHalf = 8.f;
constexpr int kGiftMaxFood = 3;

// ---- Trade ----------------------------------------------------------------------------
constexpr float kTradeMinWant = 0.25f;        // my want for an item to look for a trade
constexpr float kTradeMinPartnerWant = 0.10f; // their want for what I offer
constexpr float kTradeMinTrust = 0.35f;       // both ways
constexpr float kTradeMinAffinity = -0.3f;
constexpr float kTradeWantRatio = 0.6f;       // they must want my item ≥ this × the item they give
constexpr float kTradeFairness = 0.9f;        // they want ≥ this × value back
constexpr int kTradeMaxGet = 4;
constexpr float kTradeFoodComfortDays = 3.f;
constexpr float kTradeBase = 0.12f;
constexpr float kTradeWantWeight = 0.45f;     // × my want
constexpr float kTradeTrustWeight = 0.05f;    // × (trust - 0.5)
constexpr float kTradeDistHalf = 8.f;
constexpr int kTradeGiveUpTicks = 50;
constexpr float kTradeAffinity = 0.03f;
constexpr float kTradeTrust = 0.03f;
constexpr float kTradePractice = 1.0f;        // Socializing per deal

// ---- FeedChild ------------------------------------------------------------------------
constexpr float kFeedMinUrgency = 0.25f;
constexpr float kFeedWakeUrgency = 0.6f;      // wake a sleeping child only when this hungry
constexpr int kFeedSearch = 30;
constexpr int kFeedMaxUnits = 4;
constexpr float kFeedBase = 0.30f;
constexpr float kFeedUrgencyWeight = 0.90f;   // × child urgency(Food)
constexpr float kFeedEmpathyLo = 0.5f;        // × (lo + Empathy)
constexpr float kFeedDistHalf = 12.f;
constexpr float kFeedSelfishHunger = 0.6f;    // a starving low-empathy parent keeps 1 unit
constexpr float kFeedChildAffinity = 0.03f;
constexpr float kFeedChildTrust = 0.02f;

// ---- Beg ------------------------------------------------------------------------------
constexpr float kBegChildMinHunger = 0.30f;
constexpr float kBegAdultMinHunger = 0.60f;
constexpr float kBegMaxPride = 0.40f;         // adults too proud (Pride emotion) won't beg
constexpr int kBegMinGiverFood = 2;
constexpr Tick kBegRefusalCooldown = 120;     // don't beg from someone who just refused
constexpr float kBegChildWeight = 0.80f;      // × hunger
constexpr float kBegAdultWeight = 0.55f;
constexpr float kBegKindTrust = 0.35f;        // perceived kindness terms
constexpr float kBegKindAffinity = 0.35f;
constexpr float kBegKindFamily = 0.30f;
constexpr float kBegKindGenerous = 0.20f;     // they've given me before (balance > 0)
constexpr float kBegDistHalf = 8.f;
constexpr int kBegTicks = 3;
constexpr int kBegGiveUpTicks = 40;
constexpr float kBegFaceToFace = 0.15f;       // giver: hard to say no to someone's face
constexpr float kBegChildPull = 0.20f;        // giver: a hungry child tugs at you
constexpr float kBegAffinity = 0.20f;         // giver: × affinity for the beggar
constexpr float kBegGiverHunger = 0.30f;      // giver: × own urgency(Food)
constexpr float kBegSteep = 6.f;
constexpr int kBegMaxFood = 3;
constexpr float kBegRefusedSadness = 0.08f;
constexpr float kBegRefusedNorm = 0.04f;      // refused beggars expect less sharing (EMA toward 0)

// ===========================================================================
// Item heuristics
// ===========================================================================
const Structure* projectOf(const Agent& a, const Simulation& sim) {
    if (a.project == kNoStructure) return nullptr;
    const Structure* s = sim.world().structure(a.project);
    return (s && !s->complete) ? s : nullptr;
}

int projectNeeds(const Agent& a, Item i, const Simulation& sim) {
    const Structure* p = projectOf(a, sim);
    return p ? std::max(0, p->materialsMissing(i) - a.inventory.get(i)) : 0;
}

// How much `a` wants more of item i (0..1).
float wantFor(const Agent& a, Item i, const Simulation& sim) {
    const int have = a.inventory.get(i);
    switch (i) {
        case Item::Food: {
            const float days = float(foodOwned(a, sim)) / std::max(0.5f, dailyFoodUnits(a));
            return clamp01(0.7f * std::max(0.f, 1.f - days / kTradeFoodComfortDays) + 0.5f * a.needs.urgency(Need::Food));
        }
        case Item::Wood:
            return clamp01((projectNeeds(a, i, sim) > 0 ? 0.5f : 0.f) +
                           (have == 0 ? 0.25f * a.needs.urgency(Need::Warmth) + 0.15f * a.trait(Trait::Industriousness) : 0.f));
        case Item::Stone:
            return clamp01((projectNeeds(a, i, sim) > 0 ? 0.5f : 0.f) +
                           (have == 0 ? 0.12f * a.trait(Trait::Industriousness) : 0.f));
        case Item::Tools: return have == 0 ? 0.25f + 0.3f * a.trait(Trait::Industriousness) : 0.f;
        case Item::Herbs:
            return have == 0 ? clamp01(0.12f + (a.health < 0.75f ? 1.2f * (1.f - a.health) : 0.f)) : 0.f;
        case Item::Seeds: return have == 0 ? 0.1f * a.trait(Trait::Industriousness) : 0.f;
        default: return 0.f;
    }
}

// Units of item i `a` could part with.
int surplusOf(const Agent& a, Item i, const Simulation& sim) {
    const int have = a.inventory.get(i);
    switch (i) {
        case Item::Food: return spareFood(a);
        case Item::Wood:
        case Item::Stone: return std::max(0, have - projectNeeds(a, i, sim) - 2);
        case Item::Tools:
        case Item::Herbs: return std::max(0, have - 1);
        case Item::Seeds: return std::max(0, have - 3);
        default: return 0;
    }
}

float giftBenefit(const Agent& t, Item i, int n, const Simulation& sim) {
    switch (i) {
        case Item::Food: {
            const float ownedDays = float(foodOwned(t, sim)) / std::max(0.5f, dailyFoodUnits(t));
            return clamp01(0.1f + 0.6f * t.needs.urgency(Need::Food) + 0.08f * float(n) * (1.f - clamp01(ownedDays / 3.f)));
        }
        case Item::Tools: return t.inventory.has(Item::Tools) ? 0.15f : 0.5f;
        case Item::Herbs: return t.health < 0.7f ? clamp01(0.3f + 0.6f * (1.f - t.health)) : 0.15f;
        default: return 0.15f;
    }
}

// ===========================================================================
// GiveAction — walk to the recipient and hand items over (Share / Gift / FeedChild)
// ===========================================================================
enum class GiveKind : std::uint8_t { Share, Gift, Repay, Court, FeedChild };

class GiveAction final : public Action {
public:
    GiveAction(GiveKind gk, AgentId target, std::string targetName, Item item, int amount)
        : gk_(gk), target_(target), targetName_(std::move(targetName)), item_(item), amount_(amount) {}

    const char* kind() const override {
        switch (gk_) {
            case GiveKind::Share: return kinds::Share;
            case GiveKind::FeedChild: return kinds::FeedChild;
            default: return kinds::Gift;
        }
    }
    std::string label() const override {
        switch (gk_) {
            case GiveKind::Share: return "Sharing food with " + targetName_;
            case GiveKind::FeedChild: return "Feeding " + targetName_;
            case GiveKind::Repay: return "Repaying " + targetName_ + " with " + describeItems(item_, amount_);
            case GiveKind::Court: return "Bringing " + targetName_ + " a gift";
            default: return "Giving " + describeItems(item_, amount_) + " to " + targetName_;
        }
    }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        ++elapsed_;
        Agent* t = living(sim, target_);
        if (!t || self.inventory.get(item_) < 1) return finish(self, ActionStatus::Failed);
        if (gk_ != GiveKind::FeedChild && (isAsleep(*t) || isFighting(*t))) return finish(self, ActionStatus::Failed);
        if (chebyshev(self.pos, t->pos) > 1) {
            if (elapsed_ > kGiveGiveUpTicks) return finish(self, ActionStatus::Failed);
            if (stepToward(self, sim, t->pos, 1) == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        return finish(self, handOver(self, *t, sim) ? ActionStatus::Done : ActionStatus::Failed);
    }

private:
    bool handOver(Agent& self, Agent& t, Simulation& sim) {
        const Tick now = sim.now();
        if (gk_ == GiveKind::FeedChild) {
            const int n = feedFood(self, t, amount_);
            if (n <= 0) return false;
            t.remember(MemoryType::ReceivedGift, self.id, now, 0.25f);
            nudgeRelation(t, self.id, kFeedChildAffinity, kFeedChildTrust, now);
            self.feel(Emotion::Joy, kGiveJoy);
            return true;
        }
        // Judge before the transfer (my scarcity is part of the verdict).
        const Deed deed = gk_ == GiveKind::Share ? Deed::Share : Deed::Gift;
        const float benefit = gk_ == GiveKind::Share ? t.needs.urgency(Need::Food) : giftBenefit(t, item_, amount_, sim);
        const DeedContext ctx = makeDeed(deed, &t, 0.f, 0.f, benefit, countWitnesses(sim, self.pos, self.id, t.id));
        const Verdict v = judge(self, ctx, sim);
        const bool hungry = t.needs.urgency(Need::Food) > 0.6f;

        int n = 0;
        if (item_ == Item::Food && gk_ == GiveKind::Share) n = feedFood(self, t, amount_);
        else n = giveItems(self, t, item_, amount_);
        if (n <= 0 && item_ == Item::Food) n = feedFood(self, t, amount_);  // no room to carry: eat it now
        if (n <= 0) return false;

        self.feel(Emotion::Guilt, v.guilt);
        self.feel(Emotion::Pride, v.pride);
        self.feel(Emotion::Joy, kGiveJoy);
        addBalance(self, t, float(n) * itemValue(item_));
        if (gk_ == GiveKind::Court) {
            Relationship& r = t.rel(self.id);
            r.attraction = clamp01(r.attraction + kCourtGiftAttraction);
        }
        self.remember(MemoryType::GaveGift, t.id, now, clamp01(0.3f + 0.1f * float(n) * itemValue(item_)));
        ++self.stats.giftsGiven;
        ++t.stats.giftsReceived;
        ++sim.stats().gifts;

        std::string text;
        switch (gk_) {
            case GiveKind::Share:
                text = self.name + " shared " + describeItems(Item::Food, n) + " with " + (hungry ? "starving " : "hungry ") +
                       t.name;
                break;
            case GiveKind::Repay:
                text = self.name + " gave " + describeItems(item_, n) + " to " + t.name + " to repay a kindness";
                break;
            case GiveKind::Court:
                text = self.name + " gave " + describeItems(item_, n) + " to " + t.name + ", hoping to win " + hisHer(t) +
                       " heart";
                break;
            default: text = self.name + " gave " + describeItems(item_, n) + " to " + t.name; break;
        }
        sim.log(EventKind::Gift, text, self.id, t.id);
        broadcastDeed(sim, self, ctx, self.pos);
        return true;
    }

    GiveKind gk_;
    AgentId target_;
    std::string targetName_;
    Item item_;
    int amount_;
    int elapsed_ = 0;
};

// ===========================================================================
// Share
// ===========================================================================
class ShareBehavior final : public Behavior {
public:
    const char* name() const override { return "share"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (!self.isAdult(sim.now())) return;
        int giveable = spareFood(self);
        if (self.trait(Trait::Empathy) > kShareSacrificeEmpathy &&
            self.needs.urgency(Need::Food) < kShareSacrificeMaxHunger) {
            const int keep = int(std::ceil(dailyFoodUnits(self) * kShareSacrificeKeepDays));
            giveable = std::max(giveable, self.inventory.get(Item::Food) - keep);
        }
        if (giveable < 1) return;

        const Agent* best = nullptr;
        float bestScore = 0.f;
        int bestAmount = 0;
        Verdict bestV;
        for (Agent* o : sim.agentsNear(self.pos, cfg::kSightRadius, self.id)) {
            if (!o || !o->alive || o->id == self.id || isAsleep(*o) || isFighting(*o)) continue;
            if (isParentOf(self, *o) && isChildAgent(*o, sim)) continue;  // FeedChild covers my own kids
            const float need = o->needs.urgency(Need::Food);
            if (need < kShareMinNeed || o->inventory.get(Item::Food) > kShareMaxRecipientFood) continue;
            const int amount = std::min({giveable, std::max(1, foodToSate(*o)), kShareMaxUnits});
            const DeedContext ctx = makeDeed(Deed::Share, o, 0.f, 0.f, need, countWitnesses(sim, o->pos, self.id, o->id));
            const Verdict v = judge(self, ctx, sim);
            const float score = (kShareBase + kShareNeedWeight * need) *
                                    distanceFactor(chebyshev(self.pos, o->pos), kShareDistHalf) +
                                v.utility;
            if (score > bestScore) {
                bestScore = score;
                best = o;
                bestAmount = amount;
                bestV = v;
            }
        }
        if (!best) return;
        const float need = best->needs.urgency(Need::Food);
        pushProposal(out, bestScore,
                     std::make_unique<GiveAction>(GiveKind::Share, best->id, best->name, Item::Food, bestAmount),
                     withWhy(best->name + " is " + (need > 0.6f ? "starving" : "hungry") + " and I have " +
                                 std::to_string(giveable) + " food to spare",
                             bestV));
    }
};

// ===========================================================================
// Gift
// ===========================================================================
struct GiftChoice {
    Item item = Item::Food;
    int n = 0;
    float benefit = 0.f;
};

bool chooseGift(const Agent& self, const Agent& t, const Simulation& sim, GiftChoice& out) {
    out = GiftChoice{};
    const int spare = spareFood(self);
    if (spare >= 1) {
        const int n = std::min(spare, kGiftMaxFood);
        out = {Item::Food, n, giftBenefit(t, Item::Food, n, sim)};
    }
    auto consider = [&](Item i, int n) {
        const float b = giftBenefit(t, i, n, sim);
        if (b > out.benefit) out = {i, n, b};
    };
    if (self.inventory.get(Item::Tools) >= 2 && !t.inventory.has(Item::Tools)) consider(Item::Tools, 1);
    if (self.inventory.get(Item::Herbs) >= 2 || (self.inventory.has(Item::Herbs) && t.health < 0.6f && self.health > 0.8f))
        consider(Item::Herbs, 1);
    if (out.n <= 0) return false;
    return t.freeCarry() >= 1 || out.item == Item::Food;
}

class GiftBehavior final : public Behavior {
public:
    const char* name() const override { return "gift"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        const Tick now = sim.now();
        if (!self.isAdult(now)) return;
        if (recentMemory(self, MemoryType::GaveGift, kNoAgent, now, kGiftCooldown)) return;
        const float E = self.trait(Trait::Empathy);
        const float conscientious = 0.4f + 0.3f * (self.trait(Trait::Integrity) + E);
        const bool single = living(std::as_const(sim), self.partner) == nullptr;

        const Agent* best = nullptr;
        float bestScore = 0.f;
        GiveKind bestKind = GiveKind::Gift;
        GiftChoice bestGift;
        Verdict bestV;
        for (Agent* o : sim.agentsNear(self.pos, cfg::kSightRadius, self.id)) {
            if (!o || !o->alive || o->id == self.id || isAsleep(*o) || isFighting(*o)) continue;
            const Relationship* r = self.findRel(o->id);
            const float aff = r ? r->affinity : 0.f;
            const float bal = r ? r->balance : 0.f;
            float m = 0.f;
            GiveKind gk = GiveKind::Gift;
            if (bal > kGiftRepayMin) {
                m = kGiftRepay * std::min(1.f, bal) * conscientious;
                gk = GiveKind::Repay;
            }
            if (aff > kGiftFriendMin) {
                const float f = kGiftFriend * (aff - kGiftFriendMin) * (0.4f + E);
                if (f > m) { m = f; gk = GiveKind::Gift; }
            }
            if (self.partner == o->id) {
                const float p = kGiftPartner * (0.5f + E);
                if (p > m) { m = p; gk = GiveKind::Gift; }
            }
            if (single && o->isAdult(now) && !bloodKin(self, *o) && !living(std::as_const(sim), o->partner)) {
                const float att = attraction(self, *o, sim);
                if (att >= kGiftCourtMinAttraction) {
                    const float c = kGiftCourt * att * self.needs.urgency(Need::Intimacy) *
                                    (0.5f + self.trait(Trait::Libido));
                    if (c > m) { m = c; gk = GiveKind::Court; }
                }
            }
            if (m <= 0.f) continue;
            GiftChoice g;
            if (!chooseGift(self, *o, sim, g)) continue;
            if (gk == GiveKind::Gift && g.benefit < kGiftMinBenefit) continue;
            const DeedContext ctx = makeDeed(Deed::Gift, o, 0.f, 0.f, g.benefit, countWitnesses(sim, o->pos, self.id, o->id));
            const Verdict v = judge(self, ctx, sim);
            const float score = m * needsDamp(self) * distanceFactor(chebyshev(self.pos, o->pos), kGiftDistHalf) +
                                kGiftMoralWeight * v.utility;
            if (score > bestScore) {
                bestScore = score;
                best = o;
                bestKind = gk;
                bestGift = g;
                bestV = v;
            }
        }
        if (!best) return;
        std::string why;
        switch (bestKind) {
            case GiveKind::Repay: why = "I owe " + best->name + " for past kindness"; break;
            case GiveKind::Court: why = "I'm sweet on " + best->name; break;
            default:
                why = self.partner == best->id ? best->name + " is my partner" : best->name + " is a dear friend";
                break;
        }
        why += "; giving " + describeItems(bestGift.item, bestGift.n);
        pushProposal(out, bestScore,
                     std::make_unique<GiveAction>(bestKind, best->id, best->name, bestGift.item, bestGift.n),
                     withWhy(why, bestV));
    }
};

// ===========================================================================
// Trade
// ===========================================================================
struct Deal {
    Item give = Item::Wood;
    int giveN = 0;
    Item get = Item::Food;
    int getN = 0;
};

// Would `partner` accept `deal` from `self`? Deterministic, so proposal and execution agree.
bool partnerAccepts(const Agent& partner, const Agent& self, const Deal& d, const Simulation& sim) {
    if (partner.trustIn(self.id) < kTradeMinTrust || partner.affinityFor(self.id) < kTradeMinAffinity) return false;
    if (partner.inventory.get(d.get) < d.getN || surplusOf(partner, d.get, sim) < d.getN) return false;
    const float theirWant = wantFor(partner, d.give, sim);
    if (theirWant < kTradeMinPartnerWant || theirWant < kTradeWantRatio * wantFor(partner, d.get, sim)) return false;
    const float valueIn = float(d.giveN) * itemValue(d.give);
    const float valueOut = float(d.getN) * itemValue(d.get);
    if (valueIn < kTradeFairness * valueOut) return false;
    return partner.freeCarry() + d.getN >= d.giveN;
}

// Sizes a deal: I get up to `getWanted` units of `get`, paying in `give` at fair value.
bool sizeDeal(const Agent& self, const Agent& partner, Item give, Item get, int getWanted, const Simulation& sim,
              Deal& d) {
    const int mySurplus = surplusOf(self, give, sim);
    const int theirSurplus = surplusOf(partner, get, sim);
    int getN = std::min({getWanted, theirSurplus, kTradeMaxGet});
    if (getN < 1 || mySurplus < 1) return false;
    int giveN = int(std::ceil(float(getN) * itemValue(get) / itemValue(give) - 1e-4f));
    if (giveN > mySurplus) {
        giveN = mySurplus;
        getN = int(std::floor(float(giveN) * itemValue(give) / itemValue(get) / kTradeFairness + 1e-4f));
        getN = std::min({getN, theirSurplus, kTradeMaxGet});
    }
    giveN = std::max(1, giveN);
    if (getN < 1) return false;
    if (self.freeCarry() + giveN < getN) return false;
    d = Deal{give, giveN, get, getN};
    return true;
}

class TradeAction final : public Action {
public:
    TradeAction(AgentId partner, std::string partnerName, Deal deal)
        : partner_(partner), partnerName_(std::move(partnerName)), deal_(deal) {}
    const char* kind() const override { return kinds::Trade; }
    std::string label() const override {
        return std::string("Trading ") + itemName(deal_.give) + " for " + itemName(deal_.get) + " with " + partnerName_;
    }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        ++elapsed_;
        Agent* p = living(sim, partner_);
        if (!p || isAsleep(*p) || isFighting(*p)) return finish(self, ActionStatus::Failed);
        if (chebyshev(self.pos, p->pos) > 1) {
            if (elapsed_ > kTradeGiveUpTicks) return finish(self, ActionStatus::Failed);
            if (stepToward(self, sim, p->pos, 1) == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        // Holdings may have changed on the way: resize, then let them decide.
        Deal d;
        if (!sizeDeal(self, *p, deal_.give, deal_.get, deal_.getN, sim, d) || !partnerAccepts(*p, self, d, sim))
            return finish(self, ActionStatus::Failed);
        const int gave = self.inventory.take(d.give, d.giveN);
        const int got = p->inventory.take(d.get, d.getN);
        p->inventory.add(d.give, gave);
        self.inventory.add(d.get, got);

        const Tick now = sim.now();
        nudgeRelation(self, p->id, kTradeAffinity, kTradeTrust, now);
        nudgeRelation(*p, self.id, kTradeAffinity, kTradeTrust, now);
        self.remember(MemoryType::Traded, p->id, now, 0.3f);
        p->remember(MemoryType::Traded, self.id, now, 0.3f);
        self.practice(Skill::Socializing, kTradePractice);
        p->practice(Skill::Socializing, kTradePractice * 0.5f);
        self.feel(Emotion::Joy, 0.05f);
        p->feel(Emotion::Joy, 0.05f);
        ++sim.stats().trades;
        sim.log(EventKind::Social,
                self.name + " traded " + describeItems(d.give, gave) + " to " + p->name + " for " + describeItems(d.get, got),
                self.id, p->id);
        return finish(self, ActionStatus::Done);
    }

private:
    AgentId partner_;
    std::string partnerName_;
    Deal deal_;
    int elapsed_ = 0;
};

class TradeBehavior final : public Behavior {
public:
    const char* name() const override { return "trade"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (!self.isAdult(sim.now())) return;
        // What do I want most that I don't have to spare?
        Item want = Item::Food;
        float myWant = 0.f;
        for (int i = 0; i < kItemCount; ++i) {
            const Item it = Item(i);
            if (surplusOf(self, it, sim) > 0) continue;
            const float w = wantFor(self, it, sim);
            if (w > myWant) {
                myWant = w;
                want = it;
            }
        }
        if (myWant < kTradeMinWant) return;
        std::vector<Item> offers;
        for (int i = 0; i < kItemCount; ++i)
            if (Item(i) != want && surplusOf(self, Item(i), sim) > 0) offers.push_back(Item(i));
        if (offers.empty()) return;
        const int getWanted = want == Item::Food ? std::max(1, foodToSate(self) + 2)
                              : (want == Item::Wood || want == Item::Stone) ? std::max(1, projectNeeds(self, want, sim))
                                                                            : 1;

        const Agent* best = nullptr;
        float bestScore = 0.f;
        Deal bestDeal;
        for (Agent* o : sim.agentsNear(self.pos, cfg::kSightRadius, self.id)) {
            if (!o || !o->alive || o->id == self.id || isAsleep(*o) || isFighting(*o) || o->inBreakdown()) continue;
            if (!o->isAdult(sim.now())) continue;
            if (self.trustIn(o->id) < kTradeMinTrust || self.affinityFor(o->id) < kTradeMinAffinity) continue;
            if (surplusOf(*o, want, sim) < 1) continue;
            for (Item give : offers) {
                Deal d;
                if (!sizeDeal(self, *o, give, want, getWanted, sim, d) || !partnerAccepts(*o, self, d, sim)) continue;
                const float score = (kTradeBase + kTradeWantWeight * myWant) *
                                        distanceFactor(chebyshev(self.pos, o->pos), kTradeDistHalf) +
                                    kTradeTrustWeight * (self.trustIn(o->id) - 0.5f);
                if (score > bestScore) {
                    bestScore = score;
                    best = o;
                    bestDeal = d;
                }
            }
        }
        if (!best) return;
        pushProposal(out, bestScore, std::make_unique<TradeAction>(best->id, best->name, bestDeal),
                     std::string("I need ") + itemName(want) + "; " + best->name + " has spare and wants " +
                         itemName(bestDeal.give) + " (" + describeItems(bestDeal.give, bestDeal.giveN) + " for " +
                         describeItems(bestDeal.get, bestDeal.getN) + ")");
    }
};

// ===========================================================================
// FeedChild
// ===========================================================================
class FeedChildBehavior final : public Behavior {
public:
    const char* name() const override { return "feed_child"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (!self.isAdult(sim.now()) || self.children.empty()) return;
        const int myFood = self.inventory.get(Item::Food);
        if (myFood < 1) return;
        const float E = self.trait(Trait::Empathy);
        const bool selfish = self.needs.urgency(Need::Food) > kFeedSelfishHunger && E < 0.5f;
        const int giveable = selfish ? myFood - 1 : myFood;
        if (giveable < 1) return;

        const Agent* best = nullptr;
        float bestScore = 0.f;
        int bestAmount = 0;
        for (AgentId cid : self.children) {
            const Agent* c = living(std::as_const(sim), cid);
            if (!c || !isChildAgent(*c, sim)) continue;
            const int dist = chebyshev(self.pos, c->pos);
            if (dist > kFeedSearch) continue;
            const float cu = c->needs.urgency(Need::Food);
            if (cu < kFeedMinUrgency || c->inventory.get(Item::Food) > 0) continue;
            if (isAsleep(*c) && cu < kFeedWakeUrgency) continue;
            const float score = (kFeedBase + kFeedUrgencyWeight * cu) * (kFeedEmpathyLo + E) *
                                distanceFactor(dist, kFeedDistHalf);
            if (score > bestScore) {
                bestScore = score;
                best = c;
                bestAmount = std::min({giveable, std::max(1, foodToSate(*c)), kFeedMaxUnits});
            }
        }
        if (!best) return;
        const float cu = best->needs.urgency(Need::Food);
        pushProposal(out, bestScore,
                     std::make_unique<GiveAction>(GiveKind::FeedChild, best->id, best->name, Item::Food, bestAmount),
                     "my child " + best->name + " is " + (cu > 0.6f ? "starving" : "hungry") + " (food " +
                         pct(best->needs.get(Need::Food)) + ")");
    }
};

// ===========================================================================
// Beg
// ===========================================================================
class BegAction final : public Action {
public:
    BegAction(AgentId giver, std::string giverName) : giver_(giver), giverName_(std::move(giverName)) {}
    const char* kind() const override { return kinds::Beg; }
    std::string label() const override { return "Begging " + giverName_ + " for food"; }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        ++elapsed_;
        Agent* g = living(sim, giver_);
        if (!g || isAsleep(*g) || isFighting(*g) || g->inventory.get(Item::Food) < 1)
            return finish(self, ActionStatus::Failed);
        if (chebyshev(self.pos, g->pos) > 1) {
            if (elapsed_ > kBegGiveUpTicks) return finish(self, ActionStatus::Failed);
            if (stepToward(self, sim, g->pos, 1) == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        if (++pleads_ < kBegTicks) return ActionStatus::Running;
        resolve(self, *g, sim);
        return finish(self, ActionStatus::Done);
    }

private:
    // The giver decides on the spot, with their own conscience.
    void resolve(Agent& self, Agent& g, Simulation& sim) {
        const Tick now = sim.now();
        const bool child = isChildAgent(self, sim);
        const float hunger = self.needs.urgency(Need::Food);
        const DeedContext ctx = makeDeed(Deed::Share, &self, 0.f, 0.f, hunger, countWitnesses(sim, g.pos, g.id, self.id));
        const Verdict v = judge(g, ctx, sim);
        const float willingness = v.utility + kBegFaceToFace + (child ? kBegChildPull : 0.f) +
                                  kBegAffinity * g.affinityFor(self.id) - kBegGiverHunger * g.needs.urgency(Need::Food);
        if (sim.rng().chance(logistic(willingness, 0.f, kBegSteep))) {
            const int gFood = g.inventory.get(Item::Food);
            const int keep = int(std::ceil(dailyFoodUnits(g) * 0.5f));
            const int amount = std::min({std::max(1, gFood - keep), std::max(1, foodToSate(self)), kBegMaxFood});
            const int n = feedFood(g, self, amount);
            if (n > 0) {
                g.feel(Emotion::Guilt, v.guilt);
                g.feel(Emotion::Pride, v.pride);
                addBalance(g, self, float(n));
                g.remember(MemoryType::GaveGift, self.id, now, 0.3f);
                ++g.stats.giftsGiven;
                ++self.stats.giftsReceived;
                ++sim.stats().gifts;
                sim.log(EventKind::Gift,
                        g.name + " gave " + describeItems(Item::Food, n) + " to " + (child ? "a begging child, " : "begging ") +
                            self.name,
                        g.id, self.id);
                broadcastDeed(sim, g, ctx, g.pos);  // counts as Share; I remember ReceivedGift
                return;
            }
        }
        // Refused.
        self.remember(MemoryType::Rejected, g.id, now, 0.3f);
        self.feel(Emotion::Sadness, kBegRefusedSadness);
        self.norms.sharingExpectation = clamp01(self.norms.sharingExpectation * (1.f - kBegRefusedNorm));
        nudgeRelation(self, g.id, -0.04f, -0.02f, now);
        if (g.inventory.get(Item::Food) >= 6) {
            Relationship& r = self.rel(g.id);
            r.grievance = clamp01(r.grievance + 0.05f);  // "she has plenty and gave me nothing"
        }
    }

    AgentId giver_;
    std::string giverName_;
    int elapsed_ = 0;
    int pleads_ = 0;
};

class BegBehavior final : public Behavior {
public:
    const char* name() const override { return "beg"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        const Tick now = sim.now();
        if (self.inventory.get(Item::Food) > 0) return;
        const bool child = isChildAgent(self, sim);
        const float hunger = self.needs.urgency(Need::Food);
        const float pride = self.emotions.get(Emotion::Pride);
        if (child ? hunger < kBegChildMinHunger : (hunger < kBegAdultMinHunger || pride > kBegMaxPride)) return;
        const float willingness = child ? 1.f : (1.f - pride) * (0.6f + 0.4f * self.trait(Trait::Sociability));

        const Agent* best = nullptr;
        float bestScore = 0.f;
        for (Agent* o : sim.agentsNear(self.pos, cfg::kSightRadius, self.id)) {
            if (!o || !o->alive || o->id == self.id || isAsleep(*o) || isFighting(*o) || o->inBreakdown()) continue;
            if (!o->isAdult(now) || o->inventory.get(Item::Food) < kBegMinGiverFood) continue;
            if (recentMemory(self, MemoryType::Rejected, o->id, now, kBegRefusalCooldown)) continue;
            const Relationship* r = self.findRel(o->id);
            const float kindness = kBegKindTrust * self.trustIn(o->id) +
                                   kBegKindAffinity * std::max(0.f, r ? r->affinity : 0.f) +
                                   (self.isFamily(*o) ? kBegKindFamily : 0.f) +
                                   kBegKindGenerous * clamp01(r ? r->balance * 2.f : 0.f);
            const float score = hunger * (child ? kBegChildWeight : kBegAdultWeight) * willingness * (0.4f + kindness) *
                                distanceFactor(chebyshev(self.pos, o->pos), kBegDistHalf);
            if (score > bestScore) {
                bestScore = score;
                best = o;
            }
        }
        if (!best) return;
        pushProposal(out, bestScore, std::make_unique<BegAction>(best->id, best->name),
                     std::string(hunger > 0.6f ? "starving" : "hungry") + " with no food; " + best->name + " carries " +
                         std::to_string(best->inventory.get(Item::Food)) + " food" +
                         (self.isFamily(*best) ? " and is family" : ""));
    }
};

}  // namespace

void addExchangeBehaviors(BehaviorList& out) {
    out.push_back(std::make_unique<ShareBehavior>());
    out.push_back(std::make_unique<GiftBehavior>());
    out.push_back(std::make_unique<TradeBehavior>());
    out.push_back(std::make_unique<FeedChildBehavior>());
    out.push_back(std::make_unique<BegBehavior>());
}

}  // namespace heur::social
