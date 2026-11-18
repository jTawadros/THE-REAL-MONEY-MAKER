// Heuristica — headless runs, balancing reports and CSV export.
#pragma once

#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "agent/Agent.h"
#include "render/Census.h"

namespace heur {

class Simulation;

// Remembers every agent ever seen (last snapshot), so reports can name notable dead individuals too.
class Chronicle {
public:
    struct Record {
        AgentId id = kNoAgent;
        std::string name;
        Sex sex = Sex::Female;
        int clan = 0;
        int generation = 0;
        Tick birth = 0;
        Tick lastSeen = 0;
        bool alive = true;
        int children = 0;
        AgentStats stats;
    };
    struct DayPoint {
        int day = 0;
        int population = 0;
        int children = 0;
    };

    // Call once per simulated day (and once at the start).
    void observe(const Simulation& sim);
    const std::unordered_map<AgentId, Record>& records() const { return records_; }
    const std::vector<DayPoint>& days() const { return days_; }
    long long agentDays() const { return agentDays_; }

private:
    std::unordered_map<AgentId, Record> records_;
    std::vector<DayPoint> days_;
    long long agentDays_ = 0;
};

struct HeadlessOptions {
    int days = 120;
    int reportEvery = 0;  // days between status lines; 0 = auto (~24 lines per run)
    bool ascii = false;
};

// Runs the simulation for opt.days (stops early on extinction or Ctrl+C), printing status lines and a
// final report to `out`. Returns the process exit code.
int runHeadless(Simulation& sim, const HeadlessOptions& opt, std::FILE* out);

// "Day 24 [Autumn] pop 41 (9 kids) mood +0.12 stress 0.31 thefts 18 gifts 77 fights 3 built 14 gen 2 ..."
std::string statusLine(const Simulation& sim, const Census& c);

void printFinalReport(std::FILE* out, const Simulation& sim, const Chronicle& chron, const Census& baseline,
                      const Census& finalCensus, bool ascii, const std::string& outcome, double elapsedSeconds);

// Short summary printed after an interactive session ends.
void printSessionSummary(std::FILE* out, const Simulation& sim);

// Writes Simulation::history() as CSV. Returns false and fills `err` on failure.
bool writeHistoryCsv(const Simulation& sim, const std::string& path, std::string& err);

}  // namespace heur
