// Heuristica — off-screen cell buffer with flicker-free ANSI diff encoding.
#pragma once

#include <climits>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace heur {

// 0xRRGGBB, or kDefaultColor for the terminal's own default colour.
using Rgb = std::uint32_t;
inline constexpr Rgb kDefaultColor = 0xFF000000u;

constexpr Rgb rgb(int r, int g, int b) {
    auto c = [](int v) { return std::uint32_t(v < 0 ? 0 : (v > 255 ? 255 : v)); };
    return (c(r) << 16) | (c(g) << 8) | c(b);
}
constexpr int rOf(Rgb c) { return int((c >> 16) & 0xFF); }
constexpr int gOf(Rgb c) { return int((c >> 8) & 0xFF); }
constexpr int bOf(Rgb c) { return int(c & 0xFF); }
Rgb scaleRgb(Rgb c, float k);              // multiply brightness
Rgb blendRgb(Rgb a, Rgb b, float t);       // a -> b by t in [0,1]

enum Attr : std::uint8_t { kAttrNone = 0, kAttrBold = 1, kAttrItalic = 2, kAttrUnderline = 4 };

struct Style {
    Rgb fg = kDefaultColor;
    Rgb bg = kDefaultColor;
    std::uint8_t attr = kAttrNone;
};

struct Cell {
    char32_t ch = U' ';
    Rgb fg = kDefaultColor;
    Rgb bg = kDefaultColor;
    std::uint8_t attr = kAttrNone;
    bool operator==(const Cell&) const = default;
};

// Number of terminal cells a UTF-8 string occupies (every code point counts as one cell).
int textWidth(std::string_view utf8);
// Decodes one code point at utf8[i] and advances i (invalid bytes decode as '?').
char32_t decodeUtf8(std::string_view utf8, std::size_t& i);
void appendUtf8(std::string& out, char32_t cp);
// Truncates to at most maxCells cells, ending with `ellipsis` when shortened.
std::string truncateUtf8(std::string_view utf8, int maxCells, std::string_view ellipsis);
// Greedy word wrap into lines of at most `width` cells.
std::vector<std::string> wrapText(std::string_view utf8, int width);

class Screen {
public:
    void resize(int w, int h);
    int width() const { return w_; }
    int height() const { return h_; }

    void clear(Style st);
    void put(int x, int y, char32_t ch, Style st);
    Cell* cell(int x, int y);  // nullptr when out of bounds
    void fill(int x, int y, int w, int h, char32_t ch, Style st);
    // Draws UTF-8 text clipped to maxW cells and the screen edge. Returns the cells advanced.
    int text(int x, int y, std::string_view utf8, Style st, int maxW = INT_MAX);

    // Encodes the cells that changed since the last encode() into `out` (cleared first). Produces an
    // empty string when nothing changed. Wrapped in synchronized-output markers (DEC 2026).
    void encode(std::string& out, bool truecolor);
    // Next encode() repaints everything (after resize, resume from suspend, ...).
    void invalidate() { full_ = true; }

private:
    int w_ = 0;
    int h_ = 0;
    std::vector<Cell> cur_;
    std::vector<Cell> prev_;
    bool full_ = true;
};

}  // namespace heur
