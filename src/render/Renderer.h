// Heuristica — live terminal view. Draws a whole frame from a const Simulation& into a cell buffer and
// emits only the changed cells as one ANSI string (one write() per frame, no flicker).
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/Types.h"
#include "render/Census.h"
#include "render/Screen.h"

namespace heur {

class Agent;
class Simulation;
struct LogEntry;

enum class PanelMode : std::uint8_t { Inspector, Society, AgentList, Count };
enum class LogFilter : std::uint8_t { All, Crime, Romance, Deaths, Selected, Count };
const char* panelModeName(PanelMode m);
const char* logFilterName(LogFilter f);
bool logFilterAccepts(LogFilter f, const LogEntry& e, AgentId selected);

// Simulation speed. Levels 0..8 => 1,2,4..256 ticks per frame; negative levels are slow motion
// (-1 => 1/2, -2 => 1/4 tick per frame).
inline constexpr int kMinSpeedLevel = -2;
inline constexpr int kMaxSpeedLevel = 8;
double speedTicksPerFrame(int level);
std::string speedLabel(int level);  // "x16", "x1/4"

// Everything the player controls. Mutated by the input handler (main) and Renderer::sync().
struct ViewState {
    // Selection & camera
    AgentId selected = kNoAgent;
    bool follow = true;
    Vec2i camera{0, 0};         // world tile shown at the map viewport's top-left
    bool centerRequest = true;  // centre the camera on the selection at the next sync()
    // Panels
    PanelMode panel = PanelMode::Inspector;
    LogFilter logFilter = LogFilter::All;
    int panelScroll = 0;
    AgentId listCursor = kNoAgent;
    bool showHelp = false;
    bool narrowPanel = false;   // terminal too narrow for a side panel: show the panel instead of the map
    // Simulation control (owned by the main loop; displayed here)
    bool paused = false;
    int speedLevel = 0;
    bool lagging = false;
    double ticksPerSecond = 0.0;
    // Transient footer message
    std::string status;
    int statusFrames = 0;
    // Bookkeeping so a dead selection can be handed to a relative
    std::string selectedName;
    Vec2i selectedLastPos{0, 0};
    std::vector<AgentId> selectedKin;

    void flash(std::string msg, int frames = 90) {
        status = std::move(msg);
        statusFrames = frames;
    }
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};

struct Layout {
    Rect top, map, panel, log, footer;
    bool hasPanel = false;
    bool tooSmall = false;
};

struct RenderOptions {
    bool ascii = false;      // ASCII-only glyphs (no box drawing / symbols)
    bool truecolor = true;   // 24-bit colour; false => xterm-256 approximation
};

// Next/previous living agent in id order (wraps). Returns kNoAgent if nobody is alive.
AgentId cycleAgent(const Simulation& sim, AgentId current, int dir);

class Renderer {
public:
    explicit Renderer(RenderOptions opt);

    const RenderOptions& options() const { return opt_; }
    // Population averages at day 0 (founders) for the "trait evolution" comparison.
    void setBaseline(const Census& c) {
        baseline_ = c;
        haveBaseline_ = true;
    }

    Layout layout(int cols, int rows) const;
    // Resolves the view against the simulation: hands a dead selection to a relative, follows the
    // selected agent, clamps camera / scroll / list cursor, ages the status message.
    void sync(const Simulation& sim, ViewState& view, int cols, int rows);
    // Builds the frame and returns the bytes to write (empty if nothing changed).
    const std::string& render(const Simulation& sim, const ViewState& view, int cols, int rows);
    // Forces a full repaint next frame (after resize or resume).
    void invalidate() { screen_.invalidate(); }

private:
    class PanelWriter;

    void drawTooSmall(int cols, int rows);
    void drawTopBar(const Simulation& sim, const ViewState& v, Rect r);
    void drawMap(const Simulation& sim, const ViewState& v, Rect r);
    void drawPanel(const Simulation& sim, const ViewState& v, Rect r);
    void drawInspector(const Simulation& sim, const ViewState& v, PanelWriter& p);
    void drawSociety(const Simulation& sim, PanelWriter& p);
    void drawAgentList(const Simulation& sim, const ViewState& v, Rect r);
    void drawLog(const Simulation& sim, const ViewState& v, Rect r);
    void drawFooter(const ViewState& v, Rect r);
    void drawHelp();
    void drawCenteredBox(const std::vector<std::string>& lines, Style border, Style text);

    // Widgets
    void section(PanelWriter& p, const std::string& title);
    void bar(PanelWriter& p, int width, float value, Rgb color);
    void chart(PanelWriter& p, const std::vector<float>& values, int height, Rgb color);

    RenderOptions opt_;
    Screen screen_;
    std::string out_;
    Census census_;
    Census baseline_;
    bool haveBaseline_ = false;
    int lastPanelRows_ = 0;
    std::uint64_t frame_ = 0;
};

}  // namespace heur
