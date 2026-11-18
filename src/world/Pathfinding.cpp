// Heuristica — A* over the 8-connected world grid.
#include "world/Pathfinding.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

#include "world/World.h"

namespace heur {

namespace {

constexpr float kDiagonalStep = 1.41421356f;
constexpr int kQuickRejectMaxRange = 3;  // probe the goal neighbourhood for walkability up to this range

struct OpenEntry {
    float f;
    float g;
    int idx;
};

// Min-heap on f; on ties prefer the deeper node (larger g) — fewer expansions on open terrain.
struct OpenCmp {
    bool operator()(const OpenEntry& a, const OpenEntry& b) const {
        if (a.f != b.f) return a.f > b.f;
        if (a.g != b.g) return a.g < b.g;
        return a.idx > b.idx;
    }
};

// Per-thread scratch buffers reused across queries. A node's data is valid only when its stamp equals the
// current generation, so nothing has to be cleared between searches.
struct Scratch {
    int size = 0;
    std::uint32_t generation = 0;
    std::vector<std::uint32_t> seen;     // g/parent valid for this generation
    std::vector<std::uint32_t> closed;   // expanded in this generation
    std::vector<std::uint32_t> walkGen;  // walkability cached for this generation
    std::vector<std::uint8_t> walk;
    std::vector<float> g;
    std::vector<int> parent;
    std::vector<OpenEntry> open;

    void begin(int n) {
        if (n != size) {
            size = n;
            seen.assign(std::size_t(n), 0);
            closed.assign(std::size_t(n), 0);
            walkGen.assign(std::size_t(n), 0);
            walk.assign(std::size_t(n), 0);
            g.assign(std::size_t(n), 0.f);
            parent.assign(std::size_t(n), -1);
            generation = 0;
        }
        if (++generation == 0) {  // wrapped: reset stamps once every 2^32 searches
            std::fill(seen.begin(), seen.end(), 0);
            std::fill(closed.begin(), closed.end(), 0);
            std::fill(walkGen.begin(), walkGen.end(), 0);
            generation = 1;
        }
        open.clear();
    }
};

Scratch& scratch() {
    thread_local Scratch s;
    return s;
}

// Admissible & consistent: octile distance from p to the nearest tile of the goal square
// [goal - range, goal + range]^2 (every step costs >= 1 per unit length).
float heuristic(Vec2i p, Vec2i goal, int range) {
    const int dx = std::max(0, std::abs(p.x - goal.x) - range);
    const int dy = std::max(0, std::abs(p.y - goal.y) - range);
    return float(std::max(dx, dy)) + 0.41421356f * float(std::min(dx, dy));
}

}  // namespace

std::vector<Vec2i> findPath(const World& world, Vec2i start, Vec2i goal, int range, int maxExpansions) {
    std::vector<Vec2i> path;
    range = std::max(0, range);
    if (chebyshev(start, goal) <= range) return path;
    if (!world.inBounds(start)) return path;

    const int w = world.width(), h = world.height();

    // Quick reject: nothing walkable within range of the goal => no search needed.
    if (range <= kQuickRejectMaxRange) {
        bool any = false;
        for (int dy = -range; dy <= range && !any; ++dy)
            for (int dx = -range; dx <= range && !any; ++dx) any = world.walkable(Vec2i{goal.x + dx, goal.y + dy});
        if (!any) return path;
    }

    Scratch& S = scratch();
    S.begin(w * h);
    const std::uint32_t gen = S.generation;

    const auto walkableCached = [&](int x, int y) -> bool {
        if (x < 0 || y < 0 || x >= w || y >= h) return false;
        const std::size_t i = std::size_t(y * w + x);
        if (S.walkGen[i] != gen) {
            S.walkGen[i] = gen;
            S.walk[i] = world.walkable(Vec2i{x, y}) ? 1 : 0;
        }
        return S.walk[i] != 0;
    };

    const int startIdx = start.y * w + start.x;
    S.seen[std::size_t(startIdx)] = gen;
    S.g[std::size_t(startIdx)] = 0.f;
    S.parent[std::size_t(startIdx)] = -1;
    S.open.push_back({heuristic(start, goal, range), 0.f, startIdx});

    int expansions = 0;
    int found = -1;
    while (!S.open.empty()) {
        std::pop_heap(S.open.begin(), S.open.end(), OpenCmp{});
        const OpenEntry cur = S.open.back();
        S.open.pop_back();
        const std::size_t ci = std::size_t(cur.idx);
        if (S.closed[ci] == gen || cur.g > S.g[ci]) continue;  // stale duplicate
        S.closed[ci] = gen;

        const int cx = cur.idx % w, cy = cur.idx / w;
        const Vec2i cp{cx, cy};
        if (cur.idx != startIdx && chebyshev(cp, goal) <= range) {  // expanded nodes are always walkable
            found = cur.idx;
            break;
        }
        if (++expansions > maxExpansions) break;

        // Orthogonal neighbours first; diagonals only when both adjacent orthogonals are open (no corner cutting).
        bool orthOpen[4];
        for (int d = 0; d < 4; ++d) orthOpen[d] = walkableCached(cx + kDirs8[std::size_t(d)].x, cy + kDirs8[std::size_t(d)].y);
        // kDirs8: 0:(1,0) 1:(-1,0) 2:(0,1) 3:(0,-1) 4:(1,1) 5:(1,-1) 6:(-1,1) 7:(-1,-1)
        for (int d = 0; d < 8; ++d) {
            const Vec2i dir = kDirs8[std::size_t(d)];
            const int nx = cx + dir.x, ny = cy + dir.y;
            float step = 1.f;
            if (d < 4) {
                if (!orthOpen[d]) continue;
            } else {
                const bool xOpen = orthOpen[dir.x > 0 ? 0 : 1];
                const bool yOpen = orthOpen[dir.y > 0 ? 2 : 3];
                if (!xOpen || !yOpen || !walkableCached(nx, ny)) continue;
                step = kDiagonalStep;
            }
            const std::size_t ni = std::size_t(ny * w + nx);
            if (S.closed[ni] == gen) continue;
            const Vec2i np{nx, ny};
            const float ng = cur.g + step * world.moveCost(np);
            if (S.seen[ni] == gen && ng >= S.g[ni]) continue;
            S.seen[ni] = gen;
            S.g[ni] = ng;
            S.parent[ni] = cur.idx;
            S.open.push_back({ng + heuristic(np, goal, range), ng, int(ni)});
            std::push_heap(S.open.begin(), S.open.end(), OpenCmp{});
        }
    }

    if (found < 0) return path;
    for (int i = found; i != startIdx && i >= 0; i = S.parent[std::size_t(i)]) path.push_back(Vec2i{i % w, i / w});
    std::reverse(path.begin(), path.end());
    return path;
}

}  // namespace heur
