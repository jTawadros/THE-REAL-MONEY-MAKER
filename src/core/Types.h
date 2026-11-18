// Heuristica — core value types shared by every module.
// FROZEN CONTRACT: other modules compile against this header.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>

namespace heur {

using AgentId = std::uint32_t;
using StructureId = std::uint32_t;
using Tick = std::uint64_t;

inline constexpr AgentId kNoAgent = 0;
inline constexpr StructureId kNoStructure = 0;

// ---------------------------------------------------------------------------
// Grid vector
// ---------------------------------------------------------------------------
struct Vec2i {
    int x = 0;
    int y = 0;

    constexpr Vec2i() = default;
    constexpr Vec2i(int x_, int y_) : x(x_), y(y_) {}

    constexpr Vec2i operator+(Vec2i o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2i operator-(Vec2i o) const { return {x - o.x, y - o.y}; }
    constexpr bool operator==(const Vec2i&) const = default;
};

inline int manhattan(Vec2i a, Vec2i b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }
inline int chebyshev(Vec2i a, Vec2i b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }
inline float euclid(Vec2i a, Vec2i b) {
    const float dx = float(a.x - b.x), dy = float(a.y - b.y);
    return std::sqrt(dx * dx + dy * dy);
}
// Octile distance: admissible A* heuristic for 8-connected grids (diagonal cost sqrt2).
inline float octile(Vec2i a, Vec2i b) {
    const int dx = std::abs(a.x - b.x), dy = std::abs(a.y - b.y);
    return float(std::max(dx, dy)) + 0.41421356f * float(std::min(dx, dy));
}

struct Vec2iHash {
    std::size_t operator()(const Vec2i& v) const noexcept {
        return (std::size_t(std::uint32_t(v.x)) * 73856093u) ^ (std::size_t(std::uint32_t(v.y)) * 19349663u);
    }
};

inline constexpr std::array<Vec2i, 8> kDirs8 = {
    Vec2i{1, 0}, Vec2i{-1, 0}, Vec2i{0, 1}, Vec2i{0, -1},
    Vec2i{1, 1}, Vec2i{1, -1}, Vec2i{-1, 1}, Vec2i{-1, -1}};
inline constexpr std::array<Vec2i, 4> kDirs4 = {Vec2i{1, 0}, Vec2i{-1, 0}, Vec2i{0, 1}, Vec2i{0, -1}};

// ---------------------------------------------------------------------------
// Math helpers
// ---------------------------------------------------------------------------
inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
// Smooth logistic curve: 0 at x<<mid, 1 at x>>mid. steep ~ 8..15 for 0..1 inputs.
inline float logistic(float x, float mid, float steep) { return 1.f / (1.f + std::exp(-steep * (x - mid))); }

// ---------------------------------------------------------------------------
// Items & inventory (every item weighs 1 carry unit)
// ---------------------------------------------------------------------------
enum class Item : std::uint8_t { Food, Wood, Stone, Seeds, Herbs, Tools, Count };
inline constexpr int kItemCount = static_cast<int>(Item::Count);

inline const char* itemName(Item i) {
    switch (i) {
        case Item::Food: return "food";
        case Item::Wood: return "wood";
        case Item::Stone: return "stone";
        case Item::Seeds: return "seeds";
        case Item::Herbs: return "herbs";
        case Item::Tools: return "tools";
        default: return "?";
    }
}

using ItemCounts = std::array<int, kItemCount>;

struct Inventory {
    ItemCounts counts{};

    int get(Item i) const { return counts[std::size_t(i)]; }
    bool has(Item i, int n = 1) const { return get(i) >= n; }
    void add(Item i, int n) { counts[std::size_t(i)] = std::max(0, counts[std::size_t(i)] + n); }
    // Removes up to n units; returns how many were actually removed.
    int take(Item i, int n) {
        const int got = std::min(n, get(i));
        counts[std::size_t(i)] -= got;
        return got;
    }
    int total() const {
        int t = 0;
        for (int c : counts) t += c;
        return t;
    }
    bool empty() const { return total() == 0; }
};

}  // namespace heur
