// Heuristica — off-screen cell buffer & ANSI encoder. See Screen.h.
#include "render/Screen.h"

#include <algorithm>
#include <cstdio>

namespace heur {

Rgb scaleRgb(Rgb c, float k) {
    return rgb(int(float(rOf(c)) * k + 0.5f), int(float(gOf(c)) * k + 0.5f), int(float(bOf(c)) * k + 0.5f));
}

Rgb blendRgb(Rgb a, Rgb b, float t) {
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    auto mix = [t](int x, int y) { return int(float(x) + (float(y) - float(x)) * t + 0.5f); };
    return rgb(mix(rOf(a), rOf(b)), mix(gOf(a), gOf(b)), mix(bOf(a), bOf(b)));
}

// ---------------------------------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------------------------------
char32_t decodeUtf8(std::string_view s, std::size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) { ++i; return c; }
    const int len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 0;
    if (len == 0 || i + std::size_t(len) > s.size()) { ++i; return U'?'; }
    char32_t cp = c & (len == 2 ? 0x1F : len == 3 ? 0x0F : 0x07);
    for (int k = 1; k < len; ++k) {
        const unsigned char cc = static_cast<unsigned char>(s[i + std::size_t(k)]);
        if ((cc & 0xC0) != 0x80) { ++i; return U'?'; }
        cp = (cp << 6) | (cc & 0x3F);
    }
    i += std::size_t(len);
    return cp;
}

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

int textWidth(std::string_view s) {
    int n = 0;
    for (std::size_t i = 0; i < s.size();) {
        decodeUtf8(s, i);
        ++n;
    }
    return n;
}

std::string truncateUtf8(std::string_view s, int maxCells, std::string_view ellipsis) {
    if (maxCells <= 0) return {};
    if (textWidth(s) <= maxCells) return std::string(s);
    const int ell = textWidth(ellipsis);
    const int keep = std::max(0, maxCells - ell);
    std::size_t i = 0;
    for (int n = 0; n < keep && i < s.size(); ++n) decodeUtf8(s, i);
    std::string out(s.substr(0, i));
    if (ell <= maxCells) out += ellipsis;
    return out;
}

std::vector<std::string> wrapText(std::string_view s, int width) {
    std::vector<std::string> lines;
    if (width <= 0) return lines;
    std::string line;
    int lineW = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') ++i;
        std::size_t j = i;
        while (j < s.size() && s[j] != ' ') ++j;
        if (j == i) break;
        std::string_view word = s.substr(i, j - i);
        int ww = textWidth(word);
        if (lineW > 0 && lineW + 1 + ww > width) {
            lines.push_back(line);
            line.clear();
            lineW = 0;
        }
        while (ww > width) {  // hard-split very long words
            std::size_t k = 0;
            for (int n = 0; n < width - lineW && k < word.size(); ++n) decodeUtf8(word, k);
            line += word.substr(0, k);
            lines.push_back(line);
            line.clear();
            lineW = 0;
            word = word.substr(k);
            ww = textWidth(word);
        }
        if (lineW > 0) { line += ' '; ++lineW; }
        line += word;
        lineW += ww;
        i = j;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

// ---------------------------------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------------------------------
void Screen::resize(int w, int h) {
    w = std::max(0, w);
    h = std::max(0, h);
    if (w == w_ && h == h_) return;
    w_ = w;
    h_ = h;
    cur_.assign(std::size_t(w) * std::size_t(h), Cell{});
    prev_.assign(cur_.size(), Cell{});
    full_ = true;
}

void Screen::clear(Style st) {
    const Cell c{U' ', st.fg, st.bg, st.attr};
    std::fill(cur_.begin(), cur_.end(), c);
}

Cell* Screen::cell(int x, int y) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return nullptr;
    return &cur_[std::size_t(y) * std::size_t(w_) + std::size_t(x)];
}

void Screen::put(int x, int y, char32_t ch, Style st) {
    if (Cell* c = cell(x, y)) *c = Cell{ch, st.fg, st.bg, st.attr};
}

void Screen::fill(int x, int y, int w, int h, char32_t ch, Style st) {
    for (int yy = std::max(0, y); yy < std::min(h_, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(w_, x + w); ++xx) put(xx, yy, ch, st);
}

int Screen::text(int x, int y, std::string_view s, Style st, int maxW) {
    int n = 0;
    for (std::size_t i = 0; i < s.size() && n < maxW;) {
        const char32_t cp = decodeUtf8(s, i);
        put(x + n, y, cp, st);
        ++n;
    }
    return n;
}

namespace {

// Code points whose rendered width varies between terminals/fonts (emoji-capable symbols). After
// emitting one we re-position the cursor explicitly so a 2-wide rendering can't shift the row.
bool riskyWidth(char32_t c) { return (c >= 0x2600 && c <= 0x27BF) || c >= 0x1F000; }

int to256(Rgb c) {
    static constexpr int kLevels[6] = {0, 95, 135, 175, 215, 255};
    const int r = rOf(c), g = gOf(c), b = bOf(c);
    auto q = [](int v) { return v < 48 ? 0 : (v < 115 ? 1 : std::min(5, (v - 35) / 40)); };
    const int qr = q(r), qg = q(g), qb = q(b);
    auto d2 = [](int r1, int g1, int b1, int r2, int g2, int b2) {
        return (r1 - r2) * (r1 - r2) + (g1 - g2) * (g1 - g2) + (b1 - b2) * (b1 - b2);
    };
    const int cubeD = d2(r, g, b, kLevels[qr], kLevels[qg], kLevels[qb]);
    const int avg = (r + g + b) / 3;
    const int gi = avg > 238 ? 23 : std::max(0, (avg - 3) / 10);
    const int gv = 8 + 10 * gi;
    const int grayD = d2(r, g, b, gv, gv, gv);
    return grayD < cubeD ? 232 + gi : 16 + 36 * qr + 6 * qg + qb;
}

void appendInt(std::string& out, int v) {
    char buf[12];
    const int n = std::snprintf(buf, sizeof(buf), "%d", v);
    out.append(buf, std::size_t(n));
}

void appendColor(std::string& out, Rgb c, bool fg, bool truecolor) {
    if (c == kDefaultColor) {
        out += fg ? ";39" : ";49";
        return;
    }
    out += fg ? ";38" : ";48";
    if (truecolor) {
        out += ";2;";
        appendInt(out, rOf(c));
        out += ';';
        appendInt(out, gOf(c));
        out += ';';
        appendInt(out, bOf(c));
    } else {
        out += ";5;";
        appendInt(out, to256(c));
    }
}

}  // namespace

void Screen::encode(std::string& out, bool truecolor) {
    out.clear();
    if (w_ == 0 || h_ == 0) return;
    std::string body;
    body.reserve(full_ ? cur_.size() * 12 : 4096);
    if (full_) body += "\x1b[0m\x1b[2J";
    int curX = -1, curY = -1;
    bool haveStyle = false;
    Rgb sFg = 0, sBg = 0;
    std::uint8_t sAttr = 0;
    for (int y = 0; y < h_; ++y) {
        for (int x = 0; x < w_; ++x) {
            const std::size_t idx = std::size_t(y) * std::size_t(w_) + std::size_t(x);
            const Cell& c = cur_[idx];
            if (!full_ && c == prev_[idx]) continue;
            if (curX != x || curY != y) {
                body += "\x1b[";
                appendInt(body, y + 1);
                body += ';';
                appendInt(body, x + 1);
                body += 'H';
            }
            // A blank cell's foreground is invisible, so it never forces a colour change.
            const bool blank = c.ch == U' ' && !(c.attr & kAttrUnderline);
            const bool fgDiff = !blank && c.fg != sFg;
            const bool bgDiff = c.bg != sBg;
            if (!haveStyle || c.attr != sAttr) {
                body += "\x1b[0";
                if (c.attr & kAttrBold) body += ";1";
                if (c.attr & kAttrItalic) body += ";3";
                if (c.attr & kAttrUnderline) body += ";4";
                appendColor(body, c.fg, true, truecolor);
                appendColor(body, c.bg, false, truecolor);
                body += 'm';
                haveStyle = true;
                sFg = c.fg;
                sBg = c.bg;
                sAttr = c.attr;
            } else if (fgDiff || bgDiff) {
                // Incremental update: only the colour(s) that changed.
                std::string sgr;
                if (fgDiff) appendColor(sgr, c.fg, true, truecolor);
                if (bgDiff) appendColor(sgr, c.bg, false, truecolor);
                body += "\x1b[";
                body.append(sgr, 1, std::string::npos);  // drop the leading ';'
                body += 'm';
                if (fgDiff) sFg = c.fg;
                sBg = c.bg;
            }
            appendUtf8(body, c.ch < 0x20 ? U' ' : c.ch);
            curX = riskyWidth(c.ch) ? -1 : x + 1;
            curY = y;
        }
    }
    prev_ = cur_;
    full_ = false;
    if (body.empty()) return;
    out.reserve(body.size() + 32);
    out += "\x1b[?2026h";  // begin synchronized update (ignored by terminals without support)
    out += body;
    out += "\x1b[0m\x1b[?2026l";
}

}  // namespace heur
