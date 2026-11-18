// Heuristica — simulation core: tick loop, population, births, deaths, spatial index, log & stats.
#include "sim/Simulation.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <unordered_set>

#include "ai/Action.h"
#include "core/Config.h"

namespace heur {

namespace {
constexpr std::size_t kMaxLog = 600;
constexpr int kFounderMinAgeDays = 14;
constexpr int kFounderMaxAgeDays = 40;
constexpr int kFounderSpawnRadius = 4;
constexpr float kFounderPartnerChance = 0.45f;  // chance an opposite-sex pair in a group starts bonded
constexpr float kGriefAffinity = 0.35f;         // friends above this affinity grieve a death
constexpr float kNormInheritNoise = 0.05f;

// Syllable tables for generated names.
const char* const kOnsets[] = {"b", "d", "f", "g", "h", "j", "k", "l", "m", "n", "p", "r", "s", "t", "v", "z",
                               "th", "sh", "kr", "br", "dr", "el", "al", "or", "y", "w", "c", "gr", "st", "ph"};
const char* const kVowels[] = {"a", "e", "i", "o", "u", "ae", "ia", "ei", "ou", "y", "aa", "io"};
const char* const kFemEnd[] = {"a", "ra", "na", "lia", "wyn", "ine", "is", "elle", "ah", "ys", "eth", "ira"};
const char* const kMaleEnd[] = {"n", "r", "k", "m", "s", "th", "dor", "ric", "an", "ok", "us", "ir"};

template <class T, std::size_t N>
const T& pickFrom(Random& rng, const T (&arr)[N]) {
    return arr[std::size_t(rng.rangeInt(0, int(N) - 1))];
}

Norms blendNorms(const Norms& a, const Norms& b, Random& rng) {
    auto mix = [&](float x, float y) { return clamp01((x + y) * 0.5f + rng.normal(0.f, kNormInheritNoise)); };
    Norms n;
    n.theftTolerance = mix(a.theftTolerance, b.theftTolerance);
    n.violenceTolerance = mix(a.violenceTolerance, b.violenceTolerance);
    n.sharingExpectation = mix(a.sharingExpectation, b.sharingExpectation);
    n.punishmentExpectation = mix(a.punishmentExpectation, b.punishmentExpectation);
    return n;
}

bool contains(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }
}  // namespace

const char* eventKindName(EventKind k) {
    switch (k) {
        case EventKind::Birth: return "birth";
        case EventKind::Death: return "death";
        case EventKind::Theft: return "theft";
        case EventKind::Gift: return "gift";
        case EventKind::Fight: return "fight";
        case EventKind::Build: return "build";
        case EventKind::Romance: return "romance";
        case EventKind::Breakdown: return "breakdown";
        case EventKind::Justice: return "justice";
        case EventKind::Social: return "social";
        case EventKind::World: return "world";
        case EventKind::Misc: return "misc";
        default: return "?";
    }
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
Simulation::Simulation(const Params& p)
    : params_(p), rng_(p.seed), world_(p.width, p.height) {
    world_.generate(rng_);
    cellsW_ = (p.width + kCell - 1) / kCell;
    cellsH_ = (p.height + kCell - 1) / kCell;
    cells_.assign(std::size_t(cellsW_ * cellsH_), {});
    spawnFounders();
    rebuildSpatialIndex();
    takeSample();
}

Simulation::~Simulation() {
    for (auto& a : agents_) brain_.abort(*a, *this);
}

void Simulation::spawnFounders() {
    const int groups = std::max(1, params_.founderClans);
    const int total = std::max(2, params_.initialAgents);

    // Pick group centres: walkable grass with food nearby, spread apart (greedy farthest-point).
    std::vector<Vec2i> candidates;
    for (int tries = 0; tries < 600 && candidates.size() < 120; ++tries) {
        const Vec2i c{rng_.rangeInt(4, world_.width() - 5), rng_.rangeInt(4, world_.height() - 5)};
        if (!world_.walkable(c) || world_.at(c).terrain != Terrain::Grass) continue;
        if (!world_.findNearestResource(c, ResourceKind::BerryBush, 10)) continue;
        candidates.push_back(c);
    }
    if (candidates.empty()) {
        if (auto w = world_.nearestWalkable({world_.width() / 2, world_.height() / 2}, 60)) candidates.push_back(*w);
        else candidates.push_back({world_.width() / 2, world_.height() / 2});
    }
    std::vector<Vec2i> centres{candidates[std::size_t(rng_.rangeInt(0, int(candidates.size()) - 1))]};
    while (int(centres.size()) < groups) {
        Vec2i best = candidates.front();
        int bestD = -1;
        for (Vec2i c : candidates) {
            int d = 1 << 30;
            for (Vec2i e : centres) d = std::min(d, chebyshev(c, e));
            if (d > bestD) { bestD = d; best = c; }
        }
        centres.push_back(best);
    }

    for (int g = 0; g < groups; ++g) {
        const int clan = nextClan_++;
        // Each tribe starts with its own moral culture.
        Norms culture;
        culture.theftTolerance = rng_.range(0.05f, 0.35f);
        culture.violenceTolerance = rng_.range(0.02f, 0.22f);
        culture.sharingExpectation = rng_.range(0.30f, 0.75f);
        culture.punishmentExpectation = rng_.range(0.30f, 0.70f);

        const int count = total / groups + (g < total % groups ? 1 : 0);
        std::vector<Agent*> members;
        for (int i = 0; i < count; ++i) {
            Vec2i pos = centres[std::size_t(g)];
            if (auto r = world_.randomWalkableNear(pos, kFounderSpawnRadius, rng_)) pos = *r;
            const int age = rng_.rangeInt(kFounderMinAgeDays, kFounderMaxAgeDays);
            const Tick birth = Tick(0) - Tick(age) * Tick(cfg::kTicksPerDay);  // wraps on purpose
            const Sex sex = (i % 2 == 0) ? Sex::Female : Sex::Male;
            Agent& a = spawnAgent(Genome::random(rng_), sex, pos, birth, kNoAgent, kNoAgent);
            a.clan = clan;
            a.generation = 0;
            a.norms = blendNorms(culture, culture, rng_);
            for (auto& s : a.skills.v) s = rng_.range(0.f, 0.3f);
            for (int n = 0; n < kNeedCount; ++n) a.needs.level[std::size_t(n)] = rng_.range(0.55f, 0.95f);
            a.inventory.add(Item::Food, rng_.rangeInt(2, 5));
            members.push_back(&a);
        }
        // Tribe-mates know each other; some opposite-sex pairs start bonded.
        for (Agent* a : members) {
            for (Agent* b : members) {
                if (a == b) continue;
                Relationship& r = a->rel(b->id);
                r.affinity = rng_.range(0.05f, 0.4f);
                r.trust = rng_.range(0.55f, 0.75f);
                r.lastSeen = 0;
            }
        }
        for (std::size_t i = 0; i + 1 < members.size(); i += 2) {
            Agent* f = members[i];
            Agent* m = members[i + 1];
            if (f->sex == m->sex || !rng_.chance(kFounderPartnerChance)) continue;
            f->partner = m->id;
            m->partner = f->id;
            for (auto [x, y] : {std::pair{f, m}, std::pair{m, f}}) {
                Relationship& r = x->rel(y->id);
                r.affinity = rng_.range(0.55f, 0.85f);
                r.trust = rng_.range(0.7f, 0.9f);
                r.attraction = rng_.range(0.5f, 0.9f);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------
void Simulation::step() {
    if (now_ > 0 && now_ % Tick(cfg::kTicksPerDay) == 0) takeSample();

    world_.update(now_, rng_);
    rebuildSpatialIndex();

    std::vector<Agent*> order;
    order.reserve(agents_.size());
    for (auto& a : agents_)
        if (a->alive) order.push_back(a.get());
    rng_.shuffle(order.begin(), order.end());

    for (Agent* a : order) {
        if (!a->alive) continue;
        a->updateBody(*this);
        if (!a->alive) continue;
        a->updateMind(*this);
        if (!a->alive) continue;
        brain_.think(*a, *this);
        if (!a->alive) continue;
        brain_.act(*a, *this);
    }

    processBirths();
    processDeaths();
    ++now_;
}

void Simulation::rebuildSpatialIndex() {
    for (auto& c : cells_) c.clear();
    for (auto& a : agents_) {
        if (!a->alive) continue;
        const int cx = std::clamp(a->pos.x / kCell, 0, cellsW_ - 1);
        const int cy = std::clamp(a->pos.y / kCell, 0, cellsH_ - 1);
        cells_[std::size_t(cy * cellsW_ + cx)].push_back(a.get());
    }
}

std::vector<Agent*> Simulation::agentsNear(Vec2i p, int radius, AgentId exclude) {
    std::vector<Agent*> out;
    // Search one extra cell of slack: agents may have moved a tile since the index was built.
    const int x0 = std::max(0, (p.x - radius - 1) / kCell), x1 = std::min(cellsW_ - 1, (p.x + radius + 1) / kCell);
    const int y0 = std::max(0, (p.y - radius - 1) / kCell), y1 = std::min(cellsH_ - 1, (p.y + radius + 1) / kCell);
    for (int cy = y0; cy <= y1; ++cy) {
        for (int cx = x0; cx <= x1; ++cx) {
            for (Agent* a : cells_[std::size_t(cy * cellsW_ + cx)]) {
                if (!a->alive || a->id == exclude) continue;
                if (chebyshev(a->pos, p) <= radius) out.push_back(a);
            }
        }
    }
    return out;
}

std::vector<const Agent*> Simulation::agentsNear(Vec2i p, int radius, AgentId exclude) const {
    auto tmp = const_cast<Simulation*>(this)->agentsNear(p, radius, exclude);
    return std::vector<const Agent*>(tmp.begin(), tmp.end());
}

Agent* Simulation::agent(AgentId id) {
    auto it = byId_.find(id);
    if (it == byId_.end() || !it->second->alive) return nullptr;
    return it->second;
}

const Agent* Simulation::agent(AgentId id) const {
    auto it = byId_.find(id);
    if (it == byId_.end() || !it->second->alive) return nullptr;
    return it->second;
}

int Simulation::population() const {
    int n = 0;
    for (auto& a : agents_)
        if (a->alive) ++n;
    return n;
}

std::string Simulation::nameOf(AgentId id) const {
    if (id == kNoAgent) return "someone";
    auto it = byId_.find(id);
    if (it != byId_.end()) return it->second->name;
    auto dit = deadNames_.find(id);
    if (dit != deadNames_.end()) return dit->second;
    return "someone";
}

// ---------------------------------------------------------------------------
// Life & death
// ---------------------------------------------------------------------------
Agent& Simulation::spawnAgent(const Genome& g, Sex sex, Vec2i pos, Tick birthTick, AgentId mother, AgentId father) {
    const AgentId id = nextAgentId_++;
    auto owned = std::make_unique<Agent>(id, makeName(sex), sex, g, birthTick);
    Agent& a = *owned;
    a.pos = pos;
    a.mother = mother;
    a.father = father;
    a.nextThink = now_ + Tick(rng_.rangeInt(0, cfg::kThinkInterval));

    Agent* mom = agent(mother);
    Agent* dad = agent(father);
    if (mom) {
        a.clan = mom->clan;
        a.home = mom->home;
    } else if (dad) {
        a.clan = dad->clan;
    }
    a.generation = std::max(mom ? mom->generation : 0, dad ? dad->generation : 0) + ((mom || dad) ? 1 : 0);
    if (mom && dad) a.norms = blendNorms(mom->norms, dad->norms, rng_);
    else if (mom) a.norms = blendNorms(mom->norms, mom->norms, rng_);
    else if (dad) a.norms = blendNorms(dad->norms, dad->norms, rng_);

    for (Agent* parent : {mom, dad}) {
        if (!parent) continue;
        parent->children.push_back(id);
        Relationship& pr = parent->rel(id);
        pr.affinity = 0.85f;
        pr.trust = 0.9f;
        Relationship& cr = a.rel(parent->id);
        cr.affinity = 0.75f;
        cr.trust = 0.95f;
        // Siblings know each other.
        for (AgentId sib : parent->children) {
            if (sib == id) continue;
            if (Agent* s = agent(sib)) {
                a.rel(sib).affinity = std::max(a.rel(sib).affinity, 0.5f);
                a.rel(sib).trust = std::max(a.rel(sib).trust, 0.75f);
                s->rel(id).affinity = std::max(s->rel(id).affinity, 0.5f);
                s->rel(id).trust = std::max(s->rel(id).trust, 0.75f);
            }
        }
    }

    stats_.maxGeneration = std::max(stats_.maxGeneration, a.generation);
    byId_[id] = &a;
    agents_.push_back(std::move(owned));
    return a;
}

bool Simulation::conceive(Agent& mother, Agent& father) {
    if (!mother.alive || !father.alive) return false;
    if (mother.sex != Sex::Female || father.sex != Sex::Male) return false;
    if (mother.pregnant || !mother.isFertile(now_) || !father.isFertile(now_)) return false;
    mother.pregnant = true;
    mother.dueTick = now_ + Tick(cfg::kPregnancyDays) * Tick(cfg::kTicksPerDay);
    mother.fetusGenome = Genome::inherit(mother.genome, father.genome, rng_);
    mother.fetusFather = father.id;
    mother.lastConceptionTick = std::max<Tick>(1, now_);
    log(EventKind::Romance, mother.name + " is expecting a child with " + father.name, mother.id, father.id);
    return true;
}

void Simulation::processBirths() {
    // Collect first: spawnAgent appends to agents_.
    std::vector<Agent*> due;
    for (auto& a : agents_)
        if (a->alive && a->pregnant && now_ >= a->dueTick) due.push_back(a.get());

    for (Agent* mom : due) {
        mom->pregnant = false;
        const Sex sex = rng_.chance(0.5f) ? Sex::Female : Sex::Male;
        Agent& child = spawnAgent(mom->fetusGenome, sex, mom->pos, now_, mom->id, mom->fetusFather);
        child.needs.set(Need::Food, 0.9f);
        child.needs.set(Need::Warmth, 0.9f);
        child.needs.set(Need::Social, 0.9f);
        ++stats_.births;
        ++mom->stats.childrenBorn;
        mom->remember(MemoryType::ChildBorn, child.id, now_, 1.f);
        mom->feel(Emotion::Joy, 0.7f);
        mom->stress = std::max(0.f, mom->stress - 0.2f);
        std::string text = child.name + " was born to " + mom->name;
        if (Agent* dad = agent(mom->fetusFather)) {
            ++dad->stats.childrenBorn;
            dad->remember(MemoryType::ChildBorn, child.id, now_, 1.f);
            dad->feel(Emotion::Joy, 0.6f);
            text += " and " + dad->name;
        } else if (mom->fetusFather != kNoAgent) {
            text += " and the late " + nameOf(mom->fetusFather);
        }
        text += " (gen " + std::to_string(child.generation) + ")";
        log(EventKind::Birth, text, child.id, mom->id);
        mom->fetusFather = kNoAgent;
    }
}

void Simulation::processDeaths() {
    bool anyDead = false;
    for (auto& up : agents_) {
        Agent& d = *up;
        if (d.alive) continue;
        anyDead = true;
        brain_.abort(d, *this);

        ++stats_.deaths;
        if (d.killer != kNoAgent) ++stats_.killings;
        else if (contains(d.causeOfDeath, "starv")) ++stats_.starvations;
        else if (contains(d.causeOfDeath, "cold") || contains(d.causeOfDeath, "freez")) ++stats_.freezings;
        else if (contains(d.causeOfDeath, "old age")) ++stats_.oldAgeDeaths;

        const int age = d.ageDays(now_);
        if (d.killer != kNoAgent) {
            log(EventKind::Death, d.name + " (age " + std::to_string(age) + ") was killed by " + nameOf(d.killer),
                d.id, d.killer);
        } else {
            log(EventKind::Death,
                d.name + " (age " + std::to_string(age) + ") died of " +
                    (d.causeOfDeath.empty() ? std::string("unknown causes") : d.causeOfDeath),
                d.id);
        }

        // Grief & vendetta.
        for (auto& op : agents_) {
            Agent& o = *op;
            if (!o.alive || o.id == d.id) continue;
            const Relationship* r = o.findRel(d.id);
            const bool family = o.isFamily(d);
            if (!family && (!r || r->affinity < kGriefAffinity)) continue;
            const float bond = family ? std::max(0.6f, r ? r->affinity : 0.6f) : r->affinity;
            o.remember(MemoryType::LovedOneDied, d.id, now_, clamp01(bond));
            o.feel(Emotion::Sadness, 0.3f + 0.6f * bond);
            o.stress = clamp01(o.stress + 0.15f * bond);
            o.wantsRethink = true;
            if (d.killer != kNoAgent && d.killer != o.id) {
                Relationship& kr = o.rel(d.killer);
                kr.grievance = clamp01(kr.grievance + 0.4f + 0.4f * bond);
                kr.affinity = clampf(kr.affinity - 0.5f * bond, -1.f, 1.f);
                kr.trust = clamp01(kr.trust - 0.4f * bond);
                o.feel(Emotion::Anger, 0.3f + 0.5f * bond * o.trait(Trait::Vengefulness));
            }
        }

        // Partner bond ends.
        if (Agent* p = agent(d.partner)) {
            if (p->partner == d.id) p->partner = kNoAgent;
        }

        // Heir: partner, else oldest living child, else a living parent.
        Agent* heir = agent(d.partner);
        if (!heir) {
            for (AgentId c : d.children) {
                Agent* ch = agent(c);
                if (ch && (!heir || ch->birthTick < heir->birthTick)) heir = ch;
            }
        }
        if (!heir) heir = agent(d.mother);
        if (!heir) heir = agent(d.father);

        // Inheritance of possessions: carry what fits, the rest into the heir's home, the rest is lost.
        if (heir) {
            Structure* heirHome = world_.structure(heir->home);
            for (int i = 0; i < kItemCount; ++i) {
                const Item it = Item(i);
                int n = d.inventory.get(it);
                const int carry = std::min(n, heir->freeCarry());
                heir->inventory.add(it, carry);
                n -= carry;
                if (n > 0 && heirHome && heirHome->complete) {
                    const int store = std::min(n, heirHome->stockFree());
                    heirHome->stock.add(it, store);
                }
            }
        }
        d.inventory = Inventory{};

        // Property passes to the heir or becomes communal.
        for (auto& [sid, s] : world_.structures()) {
            if (s.owner == d.id) s.owner = heir ? heir->id : kNoAgent;
        }
        if (heir && heir->home == kNoStructure && d.home != kNoStructure && world_.structure(d.home))
            heir->home = d.home;

        deadNames_[d.id] = d.name;
        byId_.erase(d.id);
    }
    if (anyDead) {
        agents_.erase(std::remove_if(agents_.begin(), agents_.end(), [](const auto& a) { return !a->alive; }),
                      agents_.end());
    }
}

// ---------------------------------------------------------------------------
// Log, names, stats
// ---------------------------------------------------------------------------
void Simulation::log(EventKind k, std::string text, AgentId a, AgentId b) {
    if (echoLog_) {
        std::printf("[D%-3d %-9s] %s\n", day(), eventKindName(k), text.c_str());
    }
    log_.push_back(LogEntry{now_, k, std::move(text), a, b});
    while (log_.size() > kMaxLog) log_.pop_front();
}

std::string Simulation::makeName(Sex sex) {
    std::unordered_set<std::string> used;
    for (auto& a : agents_)
        if (a->alive) used.insert(a->name);
    std::string name;
    for (int attempt = 0; attempt < 8; ++attempt) {
        name = pickFrom(rng_, kOnsets);
        name += pickFrom(rng_, kVowels);
        if (rng_.chance(0.45f)) {
            name += pickFrom(rng_, kOnsets);
            name += pickFrom(rng_, kVowels);
        }
        name += sex == Sex::Female ? pickFrom(rng_, kFemEnd) : pickFrom(rng_, kMaleEnd);
        name[0] = char(std::toupper(static_cast<unsigned char>(name[0])));
        if (name.size() > 9) name.resize(9);
        if (!used.count(name)) break;
    }
    return name;
}

void Simulation::takeSample() {
    Sample s;
    s.tick = now_;
    s.day = day();
    int n = 0;
    for (auto& up : agents_) {
        const Agent& a = *up;
        if (!a.alive) continue;
        ++n;
        if (a.stage(now_) == LifeStage::Child) ++s.children;
        s.avgMood += a.mood();
        s.avgStress += a.stress;
        s.avgHealth += a.health;
        s.avgFood += a.needs.get(Need::Food);
        for (int t = 0; t < kTraitCount; ++t) s.avgTraits[std::size_t(t)] += a.genome.genes[std::size_t(t)];
        s.avgTheftTolerance += a.norms.theftTolerance;
        s.avgViolenceTolerance += a.norms.violenceTolerance;
        s.avgSharingExpectation += a.norms.sharingExpectation;
    }
    s.population = n;
    if (n > 0) {
        const float inv = 1.f / float(n);
        s.avgMood *= inv;
        s.avgStress *= inv;
        s.avgHealth *= inv;
        s.avgFood *= inv;
        for (auto& t : s.avgTraits) t *= inv;
        s.avgTheftTolerance *= inv;
        s.avgViolenceTolerance *= inv;
        s.avgSharingExpectation *= inv;
    }
    for (auto& [id, st] : world_.structures())
        if (st.complete) ++s.structures;
    s.wildFood = world_.wildFoodTotal();
    s.totals = stats_;
    history_.push_back(s);
}

}  // namespace heur
