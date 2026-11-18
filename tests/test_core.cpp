// Tests for genome, world, pathfinding and the simulation loop.
#include <cmath>

#include "TestFramework.h"
#include "agent/Agent.h"
#include "agent/Genome.h"
#include "core/Config.h"
#include "core/Random.h"
#include "sim/Simulation.h"
#include "world/Pathfinding.h"
#include "world/World.h"

using namespace heur;

TEST(genome_random_and_inherit_stay_in_bounds) {
    Random rng(7);
    for (int i = 0; i < 200; ++i) {
        Genome a = Genome::random(rng), b = Genome::random(rng);
        Genome c = Genome::inherit(a, b, rng);
        for (int t = 0; t < kTraitCount; ++t) {
            CHECK(a.genes[t] >= 0.f && a.genes[t] <= 1.f);
            CHECK(c.genes[t] >= 0.f && c.genes[t] <= 1.f);
        }
        CHECK(a.distance(a) == 0.f);
        CHECK(c.distance(a) <= 1.f);
    }
}

TEST(genome_children_resemble_parents) {
    Random rng(11);
    float toParents = 0.f, toStranger = 0.f;
    for (int i = 0; i < 300; ++i) {
        Genome mom = Genome::random(rng), dad = Genome::random(rng), stranger = Genome::random(rng);
        Genome kid = Genome::inherit(mom, dad, rng);
        toParents += 0.5f * (kid.distance(mom) + kid.distance(dad));
        toStranger += kid.distance(stranger);
    }
    CHECK(toParents < toStranger);
}

TEST(world_generation_is_deterministic_and_habitable) {
    World a(120, 60), b(120, 60);
    Random ra(99), rb(99);
    a.generate(ra);
    b.generate(rb);
    int walk = 0, same = 0, berries = 0, trees = 0;
    for (int y = 0; y < 60; ++y)
        for (int x = 0; x < 120; ++x) {
            const Vec2i p{x, y};
            if (a.at(p).terrain == b.at(p).terrain && a.at(p).resource == b.at(p).resource) ++same;
            if (a.walkable(p)) ++walk;
            if (a.at(p).resource == ResourceKind::BerryBush) ++berries;
            if (a.at(p).resource == ResourceKind::Tree) ++trees;
        }
    CHECK(same == 120 * 60);
    CHECK(walk > 120 * 60 / 2);
    CHECK(berries > 50);
    CHECK(trees > 100);
}

TEST(pathfinding_paths_are_valid) {
    World w(120, 60);
    Random rng(5);
    w.generate(rng);
    int found = 0;
    for (int i = 0; i < 200; ++i) {
        auto s = w.randomWalkableNear({rng.rangeInt(0, 119), rng.rangeInt(0, 59)}, 10, rng);
        auto g = w.randomWalkableNear({rng.rangeInt(0, 119), rng.rangeInt(0, 59)}, 10, rng);
        if (!s || !g || *s == *g) continue;
        auto path = findPath(w, *s, *g, 0);
        if (path.empty()) continue;
        ++found;
        Vec2i prev = *s;
        bool ok = true;
        for (Vec2i p : path) {
            if (chebyshev(prev, p) != 1 || !w.walkable(p)) ok = false;
            if (p.x != prev.x && p.y != prev.y) {  // diagonal: no corner cutting
                if (!w.walkable({p.x, prev.y}) || !w.walkable({prev.x, p.y})) ok = false;
            }
            prev = p;
        }
        CHECK(ok);
        CHECK(path.back() == *g);
    }
    CHECK(found > 100);
}

TEST(pathfinding_respects_range) {
    World w(80, 40);
    Random rng(3);
    w.generate(rng);
    auto s = w.nearestWalkable({20, 20}, 20);
    auto g = w.nearestWalkable({50, 20}, 20);
    CHECK(s.has_value() && g.has_value());
    if (!s || !g) return;
    auto path = findPath(w, *s, *g, 3);
    if (!path.empty()) CHECK(chebyshev(path.back(), *g) <= 3);
}

TEST(structure_lifecycle) {
    World w(80, 40);
    Random rng(8);
    w.generate(rng);
    auto site = w.findBuildSite({40, 20}, StructureType::Campfire, 30, rng);
    CHECK(site.has_value());
    if (!site) return;
    StructureId id = w.beginStructure(StructureType::Campfire, *site, 1, 1);
    CHECK(id != kNoStructure);
    Structure* s = w.structure(id);
    CHECK(s != nullptr && !s->complete);
    Inventory inv;
    inv.add(Item::Wood, 20);
    w.deliverMaterials(*s, inv);
    CHECK(s->materialsComplete());
    bool done = false;
    for (int i = 0; i < 1000 && !done; ++i) done = w.applyWork(*s, 1.f, 1);
    CHECK(done && s->complete);
    CHECK(s->isLit());
    CHECK(w.nearWarmth(*site));
}

TEST(founders_have_adult_ages_with_wrapped_birth_ticks) {
    Simulation::Params p;
    p.seed = 1;
    Simulation sim(p);
    CHECK(sim.population() == p.initialAgents);
    for (auto& a : sim.agents()) {
        const int age = a->ageDays(sim.now());
        CHECK(age >= 14 && age <= 40);
        CHECK(a->isAdult(sim.now()));
    }
}

TEST(simulation_is_deterministic) {
    Simulation::Params p;
    p.seed = 1234;
    Simulation a(p), b(p);
    for (int i = 0; i < cfg::kTicksPerDay * 2; ++i) {
        a.step();
        b.step();
    }
    CHECK(a.population() == b.population());
    CHECK(a.logEntries().size() == b.logEntries().size());
    CHECK(a.stats().thefts == b.stats().thefts);
    CHECK(a.stats().gifts == b.stats().gifts);
    const std::size_t n = std::min(a.agents().size(), b.agents().size());
    for (std::size_t i = 0; i < n; ++i) CHECK(a.agents()[i]->pos == b.agents()[i]->pos);
}

TEST(society_survives_first_ten_days) {
    Simulation::Params p;
    p.seed = 77;
    Simulation sim(p);
    for (int i = 0; i < cfg::kTicksPerDay * 10 && sim.population() > 0; ++i) sim.step();
    CHECK_MSG(sim.population() >= p.initialAgents / 2, "population " + std::to_string(sim.population()));
    CHECK(sim.history().size() >= 10);
}
