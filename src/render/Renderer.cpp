// Heuristica — live terminal view. See Renderer.h.
#include "render/Renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "ai/Action.h"
#include "core/Config.h"
#include "sim/Simulation.h"
#include "world/World.h"

namespace heur {

// ===================================================================================================
// Glyphs, palette
// ===================================================================================================
namespace {

struct Glyphs {
    char32_t tree, berry, rock, fish, herb, grain, mountain;
    char32_t adult, child, incomplete, path;
    char32_t hline, vline, teeDown, teeUp;
    char32_t heart, friendly, grudge, neutral, up, down, same, selector, dead, scrollUp, scrollDown;
    char32_t barFull;
    std::array<char32_t, 8> eighthsH;  // horizontal partial cells 1/8 .. 8/8
    std::array<char32_t, 8> eighthsV;  // vertical partial cells 1/8 .. 8/8
    const char* sun;
    const char* moon;
    const char* ellipsis;
    const char* arrow;
    const char* female;
    const char* male;
    const char* sep;  // separator between top-bar segments
    const char* dot;  // inline separator
};

const Glyphs kUnicode{
    U'♣', U'*', U'^', U'~', U'"', U',', U'▲',
    U'@', U'o', U'+', U'·',
    U'─', U'│', U'┬', U'┴',
    U'♥', U'+', U'✖', U'·', U'▲', U'▼', U'·', U'▸', U'†', U'▲',
    U'▼',
    U'█',
    {U'▏', U'▎', U'▍', U'▌', U'▋', U'▊', U'▉', U'█'},
    {U'▁', U'▂', U'▃', U'▄', U'▅', U'▆', U'▇', U'█'},
    "☀", "☾", "…", "→", "♀", "♂", " │ ", " · ",
};

const Glyphs kAscii{
    U'T', U'*', U'^', U'~', U'"', U',', U'A',
    U'@', U'o', U'+', U'.',
    U'-', U'|', U'+', U'+',
    U'*', U'+', U'x', U'.', U'^', U'v', U'=', U'>', U'+', U'^', U'v',
    U'#',
    {U' ', U' ', U' ', U':', U':', U':', U'#', U'#'},
    {U'_', U'_', U'.', U'.', U'-', U'=', U'+', U'#'},
    "day", "night", "~", "->", "F", "M", " | ", " - ",
};

// UI palette
constexpr Rgb kBgVoid = rgb(10, 10, 14);
constexpr Rgb kPanelBg = rgb(17, 18, 26);
constexpr Rgb kTopBg = rgb(30, 32, 48);
constexpr Rgb kLogBg = rgb(13, 13, 19);
constexpr Rgb kText = rgb(212, 212, 220);
constexpr Rgb kDim = rgb(132, 135, 150);
constexpr Rgb kFaint = rgb(84, 86, 100);
constexpr Rgb kHeader = rgb(150, 165, 210);
constexpr Rgb kAccent = rgb(120, 200, 255);
constexpr Rgb kWarn = rgb(240, 200, 80);
constexpr Rgb kBad = rgb(240, 85, 75);
constexpr Rgb kGood = rgb(110, 215, 110);
constexpr Rgb kPink = rgb(255, 130, 200);
constexpr Rgb kTrack = rgb(44, 46, 60);
constexpr Rgb kThought = rgb(255, 224, 150);

constexpr std::array<Rgb, 12> kClanPalette = {
    rgb(255, 90, 90),   rgb(90, 170, 255),  rgb(255, 205, 60),  rgb(120, 230, 120),
    rgb(225, 120, 255), rgb(255, 145, 60),  rgb(80, 230, 220),  rgb(255, 125, 190),
    rgb(185, 235, 80),  rgb(170, 150, 255), rgb(240, 240, 240), rgb(205, 165, 115),
};

Rgb clanColor(int clan) {
    if (clan <= 0) return rgb(235, 235, 235);
    return kClanPalette[std::size_t((clan - 1) % int(kClanPalette.size()))];
}

Rgb eventColor(EventKind k) {
    switch (k) {
        case EventKind::Birth: return rgb(130, 230, 130);
        case EventKind::Death: return rgb(240, 95, 95);
        case EventKind::Theft: return rgb(245, 170, 60);
        case EventKind::Gift: return rgb(100, 215, 200);
        case EventKind::Fight: return rgb(255, 80, 60);
        case EventKind::Build: return rgb(210, 180, 120);
        case EventKind::Romance: return rgb(255, 135, 205);
        case EventKind::Breakdown: return rgb(205, 125, 255);
        case EventKind::Justice: return rgb(115, 165, 255);
        case EventKind::Social: return rgb(178, 188, 205);
        case EventKind::World: return rgb(150, 205, 255);
        default: return rgb(145, 145, 155);
    }
}

// Red -> yellow -> green for 0..1 "goodness".
Rgb levelColor(float v) {
    v = clamp01(v);
    const Rgb red = rgb(225, 65, 55), yellow = rgb(230, 200, 60), green = rgb(85, 200, 95);
    return v < 0.5f ? blendRgb(red, yellow, v * 2.f) : blendRgb(yellow, green, (v - 0.5f) * 2.f);
}

Rgb emotionColor(Emotion e) {
    switch (e) {
        case Emotion::Joy: return rgb(255, 215, 90);
        case Emotion::Sadness: return rgb(100, 140, 235);
        case Emotion::Anger: return rgb(240, 70, 60);
        case Emotion::Fear: return rgb(185, 120, 235);
        case Emotion::Guilt: return rgb(170, 150, 120);
        case Emotion::Pride: return rgb(120, 220, 160);
        case Emotion::Envy: return rgb(150, 210, 60);
        default: return kText;
    }
}

Rgb terrainColor(Terrain t) {
    switch (t) {
        case Terrain::DeepWater: return rgb(16, 42, 96);
        case Terrain::ShallowWater: return rgb(38, 94, 156);
        case Terrain::Sand: return rgb(188, 170, 118);
        case Terrain::Grass: return rgb(66, 120, 50);
        case Terrain::Forest: return rgb(32, 78, 38);
        case Terrain::Hills: return rgb(118, 96, 64);
        case Terrain::Mountain: return rgb(120, 120, 128);
        default: return rgb(255, 0, 255);
    }
}

Rgb structureColor(StructureType t) {
    switch (t) {
        case StructureType::Campfire: return rgb(150, 125, 110);
        case StructureType::Hut: return rgb(225, 170, 105);
        case StructureType::Storage: return rgb(245, 205, 90);
        case StructureType::Farm: return rgb(160, 120, 75);
        case StructureType::Wall: return rgb(228, 228, 232);
        case StructureType::Workshop: return rgb(110, 205, 235);
        case StructureType::Monument: return rgb(238, 150, 255);
        default: return kText;
    }
}

std::uint32_t hash2(int x, int y) {
    std::uint32_t h = std::uint32_t(x) * 374761393u + std::uint32_t(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Smooth value noise over ~7-tile cells, quantised to -1 / 0 / +1.
float patchNoise(int x, int y) {
    constexpr int kCell = 7;
    const int gx = x >= 0 ? x / kCell : (x - kCell + 1) / kCell, gy = y >= 0 ? y / kCell : (y - kCell + 1) / kCell;
    const float fx = float(x - gx * kCell) / float(kCell), fy = float(y - gy * kCell) / float(kCell);
    auto h = [](int a, int b) { return float(hash2(a, b) & 0xFFFF) / 65535.f; };
    const float top = h(gx, gy) + (h(gx + 1, gy) - h(gx, gy)) * fx;
    const float bot = h(gx, gy + 1) + (h(gx + 1, gy + 1) - h(gx, gy + 1)) * fx;
    const float v = top + (bot - top) * fy;  // 0..1
    return v < 0.36f ? -1.f : (v > 0.64f ? 1.f : 0.f);
}

float smooth01(float x) {
    x = clamp01(x);
    return x * x * (3.f - 2.f * x);
}

// 1 = full daylight, 0 = deep night (with short twilights around cfg night boundaries).
float daylight(float tod) {
    const float dawn = smooth01((tod - (cfg::kNightEnd - 0.04f)) / 0.10f);
    const float dusk = 1.f - smooth01((tod - (cfg::kNightStart - 0.06f)) / 0.10f);
    return std::min(dawn, dusk);
}

bool isVegetation(Terrain t) { return t == Terrain::Grass || t == Terrain::Forest; }

Rgb seasonal(Rgb c, Terrain t, Season s, float progress) {
    switch (s) {
        case Season::Spring:
            if (isVegetation(t)) c = blendRgb(c, rgb(95, 165, 70), 0.18f);
            break;
        case Season::Summer:
            if (t == Terrain::Grass) c = blendRgb(c, rgb(130, 145, 55), 0.16f);
            break;
        case Season::Autumn:
            if (isVegetation(t)) c = blendRgb(c, rgb(150, 105, 40), 0.12f + 0.25f * progress);
            break;
        case Season::Winter: {
            const float frost = 0.55f * smooth01(progress / 0.15f) * (1.f - smooth01((progress - 0.85f) / 0.15f));
            if (t == Terrain::ShallowWater) c = blendRgb(c, rgb(150, 182, 210), 0.45f * frost);
            else if (t == Terrain::DeepWater) c = blendRgb(c, rgb(60, 90, 130), 0.25f * frost);
            else c = blendRgb(c, rgb(214, 222, 234), (isVegetation(t) ? 0.85f : 0.7f) * frost);
            break;
        }
    }
    return c;
}

// Night darkening with a blue cast; `warm` (0..1) is campfire glow that pushes back the dark.
Rgb lit(Rgb c, float light, float warm) {
    const float l = std::max(light, warm * 0.85f);
    Rgb out = scaleRgb(c, 0.28f + 0.72f * l);
    out = blendRgb(out, rgb(10, 16, 44), (1.f - l) * 0.30f);
    if (warm > 0.f && light < 0.9f) out = blendRgb(out, rgb(255, 150, 60), warm * 0.22f * (1.f - light));
    return out;
}

const char* ageStageLetter(LifeStage s) {
    switch (s) {
        case LifeStage::Child: return "c";
        case LifeStage::Adult: return "a";
        case LifeStage::Elder: return "e";
    }
    return "?";
}

std::string fmt2(float v) {  // ".62" style compact 0..1 value
    char buf[16];
    if (v >= 0.995f) return "1.0";
    std::snprintf(buf, sizeof(buf), ".%02d", int(std::lround(clamp01(v) * 100.f)) % 100);
    return buf;
}

std::string hoursString(Tick ticks) {
    const int minutes = int(ticks * 24 * 60 / Tick(cfg::kTicksPerDay));
    char buf[32];
    if (minutes >= 24 * 60) std::snprintf(buf, sizeof(buf), "%.1fd", double(minutes) / (24.0 * 60.0));
    else if (minutes >= 60) std::snprintf(buf, sizeof(buf), "%dh", minutes / 60);
    else std::snprintf(buf, sizeof(buf), "%dm", minutes);
    return buf;
}

}  // namespace

// ===================================================================================================
// Free helpers
// ===================================================================================================
const char* panelModeName(PanelMode m) {
    switch (m) {
        case PanelMode::Inspector: return "Inspector";
        case PanelMode::Society: return "Society";
        case PanelMode::AgentList: return "Agents";
        default: return "?";
    }
}

const char* logFilterName(LogFilter f) {
    switch (f) {
        case LogFilter::All: return "all";
        case LogFilter::Crime: return "crime & justice";
        case LogFilter::Romance: return "romance & births";
        case LogFilter::Deaths: return "deaths";
        case LogFilter::Selected: return "selected agent";
        default: return "?";
    }
}

bool logFilterAccepts(LogFilter f, const LogEntry& e, AgentId selected) {
    switch (f) {
        case LogFilter::All: return true;
        case LogFilter::Crime:
            return e.kind == EventKind::Theft || e.kind == EventKind::Fight || e.kind == EventKind::Justice ||
                   e.kind == EventKind::Breakdown;
        case LogFilter::Romance: return e.kind == EventKind::Romance || e.kind == EventKind::Birth;
        case LogFilter::Deaths: return e.kind == EventKind::Death;
        case LogFilter::Selected: return selected != kNoAgent && (e.a == selected || e.b == selected);
        default: return true;
    }
}

double speedTicksPerFrame(int level) {
    level = std::clamp(level, kMinSpeedLevel, kMaxSpeedLevel);
    return level >= 0 ? double(1 << level) : 1.0 / double(1 << -level);
}

std::string speedLabel(int level) {
    level = std::clamp(level, kMinSpeedLevel, kMaxSpeedLevel);
    if (level >= 0) return "x" + std::to_string(1 << level);
    return "x1/" + std::to_string(1 << -level);
}

AgentId cycleAgent(const Simulation& sim, AgentId current, int dir) {
    const std::vector<const Agent*> all = livingAgents(sim);
    if (all.empty()) return kNoAgent;
    if (dir >= 0) {
        for (const Agent* a : all)
            if (a->id > current) return a->id;
        return all.front()->id;
    }
    for (auto it = all.rbegin(); it != all.rend(); ++it)
        if ((*it)->id < current) return (*it)->id;
    return all.back()->id;
}

// ===================================================================================================
// PanelWriter: a scrolling line cursor inside a rectangle
// ===================================================================================================
class Renderer::PanelWriter {
public:
    PanelWriter(Screen& s, Rect r, int scroll, Rgb bg) : s_(s), r_(r), scroll_(scroll), bg_(bg) {}

    bool visible() const {
        const int y = row_ - scroll_;
        return y >= 0 && y < r_.h;
    }
    int width() const { return r_.w; }
    int col() const { return col_; }
    int remaining() const { return std::max(0, r_.w - col_); }
    Rgb bg() const { return bg_; }
    int rows() const { return row_ + (col_ > 0 ? 1 : 0); }

    void text(std::string_view t, Rgb fg, std::uint8_t attr = kAttrNone) {
        const int w = std::min(textWidth(t), remaining());
        if (visible()) s_.text(r_.x + col_, r_.y + row_ - scroll_, t, Style{fg, bg_, attr}, w);
        col_ += w;
    }
    void glyph(char32_t ch, Rgb fg, Rgb bg, std::uint8_t attr = kAttrNone) {
        if (col_ >= r_.w) return;
        if (visible()) s_.put(r_.x + col_, r_.y + row_ - scroll_, ch, Style{fg, bg, attr});
        ++col_;
    }
    void space(int n = 1) { col_ = std::min(r_.w, col_ + n); }
    void moveTo(int c) { col_ = std::clamp(c, 0, r_.w); }
    void right(std::string_view t, Rgb fg, std::uint8_t attr = kAttrNone) {
        moveTo(r_.w - textWidth(t));
        text(t, fg, attr);
    }
    // Paints the rest of the current row with a background colour (for highlighted rows).
    void highlightRow(Rgb bg) {
        if (!visible()) return;
        for (int x = 0; x < r_.w; ++x)
            if (Cell* c = s_.cell(r_.x + x, r_.y + row_ - scroll_)) c->bg = bg;
    }
    void newline() {
        ++row_;
        col_ = 0;
    }
    void ensureNewline() {
        if (col_ > 0) newline();
    }

private:
    Screen& s_;
    Rect r_;
    int scroll_;
    Rgb bg_;
    int row_ = 0;
    int col_ = 0;
};

// ===================================================================================================
// Renderer
// ===================================================================================================
Renderer::Renderer(RenderOptions opt) : opt_(opt) {}

Layout Renderer::layout(int cols, int rows) const {
    Layout L;
    if (cols < 40 || rows < 12) {
        L.tooSmall = true;
        return L;
    }
    L.top = {0, 0, cols, 1};
    const bool footer = rows >= 20;
    L.footer = footer ? Rect{0, rows - 1, cols, 1} : Rect{0, rows, cols, 0};
    const int logLines = rows >= 52 ? 10 : rows >= 42 ? 8 : rows >= 32 ? 6 : rows >= 24 ? 4 : 2;
    const int bottom = rows - (footer ? 1 : 0);
    L.log = {0, bottom - (logLines + 1), cols, logLines + 1};
    const int mainY = 1;
    const int mainH = std::max(1, L.log.y - mainY);
    const int panelW = cols >= 176 ? 50 : cols >= 144 ? 46 : cols >= 116 ? 42 : cols >= 96 ? 40 : cols >= 76 ? 34 : 0;
    L.hasPanel = panelW > 0;
    if (L.hasPanel) {
        L.map = {0, mainY, cols - panelW - 1, mainH};
        L.panel = {cols - panelW, mainY, panelW, mainH};
    } else {
        L.map = {0, mainY, cols, mainH};
    }
    return L;
}

void Renderer::sync(const Simulation& sim, ViewState& v, int cols, int rows) {
    const Layout L = layout(cols, rows);
    const Agent* sel = v.selected != kNoAgent ? sim.agent(v.selected) : nullptr;

    if (!sel) {
        const AgentId old = v.selected;
        const Agent* next = nullptr;
        for (AgentId k : v.selectedKin)
            if (const Agent* a = sim.agent(k)) {
                next = a;
                break;
            }
        if (!next && old != kNoAgent) {
            int best = 1 << 30;
            for (const auto& up : sim.agents()) {
                if (!up->alive) continue;
                const int d = chebyshev(up->pos, v.selectedLastPos);
                if (d < best) {
                    best = d;
                    next = up.get();
                }
            }
        }
        if (!next) {
            const auto all = livingAgents(sim);
            if (!all.empty()) next = all.front();
        }
        if (old != kNoAgent && !v.selectedName.empty()) {
            std::string msg = v.selectedName + " is gone";
            const auto& log = sim.logEntries();
            int scanned = 0;
            for (auto it = log.rbegin(); it != log.rend() && scanned < 400; ++it, ++scanned)
                if (it->kind == EventKind::Death && it->a == old) {
                    msg = it->text;
                    break;
                }
            if (next) msg += " " + std::string(opt_.ascii ? "--" : "—") + " now watching " + next->name;
            v.flash(msg, 180);
        }
        v.selected = next ? next->id : kNoAgent;
        v.panelScroll = 0;
        sel = next;
        if (sel && v.follow) v.centerRequest = true;
    }
    if (sel) {
        v.selectedName = sel->name;
        v.selectedLastPos = sel->pos;
        v.selectedKin.clear();
        if (sel->partner != kNoAgent) v.selectedKin.push_back(sel->partner);
        for (AgentId c : sel->children) v.selectedKin.push_back(c);
        if (sel->mother != kNoAgent) v.selectedKin.push_back(sel->mother);
        if (sel->father != kNoAgent) v.selectedKin.push_back(sel->father);
    }
    if (v.listCursor == kNoAgent || !sim.agent(v.listCursor)) v.listCursor = v.selected;

    // Camera
    const int vw = std::max(1, L.map.w), vh = std::max(1, L.map.h);
    if (sel && v.centerRequest) {
        v.camera = {sel->pos.x - vw / 2, sel->pos.y - vh / 2};
        v.centerRequest = false;
    } else if (sel && v.follow) {
        // Dead-zone follow: only scroll when the agent leaves the central box (keeps the map calm).
        const int mx = std::max(1, vw * 3 / 10), my = std::max(1, vh * 3 / 10);
        const int sx = sel->pos.x - v.camera.x, sy = sel->pos.y - v.camera.y;
        if (sx < mx) v.camera.x = sel->pos.x - mx;
        if (sx > vw - 1 - mx) v.camera.x = sel->pos.x - (vw - 1 - mx);
        if (sy < my) v.camera.y = sel->pos.y - my;
        if (sy > vh - 1 - my) v.camera.y = sel->pos.y - (vh - 1 - my);
    }
    const int W = sim.world().width(), H = sim.world().height();
    v.camera.x = W <= vw ? -(vw - W) / 2 : std::clamp(v.camera.x, 0, W - vw);
    v.camera.y = H <= vh ? -(vh - H) / 2 : std::clamp(v.camera.y, 0, H - vh);

    // Panel scroll (inspector / society); content height from the previous frame.
    const int viewRows = std::max(1, (L.hasPanel ? L.panel.h : L.map.h) - 1);
    v.panelScroll = std::clamp(v.panelScroll, 0, std::max(0, lastPanelRows_ - viewRows));
    if (v.statusFrames > 0 && --v.statusFrames == 0) v.status.clear();
}

const std::string& Renderer::render(const Simulation& sim, const ViewState& v, int cols, int rows) {
    ++frame_;
    screen_.resize(cols, rows);
    screen_.clear(Style{kText, kBgVoid, kAttrNone});
    const Layout L = layout(cols, rows);
    if (L.tooSmall) {
        drawTooSmall(cols, rows);
    } else {
        census_ = takeCensus(sim);
        drawTopBar(sim, v, L.top);
        if (L.hasPanel) {
            drawMap(sim, v, L.map);
            const Glyphs& g = opt_.ascii ? kAscii : kUnicode;
            const int sx = L.panel.x - 1;
            for (int y = L.panel.y; y < L.panel.y + L.panel.h; ++y)
                screen_.put(sx, y, g.vline, Style{kFaint, kPanelBg, kAttrNone});
            drawPanel(sim, v, L.panel);
        } else if (v.narrowPanel) {
            drawPanel(sim, v, L.map);  // narrow terminal: the panel replaces the map ('v' cycles)
        } else {
            drawMap(sim, v, L.map);
        }
        drawLog(sim, v, L.log);
        if (L.footer.h > 0) drawFooter(v, L.footer);
        if (sim.population() == 0) {
            drawCenteredBox({"EXTINCTION", "", "The last of them died on day " + std::to_string(dayOfTick(sim.now()) + 1) + ".",
                             "Press q to quit."},
                            Style{kBad, kPanelBg, kAttrBold}, Style{kText, kPanelBg, kAttrNone});
        }
        if (v.showHelp) drawHelp();
    }
    screen_.encode(out_, opt_.truecolor);
    return out_;
}

void Renderer::drawTooSmall(int cols, int rows) {
    const std::string a = "Terminal too small";
    const std::string b = std::to_string(cols) + "x" + std::to_string(rows) + " (need 40x12, best 100x30+)";
    const int y = std::max(0, rows / 2 - 1);
    screen_.text(std::max(0, (cols - textWidth(a)) / 2), y, a, Style{kWarn, kBgVoid, kAttrBold});
    screen_.text(std::max(0, (cols - textWidth(b)) / 2), y + 1, b, Style{kDim, kBgVoid, kAttrNone});
}

// ---------------------------------------------------------------------------------------------------
// Top bar
// ---------------------------------------------------------------------------------------------------
void Renderer::drawTopBar(const Simulation& sim, const ViewState& v, Rect r) {
    const Glyphs& g = opt_.ascii ? kAscii : kUnicode;
    const World& w = sim.world();
    const SimStats& st = sim.stats();
    screen_.fill(r.x, r.y, r.w, 1, U' ', Style{kText, kTopBg, kAttrNone});

    struct Seg {
        std::string text;
        Rgb fg;
        std::uint8_t attr;
        int priority;  // lower = more important
        bool sepBefore;
    };
    std::vector<Seg> segs;
    auto add = [&](std::string t, Rgb fg, int prio, bool sep = false, std::uint8_t attr = kAttrNone) {
        segs.push_back(Seg{std::move(t), fg, attr, prio, sep});
    };

    add(" HEURISTICA", rgb(255, 210, 120), 0, false, kAttrBold);
    const Season season = w.season();
    const int day = dayOfTick(sim.now());
    add("Day " + std::to_string(day + 1), kText, 0, true, kAttrBold);
    const int seasonDay = int(w.seasonProgress() * float(cfg::kDaysPerSeason)) + 1;
    const Rgb seasonC = season == Season::Spring   ? rgb(140, 225, 120)
                        : season == Season::Summer ? rgb(250, 215, 90)
                        : season == Season::Autumn ? rgb(235, 150, 70)
                                                   : rgb(180, 215, 255);
    add(" " + std::string(seasonLabel(season)) + " " + std::to_string(std::min(seasonDay, cfg::kDaysPerSeason)) +
            "/" + std::to_string(cfg::kDaysPerSeason),
        seasonC, 2);
    add(" Y" + std::to_string(day / cfg::kDaysPerYear + 1), kDim, 4);
    const bool night = w.isNight();
    add(std::string(" ") + (night ? g.moon : g.sun) + " " + clockString(sim.now()),
        night ? rgb(150, 160, 230) : rgb(255, 220, 110), 1);
    const int tc = temperatureCelsius(w.temperature());
    const Rgb tempC = blendRgb(rgb(120, 180, 255), rgb(255, 120, 70), clamp01(w.temperature()));
    add(" " + std::to_string(tc) + (opt_.ascii ? "C" : "°C"), tempC, 2);

    const int pop = census_.population;
    add("Pop " + std::to_string(pop), pop > 0 ? kText : kBad, 0, true, kAttrBold);
    add(" (" + std::to_string(census_.children) + " kids)", kDim, 3);
    add("+" + std::to_string(st.births) + " born", rgb(130, 230, 130), 2, true);
    add(" " + std::to_string(st.deaths) + " died", rgb(240, 110, 110), 2);
    add("thefts " + fmtCompact(st.thefts), eventColor(EventKind::Theft), 3, true);
    add(" gifts " + fmtCompact(st.gifts), eventColor(EventKind::Gift), 3);
    add(" fights " + fmtCompact(st.fights), eventColor(EventKind::Fight), 3);
    add("built " + std::to_string(census_.structuresComplete), eventColor(EventKind::Build), 4, true);
    if (st.maxGeneration > 0) add(" gen " + std::to_string(st.maxGeneration), kDim, 5);

    // Right side: speed / paused
    std::string speed = speedLabel(v.speedLevel);
    if (v.lagging && !v.paused) speed += " lag " + fmtCompact(int(v.ticksPerSecond)) + "/s";
    const std::string pausedTxt = " PAUSED ";

    auto widthOf = [&](int maxPrio) {
        int n = 0;
        for (const Seg& s : segs)
            if (s.priority <= maxPrio) n += textWidth(s.text) + (s.sepBefore ? textWidth(g.sep) : 0);
        return n;
    };
    const int rightW = textWidth(speed) + 2 + (v.paused ? textWidth(pausedTxt) + 1 : 0);
    int maxPrio = 5;
    while (maxPrio > 0 && widthOf(maxPrio) + rightW > r.w) --maxPrio;

    int x = r.x;
    const int limit = r.x + r.w - rightW;
    for (const Seg& s : segs) {
        if (s.priority > maxPrio) continue;
        if (s.sepBefore) x += screen_.text(x, r.y, g.sep, Style{kFaint, kTopBg, kAttrNone}, std::max(0, limit - x));
        x += screen_.text(x, r.y, s.text, Style{s.fg, kTopBg, s.attr}, std::max(0, limit - x));
    }
    int rx = r.x + r.w - rightW;
    if (v.paused) {
        rx += screen_.text(rx, r.y, pausedTxt, Style{rgb(20, 20, 20), kWarn, kAttrBold});
        ++rx;
    }
    screen_.text(rx + 1, r.y, speed, Style{v.lagging ? kWarn : kAccent, kTopBg, kAttrBold});
}

// ---------------------------------------------------------------------------------------------------
// Map
// ---------------------------------------------------------------------------------------------------
void Renderer::drawMap(const Simulation& sim, const ViewState& v, Rect r) {
    const Glyphs& g = opt_.ascii ? kAscii : kUnicode;
    const World& w = sim.world();
    // Quantised so twilight repaints the map a handful of times instead of every tick.
    const float light = std::round(daylight(w.timeOfDay()) * 16.f) / 16.f;
    const Season season = w.season();
    const float sprog = std::round(w.seasonProgress() * 8.f) / 8.f;  // season tint changes once a day
    const int cx = v.camera.x, cy = v.camera.y;
    const int vw = r.w, vh = r.h;
    if (vw <= 0 || vh <= 0) return;

    // Campfire glow (warm light at night).
    std::vector<float> warm(std::size_t(vw) * std::size_t(vh), 0.f);
    constexpr int kGlow = 5;
    if (light < 0.95f) {
        for (const auto& [id, s] : w.structures()) {
            if (!s.isLit()) continue;
            if (s.pos.x < cx - kGlow || s.pos.x >= cx + vw + kGlow || s.pos.y < cy - kGlow || s.pos.y >= cy + vh + kGlow)
                continue;
            for (int dy = -kGlow; dy <= kGlow; ++dy)
                for (int dx = -kGlow; dx <= kGlow; ++dx) {
                    const int sx = s.pos.x + dx - cx, sy = s.pos.y + dy - cy;
                    if (sx < 0 || sy < 0 || sx >= vw || sy >= vh) continue;
                    const float d = std::sqrt(float(dx * dx + dy * dy));
                    const float k = clamp01(1.f - d / (float(kGlow) + 0.5f));
                    float& cell = warm[std::size_t(sy) * std::size_t(vw) + std::size_t(sx)];
                    cell = std::max(cell, k * k * (3.f - 2.f * k));
                }
        }
    }

    const Agent* sel = sim.agent(v.selected);
    const Structure* home = (sel && sel->home != kNoStructure) ? w.structure(sel->home) : nullptr;

    // Terrain, resources, structures.
    for (int sy = 0; sy < vh; ++sy) {
        for (int sx = 0; sx < vw; ++sx) {
            const Vec2i p{cx + sx, cy + sy};
            const int X = r.x + sx, Y = r.y + sy;
            if (!w.inBounds(p)) {
                screen_.put(X, Y, U' ', Style{kFaint, kBgVoid, kAttrNone});
                continue;
            }
            const Tile& t = w.at(p);
            const float glow = warm[std::size_t(sy) * std::size_t(vw) + std::size_t(sx)];
            const std::uint32_t hsh = hash2(p.x, p.y);
            Rgb base = terrainColor(t.terrain);
            // Coherent, quantised texture: natural-looking patches that also keep long runs of
            // identical colours (cheap to encode).
            base = scaleRgb(base, 1.f + 0.05f * patchNoise(p.x, p.y));
            if (t.terrain == Terrain::Grass)
                base = blendRgb(base, rgb(40, 100, 40), std::round((1.f - t.fertility) * 3.f) / 3.f * 0.15f);
            base = seasonal(base, t.terrain, season, sprog);
            Rgb bg = lit(base, light, glow);
            char32_t ch = U' ';
            Rgb fg = kText;
            std::uint8_t attr = kAttrNone;

            const Structure* s = t.structure != kNoStructure ? w.structure(t.structure) : nullptr;
            if (s) {
                Rgb sc = structureColor(s->type);
                if (!s->complete) {
                    ch = g.incomplete;
                    sc = blendRgb(sc, bg, 0.35f);
                } else {
                    ch = static_cast<unsigned char>(s->bp().glyph);
                    attr = kAttrBold;
                    if (s->type == StructureType::Campfire && s->isLit()) {
                        const bool flick = ((frame_ / 3 + hsh) % 3) == 0;
                        sc = flick ? rgb(255, 225, 100) : rgb(255, 150, 40);
                        bg = blendRgb(bg, rgb(120, 45, 10), 0.55f);
                    } else if (s->type == StructureType::Farm) {
                        bg = lit(rgb(92, 64, 38), light, glow);
                        if (s->harvestable()) sc = rgb(250, 215, 70);
                        else if (s->planted) sc = blendRgb(rgb(120, 170, 70), rgb(170, 220, 60), s->growth);
                        else sc = rgb(165, 125, 80);
                    } else if (s->type == StructureType::Wall) {
                        bg = lit(rgb(92, 92, 100), light, glow);
                    }
                    if (s->condition < 0.3f) sc = blendRgb(sc, rgb(130, 70, 60), (0.3f - s->condition) / 0.3f);
                }
                if (home && s->id == home->id) attr |= kAttrUnderline;
                fg = lit(sc, std::max(light, 0.75f), glow);
            } else if (t.resource != ResourceKind::None) {
                const bool depleted = t.amount < 1.f;
                Rgb rc = kText;
                switch (t.resource) {
                    case ResourceKind::Tree: ch = g.tree; rc = rgb(120, 200, 90); break;
                    case ResourceKind::BerryBush: ch = g.berry; rc = rgb(235, 55, 95); break;
                    case ResourceKind::Rock: ch = g.rock; rc = rgb(220, 220, 225); break;
                    case ResourceKind::FishShoal: ch = depleted ? U' ' : g.fish; rc = rgb(160, 225, 255); break;
                    case ResourceKind::HerbPatch: ch = g.herb; rc = rgb(130, 245, 170); break;
                    case ResourceKind::WildGrain: ch = g.grain; rc = rgb(240, 210, 105); break;
                    default: break;
                }
                if (depleted) rc = blendRgb(rc, base, 0.65f);
                fg = lit(rc, std::max(light, 0.55f), glow);
            } else if (t.terrain == Terrain::Mountain && (hsh % 3u) == 0u) {
                ch = g.mountain;
                fg = lit(rgb(160, 160, 170), light, glow);
            }
            screen_.put(X, Y, ch, Style{fg, bg, attr});
        }
    }

    // Selected agent's planned path.
    if (sel) {
        int n = 0;
        for (const Vec2i& p : sel->path) {
            if (++n > 60) break;
            const int sx = p.x - cx, sy = p.y - cy;
            if (sx < 0 || sy < 0 || sx >= vw || sy >= vh) continue;
            if (Cell* c = screen_.cell(r.x + sx, r.y + sy))
                if (c->ch == U' ') {
                    c->ch = g.path;
                    c->fg = blendRgb(c->bg, rgb(255, 255, 255), 0.65f);
                }
        }
    }

    // Agents (selected > breakdown > adult > child when sharing a tile).
    std::vector<const Agent*> occ(std::size_t(vw) * std::size_t(vh), nullptr);
    auto rank = [&](const Agent* a) {
        if (a->id == v.selected) return 4;
        if (a->inBreakdown()) return 3;
        if (a->pregnant) return 2;
        return a->isAdult(sim.now()) ? 1 : 0;
    };
    for (const auto& up : sim.agents()) {
        const Agent* a = up.get();
        if (!a->alive) continue;
        const int sx = a->pos.x - cx, sy = a->pos.y - cy;
        if (sx < 0 || sy < 0 || sx >= vw || sy >= vh) continue;
        const Agent*& slot = occ[std::size_t(sy) * std::size_t(vw) + std::size_t(sx)];
        if (!slot || rank(a) > rank(slot)) slot = a;
    }
    const bool blinkOn = (frame_ / 8) % 2 == 0;
    for (int sy = 0; sy < vh; ++sy) {
        for (int sx = 0; sx < vw; ++sx) {
            const Agent* a = occ[std::size_t(sy) * std::size_t(vw) + std::size_t(sx)];
            if (!a) continue;
            Cell* c = screen_.cell(r.x + sx, r.y + sy);
            if (!c) continue;
            const LifeStage stage = a->stage(sim.now());
            const char32_t ch = stage == LifeStage::Child ? g.child : g.adult;
            Rgb fg = clanColor(a->clan);
            Rgb bg = scaleRgb(c->bg, 0.42f);
            std::uint8_t attr = stage == LifeStage::Elder ? kAttrNone : kAttrBold;
            if (stage == LifeStage::Elder) fg = blendRgb(fg, rgb(255, 255, 255), 0.35f);
            if (a->actionKind == kinds::Sleep) {
                fg = scaleRgb(fg, 0.55f);
                attr = kAttrNone;
            }
            if (a->inBreakdown()) {
                fg = blinkOn ? rgb(255, 255, 255) : rgb(255, 120, 110);
                bg = blinkOn ? rgb(200, 25, 25) : rgb(90, 0, 0);
                attr = kAttrBold;
            }
            if (a->id == v.selected) {
                bg = a->inBreakdown() && !blinkOn ? rgb(255, 90, 90) : clanColor(a->clan);
                fg = rgb(0, 0, 0);
                attr = kAttrBold;
            }
            if (a->pregnant) attr |= kAttrUnderline;  // ('&' is taken by the campfire blueprint)
            *c = Cell{ch, fg, bg, attr};
        }
    }

    // Map edge hints when following is off.
    if (!v.follow && vw > 20) {
        const std::string hint = "free camera (f: follow)";
        screen_.text(r.x + r.w - textWidth(hint) - 1, r.y, hint, Style{kDim, scaleRgb(kBgVoid, 1.f), kAttrNone});
    }
}

// ---------------------------------------------------------------------------------------------------
// Right-hand panel
// ---------------------------------------------------------------------------------------------------
void Renderer::section(PanelWriter& p, const std::string& title) {
    const Glyphs& g = opt_.ascii ? kAscii : kUnicode;
    p.ensureNewline();
    p.glyph(g.hline, kFaint, p.bg());
    p.text(" " + title + " ", kHeader, kAttrBold);
    while (p.remaining() > 0) p.glyph(g.hline, kFaint, p.bg());
    p.newline();
}

void Renderer::bar(PanelWriter& p, int width, float value, Rgb color) {
    const Glyphs& g = opt_.ascii ? kAscii : kUnicode;
    value = clamp01(value);
    const int eighths = int(std::lround(value * float(width) * 8.f));
    for (int i = 0; i < width; ++i) {
        const int e = std::clamp(eighths - i * 8, 0, 8);
        if (opt_.ascii) {
            p.glyph(e >= 6 ? U'#' : (e >= 3 ? U':' : U'-'), e >= 3 ? color : kFaint, p.bg());
        } else if (e == 0) {
            p.glyph(U' ', color, kTrack);
        } else {
            p.glyph(g.eighthsH[std::size_t(e - 1)], color, kTrack);
        }
    }
}

void Renderer::chart(PanelWriter& p, const std::vector<float>& values, int height, Rgb color) {
    const Glyphs& g = opt_.ascii ? kAscii : kUnicode;
    const int w = p.width();
    if (values.empty() || w <= 0) {
        p.text("(no history yet)", kDim);
        p.newline();
        return;
    }
    // Resample to at most w columns (each column = max of its bucket).
    std::vector<float> cols;
    const int n = int(values.size());
    const int ncols = std::min(n, w);
    for (int c = 0; c < ncols; ++c) {
        const int a = c * n / ncols, b = std::max(a + 1, (c + 1) * n / ncols);
        float m = 0.f;
        for (int i = a; i < b; ++i) m = std::max(m, values[std::size_t(i)]);
        cols.push_back(m);
    }
    const float maxV = std::max(1.f, *std::max_element(cols.begin(), cols.end()));
    for (int row = height - 1; row >= 0; --row) {
        for (int c = 0; c < ncols; ++c) {
            const int units = int(std::lround(cols[std::size_t(c)] / maxV * float(height * 8)));
            const int e = std::clamp(units - row * 8, 0, 8);
            const Rgb cc = blendRgb(scaleRgb(color, 0.55f), color, float(row + 1) / float(height));
            if (e == 0) p.glyph(U' ', cc, p.bg());
            else p.glyph(g.eighthsV[std::size_t(e - 1)], cc, p.bg());
        }
        p.newline();
    }
}

void Renderer::drawPanel(const Simulation& sim, const ViewState& v, Rect r) {
    screen_.fill(r.x, r.y, r.w, r.h, U' ', Style{kText, kPanelBg, kAttrNone});
    // Tabs
    int x = r.x + 1;
    for (int m = 0; m < int(PanelMode::Count); ++m) {
        const bool active = m == int(v.panel);
        const std::string label = std::string(" ") + panelModeName(PanelMode(m)) + " ";
        x += screen_.text(x, r.y, label,
                          active ? Style{rgb(15, 15, 20), kAccent, kAttrBold} : Style{kDim, kPanelBg, kAttrNone},
                          std::max(0, r.x + r.w - x));
        ++x;
    }
    const std::string vHint = "v";
    screen_.text(r.x + r.w - 2, r.y, vHint, Style{kFaint, kPanelBg, kAttrNone});

    const Rect content{r.x + 1, r.y + 1, r.w - 2, r.h - 1};
    if (content.w <= 4 || content.h <= 0) return;
    if (v.panel == PanelMode::AgentList) {
        drawAgentList(sim, v, content);
        return;
    }
    PanelWriter p(screen_, content, v.panelScroll, kPanelBg);
    if (v.panel == PanelMode::Inspector) drawInspector(sim, v, p);
    else drawSociety(sim, p);
    lastPanelRows_ = p.rows();

    const Glyphs& g = opt_.ascii ? kAscii : kUnicode;
    if (v.panelScroll > 0) {
        std::string up = " ";
        appendUtf8(up, g.scrollUp);
        up += " PgUp ";
        screen_.text(content.x + content.w - textWidth(up), content.y, up, Style{kWarn, kTrack, kAttrNone});
    }
    if (lastPanelRows_ - v.panelScroll > content.h) {
        std::string dn = " ";
        appendUtf8(dn, g.scrollDown);
        dn += " more: PgDn ";
        screen_.text(content.x + content.w - textWidth(dn), content.y + content.h - 1, dn,
                     Style{kWarn, kTrack, kAttrNone});
    }
}

void Renderer::drawInspector(const Simulation& sim, const ViewState& v, PanelWriter& p) {
    const Glyphs& g = opt_.ascii ? kAscii : kUnicode;
    const Agent* a = sim.agent(v.selected);
    if (!a) {
        p.text("Nobody is selected.", kDim);
        p.newline();
        p.text("Press Tab to pick an agent.", kDim);
        p.newline();
        return;
    }
    const Tick now = sim.now();
    const Rgb clanC = clanColor(a->clan);
    const int W = p.width();
    const LifeStage stage = a->stage(now);

    // ---- Identity ----
    p.text(a->name, clanC, kAttrBold);
    p.space();
    p.text(a->sex == Sex::Female ? g.female : g.male, a->sex == Sex::Female ? kPink : kAccent);
    p.text(" #" + std::to_string(a->id), kFaint);
    if (v.follow) p.right("[following]", kAccent);
    p.newline();
    p.text(stageLabel(stage), stage == LifeStage::Elder ? kWarn : kText);
    p.text(g.dot, kFaint);
    p.text(std::to_string(a->ageDays(now)) + " days", kText);
    p.text(g.dot, kFaint);
    p.text("clan " + std::to_string(a->clan), clanC);
    p.text(g.dot, kFaint);
    p.text("gen " + std::to_string(a->generation), kText);
    p.newline();
    if (a->partner != kNoAgent) {
        const Agent* pa = sim.agent(a->partner);
        p.glyph(g.heart, kPink, p.bg());
        p.text(" " + sim.nameOf(a->partner) + (pa ? "" : " (dead)"), pa ? kPink : kDim);
    } else {
        p.text("single", kDim);
    }
    int alive = 0;
    for (AgentId c : a->children)
        if (sim.agent(c)) ++alive;
    p.text(g.dot, kFaint);
    p.text("kids " + std::to_string(a->children.size()), kText);
    if (alive != int(a->children.size())) p.text(" (" + std::to_string(alive) + " alive)", kDim);
    p.newline();
    if (a->pregnant) {
        const Tick left = a->dueTick > now ? a->dueTick - now : 0;
        p.text("Pregnant by " + sim.nameOf(a->fetusFather) + ", due in " + hoursString(left), kPink);
        p.newline();
    }
    p.text("Health  ", kText);
    bar(p, std::max(6, W - 13), a->health, levelColor(a->health));
    p.text(" " + fmt2(a->health), levelColor(a->health));
    p.newline();

    // ---- Needs (two per row) ----
    section(p, "NEEDS");
    const int half = W / 2;
    const int labelW = half >= 20 ? 9 : 7;
    const int needBar = std::max(3, half - labelW - 5);
    for (int i = 0; i < kNeedCount; ++i) {
        const Need n = Need(i);
        const float val = a->needs.get(n);
        p.moveTo((i % 2) * half);
        std::string lbl = needLabel(n);
        if (int(lbl.size()) >= labelW) lbl = lbl.substr(0, std::size_t(labelW - 2)) + ".";
        lbl.resize(std::size_t(labelW), ' ');
        p.text(lbl, val < 0.25f ? kBad : kText, val < 0.25f ? kAttrBold : kAttrNone);
        bar(p, needBar, val, levelColor(val));
        p.text(" " + fmt2(val), levelColor(val));
        if (i % 2 == 1) p.newline();
    }
    p.ensureNewline();

    // ---- Feelings ----
    section(p, "FEELINGS");
    std::vector<std::pair<float, Emotion>> emo;
    for (int i = 0; i < kEmotionCount; ++i) {
        const float e = a->emotions.get(Emotion(i));
        if (e >= 0.08f) emo.push_back({e, Emotion(i)});
    }
    std::sort(emo.begin(), emo.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
    if (emo.empty()) {
        p.text("calm (no strong emotions)", kDim);
        p.newline();
    }
    for (std::size_t i = 0; i < emo.size(); ++i) {
        p.moveTo(int(i % 2) * half);
        std::string lbl = emotionLabel(emo[i].second);
        if (int(lbl.size()) >= labelW) lbl = lbl.substr(0, std::size_t(labelW - 2)) + ".";
        lbl.resize(std::size_t(labelW), ' ');
        p.text(lbl, emotionColor(emo[i].second));
        bar(p, needBar, emo[i].first, emotionColor(emo[i].second));
        p.text(" " + fmt2(emo[i].first), kDim);
        if (i % 2 == 1) p.newline();
    }
    p.ensureNewline();
    const float stab = a->emotionalStability();
    p.text("Stress    ", kText);
    bar(p, std::max(4, W - 27), a->stress, levelColor(1.f - a->stress));
    p.text(" " + fmt2(a->stress) + " ", levelColor(1.f - a->stress));
    {
        const Rgb mc = a->mentalState == MentalState::Stable     ? kGood
                       : a->mentalState == MentalState::Stressed ? kWarn
                                                                 : kBad;
        p.right(mentalLabel(a->mentalState), mc, kAttrBold);
    }
    p.newline();
    p.text("Stability ", kText);
    bar(p, std::max(4, W - 27), stab, levelColor(stab));
    p.text(" " + fmt2(stab), levelColor(stab));
    p.right("mood " + fmtSigned(a->mood()), a->mood() >= 0.f ? kGood : kBad);
    p.newline();
    if (a->inBreakdown()) {
        const Tick left = a->breakEnd > now ? a->breakEnd - now : 0;
        p.text("!! MENTAL BREAK: " + std::string(breakLabel(a->breakType)), kBad, kAttrBold);
        p.right(hoursString(left) + " left", kBad);
        p.newline();
    }

    // ---- Current action & thought (the star feature) ----
    section(p, "DOING");
    {
        std::string label = a->actionLabel.empty() ? std::string("(deciding...)") : a->actionLabel;
        std::string sel;
        appendUtf8(sel, g.selector);
        const auto lines = wrapText(label, W - 9);
        for (std::size_t i = 0; i < lines.size() && i < 2; ++i) {
            p.text(i == 0 ? sel + " " : "  ", kAccent, kAttrBold);
            p.text(lines[i], rgb(255, 255, 255), kAttrBold);
            if (i == 0 && !a->actionLabel.empty()) p.right(fmtFixed(a->actionScore), kDim);
            p.newline();
        }
        if (!a->thought.empty()) {
            const auto th = wrapText("\"" + a->thought + "\"", W - 2);
            for (std::size_t i = 0; i < th.size() && i < 4; ++i) {
                p.space(2);
                p.text(th[i], kThought, kAttrItalic);
                p.newline();
            }
        }
    }
    if (!a->considered.empty()) {
        section(p, "CONSIDERED");
        std::vector<const ConsideredOption*> opts;
        for (const auto& o : a->considered) opts.push_back(&o);
        std::stable_sort(opts.begin(), opts.end(),
                         [](const ConsideredOption* x, const ConsideredOption* y) { return x->score > y->score; });
        const float top = std::max(0.01f, opts.front()->score);
        for (std::size_t i = 0; i < opts.size() && i < 5; ++i) {
            const ConsideredOption& o = *opts[i];
            const bool chosen = !a->actionLabel.empty() && o.label == a->actionLabel;
            if (chosen) p.glyph(g.selector, kAccent, p.bg(), kAttrBold);
            else p.space();
            p.text(fmtFixed(o.score) + " ", blendRgb(kFaint, kGood, clamp01(o.score / top)));
            p.text(truncateUtf8(o.label, p.remaining(), g.ellipsis), chosen ? rgb(255, 255, 255) : kText,
                   chosen ? kAttrBold : kAttrNone);
            p.newline();
            if (!o.reason.empty()) {
                p.space(6);
                p.text(truncateUtf8(o.reason, p.remaining(), g.ellipsis), kDim, kAttrItalic);
                p.newline();
            }
        }
    }

    // ---- Inventory ----
    section(p, "CARRYING " + std::to_string(a->inventory.total()) + "/" + std::to_string(a->carryCapacity()));
    {
        bool any = false;
        for (int i = 0; i < kItemCount; ++i) {
            const int n = a->inventory.get(Item(i));
            if (n <= 0) continue;
            const std::string s = std::string(itemName(Item(i))) + " " + std::to_string(n);
            if (p.remaining() < textWidth(s) + 2) p.newline();
            if (any && p.col() > 0) p.text(", ", kFaint);
            p.text(s, Item(i) == Item::Food ? kGood : kText);
            any = true;
        }
        if (!any) p.text("nothing", kDim);
        p.newline();
        const float fs = a->foodSecurityDays();
        p.text("food for " + fmtFixed(fs, 1) + " days", fs < 0.5f ? kBad : (fs < 2.f ? kWarn : kDim));
        if (a->home != kNoStructure) p.text(g.dot + std::string("has a home"), kDim);
        p.newline();
    }

    // ---- Traits ----
    section(p, "TRAITS");
    {
        std::array<int, kTraitCount> idx{};
        for (int i = 0; i < kTraitCount; ++i) idx[std::size_t(i)] = i;
        std::sort(idx.begin(), idx.end(),
                  [&](int x, int y) { return a->genome.genes[std::size_t(x)] > a->genome.genes[std::size_t(y)]; });
        auto row = [&](bool high) {
            p.glyph(high ? g.up : g.down, high ? kWarn : kAccent, p.bg());
            for (int k = 0; k < 3; ++k) {
                const int t = high ? idx[std::size_t(k)] : idx[std::size_t(kTraitCount - 1 - k)];
                const float val = a->genome.genes[std::size_t(t)];
                p.text(std::string(" ") + traitShort(Trait(t)) + " ", kText);
                p.text(fmt2(val), high ? kWarn : kAccent);
            }
            p.newline();
        };
        row(true);
        row(false);
    }

    // ---- Norms ----
    section(p, "BELIEFS");
    for (int i = 0; i < kNormCount; ++i) {
        p.moveTo((i % 2) * half);
        std::string lbl = normShort(i);
        lbl.resize(9, ' ');
        const float nv = normValue(a->norms, i);
        p.text(lbl, kText);
        p.text(fmt2(nv), i < 2 ? blendRgb(kText, kBad, nv) : blendRgb(kText, kGood, nv));
        if (i % 2 == 1) p.newline();
    }
    p.ensureNewline();

    // ---- Relationships ----
    if (!a->relations.empty()) {
        section(p, "RELATIONSHIPS (" + std::to_string(a->relations.size()) + ")");
        struct RelRow {
            AgentId id;
            const Relationship* r;
            float weight;
        };
        std::vector<RelRow> rows;
        for (const auto& [id, rel] : a->relations) {
            float wgt = std::abs(rel.affinity) + rel.grievance * 0.8f;
            if (id == a->partner) wgt += 2.f;
            if (!sim.agent(id)) wgt *= 0.4f;
            rows.push_back({id, &rel, wgt});
        }
        std::sort(rows.begin(), rows.end(), [](const RelRow& x, const RelRow& y) { return x.weight > y.weight; });
        for (std::size_t i = 0; i < rows.size() && i < 6; ++i) {
            const RelRow& rr = rows[i];
            const Relationship& rel = *rr.r;
            const Agent* other = sim.agent(rr.id);
            std::string kind;
            if (rr.id == a->partner) kind = "partner";
            else if (rr.id == a->mother) kind = "mother";
            else if (rr.id == a->father) kind = "father";
            else if (std::find(a->children.begin(), a->children.end(), rr.id) != a->children.end()) kind = "child";
            else if (other && a->isFamily(*other)) kind = "kin";
            else if (rel.grievance > 0.3f) kind = "grudge";
            else if (rel.affinity > 0.5f) kind = "close friend";
            else if (rel.affinity > 0.2f) kind = "friend";
            else if (rel.affinity < -0.3f) kind = "enemy";
            else kind = "known";
            char32_t icon = g.neutral;
            Rgb ic = kDim;
            if (rr.id == a->partner || rel.affinity > 0.6f) { icon = g.heart; ic = kPink; }
            else if (rel.grievance > 0.3f || rel.affinity < -0.25f) { icon = g.grudge; ic = kBad; }
            else if (rel.affinity > 0.2f) { icon = g.friendly; ic = kGood; }
            p.glyph(icon, ic, p.bg(), kAttrBold);
            p.space();
            std::string nm = sim.nameOf(rr.id);
            if (!other) {
                std::string d;
                appendUtf8(d, g.dead);
                nm += d;
            }
            nm = truncateUtf8(nm, 11, g.ellipsis);
            const int nameEnd = p.col() + 12;
            p.text(nm, other ? clanColor(other->clan) : kFaint);
            p.moveTo(nameEnd);
            p.text(fmtSigned(rel.affinity), rel.affinity >= 0.f ? kGood : kBad);
            p.text(" t" + fmt2(rel.trust), kDim);
            if (rel.grievance > 0.05f) p.text(" g" + fmt2(rel.grievance), kBad);
            p.right(kind, kDim);
            p.newline();
        }
    }

    // ---- Record ----
    section(p, "RECORD");
    {
        const AgentStats& s = a->stats;
        auto stat = [&](const std::string& label, int n, Rgb c) {
            const std::string t = label + " " + std::to_string(n);
            if (p.remaining() < textWidth(t) + 1) p.newline();
            if (p.col() > 0) p.space();
            p.text(t, n > 0 ? c : kFaint);
        };
        stat("thefts", s.thefts, eventColor(EventKind::Theft));
        stat("robbed", s.timesStolenFrom, kDim);
        stat("gifts", s.giftsGiven, eventColor(EventKind::Gift));
        stat("got", s.giftsReceived, kDim);
        stat("fights", s.attacks, eventColor(EventKind::Fight));
        stat("kills", s.kills, kBad);
        stat("kids", s.childrenBorn, eventColor(EventKind::Birth));
        stat("built", s.structuresBuilt, eventColor(EventKind::Build));
        stat("breaks", s.breakdowns, eventColor(EventKind::Breakdown));
        stat("punished", s.punishmentsGiven, eventColor(EventKind::Justice));
        p.newline();
    }

    // ---- Skills ----
    {
        std::array<int, kSkillCount> idx{};
        for (int i = 0; i < kSkillCount; ++i) idx[std::size_t(i)] = i;
        std::sort(idx.begin(), idx.end(),
                  [&](int x, int y) { return a->skills.v[std::size_t(x)] > a->skills.v[std::size_t(y)]; });
        if (a->skills.v[std::size_t(idx[0])] > 0.01f) {
            section(p, "SKILLS");
            for (int k = 0; k < 3; ++k) {
                const float sv = a->skills.v[std::size_t(idx[std::size_t(k)])];
                if (sv <= 0.01f) break;
                std::string nm = skillName(Skill(idx[std::size_t(k)]));
                if (p.col() > 0) p.text(g.dot, kFaint);
                p.text(nm + " " + fmt2(sv), kText);
            }
            p.newline();
        }
    }

    // ---- Recent memories ----
    if (!a->memories.empty()) {
        section(p, "MEMORIES");
        int shown = 0;
        for (auto it = a->memories.rbegin(); it != a->memories.rend() && shown < 5; ++it, ++shown) {
            const Tick ago = now >= it->when ? now - it->when : 0;
            std::string when = hoursString(ago);
            when.resize(5, ' ');
            p.text(when, kFaint);
            p.text(truncateUtf8(describeMemory(*it, sim), p.remaining(), g.ellipsis),
                   blendRgb(kDim, kText, clamp01(it->intensity)));
            p.newline();
        }
    }
}

void Renderer::drawSociety(const Simulation& sim, PanelWriter& p) {
    const Glyphs& g = opt_.ascii ? kAscii : kUnicode;
    const auto& hist = sim.history();
    const Census& now = census_;
    const SimStats& st = sim.stats();

    // ---- Population chart ----
    {
        std::vector<float> pop;
        pop.reserve(hist.size() + 1);
        for (const Sample& s : hist) pop.push_back(float(s.population));
        pop.push_back(float(now.population));
        int mn = now.population, mx = now.population;
        for (const Sample& s : hist) {
            mn = std::min(mn, s.population);
            mx = std::max(mx, s.population);
        }
        section(p, "POPULATION  day 1" + std::string(g.arrow) + std::to_string(dayOfTick(sim.now()) + 1));
        chart(p, pop, 3, rgb(110, 210, 140));
        p.text("now ", kDim);
        p.text(std::to_string(now.population), kText, kAttrBold);
        p.text("  min " + std::to_string(mn) + "  max " + std::to_string(mx), kDim);
        p.newline();
        p.text(std::to_string(now.adults) + " adults" + g.dot + std::to_string(now.children) + " kids" + g.dot +
                   std::to_string(now.elders) + " elders",
               kText);
        p.newline();
        p.text(std::string(g.female) + std::to_string(now.females) + " " + g.male + std::to_string(now.males), kDim);
        if (now.pregnant > 0) p.text(g.dot + std::to_string(now.pregnant) + " pregnant", kPink);
        if (now.inBreakdown > 0) p.text(g.dot + std::to_string(now.inBreakdown) + " breaking", kBad);
        p.newline();
        p.text("mood ", kDim);
        p.text(fmtSigned(now.avgMood), now.avgMood >= 0.f ? kGood : kBad);
        p.text("  stress ", kDim);
        p.text(fmt2(now.avgStress), levelColor(1.f - now.avgStress));
        p.text("  health ", kDim);
        p.text(fmt2(now.avgHealth), levelColor(now.avgHealth));
        p.newline();
        p.text("fed ", kDim);
        p.text(fmt2(now.avgFood), levelColor(now.avgFood));
        p.text("  wild food " + std::to_string(int(now.wildFood)), kDim);
        p.newline();
    }

    // ---- Trait evolution ----
    const Census& base = haveBaseline_ ? baseline_ : now;
    const bool canCompare = haveBaseline_ && now.population > 0;
    section(p, "GENES  day 0" + std::string(g.arrow) + "now");
    for (int t = 0; t < kTraitCount; ++t) {
        const float b = base.avgTraits[std::size_t(t)];
        const float n = canCompare ? now.avgTraits[std::size_t(t)] : b;
        const float d = n - b;
        std::string lbl = traitLabel(Trait(t));
        lbl = truncateUtf8(lbl, 15, "");
        lbl.resize(16, ' ');
        p.text(lbl, kText);
        p.text(fmtFixed(b) + " " + g.arrow + " ", kDim);
        const Rgb dc = std::abs(d) > 0.03f ? (d > 0 ? kWarn : kAccent) : kText;
        p.text(fmtFixed(n), dc, std::abs(d) > 0.03f ? kAttrBold : kAttrNone);
        if (std::abs(d) > 0.03f) {
            p.space();
            p.glyph(d > 0 ? g.up : g.down, dc, p.bg());
            p.text(fmtSigned(d), dc);
        }
        p.newline();
    }

    // ---- Culture ----
    section(p, "CULTURE  day 0" + std::string(g.arrow) + "now");
    for (int i = 0; i < kNormCount; ++i) {
        const float b = base.avgNorms[std::size_t(i)];
        const float n = canCompare ? now.avgNorms[std::size_t(i)] : b;
        const float d = n - b;
        std::string lbl = normLabel(i);
        lbl.resize(20, ' ');
        p.text(lbl, kText);
        p.text(fmtFixed(b) + " " + g.arrow + " ", kDim);
        const Rgb dc = std::abs(d) > 0.03f ? (d > 0 ? kWarn : kAccent) : kText;
        p.text(fmtFixed(n), dc);
        if (std::abs(d) > 0.03f) {
            p.space();
            p.glyph(d > 0 ? g.up : g.down, dc, p.bg());
        }
        p.newline();
    }

    // ---- Deaths ----
    section(p, "DEATHS " + std::to_string(st.deaths) + g.dot + "BIRTHS " + std::to_string(st.births));
    {
        const int other = std::max(0, st.deaths - st.starvations - st.freezings - st.killings - st.oldAgeDeaths);
        const std::pair<const char*, int> causes[] = {
            {"starved", st.starvations}, {"froze", st.freezings}, {"killed", st.killings},
            {"old age", st.oldAgeDeaths}, {"other", other}};
        const int total = std::max(1, st.deaths);
        for (const auto& [name, n] : causes) {
            std::string lbl = name;
            lbl.resize(9, ' ');
            p.text(lbl, kText);
            bar(p, std::max(4, p.width() - 16), float(n) / float(total), eventColor(EventKind::Death));
            p.text(" " + std::to_string(n), n > 0 ? kText : kFaint);
            p.newline();
        }
        p.text("thefts " + std::to_string(st.thefts) + "  gifts " + std::to_string(st.gifts) + "  fights " +
                   std::to_string(st.fights),
               kDim);
        p.newline();
        p.text("punish " + std::to_string(st.punishments) + "  forgive " + std::to_string(st.forgivenesses) +
                   "  trades " + std::to_string(st.trades),
               kDim);
        p.newline();
        p.text("breakdowns " + std::to_string(st.breakdowns) + "  matings " + std::to_string(st.matings), kDim);
        p.newline();
    }

    // ---- Notable individuals ----
    auto top = [&](const char* title, auto key, const char* unit, Rgb c) {
        std::vector<const Agent*> all = livingAgents(sim);
        std::stable_sort(all.begin(), all.end(), [&](const Agent* x, const Agent* y) { return key(*x) > key(*y); });
        section(p, title);
        int shown = 0;
        for (const Agent* ag : all) {
            if (shown >= 3 || key(*ag) <= 0) break;
            p.text(truncateUtf8(ag->name, 14, g.ellipsis), clanColor(ag->clan));
            p.moveTo(15);
            p.text(std::to_string(key(*ag)) + " " + unit, c);
            p.newline();
            ++shown;
        }
        if (shown == 0) {
            p.text("nobody yet", kFaint);
            p.newline();
        }
    };
    top("MOST WANTED", [](const Agent& x) { return x.stats.thefts; }, "thefts", eventColor(EventKind::Theft));
    top("MOST GENEROUS", [](const Agent& x) { return x.stats.giftsGiven; }, "gifts", eventColor(EventKind::Gift));
    top("MOST VIOLENT", [](const Agent& x) { return x.stats.attacks; }, "attacks", eventColor(EventKind::Fight));

    // ---- Clans ----
    section(p, "CLANS (" + std::to_string(now.clanSizes.size()) + ")");
    {
        std::vector<std::pair<int, int>> clans(now.clanSizes.begin(), now.clanSizes.end());
        std::sort(clans.begin(), clans.end(), [](const auto& x, const auto& y) { return x.second > y.second; });
        const int biggest = clans.empty() ? 1 : std::max(1, clans.front().second);
        for (std::size_t i = 0; i < clans.size() && i < 5; ++i) {
            std::string lbl = "clan " + std::to_string(clans[i].first);
            lbl.resize(9, ' ');
            p.text(lbl, clanColor(clans[i].first), kAttrBold);
            bar(p, std::max(4, p.width() - 15), float(clans[i].second) / float(biggest), clanColor(clans[i].first));
            p.text(" " + std::to_string(clans[i].second), kText);
            p.newline();
        }
    }

    // ---- Economy ----
    section(p, "STRUCTURES " + std::to_string(now.structuresComplete) + " (+" +
                   std::to_string(now.structuresBuilding) + " building)");
    for (int t = 0; t < kStructureTypeCount; ++t) {
        const int n = now.completeByType[std::size_t(t)];
        const std::string s = std::string(structureLabel(StructureType(t))) + " " + std::to_string(n);
        if (p.remaining() < textWidth(s) + 2) p.newline();
        if (p.col() > 0) p.space(2);
        p.text(s, n > 0 ? structureColor(StructureType(t)) : kFaint);
    }
    p.newline();
    p.text("max generation " + std::to_string(st.maxGeneration), kDim);
    p.newline();
}

void Renderer::drawAgentList(const Simulation& sim, const ViewState& v, Rect r) {
    const Glyphs& g = opt_.ascii ? kAscii : kUnicode;
    const std::vector<const Agent*> all = livingAgents(sim);
    const Tick now = sim.now();
    std::string head = "LIVING " + std::to_string(all.size());
    screen_.text(r.x, r.y, head, Style{kHeader, kPanelBg, kAttrBold});
    const std::string hint = opt_.ascii ? "up/dn Enter=select" : "↑↓ Enter=select";
    screen_.text(r.x + r.w - textWidth(hint), r.y, hint, Style{kFaint, kPanelBg, kAttrNone});
    const int listH = r.h - 1;
    if (listH <= 0) return;
    int cursor = 0;
    for (int i = 0; i < int(all.size()); ++i)
        if (all[std::size_t(i)]->id == v.listCursor) cursor = i;
    const int n = int(all.size());
    const int start = std::clamp(cursor - (listH - 1) / 2, 0, std::max(0, n - listH));
    for (int row = 0; row < listH && start + row < n; ++row) {
        const Agent* a = all[std::size_t(start + row)];
        const int y = r.y + 1 + row;
        const bool isCursor = a->id == v.listCursor;
        const Rgb bg = isCursor ? rgb(50, 60, 95) : kPanelBg;
        screen_.fill(r.x, y, r.w, 1, U' ', Style{kText, bg, kAttrNone});
        int x = r.x;
        const LifeStage stage = a->stage(now);
        screen_.put(x++, y, a->id == v.selected ? g.selector : U' ', Style{kAccent, bg, kAttrBold});
        const char32_t ch = stage == LifeStage::Child ? g.child : g.adult;
        Style gs{clanColor(a->clan), bg, std::uint8_t(kAttrBold | (a->pregnant ? kAttrUnderline : 0))};
        if (a->inBreakdown()) gs = Style{rgb(255, 255, 255), rgb(190, 30, 30), kAttrBold};
        screen_.put(x++, y, ch, gs);
        ++x;
        x += screen_.text(x, y, truncateUtf8(a->name, 11, g.ellipsis), Style{clanColor(a->clan), bg, kAttrNone});
        x = r.x + 15;
        std::string age = std::to_string(a->ageDays(now)) + ageStageLetter(stage);
        x += screen_.text(x, y, age, Style{kDim, bg, kAttrNone});
        x = r.x + 20;
        const std::string act = a->actionLabel.empty() ? std::string("-") : a->actionLabel;
        const Rgb ac = a->inBreakdown() ? kBad : (a->actionKind == kinds::Sleep ? kFaint : kText);
        screen_.text(x, y, truncateUtf8(act, r.x + r.w - x, g.ellipsis), Style{ac, bg, kAttrNone});
    }
}

// ---------------------------------------------------------------------------------------------------
// Event log & footer
// ---------------------------------------------------------------------------------------------------
void Renderer::drawLog(const Simulation& sim, const ViewState& v, Rect r) {
    const Glyphs& g = opt_.ascii ? kAscii : kUnicode;
    if (r.h <= 0) return;
    screen_.fill(r.x, r.y, r.w, r.h, U' ', Style{kText, kLogBg, kAttrNone});
    // Header doubles as the separator line.
    for (int x = r.x; x < r.x + r.w; ++x) screen_.put(x, r.y, g.hline, Style{kFaint, kLogBg, kAttrNone});
    std::string title = " EVENTS: ";
    int x = r.x + 2;
    x += screen_.text(x, r.y, title, Style{kHeader, kLogBg, kAttrBold});
    std::string filt = logFilterName(v.logFilter);
    if (v.logFilter == LogFilter::Selected && !v.selectedName.empty()) filt = v.selectedName;
    x += screen_.text(x, r.y, filt + " ", Style{kAccent, kLogBg, kAttrBold});
    screen_.text(x, r.y, "(e) ", Style{kFaint, kLogBg, kAttrNone});
    if (r.x + r.w - 40 > x + 6) {
        const std::string legend = " theft gift fight romance birth death break justice ";
        int lx = r.x + r.w - textWidth(legend) - 2;
        const std::pair<const char*, EventKind> kinds[] = {
            {"theft", EventKind::Theft},     {"gift", EventKind::Gift},   {"fight", EventKind::Fight},
            {"romance", EventKind::Romance}, {"birth", EventKind::Birth}, {"death", EventKind::Death},
            {"break", EventKind::Breakdown}, {"justice", EventKind::Justice}};
        for (const auto& [name, k] : kinds) {
            screen_.put(lx++, r.y, U' ', Style{kText, kLogBg, kAttrNone});
            lx += screen_.text(lx, r.y, name, Style{eventColor(k), kLogBg, kAttrNone});
        }
        screen_.put(lx, r.y, U' ', Style{kText, kLogBg, kAttrNone});
    }

    const int lines = r.h - 1;
    std::vector<const LogEntry*> picked;
    const auto& log = sim.logEntries();
    int scanned = 0;
    for (auto it = log.rbegin(); it != log.rend() && int(picked.size()) < lines && scanned < 20000; ++it, ++scanned)
        if (logFilterAccepts(v.logFilter, *it, v.selected)) picked.push_back(&*it);
    if (picked.empty()) {
        screen_.text(r.x + 2, r.y + 1, "(no events yet)", Style{kFaint, kLogBg, kAttrNone});
        return;
    }
    for (int i = 0; i < int(picked.size()); ++i) {
        const LogEntry& e = *picked[picked.size() - 1 - std::size_t(i)];
        const int y = r.y + 1 + (lines - int(picked.size())) + i;
        int lx = r.x;
        const bool involves = v.selected != kNoAgent && (e.a == v.selected || e.b == v.selected);
        screen_.put(lx++, y, involves ? g.selector : U' ', Style{kAccent, kLogBg, kAttrBold});
        std::string d = "D" + std::to_string(dayOfTick(e.tick) + 1);
        d.resize(5, ' ');
        lx += screen_.text(lx, y, d, Style{kFaint, kLogBg, kAttrNone});
        const Rgb c = eventColor(e.kind);
        screen_.text(lx, y, truncateUtf8(e.text, r.x + r.w - lx - 1, g.ellipsis),
                     Style{involves ? blendRgb(c, rgb(255, 255, 255), 0.3f) : c, kLogBg,
                           involves ? kAttrBold : kAttrNone});
    }
}

void Renderer::drawFooter(const ViewState& v, Rect r) {
    screen_.fill(r.x, r.y, r.w, 1, U' ', Style{kDim, kTopBg, kAttrNone});
    struct KeyHint {
        const char* key;
        const char* what;
    };
    static constexpr KeyHint hints[] = {{"spc", "pause"}, {"+/-", "speed"}, {"tab", "next"}, {"f", "follow"},
                                        {"v", "panel"},   {"e", "events"},  {"?", "help"},   {"q", "quit"}};
    const int statusW = v.status.empty() ? 0 : std::min(r.w / 2, textWidth(v.status) + 2);
    int x = r.x + 1;
    for (const KeyHint& h : hints) {
        const int need = textWidth(h.key) + textWidth(h.what) + 3;
        if (x + need > r.x + r.w - statusW) break;
        x += screen_.text(x, r.y, h.key, Style{kAccent, kTopBg, kAttrBold});
        x += screen_.text(x + 1, r.y, h.what, Style{kDim, kTopBg, kAttrNone}) + 3;
    }
    if (!v.status.empty()) {
        const std::string s = truncateUtf8(v.status, statusW - 1, opt_.ascii ? "~" : "…");
        screen_.text(r.x + r.w - textWidth(s) - 1, r.y, s, Style{kWarn, kTopBg, kAttrBold});
    }
}

// ---------------------------------------------------------------------------------------------------
// Overlays
// ---------------------------------------------------------------------------------------------------
void Renderer::drawCenteredBox(const std::vector<std::string>& lines, Style border, Style text) {
    const Glyphs& g = opt_.ascii ? kAscii : kUnicode;
    int w = 0;
    for (const auto& l : lines) w = std::max(w, textWidth(l));
    w = std::min(w + 4, screen_.width());
    const int h = std::min(int(lines.size()) + 2, screen_.height());
    const int x0 = (screen_.width() - w) / 2, y0 = (screen_.height() - h) / 2;
    screen_.fill(x0, y0, w, h, U' ', Style{text.fg, text.bg, kAttrNone});
    for (int x = x0; x < x0 + w; ++x) {
        screen_.put(x, y0, g.hline, border);
        screen_.put(x, y0 + h - 1, g.hline, border);
    }
    for (int y = y0; y < y0 + h; ++y) {
        screen_.put(x0, y, g.vline, border);
        screen_.put(x0 + w - 1, y, g.vline, border);
    }
    for (int i = 0; i < int(lines.size()) && i + 1 < h - 1; ++i) {
        const Style st = i == 0 ? border : text;
        screen_.text(x0 + 2, y0 + 1 + i, lines[std::size_t(i)], st, w - 4);
    }
}

void Renderer::drawHelp() {
    const Glyphs& g = opt_.ascii ? kAscii : kUnicode;
    auto glyphStr = [](char32_t c) {
        std::string s;
        appendUtf8(s, c);
        return s;
    };
    std::string structures;
    for (int t = 0; t < kStructureTypeCount; ++t) {
        structures += std::string(1, blueprint(StructureType(t)).glyph) + " " + structureLabel(StructureType(t));
        if (t + 1 < kStructureTypeCount) structures += "  ";
    }
    std::vector<std::string> lines = {
        "HEURISTICA  -  help",
        "Nobody is scripted. Every agent scores its options each tick using needs,",
        "feelings, genes and a heuristic conscience; the inspector shows what it",
        "considered and WHY it chose (its thought). Just watch - or poke around.",
        "",
        "space      pause / resume            n        step one tick (pauses)",
        "+ / -      faster / slower           1..9     speed x1 .. x256  (0: x1/4)",
        "tab  ]     next agent                [  S-tab previous agent",
        "f          follow selected agent     c        centre on selected",
        "arrows/wasd pan the camera (WASD = big steps)",
        "v          panel: inspector / society / agent list",
        "PgUp/PgDn  scroll panel (list: move) Enter    select agent in list",
        "e          events: all / crime / romance & births / deaths / selected",
        "?          toggle this help          q  Esc   quit",
        "",
        "AGENTS   @ adult   o child   underlined @ = pregnant   colour = clan, dim = asleep",
        "         flashing red = mental break, reversed = selected (dots: path)",
        "NATURE   " + glyphStr(g.tree) + " tree  * berries  ^ rock  ~ fish  \" herbs  , grain  " + glyphStr(g.mountain) +
            " mountain",
        "BUILT    " + structures,
        "         + under construction; underlined = selected agent's home",
        "",
        "Press any key to close.",
    };
    drawCenteredBox(lines, Style{rgb(255, 210, 120), rgb(22, 24, 36), kAttrBold}, Style{kText, rgb(22, 24, 36), kAttrNone});
}

}  // namespace heur
