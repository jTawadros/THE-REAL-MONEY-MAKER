// Heuristica — the simulation: owns world, agents, brain, event log, statistics.
// FROZEN CONTRACT. Implemented in sim/Simulation.cpp.
#pragma once

#include <array>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "agent/Agent.h"
#include "ai/Brain.h"
#include "core/Config.h"
#include "core/Random.h"
#include "core/Types.h"
#include "world/World.h"

namespace heur {

enum class EventKind : std::uint8_t {
    Birth, Death, Theft, Gift, Fight, Build, Romance, Breakdown, Justice, Social, World, Misc, Count
};
const char* eventKindName(EventKind k);

struct LogEntry {
    Tick tick = 0;
    EventKind kind = EventKind::Misc;
    std::string text;
    AgentId a = kNoAgent;
    AgentId b = kNoAgent;
};

// Cumulative counters (incremented by whoever causes the event).
struct SimStats {
    int births = 0;
    int deaths = 0;
    int starvations = 0;
    int freezings = 0;
    int killings = 0;
    int oldAgeDeaths = 0;
    int thefts = 0;
    int gifts = 0;           // shares + gifts
    int fights = 0;          // attacks
    int punishments = 0;
    int forgivenesses = 0;
    int trades = 0;
    int matings = 0;
    int breakdowns = 0;
    int structuresCompleted = 0;
    int maxGeneration = 0;
};

// Daily snapshot for trend reporting (trait evolution, culture drift, economy).
struct Sample {
    Tick tick = 0;
    int day = 0;
    int population = 0;
    int children = 0;
    float avgMood = 0.f;
    float avgStress = 0.f;
    float avgHealth = 0.f;
    float avgFood = 0.f;            // average Need::Food satisfaction
    std::array<float, kTraitCount> avgTraits{};
    float avgTheftTolerance = 0.f;
    float avgViolenceTolerance = 0.f;
    float avgSharingExpectation = 0.f;
    int structures = 0;             // complete structures
    float wildFood = 0.f;
    SimStats totals;
};

class Simulation {
public:
    struct Params {
        int width = 120;
        int height = 60;
        int initialAgents = 30;     // cfg::kInitialAgents
        int founderClans = 3;       // founders start in this many separate groups
        std::uint64_t seed = 42;
    };

    explicit Simulation(const Params& p);
    ~Simulation();

    // Advance one tick: world.update -> spatial index -> per agent (shuffled): updateBody, updateMind,
    // brain.think, brain.act -> births -> deaths (inheritance, grief, cleanup) -> daily sample.
    void step();

    Tick now() const { return now_; }
    int day() const { return int(now_ / Tick(cfg::kTicksPerDay)); }
    const Params& params() const { return params_; }

    World& world() { return world_; }
    const World& world() const { return world_; }
    Random& rng() { return rng_; }
    Brain& brain() { return brain_; }

    // ---- Agents ---------------------------------------------------------------
    // All agents currently in the simulation (dead ones are removed at the end of the tick in which
    // they died, so always check `alive`).
    const std::vector<std::unique_ptr<Agent>>& agents() const { return agents_; }
    Agent* agent(AgentId id);               // nullptr if unknown or dead
    const Agent* agent(AgentId id) const;
    int population() const;                 // living agents
    // Living agents within chebyshev `radius` of p, excluding `exclude`. Uses the per-tick spatial index
    // (positions are as of the start of the tick — good enough for heuristics).
    std::vector<Agent*> agentsNear(Vec2i p, int radius, AgentId exclude = kNoAgent);
    std::vector<const Agent*> agentsNear(Vec2i p, int radius, AgentId exclude = kNoAgent) const;

    // Creates and registers a new agent (used for founders and births). Name generated automatically.
    Agent& spawnAgent(const Genome& g, Sex sex, Vec2i pos, Tick birthTick, AgentId mother, AgentId father);
    // Starts a pregnancy for `mother` with `father` (checks fertility; returns false if not possible).
    bool conceive(Agent& mother, Agent& father);

    // ---- Events & stats ---------------------------------------------------------
    void log(EventKind k, std::string text, AgentId a = kNoAgent, AgentId b = kNoAgent);
    const std::deque<LogEntry>& logEntries() const { return log_; }
    SimStats& stats() { return stats_; }
    const SimStats& stats() const { return stats_; }
    const std::vector<Sample>& history() const { return history_; }
    // Optional: mirror every log line to stdout (headless verbose mode).
    void setEchoLog(bool echo) { echoLog_ = echo; }

    // Name of an agent or "someone" if unknown/dead (dead names are remembered).
    std::string nameOf(AgentId id) const;

private:
    void rebuildSpatialIndex();
    void processBirths();
    void processDeaths();
    void takeSample();
    void spawnFounders();
    std::string makeName(Sex sex);

    Params params_;
    Random rng_;
    World world_;
    Brain brain_;
    Tick now_ = 0;
    AgentId nextAgentId_ = 1;
    int nextClan_ = 1;
    std::vector<std::unique_ptr<Agent>> agents_;
    std::unordered_map<AgentId, Agent*> byId_;
    std::unordered_map<AgentId, std::string> deadNames_;
    // spatial index: buckets of kCell x kCell tiles
    static constexpr int kCell = 8;
    int cellsW_ = 0, cellsH_ = 0;
    std::vector<std::vector<Agent*>> cells_;
    std::deque<LogEntry> log_;
    SimStats stats_;
    std::vector<Sample> history_;
    bool echoLog_ = false;
};

}  // namespace heur
