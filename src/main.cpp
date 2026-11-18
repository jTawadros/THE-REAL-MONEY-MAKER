// Heuristica — entry point: command line, interactive loop (keyboard + live view), headless mode.
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <random>
#include <string>
#include <string_view>
#include <thread>

#include "render/Census.h"
#include "render/Renderer.h"
#include "render/Report.h"
#include "render/Terminal.h"
#include "sim/Simulation.h"

using namespace heur;

namespace {

struct Options {
    Simulation::Params params;
    bool seedGiven = false;
    bool headless = false;
    int days = 120;
    bool verbose = false;
    int reportEvery = 0;
    std::string csvPath;
    bool ascii = false;
    int colorMode = 0;  // 0 auto, 1 truecolor, 2 256-colour
    int speedLevel = 0;
    bool startPaused = false;
};

void printUsage(std::FILE* out) {
    std::fputs(
        "Heuristica - an autonomous-agent life simulation in your terminal.\n"
        "\n"
        "Usage: heuristica [options]\n"
        "\n"
        "World:\n"
        "  --seed N            RNG seed (default: random; printed at exit)\n"
        "  --agents N          founding population (default 30)\n"
        "  --clans N           founding groups (default 3)\n"
        "  --width N           map width  (default 120)\n"
        "  --height N          map height (default 60)\n"
        "\n"
        "Interactive view (default when stdout is a terminal):\n"
        "  --ascii             ASCII-only glyphs for terminals/fonts without Unicode\n"
        "  --truecolor         force 24-bit colour     --256color   force xterm-256 colours\n"
        "  --speed N           initial speed 1..9 (x1..x256)   --paused   start paused\n"
        "\n"
        "Headless / balancing:\n"
        "  --headless          no UI; print status lines and a final report\n"
        "  --days N            days to simulate headless (default 120)\n"
        "  --report-every N    status line every N days (default: ~24 lines per run)\n"
        "  --verbose           echo every event log line to stdout\n"
        "  --csv FILE          write daily history samples as CSV at exit (both modes)\n"
        "\n"
        "  -h, --help          this text\n"
        "\n"
        "Keys: space pause, +/- or 1-9 speed, tab/[ ] cycle agents, f follow, arrows/WASD pan,\n"
        "      c centre, v panel, e event filter, n step, ? help, q/Esc quit.\n",
        out);
}

bool parseInt(std::string_view s, long long lo, long long hi, long long& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const std::string tmp(s);
    errno = 0;
    const long long v = std::strtoll(tmp.c_str(), &end, 10);
    if (errno != 0 || end == tmp.c_str() || *end != '\0' || v < lo || v > hi) return false;
    out = v;
    return true;
}

// Returns 0 on success, 1 on error, 2 when --help was printed.
int parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        std::string value;
        bool hasInlineValue = false;
        if (arg.rfind("--", 0) == 0) {
            const auto eq = arg.find('=');
            if (eq != std::string::npos) {
                value = arg.substr(eq + 1);
                arg = arg.substr(0, eq);
                hasInlineValue = true;
            }
        }
        auto needValue = [&](std::string& dst) -> bool {
            if (hasInlineValue) {
                dst = value;
                return true;
            }
            if (i + 1 >= argc) {
                std::fprintf(stderr, "heuristica: %s needs a value\n", arg.c_str());
                return false;
            }
            dst = argv[++i];
            return true;
        };
        auto intArg = [&](long long lo, long long hi, long long& dst) -> bool {
            std::string v;
            if (!needValue(v)) return false;
            if (!parseInt(v, lo, hi, dst)) {
                std::fprintf(stderr, "heuristica: %s expects an integer in [%lld, %lld], got '%s'\n", arg.c_str(), lo,
                             hi, v.c_str());
                return false;
            }
            return true;
        };
        long long n = 0;
        if (arg == "-h" || arg == "--help") {
            printUsage(stdout);
            return 2;
        } else if (arg == "--seed") {
            std::string v;
            if (!needValue(v)) return 1;
            char* end = nullptr;
            const unsigned long long s = std::strtoull(v.c_str(), &end, 10);
            if (v.empty() || *end != '\0') {
                std::fprintf(stderr, "heuristica: --seed expects a non-negative integer, got '%s'\n", v.c_str());
                return 1;
            }
            o.params.seed = s;
            o.seedGiven = true;
        } else if (arg == "--agents") {
            if (!intArg(1, 5000, n)) return 1;
            o.params.initialAgents = int(n);
        } else if (arg == "--clans") {
            if (!intArg(1, 64, n)) return 1;
            o.params.founderClans = int(n);
        } else if (arg == "--width") {
            if (!intArg(16, 4000, n)) return 1;
            o.params.width = int(n);
        } else if (arg == "--height") {
            if (!intArg(16, 4000, n)) return 1;
            o.params.height = int(n);
        } else if (arg == "--headless") {
            o.headless = true;
        } else if (arg == "--days") {
            if (!intArg(1, 1000000, n)) return 1;
            o.days = int(n);
        } else if (arg == "--verbose" || arg == "-v") {
            o.verbose = true;
        } else if (arg == "--report-every") {
            if (!intArg(1, 1000000, n)) return 1;
            o.reportEvery = int(n);
        } else if (arg == "--csv") {
            if (!needValue(o.csvPath)) return 1;
        } else if (arg == "--ascii") {
            o.ascii = true;
        } else if (arg == "--truecolor") {
            o.colorMode = 1;
        } else if (arg == "--256color" || arg == "--256") {
            o.colorMode = 2;
        } else if (arg == "--speed") {
            if (!intArg(1, 9, n)) return 1;
            o.speedLevel = int(n) - 1;
        } else if (arg == "--paused") {
            o.startPaused = true;
        } else {
            std::fprintf(stderr, "heuristica: unknown option '%s' (try --help)\n", argv[i]);
            return 1;
        }
    }
    return 0;
}

bool detectTruecolor() {
    const char* ct = std::getenv("COLORTERM");
    if (ct && (std::strstr(ct, "truecolor") || std::strstr(ct, "24bit"))) return true;
    if (std::getenv("WT_SESSION")) return true;  // Windows Terminal (WSL)
    const char* tp = std::getenv("TERM_PROGRAM");
    if (tp && (std::strstr(tp, "iTerm") || std::strstr(tp, "WezTerm") || std::strstr(tp, "vscode"))) return true;
    const char* term = std::getenv("TERM");
    if (term && (std::strstr(term, "direct") || std::strstr(term, "kitty") || std::strstr(term, "alacritty")))
        return true;
    return false;
}

// ---------------------------------------------------------------------------------------------------
// Interactive mode
// ---------------------------------------------------------------------------------------------------
// Returns false if the terminal could not be put into raw mode (caller falls back to headless).
bool runInteractive(Simulation& sim, const Options& o) {
    using Clock = std::chrono::steady_clock;
    Terminal term;
    if (!term.open()) return false;

    RenderOptions ro;
    ro.ascii = o.ascii;
    ro.truecolor = o.colorMode == 1 || (o.colorMode == 0 && detectTruecolor());
    Renderer renderer(ro);
    renderer.setBaseline(takeCensus(sim));

    ViewState v;
    v.speedLevel = o.speedLevel;
    v.paused = o.startPaused;
    v.selected = cycleAgent(sim, kNoAgent, +1);
    v.listCursor = v.selected;
    v.flash("Welcome! Press ? for help. Tab picks another agent; watch their thoughts.", 240);

    constexpr int kFps = 30;
    const auto framePeriod = std::chrono::microseconds(1'000'000 / kFps);
    double owed = 0.0;
    long long ticksInWindow = 0;
    auto tpsWindowStart = Clock::now();
    bool quit = false;

    auto select = [&](AgentId id) {
        if (id == kNoAgent) return;
        if (id != v.selected) v.panelScroll = 0;
        v.selected = id;
        v.listCursor = id;
        if (v.follow) v.centerRequest = true;
    };
    auto pan = [&](int dx, int dy) {
        if (v.follow) v.flash("Free camera - press f to follow again, c to centre", 90);
        v.follow = false;
        v.camera.x += dx;
        v.camera.y += dy;
    };
    auto moveListCursor = [&](int delta) {
        const auto all = livingAgents(sim);
        if (all.empty()) return;
        int idx = 0;
        for (int i = 0; i < int(all.size()); ++i)
            if (all[std::size_t(i)]->id == v.listCursor) idx = i;
        idx = std::clamp(idx + delta, 0, int(all.size()) - 1);
        v.listCursor = all[std::size_t(idx)]->id;
    };
    auto setSpeed = [&](int level) {
        v.speedLevel = std::clamp(level, kMinSpeedLevel, kMaxSpeedLevel);
        v.flash("Speed " + speedLabel(v.speedLevel) + " (" +
                    std::to_string(int(speedTicksPerFrame(v.speedLevel) * kFps + 0.5)) + " ticks/s target)",
                45);
    };
    bool stepOnce = false;

    auto handleKey = [&](const Key& k) {
        if (k.code == KeyCode::CtrlC) {
            quit = true;
            return;
        }
        if (v.showHelp) {  // any key closes the help overlay
            v.showHelp = false;
            return;
        }
        const Layout L = renderer.layout(term.cols(), term.rows());
        const int page = std::max(1, (L.hasPanel ? L.panel.h : L.map.h) - 3);
        const bool panelShown = L.hasPanel || v.narrowPanel;
        const bool list = panelShown && v.panel == PanelMode::AgentList;
        switch (k.code) {
            case KeyCode::Escape: quit = true; return;
            case KeyCode::Tab: select(cycleAgent(sim, v.selected, +1)); return;
            case KeyCode::BackTab: select(cycleAgent(sim, v.selected, -1)); return;
            case KeyCode::Up: list ? moveListCursor(-1) : pan(0, -2); return;
            case KeyCode::Down: list ? moveListCursor(+1) : pan(0, 2); return;
            case KeyCode::Left: pan(-2, 0); return;
            case KeyCode::Right: pan(2, 0); return;
            case KeyCode::PageUp: list ? moveListCursor(-page) : void(v.panelScroll -= page); return;
            case KeyCode::PageDown: list ? moveListCursor(page) : void(v.panelScroll += page); return;
            case KeyCode::Home: list ? moveListCursor(-1 << 20) : void(v.panelScroll = 0); return;
            case KeyCode::End: list ? moveListCursor(1 << 20) : void(v.panelScroll = 1 << 20); return;
            case KeyCode::Enter:
                if (list && v.listCursor != kNoAgent) {
                    select(v.listCursor);
                    v.panel = PanelMode::Inspector;
                    v.panelScroll = 0;
                    v.follow = true;
                    v.centerRequest = true;
                    if (!L.hasPanel) v.narrowPanel = false;  // narrow: jump back to the map
                }
                return;
            case KeyCode::Char: break;
            default: return;
        }
        const char32_t c = k.ch;
        switch (c) {
            case U' ': v.paused = !v.paused; break;
            case U'+': case U'=': setSpeed(v.speedLevel + 1); break;
            case U'-': case U'_': setSpeed(v.speedLevel - 1); break;
            case U'0': setSpeed(kMinSpeedLevel); break;
            case U'1': case U'2': case U'3': case U'4': case U'5': case U'6': case U'7': case U'8': case U'9':
                setSpeed(int(c - U'1'));
                break;
            case U']': select(cycleAgent(sim, v.selected, +1)); break;
            case U'[': select(cycleAgent(sim, v.selected, -1)); break;
            case U'f': case U'F':
                v.follow = !v.follow;
                if (v.follow) v.centerRequest = true;
                v.flash(v.follow ? "Following the selected agent" : "Free camera (arrows/WASD to pan)", 60);
                break;
            case U'c': case U'C': v.centerRequest = true; break;
            case U'v': case U'V':
                if (!L.hasPanel && !v.narrowPanel) {
                    // Narrow terminal: map -> inspector -> society -> agents -> map
                    v.narrowPanel = true;
                    v.panel = PanelMode::Inspector;
                } else if (!L.hasPanel && int(v.panel) + 1 == int(PanelMode::Count)) {
                    v.narrowPanel = false;
                } else {
                    v.panel = PanelMode((int(v.panel) + 1) % int(PanelMode::Count));
                }
                v.panelScroll = 0;
                if (v.panel == PanelMode::AgentList) v.listCursor = v.selected;
                break;
            case U'e': case U'E':
                v.logFilter = LogFilter((int(v.logFilter) + 1) % int(LogFilter::Count));
                v.flash(std::string("Event log: ") + logFilterName(v.logFilter), 45);
                break;
            case U'n': case U'N':
                v.paused = true;
                stepOnce = true;
                break;
            case U'q': case U'Q': quit = true; break;
            case U'?': case U'h': case U'H': v.showHelp = !v.showHelp; break;
            case U'w': pan(0, -2); break;
            case U's': pan(0, 2); break;
            case U'a': pan(-2, 0); break;
            case U'd': pan(2, 0); break;
            case U'W': pan(0, -10); break;
            case U'S': pan(0, 10); break;
            case U'A': pan(-10, 0); break;
            case U'D': pan(10, 0); break;
            case U'r': case U'R': renderer.invalidate(); break;
            default: break;
        }
    };

    bool announcedExtinction = false;
    while (!quit) {
        const auto frameStart = Clock::now();
        if (Terminal::interruptRequested()) break;
        const bool resized = term.pollResize();
        const bool resumed = term.consumeResumed();
        if (resized || resumed) renderer.invalidate();

        for (const Key& k : term.readKeys()) {
            handleKey(k);
            if (quit) break;
        }
        if (quit) break;

        // Simulate: a fixed number of ticks per frame. If the simulation can't keep up we render less
        // often (frames are dropped, ticks are not), but never block input for more than ~0.1 s.
        if (!v.paused && sim.population() > 0) owed += speedTicksPerFrame(v.speedLevel);
        const auto budgetEnd = frameStart + std::chrono::milliseconds(100);
        while (owed >= 1.0 && sim.population() > 0) {
            sim.step();
            owed -= 1.0;
            ++ticksInWindow;
            if (Terminal::interruptRequested() || Clock::now() >= budgetEnd) break;
        }
        v.lagging = owed >= 1.0;
        // Carry up to ~1 s of unfinished ticks into later frames; beyond that the machine simply
        // can't sustain this speed (shown as "lag" with the measured ticks/s in the top bar).
        owed = std::min(owed, std::max(1.0, speedTicksPerFrame(v.speedLevel)) * double(kFps));
        if (v.paused) owed = 0.0;
        if (stepOnce) {
            stepOnce = false;
            if (sim.population() > 0) {
                sim.step();
                ++ticksInWindow;
            }
        }
        if (sim.population() == 0 && !announcedExtinction) {
            announcedExtinction = true;
            v.paused = true;
            v.flash("Extinction on day " + std::to_string(dayOfTick(sim.now()) + 1), 1 << 30);
        }
        const auto tnow = Clock::now();
        const double windowSecs = std::chrono::duration<double>(tnow - tpsWindowStart).count();
        if (windowSecs >= 1.0) {
            v.ticksPerSecond = double(ticksInWindow) / windowSecs;
            ticksInWindow = 0;
            tpsWindowStart = tnow;
        }

        renderer.sync(sim, v, term.cols(), term.rows());
        const std::string& frame = renderer.render(sim, v, term.cols(), term.rows());
        if (!frame.empty()) term.write(frame);
        std::this_thread::sleep_until(frameStart + framePeriod);
    }
    term.close();
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    const int pr = parseArgs(argc, argv, o);
    if (pr == 2) return 0;
    if (pr != 0) return 2;
    if (!o.seedGiven) o.params.seed = (std::uint64_t(std::random_device{}()) << 32) ^ std::uint64_t(std::random_device{}());

    if (!o.headless && (!Terminal::stdoutIsTty() || !Terminal::stdinIsTty())) {
        std::fprintf(stderr, "heuristica: not attached to a terminal - running headless (%d days)\n", o.days);
        o.headless = true;
    }

    try {
        Simulation sim(o.params);
        int rc = 0;
        if (!o.headless) {
            if (!runInteractive(sim, o)) {
                std::fprintf(stderr, "heuristica: could not initialise the terminal - running headless\n");
                o.headless = true;
            } else {
                printSessionSummary(stdout, sim);
                std::printf("seed %llu  (replay with --seed %llu)\n", static_cast<unsigned long long>(o.params.seed),
                            static_cast<unsigned long long>(o.params.seed));
            }
        }
        if (o.headless) {
            sim.setEchoLog(o.verbose);
            HeadlessOptions ho;
            ho.days = o.days;
            ho.reportEvery = o.reportEvery;
            ho.ascii = o.ascii;
            rc = runHeadless(sim, ho, stdout);
        }
        if (!o.csvPath.empty()) {
            std::string err;
            if (writeHistoryCsv(sim, o.csvPath, err)) {
                std::fprintf(stderr, "heuristica: wrote %zu daily samples to %s\n", sim.history().size(),
                             o.csvPath.c_str());
            } else {
                std::fprintf(stderr, "heuristica: %s\n", err.c_str());
                rc = 1;
            }
        }
        return rc;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "heuristica: fatal error: %s\n", e.what());
        return 1;
    }
}
