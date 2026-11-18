// Behavioural tests for the heuristic conscience: the verdicts must move in intuitive directions.
#include "TestFramework.h"
#include "agent/Agent.h"
#include "ai/Morality.h"
#include "core/Config.h"
#include "sim/Simulation.h"

using namespace heur;

namespace {

Simulation::Params smallWorld() {
    Simulation::Params p;
    p.width = 60;
    p.height = 30;
    p.initialAgents = 2;
    p.founderClans = 1;
    p.seed = 2024;
    return p;
}

struct Fixture {
    Simulation sim{smallWorld()};
    int nextClan = 100;

    Agent& make(Sex sex, std::initializer_list<std::pair<Trait, float>> traits = {}) {
        Genome g;
        for (auto& x : g.genes) x = 0.5f;
        for (auto [t, v] : traits) g.set(t, v);
        const Vec2i pos = sim.world().nearestWalkable({30, 15}, 30).value_or(Vec2i{30, 15});
        Agent& a = sim.spawnAgent(g, sex, pos, Tick(0) - Tick(25 * cfg::kTicksPerDay), kNoAgent, kNoAgent);
        a.clan = nextClan++;
        for (auto& n : a.needs.level) n = 0.7f;
        a.inventory = Inventory{};
        return a;
    }
};

DeedContext theft(const Agent& victim, int witnesses = 0) {
    DeedContext c;
    c.deed = Deed::StealFromAgent;
    c.target = &victim;
    c.selfGain = 0.5f;
    c.harm = 0.3f;
    c.witnesses = witnesses;
    return c;
}

}  // namespace

TEST(hunger_makes_stealing_more_acceptable) {
    Fixture f;
    Agent& thief = f.make(Sex::Male);
    Agent& victim = f.make(Sex::Female);
    victim.inventory.add(Item::Food, 8);
    thief.needs.set(Need::Food, 0.95f);
    const float fed = judge(thief, theft(victim), f.sim).utility;
    thief.needs.set(Need::Food, 0.03f);
    const float starving = judge(thief, theft(victim), f.sim).utility;
    CHECK_MSG(starving > fed, std::to_string(starving) + " vs " + std::to_string(fed));
}

TEST(empathy_and_integrity_make_stealing_less_acceptable) {
    Fixture f;
    Agent& callous = f.make(Sex::Male, {{Trait::Empathy, 0.05f}, {Trait::Integrity, 0.05f}});
    Agent& saint = f.make(Sex::Male, {{Trait::Empathy, 0.95f}, {Trait::Integrity, 0.95f}});
    Agent& victim = f.make(Sex::Female);
    victim.inventory.add(Item::Food, 6);
    victim.needs.set(Need::Food, 0.4f);
    const float c = judge(callous, theft(victim), f.sim).utility;
    const float s = judge(saint, theft(victim), f.sim).utility;
    CHECK_MSG(c > s, std::to_string(c) + " vs " + std::to_string(s));
    const Verdict vs = judge(saint, theft(victim), f.sim);
    CHECK(vs.utility < 0.f);
    CHECK(vs.guilt > 0.f);
}

TEST(stealing_from_family_feels_worse) {
    Fixture f;
    Agent& thief = f.make(Sex::Male);
    Agent& stranger = f.make(Sex::Female);
    Agent& sister = f.make(Sex::Female);
    thief.mother = sister.mother = 99999;  // shared (deceased) mother => siblings
    sister.clan = thief.clan;
    stranger.inventory.add(Item::Food, 6);
    sister.inventory.add(Item::Food, 6);
    const float s = judge(thief, theft(stranger), f.sim).utility;
    const float k = judge(thief, theft(sister), f.sim).utility;
    CHECK_MSG(s > k, std::to_string(s) + " vs " + std::to_string(k));
}

TEST(witnesses_deter_the_timid) {
    Fixture f;
    Agent& thief = f.make(Sex::Male, {{Trait::Bravery, 0.05f}});
    Agent& victim = f.make(Sex::Female);
    victim.inventory.add(Item::Food, 6);
    const float alone = judge(thief, theft(victim, 0), f.sim).utility;
    const float crowd = judge(thief, theft(victim, 6), f.sim).utility;
    CHECK_MSG(alone > crowd, std::to_string(alone) + " vs " + std::to_string(crowd));
}

TEST(hoarders_are_fairer_game_for_the_greedy) {
    Fixture f;
    Agent& thief = f.make(Sex::Male, {{Trait::Greed, 0.85f}});
    thief.needs.set(Need::Food, 0.3f);
    Agent& poor = f.make(Sex::Female);
    Agent& rich = f.make(Sex::Female);
    poor.inventory.add(Item::Food, 1);
    rich.inventory.add(Item::Food, 18);
    poor.needs.set(Need::Food, 0.3f);
    rich.needs.set(Need::Food, 0.95f);
    const float p = judge(thief, theft(poor), f.sim).utility;
    const float r = judge(thief, theft(rich), f.sim).utility;
    CHECK_MSG(r > p, std::to_string(r) + " vs " + std::to_string(p));
}

TEST(grievance_fuels_revenge) {
    Fixture f;
    Agent& avenger = f.make(Sex::Male, {{Trait::Vengefulness, 0.9f}, {Trait::Aggression, 0.7f}});
    Agent& wrongdoer = f.make(Sex::Male);
    DeedContext c;
    c.deed = Deed::Attack;
    c.target = &wrongdoer;
    c.harm = 0.4f;
    const float calm = judge(avenger, c, f.sim).utility;
    Relationship& r = avenger.rel(wrongdoer.id);
    r.grievance = 0.9f;
    r.affinity = -0.7f;
    avenger.feel(Emotion::Anger, 0.8f);
    const float wronged = judge(avenger, c, f.sim).utility;
    CHECK_MSG(wronged > calm, std::to_string(wronged) + " vs " + std::to_string(calm));
}

TEST(sharing_feels_better_when_they_are_starving) {
    Fixture f;
    Agent& giver = f.make(Sex::Female, {{Trait::Empathy, 0.8f}});
    giver.inventory.add(Item::Food, 10);
    giver.needs.set(Need::Food, 0.9f);
    Agent& needy = f.make(Sex::Male);
    DeedContext c;
    c.deed = Deed::Share;
    c.target = &needy;
    c.benefit = 0.5f;
    needy.needs.set(Need::Food, 0.9f);
    const float full = judge(giver, c, f.sim).utility;
    needy.needs.set(Need::Food, 0.05f);
    const float starving = judge(giver, c, f.sim).utility;
    CHECK_MSG(starving > full, std::to_string(starving) + " vs " + std::to_string(full));
    CHECK(starving > 0.f);
}

TEST(witnesses_disapprove_of_theft_from_their_friends) {
    Fixture f;
    Agent& thief = f.make(Sex::Male);
    Agent& victim = f.make(Sex::Female);
    Agent& friendOfVictim = f.make(Sex::Female, {{Trait::Empathy, 0.7f}});
    friendOfVictim.rel(victim.id).affinity = 0.8f;
    victim.inventory.add(Item::Food, 4);
    const Appraisal a = appraise(friendOfVictim, thief, theft(victim), f.sim);
    CHECK(a.approval < 0.f);
    CHECK(a.affinityDelta < 0.f);
    CHECK(a.trustDelta < 0.f);
}
