// Heuristica — shared survival/economy heuristics: seasons, homes & stores, food drive,
// material demand, project bookkeeping. See SurvivalCommon.h.
#include "ai/survival/SurvivalCommon.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "sim/Simulation.h"

namespace heur::survival {

namespace {
// ---- Season shaping -------------------------------------------------------------------------
constexpr float kEveningRamp = 0.08f;       // fraction of a day before nightfall counted as "evening"
constexpr float kPrepStartSummer = 0.45f;   // summer progress at which winter preparation begins
constexpr float kPrepEndOfSummer = 0.5f;    // winterPrep reached at the very end of summer
constexpr float kPrepInWinter = 0.55f;      // topping up stores still matters early in winter, fading
constexpr float kColdLateSummerFrom = 0.7f; // summer progress after which the cold is "coming"
constexpr float kColdEndOfSummer = 0.3f;    // coldSeason() at the end of summer
constexpr float kColdStartAutumn = 0.45f;   // coldSeason() at the start of autumn (-> 1 at its end)
constexpr float kColdStartSpring = 0.5f;    // coldSeason() at the start of spring (fades to 0)

// ---- Food drive ---------------------------------------------------------------------------------
constexpr float kHungerWeight = 1.3f;       // urgency(Food) weight when no food is within reach
constexpr float kHungerCoverDays = 0.6f;    // reachable food (days) that fully covers immediate hunger
constexpr float kStarvingBelow = 0.15f;     // Food level counted as an emergency...
constexpr float kStarvingBonus = 0.25f;     // ...worth this much extra when nothing is within reach
constexpr float kHungerWantDays = 1.5f;     // gather this many days of food when merely hungry
constexpr float kBaseReserveDays = 2.0f;    // everyone likes a couple of days of food put by
constexpr float kWinterReserveDays = 12.f;  // extra days wanted at full winter prep (× foresight)
constexpr float kReserveWeight = 0.18f;     // score of an entirely empty everyday reserve
constexpr float kWinterPrepWeight = 0.32f;  // extra score of an empty winter reserve at full prep (× foresight)
constexpr float kForesightBase = 0.4f;      // foresight = base + Int × a + Ind × b  (0.4..1.0)
constexpr float kForesightInt = 0.35f;
constexpr float kForesightInd = 0.25f;
constexpr float kGreedSurplus = 0.08f;      // Greed × this: hoarding beyond need
constexpr float kIndustrySurplus = 0.06f;   // Industriousness × this: simply enjoys gathering
constexpr float kSurplusCapDays = 20.f;     // surplus drive fades to 0 at this many days stored
constexpr int kCommunalShareCap = 8;        // communal food counted as "within my reach" per store
constexpr float kChildMouth = 0.7f;         // a dependent child eats about this much of an adult's share

// ---- Project drives (base scores, before × industry, sunk cost & distance) ---------------------
constexpr float kHutHomeless = 0.42f;       // homeless adults want a hut badly
constexpr float kHutColdBonus = 0.15f;      // ...more so as winter approaches
constexpr float kHutWithParents = 0.22f;    // adult still sleeping in a parent's hut
constexpr float kHutPartnered = 0.12f;      // ...more if they have a partner to house
constexpr float kHutHoused = 0.15f;         // finishing a hut although a home exists
constexpr float kFireBase = 0.28f;
constexpr float kFireCold = 0.25f;
constexpr float kStorageBase = 0.28f;
constexpr float kStorageInt = 0.15f;
constexpr float kStoragePrep = 0.15f;
constexpr float kFarmBase = 0.26f;
constexpr float kFarmInt = 0.2f;
constexpr float kWorkshopBase = 0.26f;
constexpr float kMonumentBase = 0.16f;
constexpr float kMonumentCre = 0.16f;
constexpr float kWallBase = 0.24f;
constexpr float kWallAgg = 0.15f;
constexpr float kIndustryBase = 0.8f;       // drive × (base + scale × Industriousness)
constexpr float kIndustryScale = 0.4f;
constexpr float kSunkCostBonus = 0.1f;      // nearly finished projects pull harder
constexpr float kMinProgressFrac = 0.2f;    // labour allowed before any materials arrive (clearing ground)
constexpr int kProjectAbandonDist = 45;     // a project this far away is forgotten

// ---- Material demand -------------------------------------------------------------------------------
constexpr float kGatherForProject = 0.95f;  // gathering scores slightly below labouring
constexpr float kCraftBase = 0.18f;         // craftDesire with no tools: base + Ind × a + Int × b
constexpr float kCraftInd = 0.15f;
constexpr float kCraftInt = 0.15f;
constexpr float kToolGatherFactor = 0.85f;  // gathering tool materials vs crafting itself
constexpr float kDyingFuel = 80.f;          // campfire fuel (ticks) counted as "dying"
constexpr int kFireCareRadius = 12;         // fires this close to home/self are "mine to tend"
constexpr float kFireCareColdMin = 0.4f;    // only bother when it's at least this cold
constexpr float kFuelBase = 0.18f;
constexpr float kFuelCold = 0.3f;
constexpr int kFuelFetchUnits = 3;
constexpr float kFuelStockScore = 0.14f;    // put a couple of logs by for winter fires
constexpr int kFuelStockUnits = 2;
constexpr float kSeedsForFarm = 0.28f;      // an empty field is waiting
constexpr float kSeedsForFarmInt = 0.15f;
constexpr float kSeedsToStartFarm = 0.1f;   // thinking ahead to farming
constexpr float kSeedsMinInt = 0.55f;
constexpr float kHerbsHurtBelow = 0.85f;
constexpr float kHerbsHurtBase = 0.2f;
constexpr float kHerbsHurtScale = 1.2f;
constexpr float kHerbsStressAbove = 0.6f;
constexpr float kHerbsStress = 0.1f;
constexpr float kHerbsStock = 0.07f;        // × (0.5 + Int): keep a herb at hand
constexpr float kHerbsStockMinInt = 0.5f;

// ---- Labour ------------------------------------------------------------------------------------------
constexpr float kLabourBase = 0.6f;         // labour = base + Building skill + Strength × k
constexpr float kLabourStrength = 0.3f;
constexpr float kToolLabour = 1.3f;
constexpr float kElderLabour = 0.8f;

// ---- Completion ----------------------------------------------------------------------------------------
constexpr float kCompletePride = 0.4f;
constexpr float kCompleteJoy = 0.2f;
constexpr float kCompleteFun = 0.2f;
constexpr float kHelperPride = 0.25f;
constexpr float kOwnerJoy = 0.25f;
constexpr int kLightWood = 2;               // wood a builder feeds a fresh campfire if carrying it

const Structure* validHut(const World& w, StructureId id) {
    if (id == kNoStructure) return nullptr;
    const Structure* s = w.structure(id);
    return (s && s->complete && s->type == StructureType::Hut) ? s : nullptr;
}

float industry(const Agent& a) { return kIndustryBase + kIndustryScale * a.trait(Trait::Industriousness); }

bool homeIsHousehold(const Agent& a, const Simulation& sim) {
    const Structure* h = validHut(sim.world(), a.home);
    return h && (h->owner == a.id || (a.partner != kNoAgent && h->owner == a.partner));
}
}  // namespace

// ===================================================================================================
// Time, season & stage
// ===================================================================================================
bool isChild(const Agent& a, const Simulation& sim) { return a.stage(sim.now()) == LifeStage::Child; }
bool isElder(const Agent& a, const Simulation& sim) { return a.stage(sim.now()) == LifeStage::Elder; }

float eveningness(const World& w) {
    const float t = w.timeOfDay();
    const float start = cfg::kNightStart - kEveningRamp;
    if (t < start || t >= cfg::kNightStart) return 0.f;
    return (t - start) / kEveningRamp;
}

float nightWorkFactor(const World& w, float urgency) {
    if (!w.isNight()) return 1.f;
    return lerpf(tune::kNightWorkFactor, 1.f, clamp01(urgency));
}

float nightLeisureFactor(const World& w) { return w.isNight() ? tune::kNightLeisureFactor : 1.f; }

float winterPrep(const World& w) {
    const float p = w.seasonProgress();
    switch (w.season()) {
        case Season::Spring: return 0.f;
        case Season::Summer:
            if (p < kPrepStartSummer) return 0.f;
            return (p - kPrepStartSummer) / (1.f - kPrepStartSummer) * kPrepEndOfSummer;
        case Season::Autumn: return lerpf(kPrepEndOfSummer, 1.f, p);
        case Season::Winter: return lerpf(kPrepInWinter, 0.f, p);
    }
    return 0.f;
}

float coldSeason(const World& w) {
    const float p = w.seasonProgress();
    switch (w.season()) {
        case Season::Spring: return lerpf(kColdStartSpring, 0.f, std::min(1.f, p * 2.f));
        case Season::Summer:
            if (p < kColdLateSummerFrom) return 0.f;
            return (p - kColdLateSummerFrom) / (1.f - kColdLateSummerFrom) * kColdEndOfSummer;
        case Season::Autumn: return lerpf(kColdStartAutumn, 1.f, p);
        case Season::Winter: return 1.f;
    }
    return 0.f;
}

// ===================================================================================================
// Homes & stores
// ===================================================================================================
const Structure* effectiveHome(const Agent& a, const Simulation& sim) {
    const World& w = sim.world();
    if (const Structure* h = validHut(w, a.home)) return h;
    if (a.partner != kNoAgent) {
        if (const Agent* p = sim.agent(a.partner)) {
            if (const Structure* h = validHut(w, p->home)) return h;
        }
    }
    if (isChild(a, sim)) {
        const AgentId parents[2] = {a.mother, a.father};
        for (AgentId pid : parents) {
            if (const Agent* p = sim.agent(pid)) {
                if (const Structure* h = validHut(w, p->home)) return h;
            }
        }
    }
    return nullptr;
}

bool isHousehold(const Agent& a, AgentId owner, const Simulation& sim) {
    if (owner == kNoAgent) return false;
    if (owner == a.id) return true;
    if (a.partner != kNoAgent && owner == a.partner) return true;
    if (isChild(a, sim) && (owner == a.mother || owner == a.father)) return true;
    return false;
}

bool canUseStore(const Agent& a, const Structure& s, const Simulation& sim) {
    if (!s.complete) return false;
    if (s.type != StructureType::Storage && s.type != StructureType::Hut && s.type != StructureType::Farm) return false;
    if (s.isCommunal()) return true;
    if (s.id == a.home) return true;
    if (isHousehold(a, s.owner, sim)) return true;
    const Structure* h = effectiveHome(a, sim);
    return h && h->id == s.id;
}

int storedFoodAccessible(const Agent& a, const Simulation& sim, int radius) {
    const World& w = sim.world();
    int food = 0;
    for (StructureId id : w.structuresNear(a.pos, radius)) {
        const Structure* s = w.structure(id);
        if (!s || !canUseStore(a, *s, sim)) continue;
        const int n = s->stock.get(Item::Food);
        food += s->isCommunal() ? std::min(n, kCommunalShareCap) : n;
    }
    return food;
}

float householdMouths(const Agent& a, const Simulation& sim) {
    float m = 1.f;
    if (a.partner != kNoAgent && sim.agent(a.partner)) m += 1.f;
    for (AgentId c : a.children) {
        const Agent* ch = sim.agent(c);
        if (ch && isChild(*ch, sim)) m += kChildMouth;
    }
    return m;
}

float reserveDays(const Agent& a, const Simulation& sim) {
    return float(foodOwned(a, sim)) / (std::max(0.5f, dailyFoodUnits(a)) * householdMouths(a, sim));
}

// ===================================================================================================
// Food drive
// ===================================================================================================
FoodDrive foodDrive(const Agent& a, Simulation& sim) {
    // Memoised per (simulation, agent, tick, position): forage, fish and explore all ask.
    static const Simulation* cSim = nullptr;
    static AgentId cAgent = kNoAgent;
    static Tick cTick = 0;
    static Vec2i cPos{-1, -1};
    static FoodDrive cached;
    if (cSim == &sim && cAgent == a.id && cTick == sim.now() && cPos == a.pos) return cached;

    FoodDrive fd;
    const World& w = sim.world();
    const float daily = std::max(0.5f, dailyFoodUnits(a));
    const float level = a.needs.get(Need::Food);
    const float urg = a.needs.urgency(Need::Food);
    const float reach = float(a.inventory.get(Item::Food)) + float(storedFoodAccessible(a, sim, tune::kStoreRadius));
    const float cover = clamp01(reach / (daily * kHungerCoverDays));
    fd.hunger = urg * kHungerWeight * (1.f - cover);
    if (level < kStarvingBelow && reach < 1.f) fd.hunger += kStarvingBonus;
    fd.score = fd.hunger;
    fd.want = std::max(2, int(std::ceil(daily * kHungerWantDays)));
    fd.purpose = FoodPurpose::Hunger;
    fd.reason = "hungry (food " + std::to_string(int(level * 100.f)) + "%)";

    if (!isChild(a, sim)) {
        const float prep = winterPrep(w);
        const float foresight = kForesightBase + kForesightInt * a.trait(Trait::Intelligence) +
                                kForesightInd * a.trait(Trait::Industriousness);
        const float days = reserveDays(a, sim);
        const float target = kBaseReserveDays + prep * kWinterReserveDays * foresight;
        const float deficit = clamp01(1.f - days / target);
        const float reserve = deficit * (kReserveWeight + prep * kWinterPrepWeight * foresight);
        float surplus = 0.f;
        if (days < kSurplusCapDays) {
            surplus = (a.trait(Trait::Greed) * kGreedSurplus + a.trait(Trait::Industriousness) * kIndustrySurplus) *
                      (1.f - days / kSurplusCapDays);
        }
        fd.score += reserve + surplus;
        if (reserve + surplus > fd.hunger) {
            fd.want = std::max(1, a.freeCarry());  // stockpiling: fill the basket
            if (reserve >= surplus && prep > 0.2f) {
                fd.purpose = FoodPurpose::Winter;
                fd.reason = "stockpiling for winter (" + fmt1(days) + " of " + fmt1(target) + " days stored)";
            } else if (reserve >= surplus) {
                fd.purpose = FoodPurpose::Reserve;
                fd.reason = "putting food by (" + fmt1(days) + " days stored)";
            } else {
                fd.purpose = FoodPurpose::Surplus;
                fd.reason = a.trait(Trait::Greed) > a.trait(Trait::Industriousness) ? "more is always better"
                                                                                     : "there's work to be done";
            }
        }
    }

    cSim = &sim;
    cAgent = a.id;
    cTick = sim.now();
    cPos = a.pos;
    cached = fd;
    return fd;
}

// ===================================================================================================
// Resources & tools
// ===================================================================================================
int searchRadius(const Agent& a, const Simulation& sim) {
    return isChild(a, sim) ? tune::kChildSearchRadius : cfg::kSearchRadius;
}

std::optional<Vec2i> nearestResource(const Agent& a, Simulation& sim, ResourceKind k, int radius) {
    struct Entry {
        ResourceKind kind;
        int r;
        std::optional<Vec2i> found;
    };
    static const Simulation* cSim = nullptr;
    static AgentId cAgent = kNoAgent;
    static Tick cTick = 0;
    static Vec2i cPos{-1, -1};
    static std::vector<Entry> cache;
    if (cSim != &sim || cAgent != a.id || cTick != sim.now() || !(cPos == a.pos)) {
        cSim = &sim;
        cAgent = a.id;
        cTick = sim.now();
        cPos = a.pos;
        cache.clear();
    }
    for (const Entry& e : cache) {
        if (e.kind == k && e.r == radius) return e.found;
    }
    std::optional<Vec2i> found = sim.world().findNearestResource(a.pos, k, radius, 1.f);
    cache.push_back(Entry{k, radius, found});
    return found;
}

bool ownsTools(const Agent& a) { return a.inventory.has(Item::Tools); }

void wearTools(Agent& a, Simulation& sim, float chance) {
    if (!a.inventory.has(Item::Tools)) return;
    if (sim.rng().chance(chance)) {
        a.inventory.take(Item::Tools, 1);
        a.feel(Emotion::Sadness, 0.05f);
    }
}

// ===================================================================================================
// Projects
// ===================================================================================================
const Structure* activeProject(const Agent& a, const Simulation& sim) {
    if (a.project == kNoStructure) return nullptr;
    const Structure* s = sim.world().structure(a.project);
    if (!s || s->complete) return nullptr;
    if (chebyshev(a.pos, s->pos) > kProjectAbandonDist) return nullptr;
    return s;
}

float deliveredFraction(const Structure& s) {
    const ItemCounts& cost = s.bp().cost;
    int total = 0, have = 0;
    for (int i = 0; i < kItemCount; ++i) {
        total += cost[std::size_t(i)];
        have += std::min(cost[std::size_t(i)], s.delivered[std::size_t(i)]);
    }
    return total <= 0 ? 1.f : float(have) / float(total);
}

float allowedProgress(const Structure& s) {
    const float work = s.bp().work;
    if (s.materialsComplete()) return work + 1.f;
    return work * (kMinProgressFrac + (1.f - kMinProgressFrac) * deliveredFraction(s));
}

bool carriesMissingMaterial(const Structure& s, const Inventory& inv) {
    for (int i = 0; i < kItemCount; ++i) {
        const Item it = Item(i);
        if (s.materialsMissing(it) > 0 && inv.get(it) > 0) return true;
    }
    return false;
}

bool siteCanProgress(const Structure& s, const Inventory& inv) {
    if (s.complete) return false;
    if (s.materialsComplete()) return true;
    if (s.progress < allowedProgress(s) - 0.01f) return true;
    return carriesMissingMaterial(s, inv);
}

float projectDrive(const Agent& a, const Structure& s, const Simulation& sim) {
    const World& w = sim.world();
    float d = 0.f;
    switch (s.type) {
        case StructureType::Hut:
            if (!effectiveHome(a, sim)) d = kHutHomeless + kHutColdBonus * coldSeason(w);
            else if (!homeIsHousehold(a, sim)) d = kHutWithParents + (a.partner != kNoAgent ? kHutPartnered : 0.f);
            else d = kHutHoused;
            break;
        case StructureType::Campfire: d = kFireBase + kFireCold * coldSeason(w); break;
        case StructureType::Storage:
            d = kStorageBase + kStorageInt * a.trait(Trait::Intelligence) + kStoragePrep * winterPrep(w);
            break;
        case StructureType::Farm: d = kFarmBase + kFarmInt * a.trait(Trait::Intelligence); break;
        case StructureType::Workshop: d = kWorkshopBase; break;
        case StructureType::Monument: d = kMonumentBase + kMonumentCre * a.trait(Trait::Creativity); break;
        case StructureType::Wall: d = kWallBase + kWallAgg * a.trait(Trait::Aggression); break;
        default: d = 0.2f; break;
    }
    d *= industry(a);
    const float work = std::max(1.f, s.bp().work);
    const float frac = 0.5f * deliveredFraction(s) + 0.5f * clamp01(s.progress / work);
    return d + kSunkCostBonus * frac;
}

float craftDesire(const Agent& a) {
    if (a.inventory.has(Item::Tools)) return 0.f;
    return kCraftBase + kCraftInd * a.trait(Trait::Industriousness) + kCraftInt * a.trait(Trait::Intelligence);
}

float buildLabour(const Agent& a, const Simulation& sim) {
    float l = kLabourBase + a.skills.get(Skill::Building) + a.trait(Trait::Strength) * kLabourStrength;
    if (ownsTools(a)) l *= kToolLabour;
    if (isElder(a, sim)) l *= kElderLabour;
    return l;
}

// ===================================================================================================
// Material demand
// ===================================================================================================
Demand materialDemand(const Agent& a, Simulation& sim, Item item) {
    Demand best;
    auto consider = [&best](float score, int units, std::string why, std::string forWhat) {
        if (units > 0 && score > best.score) {
            best.score = score;
            best.units = units;
            best.reason = std::move(why);
            best.forWhat = std::move(forWhat);
        }
    };
    if (isChild(a, sim)) return best;
    const World& w = sim.world();
    const int have = a.inventory.get(item);
    const std::string iname = itemName(item);

    // 1. My construction project.
    if (const Structure* p = activeProject(a, sim)) {
        const int missing = p->materialsMissing(item) - have;
        if (missing > 0) {
            const std::string what = structPhrase(a, *p, sim);
            consider(projectDrive(a, *p, sim) * kGatherForProject, missing, "need " + iname + " for " + what,
                     "for " + what);
        }
    }

    // 2. Tools (wood + stone).
    if (item == Item::Wood || item == Item::Stone) {
        const float desire = craftDesire(a);
        const int need = (item == Item::Wood ? tune::kToolWood : tune::kToolStone) - have;
        if (desire > 0.f) consider(desire * kToolGatherFactor, need, "need " + iname + " to make tools", "to make tools");
    }

    // 3. Firewood.
    if (item == Item::Wood) {
        const float season = coldSeason(w);
        const float cold = std::max(season, (1.f - w.temperature()) * 0.8f);
        const Structure* home = effectiveHome(a, sim);
        const Vec2i centre = home ? home->pos : a.pos;
        if (cold > kFireCareColdMin) {
            const StructureId fid = w.nearestStructure(centre, kFireCareRadius, StructureType::Campfire,
                                                       [](const Structure& s) { return s.complete && s.fuel < kDyingFuel; });
            if (fid != kNoStructure)
                consider(kFuelBase + kFuelCold * cold, kFuelFetchUnits - have, "the campfire is dying", "for the dying fire");
        }
        if (home && season > kFireCareColdMin)
            consider(kFuelStockScore * season, kFuelStockUnits - have, "firewood for the cold nights", "for firewood");
    }

    // 4. Seeds for farming.
    if (item == Item::Seeds && have < tune::kSeedsPerPlanting) {
        bool emptyField = false, anyFarm = false;
        for (StructureId id : w.structuresNear(a.pos, tune::kStoreRadius, StructureType::Farm)) {
            const Structure* f = w.structure(id);
            if (!f) continue;
            const bool mine = f->isCommunal() || isHousehold(a, f->owner, sim);
            if (!mine) continue;
            anyFarm = true;
            if (f->complete && !f->planted) emptyField = true;
        }
        if (emptyField && w.season() != Season::Winter) {
            consider(kSeedsForFarm + kSeedsForFarmInt * a.trait(Trait::Intelligence), 2 * tune::kSeedsPerPlanting - have,
                     "the field needs sowing", "for the farm");
        } else if (!anyFarm && a.trait(Trait::Intelligence) >= kSeedsMinInt &&
                   (w.season() == Season::Spring || w.season() == Season::Summer)) {
            consider(kSeedsToStartFarm + 0.1f * (a.trait(Trait::Intelligence) - kSeedsMinInt),
                     tune::kSeedsPerPlanting - have, "I could plant these", "to start a farm");
        }
    }

    // 5. Herbs.
    if (item == Item::Herbs && have < 2) {
        if (a.health < kHerbsHurtBelow)
            consider(kHerbsHurtBase + (kHerbsHurtBelow - a.health) * kHerbsHurtScale, 2 - have, "I'm hurt", "to heal");
        if (a.stress > kHerbsStressAbove) consider(kHerbsStress, 1 - have, "my nerves are frayed", "to calm my nerves");
        if (have == 0 && a.trait(Trait::Intelligence) > kHerbsStockMinInt)
            consider(kHerbsStock * (0.5f + a.trait(Trait::Intelligence)), 1, "good to have herbs at hand", "for later");
    }
    return best;
}

// ===================================================================================================
// Completion
// ===================================================================================================
void onStructureCompleted(Structure& s, Agent& worker, Simulation& sim) {
    sim.stats().structuresCompleted++;
    Agent* founder = sim.agent(s.founder);
    Agent& credited = founder ? *founder : worker;
    credited.stats.structuresBuilt++;
    if (founder && founder->project == s.id) founder->project = kNoStructure;
    if (worker.project == s.id) worker.project = kNoStructure;

    // A finished hut becomes its owner's home (and their partner's if they have none of their own).
    if (s.type == StructureType::Hut && s.owner != kNoAgent) {
        if (Agent* owner = sim.agent(s.owner)) {
            if (!homeIsHousehold(*owner, sim)) owner->home = s.id;
            if (Agent* p = sim.agent(owner->partner)) {
                if (!homeIsHousehold(*p, sim)) p->home = s.id;
            }
        }
    }

    // A fresh fire pit gets lit with whatever wood the builder carries (the world gives it initial fuel too).
    if (s.type == StructureType::Campfire) {
        const int n = worker.inventory.take(Item::Wood, kLightWood);
        s.fuel += float(n) * tune::kFuelPerWood;
    }

    worker.feel(Emotion::Joy, kCompleteJoy);
    worker.needs.add(Need::Fun, kCompleteFun);
    const bool helper = founder && founder != &worker;
    worker.feel(Emotion::Pride, helper ? kHelperPride : kCompletePride);
    if (helper) {
        founder->feel(Emotion::Joy, kOwnerJoy);
        founder->feel(Emotion::Pride, kHelperPride);
    }

    std::string text;
    const char* noun = structNoun(s.type);
    if (!helper) {
        text = worker.name + (s.isCommunal() ? " finished building a communal " : " finished building a ") + noun;
    } else {
        text = worker.name + " helped finish " + sim.nameOf(founder->id) + "'s " + noun;
    }
    if (s.type == StructureType::Hut && s.owner != kNoAgent) text += " (a new home)";
    sim.log(EventKind::Build, text, worker.id, helper ? founder->id : kNoAgent);
}

// ===================================================================================================
// Naming
// ===================================================================================================
const char* structNoun(StructureType t) {
    switch (t) {
        case StructureType::Campfire: return "campfire";
        case StructureType::Hut: return "hut";
        case StructureType::Storage: return "storage";
        case StructureType::Farm: return "farm";
        case StructureType::Wall: return "wall";
        case StructureType::Workshop: return "workshop";
        case StructureType::Monument: return "monument";
        default: return "structure";
    }
}

std::string structPhrase(const Agent& a, const Structure& s, const Simulation& sim) {
    const std::string noun = structNoun(s.type);
    if (s.owner == a.id) return "my " + noun;
    if (s.owner != kNoAgent && s.owner == a.partner) return "our " + noun;
    if (s.isCommunal()) {
        switch (s.type) {
            case StructureType::Campfire:
            case StructureType::Workshop:
            case StructureType::Monument:
            case StructureType::Wall: return "the " + noun;
            default: return "the communal " + noun;
        }
    }
    return sim.nameOf(s.owner) + "'s " + noun;
}

std::string structPhraseWorld(const Structure& s, const Simulation& sim) {
    const std::string noun = structNoun(s.type);
    if (!s.isCommunal()) return sim.nameOf(s.owner) + "'s " + noun;
    switch (s.type) {
        case StructureType::Campfire:
        case StructureType::Workshop:
        case StructureType::Monument:
        case StructureType::Wall: return "the " + noun;
        default: return "the communal " + noun;
    }
}

std::string fmt1(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", double(v));
    return buf;
}

}  // namespace heur::survival
