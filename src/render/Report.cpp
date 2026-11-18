// Heuristica — headless runs, balancing reports and CSV export. See Report.h.
#include "render/Report.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>

#include <unistd.h>

#include "core/Config.h"
#include "render/Screen.h"
#include "sim/Simulation.h"
#include "world/World.h"

namespace heur {

namespace {

volatile std::sig_atomic_t g_headlessStop = 0;
// First Ctrl+C: finish the current tick and print the report. Second: exit immediately.
extern "C" void onHeadlessSignal(int sig) {
    if (g_headlessStop) ::_exit(128 + sig);
    g_headlessStop = 1;
}

struct ScopedSigint {
    struct sigaction oldInt {}, oldTerm {};
    ScopedSigint() {
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = onHeadlessSignal;
        sigemptyset(&sa.sa_mask);
        ::sigaction(SIGINT, &sa, &oldInt);
        ::sigaction(SIGTERM, &sa, &oldTerm);
    }
    ~ScopedSigint() {
        ::sigaction(SIGINT, &oldInt, nullptr);
        ::sigaction(SIGTERM, &oldTerm, nullptr);
    }
    ScopedSigint(const ScopedSigint&) = delete;
    ScopedSigint& operator=(const ScopedSigint&) = delete;
};

std::string sparkline(const std::vector<float>& values, int width, bool ascii) {
    static const char* kUni[] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    static const char* kAsc[] = {"_", ".", ",", "-", "=", "+", "*", "#"};
    std::string out;
    if (values.empty() || width <= 0) return out;
    const int n = int(values.size());
    const int cols = std::min(n, width);
    const float mx = std::max(1.f, *std::max_element(values.begin(), values.end()));
    for (int c = 0; c < cols; ++c) {
        const int a = c * n / cols, b = std::max(a + 1, (c + 1) * n / cols);
        float sum = 0.f;
        for (int i = a; i < b; ++i) sum += values[std::size_t(i)];
        const float v = sum / float(b - a);
        const int level = std::clamp(int(v / mx * 7.999f), 0, 7);
        out += (ascii ? kAsc : kUni)[level];
    }
    return out;
}

const char* arrowFor(float d, bool ascii) {
    if (d > 0.03f) return ascii ? "^" : "▲";
    if (d < -0.03f) return ascii ? "v" : "▼";
    return " ";
}

std::string pad(std::string s, int w) {
    const int tw = textWidth(s);
    if (tw < w) s.append(std::size_t(w - tw), ' ');
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------
// Chronicle
// ---------------------------------------------------------------------------------------------------
void Chronicle::observe(const Simulation& sim) {
    const Tick now = sim.now();
    int pop = 0, kids = 0;
    for (const auto& up : sim.agents()) {
        const Agent& a = *up;
        if (!a.alive) continue;
        ++pop;
        if (a.stage(now) == LifeStage::Child) ++kids;
        Record& r = records_[a.id];
        r.id = a.id;
        if (r.name.empty()) {
            r.name = a.name;
            r.sex = a.sex;
            r.clan = a.clan;
            r.generation = a.generation;
            r.birth = a.birthTick;
        }
        r.lastSeen = now;
        r.alive = true;
        r.children = std::max(int(a.children.size()), a.stats.childrenBorn);
        r.stats = a.stats;
    }
    for (auto& [id, r] : records_)
        if (r.lastSeen != now) r.alive = false;
    if (days_.empty() || days_.back().day != dayOfTick(sim.now())) {
        days_.push_back(DayPoint{dayOfTick(sim.now()), pop, kids});
        if (days_.size() > 1) agentDays_ += pop;
    }
}

// ---------------------------------------------------------------------------------------------------
// Status line
// ---------------------------------------------------------------------------------------------------
std::string statusLine(const Simulation& sim, const Census& c) {
    const SimStats& s = sim.stats();
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "Day %d [%s] pop %d (%d kids) mood %+.2f stress %.2f thefts %d gifts %d fights %d built %d gen %d"
                  " | food %.2f hp %.2f born %d died %d",
                  dayOfTick(sim.now()), seasonLabel(sim.world().season()), c.population, c.children, double(c.avgMood),
                  double(c.avgStress), s.thefts, s.gifts, s.fights, s.structuresCompleted, s.maxGeneration,
                  double(c.avgFood), double(c.avgHealth), s.births, s.deaths);
    return buf;
}

// ---------------------------------------------------------------------------------------------------
// Headless run
// ---------------------------------------------------------------------------------------------------
int runHeadless(Simulation& sim, const HeadlessOptions& opt, std::FILE* out) {
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    ScopedSigint sigGuard;
    g_headlessStop = 0;

    const Census baseline = takeCensus(sim);
    Chronicle chron;
    chron.observe(sim);
    const int days = std::max(1, opt.days);
    const int every = opt.reportEvery > 0 ? opt.reportEvery : std::max(1, days / 24);
    const auto& p = sim.params();
    std::fprintf(out, "Heuristica headless run: seed %llu, map %dx%d, %d founders in %d clans, %d days\n",
                 static_cast<unsigned long long>(p.seed), p.width, p.height, baseline.population, p.founderClans, days);
    std::fprintf(out, "%s\n", statusLine(sim, baseline).c_str());
    std::fflush(out);

    std::string outcome;
    Census last = baseline;
    for (int d = 0; d < days && outcome.empty(); ++d) {
        for (int t = 0; t < cfg::kTicksPerDay; ++t) {
            sim.step();
            if (sim.population() == 0) {
                outcome = "EXTINCT on day " + std::to_string(dayOfTick(sim.now()));
                break;
            }
            if (g_headlessStop) {
                outcome = "INTERRUPTED on day " + std::to_string(dayOfTick(sim.now()));
                break;
            }
        }
        chron.observe(sim);
        const Census c = takeCensus(sim);
        if (c.population > 0) last = c;
        if (sim.population() == 0) {
            std::fprintf(out, "Extinction on day %d\n", dayOfTick(sim.now()));
        } else if ((d + 1) % every == 0 || d + 1 == days || !outcome.empty()) {
            std::fprintf(out, "%s\n", statusLine(sim, c).c_str());
        }
        std::fflush(out);
    }
    if (outcome.empty()) outcome = "SURVIVED " + std::to_string(dayOfTick(sim.now())) + " days with " +
                                   std::to_string(sim.population()) + " alive";
    const double secs = std::chrono::duration<double>(Clock::now() - t0).count();
    printFinalReport(out, sim, chron, baseline, last, opt.ascii, outcome, secs);
    return 0;
}

// ---------------------------------------------------------------------------------------------------
// Final report
// ---------------------------------------------------------------------------------------------------
void printFinalReport(std::FILE* out, const Simulation& sim, const Chronicle& chron, const Census& baseline,
                      const Census& fin, bool ascii, const std::string& outcome, double elapsedSeconds) {
    const SimStats& s = sim.stats();
    const auto& p = sim.params();
    const char* arrow = ascii ? "->" : "→";
    const char* dag = ascii ? "+" : "†";
    const std::string rule(72, '=');
    const std::string thin(72, '-');
    const Tick now = sim.now();
    const Census cur = takeCensus(sim);

    std::fprintf(out, "\n%s\n HEURISTICA REPORT  |  %s\n%s\n", rule.c_str(), outcome.c_str(), rule.c_str());
    std::fprintf(out, " seed %llu | map %dx%d | %d founders | %llu ticks in %.1fs (%.0f ticks/s)\n",
                 static_cast<unsigned long long>(p.seed), p.width, p.height, baseline.population,
                 static_cast<unsigned long long>(now), elapsedSeconds,
                 elapsedSeconds > 0 ? double(now) / elapsedSeconds : 0.0);

    // ---- Population ----
    const auto& days = chron.days();
    int mn = baseline.population, mx = baseline.population, mnDay = 0, mxDay = 0;
    std::vector<float> traj;
    for (const auto& d : days) {
        traj.push_back(float(d.population));
        if (d.population < mn) { mn = d.population; mnDay = d.day; }
        if (d.population > mx) { mx = d.population; mxDay = d.day; }
    }
    const int simDays = std::max(1, int(days.size()) - 1);
    const double avgPop = chron.agentDays() > 0 ? double(chron.agentDays()) / double(simDays) : double(cur.population);
    std::fprintf(out, "\nPOPULATION\n");
    std::fprintf(out, "  start %d | min %d (day %d) | max %d (day %d) | final %d (%d kids, %d elders) | avg %.1f\n",
                 baseline.population, mn, mnDay, mx, mxDay, cur.population, cur.children, cur.elders, avgPop);
    std::fprintf(out, "  trajectory  %s  (%d days)\n", sparkline(traj, 64, ascii).c_str(), simDays);
    const int other = std::max(0, s.deaths - s.starvations - s.freezings - s.killings - s.oldAgeDeaths);
    std::fprintf(out, "  births %d | deaths %d: starved %d, froze %d, killed %d, old age %d, other %d\n", s.births,
                 s.deaths, s.starvations, s.freezings, s.killings, s.oldAgeDeaths, other);
    std::fprintf(out, "  max generation %d | avg age %.1f days | %d/%d female/male | %d pregnant now\n",
                 s.maxGeneration, double(cur.avgAgeDays), cur.females, cur.males, cur.pregnant);

    // ---- Behaviour ----
    std::fprintf(out, "\nBEHAVIOUR                       total   per capita   per 100 agent-days\n");
    const double ad = std::max(1.0, double(chron.agentDays()));
    auto row = [&](const char* name, int n) {
        std::fprintf(out, "  %-26s %7d   %10.2f   %10.2f\n", name, n, avgPop > 0 ? double(n) / avgPop : 0.0,
                     100.0 * double(n) / ad);
    };
    row("thefts", s.thefts);
    row("gifts & shares", s.gifts);
    row("fights (attacks)", s.fights);
    row("killings", s.killings);
    row("punishments", s.punishments);
    row("forgivenesses", s.forgivenesses);
    row("trades", s.trades);
    row("matings", s.matings);
    row("mental breakdowns", s.breakdowns);
    row("structures completed", s.structuresCompleted);
    std::fprintf(out, "  gifts per theft %.2f | punishments per theft %.2f | agent-days %lld\n",
                 s.thefts > 0 ? double(s.gifts) / double(s.thefts) : double(s.gifts),
                 s.thefts > 0 ? double(s.punishments) / double(s.thefts) : 0.0, chron.agentDays());

    // ---- Well-being ----
    const auto& hist = sim.history();
    float hm = 0, hs = 0, hh = 0, hf = 0;
    for (const Sample& smp : hist) {
        hm += smp.avgMood;
        hs += smp.avgStress;
        hh += smp.avgHealth;
        hf += smp.avgFood;
    }
    const float hn = hist.empty() ? 1.f : float(hist.size());
    std::fprintf(out, "\nWELL-BEING          run avg   final\n");
    std::fprintf(out, "  mood              %+7.2f %+7.2f\n", double(hm / hn), double(fin.avgMood));
    std::fprintf(out, "  stress            %7.2f %7.2f\n", double(hs / hn), double(fin.avgStress));
    std::fprintf(out, "  health            %7.2f %7.2f\n", double(hh / hn), double(fin.avgHealth));
    std::fprintf(out, "  food satisfaction %7.2f %7.2f   | food carried now %d, wild food %.0f\n", double(hf / hn),
                 double(fin.avgFood), cur.foodCarried, double(cur.wildFood));

    // ---- Genes ----
    std::fprintf(out, "\nGENES (founders %s %s)        day 0    final   change\n", arrow,
                 cur.population > 0 ? "living" : "last survivors");
    for (int t = 0; t < kTraitCount; ++t) {
        const float b = baseline.avgTraits[std::size_t(t)], f = fin.avgTraits[std::size_t(t)];
        std::fprintf(out, "  %-28s %6.3f   %6.3f   %+.3f %s\n", traitLabel(Trait(t)), double(b), double(f),
                     double(f - b), arrowFor(f - b, ascii));
    }

    // ---- Culture ----
    std::fprintf(out, "\nCULTURE (norms)                 day 0    final   change\n");
    for (int i = 0; i < kNormCount; ++i) {
        const float b = baseline.avgNorms[std::size_t(i)], f = fin.avgNorms[std::size_t(i)];
        std::fprintf(out, "  %-28s %6.3f   %6.3f   %+.3f %s\n", normLabel(i), double(b), double(f), double(f - b),
                     arrowFor(f - b, ascii));
    }
    if (!hist.empty()) {
        std::vector<float> th, sh;
        for (const Sample& smp : hist) {
            th.push_back(smp.avgTheftTolerance);
            sh.push_back(smp.avgSharingExpectation);
        }
        std::fprintf(out, "  theft tolerance over time   %s\n", sparkline(th, 48, ascii).c_str());
        std::fprintf(out, "  sharing expectation         %s\n", sparkline(sh, 48, ascii).c_str());
    }

    // ---- Structures ----
    std::fprintf(out, "\nSTRUCTURES  %d complete, %d under construction\n  ", cur.structuresComplete,
                 cur.structuresBuilding);
    for (int t = 0; t < kStructureTypeCount; ++t)
        std::fprintf(out, "%s %d%s", structureLabel(StructureType(t)), cur.completeByType[std::size_t(t)],
                     t + 1 < kStructureTypeCount ? ", " : "\n");

    // ---- Clans / dynasties ----
    struct ClanInfo {
        int ever = 0, living = 0, deepest = 0, children = 0;
    };
    std::map<int, ClanInfo> clans;
    for (const auto& [id, r] : chron.records()) {
        ClanInfo& c = clans[r.clan];
        ++c.ever;
        if (r.alive) ++c.living;
        c.deepest = std::max(c.deepest, r.generation);
    }
    std::vector<std::pair<int, ClanInfo>> clanList(clans.begin(), clans.end());
    std::sort(clanList.begin(), clanList.end(), [](const auto& a, const auto& b) {
        return a.second.living != b.second.living ? a.second.living > b.second.living : a.second.ever > b.second.ever;
    });
    std::fprintf(out, "\nCLANS / DYNASTIES (%zu)\n", clanList.size());
    for (std::size_t i = 0; i < clanList.size() && i < 6; ++i) {
        const auto& [clan, c] = clanList[i];
        std::fprintf(out, "  clan %-3d %4d members ever, %4d living, deepest generation %d%s\n", clan, c.ever,
                     c.living, c.deepest, c.living == 0 ? "  (died out)" : "");
    }

    // ---- Notable individuals ----
    std::fprintf(out, "\nNOTABLE INDIVIDUALS\n");
    auto describe = [&](const Chronicle::Record& r) {
        char buf[160];
        const int age = int(((r.alive ? now : r.lastSeen) - r.birth) / Tick(cfg::kTicksPerDay));
        if (r.alive)
            std::snprintf(buf, sizeof(buf), "%s (clan %d, gen %d, %c, age %d, living)", r.name.c_str(), r.clan,
                          r.generation, r.sex == Sex::Female ? 'F' : 'M', age);
        else
            std::snprintf(buf, sizeof(buf), "%s (clan %d, gen %d, %c, %s died ~day %d aged %d)", r.name.c_str(), r.clan,
                          r.generation, r.sex == Sex::Female ? 'F' : 'M', dag, dayOfTick(r.lastSeen), age);
        return std::string(buf);
    };
    auto best = [&](const char* title, const std::function<int(const Chronicle::Record&)>& key, const char* unit) {
        const Chronicle::Record* top = nullptr;
        for (const auto& [id, r] : chron.records())
            if (!top || key(r) > key(*top) || (key(r) == key(*top) && r.id < top->id)) top = &r;
        if (!top || key(*top) <= 0) {
            std::fprintf(out, "  %s nobody\n", pad(title, 18).c_str());
            return;
        }
        std::fprintf(out, "  %s %d %s: %s\n", pad(title, 18).c_str(), key(*top), unit, describe(*top).c_str());
    };
    best("Most thefts", [](const Chronicle::Record& r) { return r.stats.thefts; }, "thefts");
    best("Most generous", [](const Chronicle::Record& r) { return r.stats.giftsGiven; }, "gifts");
    best("Most violent", [](const Chronicle::Record& r) { return r.stats.attacks; }, "attacks");
    best("Most kills", [](const Chronicle::Record& r) { return r.stats.kills; }, "kills");
    best("Most children", [](const Chronicle::Record& r) { return r.children; }, "children");
    best("Master builder", [](const Chronicle::Record& r) { return r.stats.structuresBuilt; }, "built");
    best("Most robbed", [](const Chronicle::Record& r) { return r.stats.timesStolenFrom; }, "times");
    best("Most breakdowns", [](const Chronicle::Record& r) { return r.stats.breakdowns; }, "breaks");
    best("Top enforcer", [](const Chronicle::Record& r) { return r.stats.punishmentsGiven; }, "punishments");
    {
        const Chronicle::Record* oldest = nullptr;
        const Chronicle::Record* longest = nullptr;
        for (const auto& [id, r] : chron.records()) {
            if (r.alive && (!oldest || r.birth < oldest->birth)) oldest = &r;
            if (!r.alive && (!longest || r.lastSeen - r.birth > longest->lastSeen - longest->birth)) longest = &r;
        }
        if (oldest) std::fprintf(out, "  %s %s\n", pad("Oldest living", 18).c_str(), describe(*oldest).c_str());
        if (longest) std::fprintf(out, "  %s %s\n", pad("Longest life", 18).c_str(), describe(*longest).c_str());
    }

    // ---- Last events ----
    const auto& log = sim.logEntries();
    std::fprintf(out, "\nLAST EVENTS\n");
    const std::size_t n = std::min<std::size_t>(15, log.size());
    for (std::size_t i = log.size() - n; i < log.size(); ++i) {
        const LogEntry& e = log[i];
        std::fprintf(out, "  D%-4d %-8s %s\n", dayOfTick(e.tick) + 1, eventLabel(e.kind), e.text.c_str());
    }
    std::fprintf(out, "%s\n", thin.c_str());
    std::fflush(out);
}

void printSessionSummary(std::FILE* out, const Simulation& sim) {
    const SimStats& s = sim.stats();
    std::fprintf(out,
                 "Heuristica: stopped on day %d with %d alive | births %d, deaths %d | thefts %d, gifts %d, "
                 "fights %d | built %d | max generation %d\n",
                 dayOfTick(sim.now()) + 1, sim.population(), s.births, s.deaths, s.thefts, s.gifts, s.fights,
                 s.structuresCompleted, s.maxGeneration);
}

// ---------------------------------------------------------------------------------------------------
// CSV
// ---------------------------------------------------------------------------------------------------
bool writeHistoryCsv(const Simulation& sim, const std::string& path, std::string& err) {
    std::ofstream f(path);
    if (!f) {
        err = "cannot open '" + path + "' for writing";
        return false;
    }
    f << "day,tick,population,children,mood,stress,health,food";
    for (int t = 0; t < kTraitCount; ++t) {
        std::string name = traitLabel(Trait(t));
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        f << ",trait_" << name;
    }
    f << ",theft_tolerance,violence_tolerance,sharing_expectation,structures,wild_food"
         ",births,deaths,starvations,freezings,killings,old_age_deaths,thefts,gifts,fights,punishments"
         ",forgivenesses,trades,matings,breakdowns,structures_completed,max_generation\n";
    char buf[64];
    auto num = [&](float v) {
        std::snprintf(buf, sizeof(buf), "%.4f", double(v));
        return std::string(buf);
    };
    for (const Sample& s : sim.history()) {
        f << s.day << ',' << s.tick << ',' << s.population << ',' << s.children << ',' << num(s.avgMood) << ','
          << num(s.avgStress) << ',' << num(s.avgHealth) << ',' << num(s.avgFood);
        for (float t : s.avgTraits) f << ',' << num(t);
        f << ',' << num(s.avgTheftTolerance) << ',' << num(s.avgViolenceTolerance) << ','
          << num(s.avgSharingExpectation) << ',' << s.structures << ',' << num(s.wildFood);
        const SimStats& c = s.totals;
        f << ',' << c.births << ',' << c.deaths << ',' << c.starvations << ',' << c.freezings << ',' << c.killings
          << ',' << c.oldAgeDeaths << ',' << c.thefts << ',' << c.gifts << ',' << c.fights << ',' << c.punishments
          << ',' << c.forgivenesses << ',' << c.trades << ',' << c.matings << ',' << c.breakdowns << ','
          << c.structuresCompleted << ',' << c.maxGeneration << '\n';
    }
    if (!f) {
        err = "error while writing '" + path + "'";
        return false;
    }
    return true;
}

}  // namespace heur
