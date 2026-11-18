// Heuristica — construction: Build (choose & start a project, continue it, repair what I rely on) and
// HelpBuild (labour on others' / communal sites, repair communal structures).
//
// Flow: the Build behaviour picks WHAT to build by heuristics and proposes a SiteWorkAction carrying the
// type + anchor; on its first tick the action picks the site and calls world.beginStructure, setting
// self.project. While a project lacks materials the site can't progress, so Build stops proposing it and
// materialDemand() (Common.cpp) turns the shortfall into Chop/Mine/Withdraw proposals ("need wood for my
// hut"). Carrying materials -> deliver; materials delivered -> labour; labour + materials -> complete.
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "ai/Morality.h"
#include "ai/survival/SurvivalCommon.h"
#include "sim/Simulation.h"
#include "social/Social.h"

namespace heur::survival {

namespace {
// ---- Site work -----------------------------------------------------------------------------------
constexpr float kBuildHalfDist = 15.f;      // distanceFactor half-distance to my own site
constexpr float kToolWearPerTick = 0.002f;  // building also wears tools
constexpr int kHelpShiftTicks = 60;         // a helper labours this long, then reconsiders
constexpr float kBuiltTogetherMemory = 0.4f;
constexpr float kHelperAffinity = 0.03f;    // helper's affinity for the founder per shift
constexpr float kFounderAffinity = 0.04f;   // founder's gratitude toward the helper per shift
constexpr float kFounderBalance = 0.1f;     // founder now "owes" the helper this much
constexpr float kHelpBroadcastChance = 0.12f;  // per helper shift: rare, to avoid witness spam
constexpr int kSiteRadiusDefault = 6;
constexpr int kSiteSamples = 6;             // findBuildSite samples when looking for a fertile farm plot

// ---- New-project heuristics (scores before × industry & night) --------------------------------------
constexpr int kKinRadius = 20;              // family/friends/clan within this pull a new hut toward them
constexpr float kKinFamilyWeight = 3.f;
constexpr float kKinFriendWeight = 2.f;
constexpr float kKinClanWeight = 1.f;
constexpr float kKinHomeWeight = 1.5f;      // their hut counts more than where they happen to stand
constexpr float kFriendAffinity = 0.3f;
constexpr int kHutSiteRadius = 7;
constexpr float kFireMinCold = 0.25f;       // start thinking about a campfire when coldSeason() >= this
constexpr int kFireNeighbourhood = 15;      // ...and no fire pit (complete or planned) within this
constexpr float kFireNew = 0.18f;           // score = (new + cold × a + Int × b + Soc × c)
constexpr float kFireNewCold = 0.3f;
constexpr float kFireNewInt = 0.1f;
constexpr float kFireNewSoc = 0.05f;
constexpr int kFireSiteRadius = 4;
constexpr float kFireNewChill = 0.4f;       // × chill (current cold / own warmth urgency)
constexpr float kFireChillTemp = 0.5f;      // temperature below which it counts as chilly (chill 0..1)
constexpr float kFireUrgentChill = 0.35f;   // chill at which a fire may interrupt another project
constexpr float kFireUrgentNight = 0.5f;    // an urgent fire is still built at night
constexpr int kResumeRadius = 30;           // my own unfinished sites this close get resumed
constexpr int kStorageNeighbourhood = 20;   // a usable storage this close means no new one is needed
constexpr int kStorageSurplusUnits = 6;     // food owned that justifies a storage...
constexpr float kStorageCarryFull = 0.8f;   // ...or a basket this full...
constexpr float kStoragePrepMin = 0.3f;     // ...or winter preparation this pressing
constexpr float kStorageNew = 0.22f;        // score = new + Int × a + prep × b
constexpr float kStorageNewInt = 0.18f;
constexpr float kStorageNewPrep = 0.2f;
constexpr float kCommunalGreedWeight = 0.8f;  // communal if (Empathy + sharingExpectation)/2 > Greed × w + bias
constexpr float kCommunalBias = 0.15f;
constexpr float kFarmMinInt = 0.4f;
constexpr int kFarmNeighbourhood = 20;
constexpr float kMouthsPerFarm = 2.5f;      // a tended field feeds about this many people
constexpr float kCommunalFarmShare = 0.34f; // a nearby communal field counts as this much of "my" farming
constexpr float kFarmNew = 0.15f;           // score = (new + (Int − min)/(1 − min) × a + Farming × b) × season
constexpr float kFarmNewInt = 0.35f;
constexpr float kFarmNewSkill = 0.1f;
constexpr float kFarmSeason[4] = {1.0f, 0.8f, 0.3f, 0.f};  // spring .. winter
constexpr int kFarmSiteRadius = 10;
constexpr float kMonumentMinDrive = 1.15f;  // Industriousness + Creativity needed to dream of a monument
constexpr float kMonumentNew = 0.1f;
constexpr float kMonumentNewScale = 0.35f;
constexpr float kMonumentMaxScore = 0.38f;
constexpr int kMonumentNeighbourhood = 15;
constexpr float kMonumentMinReserve = 3.f;  // days of food stored before indulging
constexpr int kWallMemoryDays = 4;          // a theft this recent makes walls appealing
constexpr float kWallMinDefensive = 0.35f;  // Aggression × .5 + Fear × .6 + Greed × .2
constexpr float kWallNew = 0.2f;
constexpr float kWallNewScale = 0.3f;
constexpr int kMaxWallsPerStore = 6;        // never fully enclose (stores must stay reachable)
constexpr int kWallRing = 2;
constexpr int kWorkshopToollessMin = 4;     // adults without tools nearby
constexpr int kWorkshopRadius = 12;
constexpr int kWorkshopNeighbourhood = 25;
constexpr float kWorkshopMinTraits = 1.0f;  // Industriousness + Intelligence
constexpr float kWorkshopNew = 0.15f;
constexpr float kWorkshopPerToolless = 0.03f;
constexpr float kWorkshopMaxScore = 0.4f;

// ---- Repair ------------------------------------------------------------------------------------------
constexpr float kRepairBase = 0.15f;        // score = (base + need × (1 − condition/threshold)) × reliance
constexpr float kRepairNeed = 0.5f;
constexpr float kRepairDone = 0.95f;
constexpr int kRepairMaxTicks = 90;
constexpr int kRepairRadius = 20;
constexpr float kRelyHome = 1.0f;           // how much I rely on each structure I repair
constexpr float kRelyStorage = 0.8f;
constexpr float kRelyFarm = 0.7f;
constexpr float kRelyOther = 0.4f;
constexpr float kRelyCommunalFire = 0.7f;   // ×(cold season) — fires matter in the cold
constexpr float kRelyCommunalStorage = 0.6f;
constexpr float kRelyCommunalOther = 0.35f;

// ---- HelpBuild -----------------------------------------------------------------------------------------
constexpr int kHelpRadius = 15;
constexpr int kHelpCandidates = 3;          // judge() only the nearest few sites
constexpr float kHelpBase = 0.08f;          // score = base + Ind × a + affinity × b + family + judge × w
constexpr float kHelpInd = 0.12f;
constexpr float kHelpAffinity = 0.15f;
constexpr float kHelpFamily = 0.12f;
constexpr float kHelpJudgeWeight = 0.6f;
constexpr float kHelpMax = 0.6f;
constexpr float kHelpBenefitHomeless = 0.7f;  // DeedContext.benefit by site type
constexpr float kHelpBenefitCommunal = 0.5f;
constexpr float kHelpBenefitOther = 0.35f;
constexpr float kHelpHalfDist = 10.f;

float industryOf(const Agent& a) { return 0.8f + 0.4f * a.trait(Trait::Industriousness); }

int percentDone(const Structure& s) {
    const float work = std::max(1.f, s.bp().work);
    return int(100.f * (0.5f * deliveredFraction(s) + 0.5f * clamp01(s.progress / work)));
}

std::string firstMissingCarried(const Structure& s, const Inventory& inv) {
    for (int i = 0; i < kItemCount; ++i) {
        const Item it = Item(i);
        if (s.materialsMissing(it) > 0 && inv.get(it) > 0) return itemName(it);
    }
    return "materials";
}

float helpBenefit(const Structure& s, const Simulation& sim) {
    if (s.type == StructureType::Hut && s.owner != kNoAgent) {
        const Agent* o = sim.agent(s.owner);
        if (o && !effectiveHome(*o, sim)) return kHelpBenefitHomeless;
    }
    return s.isCommunal() ? kHelpBenefitCommunal : kHelpBenefitOther;
}

DeedContext helpContext(const Agent& self, const Structure& s, const Simulation& sim) {
    DeedContext ctx;
    ctx.deed = Deed::Help;
    const AgentId beneficiary = s.owner != kNoAgent ? s.owner : s.founder;
    ctx.target = beneficiary != self.id ? sim.agent(beneficiary) : nullptr;
    ctx.targetStructure = &s;
    ctx.benefit = helpBenefit(s, sim);
    ctx.witnesses = countWitnesses(sim, s.pos, self.id, beneficiary);
    return ctx;
}

// Pick a construction site near `anchor`. Farms: the most fertile of several samples. Walls: a free tile on
// the ring around the protected store (never more than kMaxWallsPerStore, so the store stays reachable).
std::optional<Vec2i> pickSite(World& w, Random& rng, StructureType type, Vec2i anchor, int radius) {
    if (type == StructureType::Wall) {
        std::vector<Vec2i> ring;
        for (int dy = -kWallRing; dy <= kWallRing; ++dy)
            for (int dx = -kWallRing; dx <= kWallRing; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != kWallRing) continue;
                const Vec2i p = anchor + Vec2i{dx, dy};
                if (w.canBuildAt(p, type)) ring.push_back(p);
            }
        if (ring.empty() || int(w.structuresNear(anchor, kWallRing, StructureType::Wall).size()) >= kMaxWallsPerStore)
            return std::nullopt;
        return rng.pick(ring);
    }
    if (type == StructureType::Farm) {
        std::optional<Vec2i> best;
        float bestFert = -1.f;
        for (int i = 0; i < kSiteSamples; ++i) {
            const auto p = w.findBuildSite(anchor, type, radius, rng);
            if (!p) break;
            const float f = w.at(*p).fertility - 0.01f * float(chebyshev(*p, anchor));
            if (f > bestFert) {
                bestFert = f;
                best = p;
            }
        }
        return best;
    }
    if (auto p = w.findBuildSite(anchor, type, radius, rng)) return p;
    return w.findBuildSite(anchor, type, radius * 2, rng);
}

// =====================================================================================================
// SiteWorkAction — start (optional), deliver, labour, complete. Kind Build (own project) or HelpBuild.
// =====================================================================================================
class SiteWorkAction final : public Action {
public:
    // Start a new project of `type` near `anchor`.
    SiteWorkAction(StructureType type, Vec2i anchor, AgentId owner, int radius, std::string label)
        : kind_(kinds::Build), newType_(type), anchor_(anchor), owner_(owner), radius_(radius), label_(std::move(label)) {}
    // Work on an existing site (own project, or helping when `helper`).
    SiteWorkAction(StructureId site, bool helper, std::string label)
        : kind_(helper ? kinds::HelpBuild : kinds::Build), site_(site), helper_(helper), label_(std::move(label)) {}

    const char* kind() const override { return kind_; }
    std::string label() const override { return label_; }
    void onAbort(Agent& self, Simulation& sim) override {
        clearPath(self);
        endShift(self, sim);
    }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        World& w = sim.world();
        if (site_ == kNoStructure && !beginSite(self, sim)) return finish(self, ActionStatus::Failed);
        Structure* s = w.structure(site_);
        if (!s) {
            if (self.project == site_) self.project = kNoStructure;
            return stop(self, sim, ActionStatus::Failed);
        }
        if (s->complete) {
            if (self.project == site_) self.project = kNoStructure;
            return stop(self, sim, ActionStatus::Done);
        }
        // Resuming one of my own abandoned sites makes it my project again (so materialDemand sees it).
        if (!helper_ && self.project != site_) self.project = site_;
        // Nothing I can do here right now: don't walk over (gathering proposals take over).
        if (!siteCanProgress(*s, self.inventory)) return stop(self, sim, labourTicks_ > 0 ? ActionStatus::Done : ActionStatus::Failed);

        const MoveResult mr = stepToward(self, sim, s->pos, 1);
        if (mr == MoveResult::Blocked) return stop(self, sim, ActionStatus::Failed);
        if (mr == MoveResult::Moving) {
            if (++travel_ > tune::kMaxTravelTicks) return stop(self, sim, ActionStatus::Failed);
            label_ = std::string(helper_ ? "Walking over to help with " : "Walking to ") + structPhrase(self, *s, sim);
            return ActionStatus::Running;
        }
        // Don't stand on the tile of something that will block movement once finished.
        if (self.pos == s->pos && s->bp().blocksMovement) {
            stepAway(self, sim, s->pos);
            return ActionStatus::Running;
        }

        if (carriesMissingMaterial(*s, self.inventory)) {
            const std::string what = firstMissingCarried(*s, self.inventory);
            if (w.deliverMaterials(*s, self.inventory) > 0) {
                label_ = "Hauling " + what + " to " + structPhrase(self, *s, sim);
                credit(self, sim, *s);
                return ActionStatus::Running;
            }
        }
        if (s->progress < allowedProgress(*s) || s->materialsComplete()) {
            const float labour = buildLabour(self, sim);
            const bool justDone = w.applyWork(*s, labour, self.id);
            self.practice(Skill::Building, tune::kPracticeWork);
            wearTools(self, sim, kToolWearPerTick);
            credit(self, sim, *s);
            ++labourTicks_;
            if (justDone) {
                onStructureCompleted(*s, self, sim);
                return stop(self, sim, ActionStatus::Done);
            }
            label_ = std::string(helper_ ? "Helping build " : "Building ") + structPhrase(self, *s, sim) + " (" +
                     std::to_string(percentDone(*s)) + "%)";
            if (helper_ && labourTicks_ >= kHelpShiftTicks) return stop(self, sim, ActionStatus::Done);
            return ActionStatus::Running;
        }
        // Stalled: the site needs materials I don't carry (gathering proposals take over).
        return stop(self, sim, labourTicks_ > 0 ? ActionStatus::Done : ActionStatus::Failed);
    }

private:
    bool beginSite(Agent& self, Simulation& sim) {
        World& w = sim.world();
        const auto pos = pickSite(w, sim.rng(), newType_, anchor_, radius_);
        if (!pos) return false;
        const StructureId id = w.beginStructure(newType_, *pos, self.id, owner_);
        if (id == kNoStructure) return false;
        site_ = id;
        self.project = id;
        const char* noun = structNoun(newType_);
        sim.log(EventKind::Build,
                self.name + " began building " + (owner_ == kNoAgent ? "a communal " : "a ") + noun, self.id);
        return true;
    }

    // Helper bookkeeping, once per shift: shared-work memories, affinity and gratitude.
    void credit(Agent& self, Simulation& sim, const Structure& s) {
        if (!helper_ || credited_) return;
        credited_ = true;
        const AgentId fid = s.founder;
        Agent* f = sim.agent(fid);
        if (!f || f->id == self.id) return;
        self.remember(MemoryType::BuiltTogether, f->id, sim.now(), kBuiltTogetherMemory);
        f->remember(MemoryType::BuiltTogether, self.id, sim.now(), kBuiltTogetherMemory);
        Relationship& mine = self.rel(f->id);
        mine.affinity = clampf(mine.affinity + kHelperAffinity, -1.f, 1.f);
        Relationship& theirs = f->rel(self.id);
        theirs.affinity = clampf(theirs.affinity + kFounderAffinity, -1.f, 1.f);
        theirs.balance += kFounderBalance;
    }

    // End of a helper shift: the warm glow, and (rarely) the deed is noticed by others.
    void endShift(Agent& self, Simulation& sim) {
        if (!helper_ || !credited_ || shiftEnded_) return;
        shiftEnded_ = true;
        const Structure* s = sim.world().structure(site_);
        if (!s) return;
        const DeedContext ctx = helpContext(self, *s, sim);
        const Verdict v = judge(self, ctx, sim);
        self.feel(Emotion::Pride, v.pride);
        if (sim.rng().chance(kHelpBroadcastChance)) {
            sim.log(EventKind::Social, self.name + " lent a hand building " + structPhraseWorld(*s, sim), self.id,
                    s->founder);
            broadcastDeed(sim, self, ctx, s->pos);
        }
    }

    ActionStatus stop(Agent& self, Simulation& sim, ActionStatus st) {
        endShift(self, sim);
        return finish(self, st);
    }

    const char* kind_;
    StructureType newType_ = StructureType::Hut;
    Vec2i anchor_{};
    AgentId owner_ = kNoAgent;
    int radius_ = kSiteRadiusDefault;
    StructureId site_ = kNoStructure;
    bool helper_ = false;
    std::string label_;
    int travel_ = 0;
    int labourTicks_ = 0;
    bool credited_ = false;
    bool shiftEnded_ = false;
};

// =====================================================================================================
// RepairAction — labour on a COMPLETE structure (world.applyWork raises condition).
// =====================================================================================================
class RepairAction final : public Action {
public:
    RepairAction(StructureId target, bool communal, std::string label)
        : target_(target), communal_(communal), label_(std::move(label)) {}
    const char* kind() const override { return communal_ ? kinds::HelpBuild : kinds::Build; }
    std::string label() const override { return label_; }
    void onAbort(Agent& self, Simulation& /*sim*/) override { clearPath(self); }

    ActionStatus tick(Agent& self, Simulation& sim) override {
        World& w = sim.world();
        Structure* s = w.structure(target_);
        if (!s || !s->complete) return finish(self, ActionStatus::Failed);
        if (s->condition >= kRepairDone) return finish(self, ticks_ > 0 ? ActionStatus::Done : ActionStatus::Failed);
        const MoveResult mr = stepToward(self, sim, s->pos, 1);
        if (mr == MoveResult::Blocked) return finish(self, ActionStatus::Failed);
        if (mr == MoveResult::Moving) {
            if (++travel_ > tune::kMaxTravelTicks) return finish(self, ActionStatus::Failed);
            return ActionStatus::Running;
        }
        w.applyWork(*s, buildLabour(self, sim), self.id);
        self.practice(Skill::Building, tune::kPracticeWork);
        wearTools(self, sim, kToolWearPerTick);
        if (++ticks_ >= kRepairMaxTicks || s->condition >= kRepairDone) {
            if (communal_) {
                self.feel(Emotion::Pride, 0.08f);
            } else {
                self.feel(Emotion::Joy, 0.05f);
            }
            return finish(self, ActionStatus::Done);
        }
        return ActionStatus::Running;
    }

private:
    StructureId target_;
    bool communal_;
    std::string label_;
    int travel_ = 0;
    int ticks_ = 0;
};

float repairScore(const Structure& s, float reliance, const Agent& self) {
    const float need = clamp01(1.f - s.condition / tune::kRepairBelow);
    return (kRepairBase + kRepairNeed * need) * reliance * industryOf(self);
}

// =====================================================================================================
// Build behaviour: continue my project, else consider a new one; repair what I rely on.
// =====================================================================================================
struct NewProject {
    StructureType type = StructureType::Hut;
    Vec2i anchor{};
    AgentId owner = kNoAgent;
    int radius = kSiteRadiusDefault;
    float score = 0.f;
    bool urgent = false;  // worth interrupting another project for (a fire on a freezing night)
    std::string label;
    std::string reason;
};

class BuildBehavior final : public Behavior {
public:
    const char* name() const override { return "build"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (isChild(self, sim)) return;
        const World& w = sim.world();
        const float night = nightWorkFactor(w, 0.f);
        proposeRepairs(self, sim, out, night);

        // A freezing night with no fire around trumps any other project (a fire pit is quick to make).
        NewProject fire = campfireProject(self, sim);
        const float fireNight = nightWorkFactor(w, fire.urgent ? self.needs.urgency(Need::Warmth) + kFireUrgentNight : 0.f);

        const Structure* p = activeProject(self, sim);
        if (!p) p = abandonedSite(self, sim);
        if (p) {
            // Stalled sites are still proposed when not yet my project, so the action can adopt them.
            const bool adopt = p->id != self.project;
            if (adopt || siteCanProgress(*p, self.inventory)) {
                const int d = chebyshev(self.pos, p->pos);
                const float score = projectDrive(self, *p, sim) * distanceFactor(d, kBuildHalfDist) * night;
                const std::string what = structPhrase(self, *p, sim);
                std::string reason = (adopt ? "back to " : "working on ") + what + " (" + std::to_string(percentDone(*p)) +
                                     "% done)";
                if (carriesMissingMaterial(*p, self.inventory)) reason += ", I have materials to deliver";
                out.push_back(Proposal{score,
                                       std::make_unique<SiteWorkAction>(p->id, false, (adopt ? "Resuming work on " : "Building ") + what),
                                       reason});
            }
            if (fire.urgent && p->type != StructureType::Campfire)
                out.push_back(Proposal{fire.score * fireNight,
                                       std::make_unique<SiteWorkAction>(fire.type, fire.anchor, fire.owner, fire.radius, fire.label),
                                       fire.reason});
            return;
        }

        NewProject best;
        consider(best, hutProject(self, sim));
        const bool fireBest = fire.score > best.score;
        consider(best, std::move(fire));
        consider(best, storageProject(self, sim));
        consider(best, farmProject(self, sim));
        consider(best, monumentProject(self, sim));
        consider(best, wallProject(self, sim));
        consider(best, workshopProject(self, sim));
        if (best.score <= 0.f) return;
        out.push_back(Proposal{best.score * (fireBest && best.type == StructureType::Campfire ? fireNight : night),
                               std::make_unique<SiteWorkAction>(best.type, best.anchor, best.owner, best.radius, best.label),
                               best.reason});
    }

private:
    static void consider(NewProject& best, NewProject cand) {
        if (cand.score > best.score) best = std::move(cand);
    }

    static bool communalMinded(const Agent& self) {
        const float giving = 0.5f * (self.trait(Trait::Empathy) + self.norms.sharingExpectation);
        return giving > self.trait(Trait::Greed) * kCommunalGreedWeight + kCommunalBias;
    }

    // My own unfinished site (e.g. left for an urgent campfire), nearest first.
    static const Structure* abandonedSite(const Agent& self, const Simulation& sim) {
        const World& w = sim.world();
        const Structure* best = nullptr;
        int bestD = kResumeRadius + 1;
        for (StructureId id : w.structuresNear(self.pos, kResumeRadius)) {
            const Structure* s = w.structure(id);
            if (!s || s->complete || s->founder != self.id) continue;
            if (!(s->owner == self.id || s->isCommunal() || isHousehold(self, s->owner, sim))) continue;
            const int d = chebyshev(self.pos, s->pos);
            if (d < bestD) {
                bestD = d;
                best = s;
            }
        }
        return best;
    }

    static Vec2i homeOrHere(const Agent& self, const Simulation& sim) {
        const Structure* h = effectiveHome(self, sim);
        return h ? h->pos : self.pos;
    }

    // Any structure of `type` (complete or under construction) near `centre` that I may use.
    static bool haveUsable(const Agent& self, const Simulation& sim, Vec2i centre, int radius, StructureType type) {
        const World& w = sim.world();
        for (StructureId id : w.structuresNear(centre, radius, type)) {
            const Structure* s = w.structure(id);
            if (s && (s->isCommunal() || isHousehold(self, s->owner, sim) || s->founder == self.id)) return true;
        }
        return false;
    }

    static NewProject hutProject(const Agent& self, Simulation& sim) {
        NewProject np;
        const World& w = sim.world();
        const Structure* home = effectiveHome(self, sim);
        const bool ownHome = home && (home->owner == self.id || (self.partner != kNoAgent && home->owner == self.partner));
        if (ownHome) return np;
        np.type = StructureType::Hut;
        np.owner = self.id;
        np.radius = kHutSiteRadius;
        // Same scale as projectDrive() for a hut so starting and continuing compare fairly.
        Structure probe;
        probe.type = StructureType::Hut;
        np.score = projectDrive(self, probe, sim);

        // Site: near family / friends / clan (their homes weigh more), else right here.
        float sx = 0.f, sy = 0.f, sw = 0.f;
        bool family = false;
        for (const Agent* o : sim.agentsNear(self.pos, kKinRadius, self.id)) {
            if (!o->alive) continue;
            float weight = 0.f;
            if (self.isFamily(*o)) {
                weight = kKinFamilyWeight;
                family = true;
            } else if (self.affinityFor(o->id) > kFriendAffinity) {
                weight = kKinFriendWeight;
            } else if (o->clan == self.clan) {
                weight = kKinClanWeight;
            }
            if (weight <= 0.f) continue;
            Vec2i p = o->pos;
            const Structure* oh = w.structure(o->home);
            if (oh && oh->complete) {
                p = oh->pos;
                weight *= kKinHomeWeight;
            }
            sx += weight * float(p.x);
            sy += weight * float(p.y);
            sw += weight;
        }
        np.anchor = sw > 0.f ? Vec2i{int(std::lround(sx / sw)), int(std::lround(sy / sw))} : self.pos;
        if (!w.inBounds(np.anchor)) np.anchor = self.pos;
        np.label = family ? "Laying out a hut near my family" : (sw > 0.f ? "Laying out a hut near my people"
                                                                           : "Laying out a hut of my own");
        if (!home) np.reason = coldSeason(w) > 0.5f ? "I have no home and winter is coming" : "I have no home";
        else np.reason = self.partner != kNoAgent ? "we need a home of our own" : "time I had a home of my own";
        return np;
    }

    static NewProject campfireProject(const Agent& self, Simulation& sim) {
        NewProject np;
        const World& w = sim.world();
        const float cold = coldSeason(w);
        // How cold it is right now (or how cold I am): drives an urgent fire on the first chilly night.
        const float chill = std::max(self.needs.urgency(Need::Warmth), clamp01((kFireChillTemp - w.temperature()) / kFireChillTemp));
        if (cold < kFireMinCold && chill < kFireUrgentChill) return np;
        const Vec2i centre = homeOrHere(self, sim);
        if (!w.structuresNear(centre, kFireNeighbourhood, StructureType::Campfire).empty()) return np;
        if (!w.structuresNear(self.pos, kFireNeighbourhood, StructureType::Campfire).empty()) return np;
        np.type = StructureType::Campfire;
        np.owner = kNoAgent;
        np.anchor = centre;
        np.radius = kFireSiteRadius;
        np.score = (kFireNew + kFireNewCold * cold + kFireNewChill * chill + kFireNewInt * self.trait(Trait::Intelligence) +
                    kFireNewSoc * self.trait(Trait::Sociability)) *
                   industryOf(self);
        np.urgent = chill >= kFireUrgentChill;
        np.label = np.urgent ? "Making a campfire against the cold" : "Laying out a campfire for the cold nights";
        np.reason = np.urgent ? "it's cold and there's no fire anywhere near" : "the cold season is coming and there's no fire nearby";
        return np;
    }

    static NewProject storageProject(const Agent& self, Simulation& sim) {
        NewProject np;
        const World& w = sim.world();
        const Vec2i centre = homeOrHere(self, sim);
        if (haveUsable(self, sim, centre, kStorageNeighbourhood, StructureType::Storage)) return np;
        const float prep = winterPrep(w);
        const bool surplus = foodOwned(self, sim) >= kStorageSurplusUnits ||
                             float(self.inventory.total()) >= kStorageCarryFull * float(self.carryCapacity()) ||
                             prep >= kStoragePrepMin;
        if (!surplus) return np;
        np.type = StructureType::Storage;
        const bool communal = communalMinded(self);
        np.owner = communal ? kNoAgent : self.id;
        np.anchor = centre;
        np.radius = kSiteRadiusDefault;
        np.score = (kStorageNew + kStorageNewInt * self.trait(Trait::Intelligence) + kStorageNewPrep * prep) * industryOf(self);
        np.label = communal ? "Laying out a communal storage" : "Laying out a storage of my own";
        np.reason = std::string(prep >= kStoragePrepMin ? "winter is coming" : "I have food to put by") +
                    (communal ? ", and a shared store helps everyone" : ", and I keep what's mine");
        return np;
    }

    static NewProject farmProject(const Agent& self, Simulation& sim) {
        NewProject np;
        const World& w = sim.world();
        const float intel = self.trait(Trait::Intelligence);
        if (intel < kFarmMinInt || self.inventory.get(Item::Seeds) < tune::kSeedsPerPlanting) return np;
        const float season = kFarmSeason[int(w.season())];
        if (season <= 0.f) return np;
        const Vec2i centre = homeOrHere(self, sim);
        // Enough fields for my household? (own/household fields count fully, communal ones partly)
        float have = 0.f;
        for (StructureId id : w.structuresNear(centre, kFarmNeighbourhood, StructureType::Farm)) {
            const Structure* f = w.structure(id);
            if (!f) continue;
            if (isHousehold(self, f->owner, sim) || (f->founder == self.id && !f->complete)) have += 1.f;
            else if (f->isCommunal()) have += kCommunalFarmShare;
        }
        const float want = std::ceil(householdMouths(self, sim) / kMouthsPerFarm);
        if (have >= want) return np;
        np.type = StructureType::Farm;
        np.owner = communalMinded(self) ? kNoAgent : self.id;
        np.anchor = centre;
        np.radius = kFarmSiteRadius;
        np.score = (kFarmNew + kFarmNewInt * (intel - kFarmMinInt) / (1.f - kFarmMinInt) +
                    kFarmNewSkill * self.skills.get(Skill::Farming)) *
                   season * industryOf(self);
        np.label = "Marking out a farm on fertile ground";
        np.reason = have > 0.f ? "more mouths than our fields can feed" : "I have seeds; a field would feed us through the winter";
        return np;
    }

    static NewProject monumentProject(const Agent& self, Simulation& sim) {
        NewProject np;
        const World& w = sim.world();
        const float drive = self.trait(Trait::Industriousness) + self.trait(Trait::Creativity);
        if (drive < kMonumentMinDrive) return np;
        const Structure* home = effectiveHome(self, sim);
        if (!home) return np;
        if (self.needs.get(Need::Food) < 0.55f || self.needs.get(Need::Rest) < 0.4f) return np;
        if (reserveDays(self, sim) < kMonumentMinReserve) return np;
        if (!w.structuresNear(home->pos, kMonumentNeighbourhood, StructureType::Monument).empty()) return np;
        np.type = StructureType::Monument;
        np.owner = kNoAgent;
        // Monuments go where people gather: by a campfire if there is one.
        const StructureId fire = w.nearestStructure(home->pos, kMonumentNeighbourhood, StructureType::Campfire);
        np.anchor = fire != kNoStructure ? w.structure(fire)->pos : home->pos;
        np.radius = kSiteRadiusDefault;
        np.score = std::min(kMonumentMaxScore, kMonumentNew + kMonumentNewScale * (drive - kMonumentMinDrive)) *
                   (1.f - 0.5f * winterPrep(w));
        np.label = "Laying the foundation of a monument";
        np.reason = "my needs are met; I want to make something that lasts";
        return np;
    }

    static NewProject wallProject(const Agent& self, Simulation& sim) {
        NewProject np;
        const World& w = sim.world();
        const Tick now = sim.now();
        const Tick window = Tick(kWallMemoryDays) * Tick(cfg::kTicksPerDay);
        AgentId thief = kNoAgent;
        for (const Memory& m : self.memories) {
            if (m.type == MemoryType::StolenFrom && now - m.when <= window) thief = m.other;
        }
        if (thief == kNoAgent) return np;
        const float defensive = self.trait(Trait::Aggression) * 0.5f + self.emotions.get(Emotion::Fear) * 0.6f +
                                self.trait(Trait::Greed) * 0.2f;
        if (defensive < kWallMinDefensive) return np;
        // Protect my storage, else my home.
        StructureId store = w.nearestStructure(self.pos, tune::kStoreRadius, StructureType::Storage,
                                               [&self](const Structure& s) { return s.complete && s.owner == self.id; });
        if (store == kNoStructure) {
            const Structure* h = effectiveHome(self, sim);
            if (h && h->owner == self.id) store = h->id;
        }
        if (store == kNoStructure) return np;
        const Structure* s = w.structure(store);
        if (int(w.structuresNear(s->pos, kWallRing, StructureType::Wall).size()) >= kMaxWallsPerStore) return np;
        np.type = StructureType::Wall;
        np.owner = self.id;
        np.anchor = s->pos;
        np.radius = kWallRing;
        np.score = (kWallNew + kWallNewScale * defensive) * industryOf(self);
        np.label = "Raising a wall around " + structPhrase(self, *s, sim);
        np.reason = sim.nameOf(thief) + " stole from me; never again";
        return np;
    }

    static NewProject workshopProject(const Agent& self, Simulation& sim) {
        NewProject np;
        const World& w = sim.world();
        if (self.trait(Trait::Industriousness) + self.trait(Trait::Intelligence) < kWorkshopMinTraits) return np;
        if (!w.structuresNear(self.pos, kWorkshopNeighbourhood, StructureType::Workshop).empty()) return np;
        int toolless = self.inventory.has(Item::Tools) ? 0 : 1;
        float sx = float(self.pos.x), sy = float(self.pos.y);
        for (const Agent* o : sim.agentsNear(self.pos, kWorkshopRadius, self.id)) {
            if (!o->alive || isChild(*o, sim) || o->inventory.has(Item::Tools)) continue;
            ++toolless;
            sx += float(o->pos.x);
            sy += float(o->pos.y);
        }
        if (toolless < kWorkshopToollessMin) return np;
        np.type = StructureType::Workshop;
        np.owner = kNoAgent;
        np.anchor = Vec2i{int(std::lround(sx / float(toolless))), int(std::lround(sy / float(toolless)))};
        if (!w.inBounds(np.anchor)) np.anchor = self.pos;
        np.radius = kSiteRadiusDefault;
        np.score = std::min(kWorkshopMaxScore, kWorkshopNew + kWorkshopPerToolless * float(toolless)) * industryOf(self);
        np.label = "Laying out a workshop for toolmaking";
        np.reason = std::to_string(toolless) + " of us have no tools";
        return np;
    }

    static void proposeRepairs(const Agent& self, Simulation& sim, std::vector<Proposal>& out, float night) {
        const World& w = sim.world();
        const Structure* home = effectiveHome(self, sim);
        const Structure* best = nullptr;
        float bestScore = 0.f;
        for (StructureId id : w.structuresNear(self.pos, kRepairRadius)) {
            const Structure* s = w.structure(id);
            if (!s || !s->complete || s->condition >= tune::kRepairBelow) continue;
            const bool mineHome = s->id == self.home || (home && home->id == s->id);
            if (!mineHome && !isHousehold(self, s->owner, sim)) continue;
            float rely = kRelyOther;
            if (mineHome) rely = kRelyHome;
            else if (s->type == StructureType::Storage || s->type == StructureType::Hut) rely = kRelyStorage;
            else if (s->type == StructureType::Farm) rely = kRelyFarm;
            const float score = repairScore(*s, rely, self) * distanceFactor(chebyshev(self.pos, s->pos), tune::kStructHalfDist);
            if (score > bestScore) {
                bestScore = score;
                best = s;
            }
        }
        if (!best) return;
        const std::string what = structPhrase(self, *best, sim);
        out.push_back(Proposal{bestScore * night, std::make_unique<RepairAction>(best->id, false, "Repairing " + what),
                               what + " is falling apart (condition " + std::to_string(int(best->condition * 100.f)) + "%)"});
    }
};

// =====================================================================================================
// HelpBuild behaviour: others' / communal construction sites, and repairing communal structures.
// =====================================================================================================
class HelpBuildBehavior final : public Behavior {
public:
    const char* name() const override { return "help_build"; }
    void propose(const Agent& self, Simulation& sim, std::vector<Proposal>& out) override {
        if (isChild(self, sim)) return;
        const World& w = sim.world();
        const float night = nightWorkFactor(w, 0.f);

        // Candidate sites: nearest few incomplete sites that aren't my own project and can progress.
        std::vector<std::pair<int, const Structure*>> sites;
        std::vector<std::pair<int, const Structure*>> repairs;
        for (StructureId id : w.structuresNear(self.pos, kHelpRadius)) {
            const Structure* s = w.structure(id);
            if (!s) continue;
            const int d = chebyshev(self.pos, s->pos);
            if (!s->complete) {
                if (s->id == self.project || s->founder == self.id || s->owner == self.id) continue;
                if (!siteCanProgress(*s, self.inventory)) continue;
                sites.emplace_back(d, s);
            } else if (s->isCommunal() && s->condition < tune::kRepairBelow) {
                repairs.emplace_back(d, s);
            }
        }
        std::sort(sites.begin(), sites.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        Proposal best;
        for (std::size_t i = 0; i < sites.size() && int(i) < kHelpCandidates; ++i) {
            const Structure& s = *sites[i].second;
            const DeedContext ctx = helpContext(self, s, sim);
            const Verdict v = judge(self, ctx, sim);
            float score = kHelpBase + kHelpInd * self.trait(Trait::Industriousness) + kHelpJudgeWeight * v.utility;
            if (ctx.target) {
                score += kHelpAffinity * self.affinityFor(ctx.target->id);
                if (self.isFamily(*ctx.target)) score += kHelpFamily;
            }
            score = std::min(kHelpMax, score) * distanceFactor(sites[i].first, kHelpHalfDist) * night;
            if (score <= best.score) continue;
            const std::string what = structPhrase(self, s, sim);
            best.score = score;
            best.action = std::make_unique<SiteWorkAction>(s.id, true, "Helping build " + what);
            best.reason = what + " needs hands" + (v.rationale.empty() ? std::string() : ": " + v.rationale);
        }
        if (best.action) out.push_back(std::move(best));

        // Communal repairs (campfire, storage, workshop, monument, walls).
        std::sort(repairs.begin(), repairs.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        if (!repairs.empty()) {
            const Structure& s = *repairs.front().second;
            float rely = kRelyCommunalOther;
            if (s.type == StructureType::Campfire) rely = kRelyCommunalFire * std::max(0.3f, coldSeason(w));
            else if (s.type == StructureType::Storage) rely = kRelyCommunalStorage;
            const DeedContext ctx = helpContext(self, s, sim);
            const Verdict v = judge(self, ctx, sim);
            const float score = (repairScore(s, rely, self) + kHelpJudgeWeight * 0.5f * v.utility) *
                                distanceFactor(repairs.front().first, tune::kStructHalfDist) * night;
            const std::string what = structPhrase(self, s, sim);
            out.push_back(Proposal{score, std::make_unique<RepairAction>(s.id, true, "Repairing " + what),
                                   what + " is falling apart" + (v.rationale.empty() ? std::string() : ": " + v.rationale)});
        }
    }
};

}  // namespace

void registerBuildBehaviors(BehaviorList& out) {
    out.push_back(std::make_unique<BuildBehavior>());
    out.push_back(std::make_unique<HelpBuildBehavior>());
}

}  // namespace heur::survival
