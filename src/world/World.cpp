// Heuristica — world implementation: terrain generation, resources, climate, structures, spatial queries.
#include "world/World.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <queue>
#include <utility>

#include "core/Config.h"

namespace heur {

namespace {

// ===========================================================================
// Tunables (world module)
// ===========================================================================

// ---- Terrain generation ----------------------------------------------------
constexpr float kElevationFreq = 1.f / 34.f;  // base fBm frequency (features ~34 tiles across)
constexpr float kMoistureFreq = 1.f / 24.f;
constexpr float kFertilityNoiseFreq = 1.f / 9.f;
constexpr int kElevationOctaves = 5;
constexpr int kMoistureOctaves = 4;
constexpr float kDeepWaterFrac = 0.09f;    // elevation quantile below which water is deep
constexpr float kWaterFrac = 0.155f;       // elevation quantile below which tiles are water
constexpr float kBeachElevFrac = 0.225f;   // low land (below this quantile) touching water becomes sand
constexpr float kDryBeachElevFrac = 0.185f; // ...and very low land within 2 tiles of water
constexpr float kRuggedFreq = 1.f / 5.f;   // detail noise that roughens hill/mountain outlines
constexpr float kRuggedAmp = 0.05f;
constexpr float kMountainFrac = 0.03f;     // top elevation quantile -> mountains
constexpr float kHillFrac = 0.085f;        // next quantile band -> hills
constexpr float kForestFrac = 0.21f;       // share of the whole map turned into forest (wettest lowland)
constexpr int kMinIslandTiles = 30;        // land components smaller than this (touching water) are drowned
constexpr int kMaxConnectPasses = 64;

// ---- Resource placement ------------------------------------------------------
constexpr float kTreeChanceForest = 0.86f;
constexpr float kTreeChanceGrass = 0.012f;    // lone trees in grassland
constexpr int kTreeMin = 4, kTreeMax = 8;
constexpr float kRockChanceHills = 0.42f;
constexpr float kRockChanceNearMountain = 0.70f;  // hills next to a mountain
constexpr float kRockChanceMountainFoot = 0.05f;  // grass/sand at the foot of a mountain
constexpr int kRockMin = 6, kRockMax = 12;
// Berries & fish are placed by exact count (not per-tile dice) so food capacity is stable across seeds.
constexpr float kBerryShare = 0.05f;          // bushes per candidate tile (grass + forest-edge tiles)
constexpr float kBerryEdgeWeight = 2.5f;      // forest edges are this much likelier than open grass
constexpr int kBerryMin = 4, kBerryMax = 6;
constexpr float kHerbChance = 0.010f;         // of grass/forest tiles
constexpr int kHerbMin = 2, kHerbMax = 4;
constexpr float kGrainChance = 0.020f;        // of grass tiles
constexpr int kGrainMin = 3, kGrainMax = 5;
constexpr float kFishShare = 0.20f;           // shoals per shallow-water tile...
constexpr float kFishMaxCoastalShare = 0.5f;  // ...placed only on shallow tiles next to walkable land
constexpr int kFishMin = 3, kFishMax = 5;

// ---- Regrowth (units per day, before season & fertility multipliers) ---------
// Map-wide targets for 120x60: spring/summer wild food ≈ 300-400/day, autumn ≈ half,
// winter ≈ fish only (40-60/day). See verification harness for measured numbers.
constexpr int kRegrowInterval = 10;            // resources are processed every N ticks (scaled by dt)
constexpr float kBerryRegrowPerDay = 1.45f;
constexpr float kFishRegrowPerDay = 0.66f;
constexpr float kTreeRegrowPerDay = 0.12f;
constexpr float kRockRegrowPerDay = 0.006f;
constexpr float kHerbRegrowPerDay = 0.30f;
constexpr float kGrainRegrowPerDay = 0.40f;
constexpr float kBerryWinterWitherPerDay = 0.25f;  // fraction of standing berries lost per winter day
//                                      Spring Summer Autumn Winter
constexpr float kBerrySeason[4] = {1.00f, 1.05f, 0.42f, 0.00f};
constexpr float kFishSeason[4] = {1.10f, 1.10f, 1.00f, 0.80f};
constexpr float kTreeSeason[4] = {1.00f, 1.00f, 0.60f, 0.00f};
constexpr float kRockSeason[4] = {1.00f, 1.00f, 1.00f, 1.00f};
constexpr float kHerbSeason[4] = {1.00f, 1.00f, 0.50f, 0.00f};
constexpr float kGrainSeason[4] = {1.00f, 1.00f, 0.00f, 0.00f};

// ---- Farms -----------------------------------------------------------------
constexpr float kFarmDaysToMature = 3.0f;      // well-tended, average fertility, spring
constexpr float kFarmSeason[4] = {1.00f, 1.15f, 0.80f, 0.15f};
constexpr float kFarmUntendedFactor = 0.25f;   // growth multiplier at tended == 0
constexpr float kTendedDecayDays = 1.5f;       // tended goes 1 -> 0 over this many days
constexpr float kMinFarmFertility = 0.35f;

// ---- Structures ------------------------------------------------------------
constexpr float kDecayDays = 45.f;             // complete structure: full condition -> collapse
constexpr float kSlowDecayDays = 150.f;        // Monument / Wall
constexpr float kSiteDecayDays = 20.f;         // abandoned construction site
constexpr float kRepairPerLabour = 0.01f;      // condition restored per labour unit on complete structures
constexpr float kCampfireInitialFuel = 300.f;  // ticks of burn when a campfire is completed
constexpr int kBuildSiteCandidates = 6;

// ---- Climate ---------------------------------------------------------------
//                                    Spring Summer Autumn Winter
constexpr float kSeasonTemp[4] = {0.55f, 0.85f, 0.45f, 0.12f};
constexpr float kSeasonBlend = 0.35f;          // final share of a season spent blending into the next
constexpr float kDaySwing = 0.15f;             // +- around the seasonal base
constexpr float kWarmestTimeOfDay = 0.65f;     // coldest is half a day earlier (0.15, pre-dawn)

// ---- Movement ----------------------------------------------------------------
constexpr float kImpassableCost = 8.f;

constexpr float kPi = 3.14159265358979f;

// ===========================================================================
// Deterministic value noise + fBm
// ===========================================================================
std::uint32_t mix32(std::uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

float lattice(int x, int y, std::uint32_t seed) {
    const std::uint32_t h =
        mix32(std::uint32_t(x) * 0x8da6b343u ^ std::uint32_t(y) * 0xd8163841u ^ seed * 0xcb1ab31fu);
    return float(h >> 8) * (1.f / 16777216.f);
}

float fade(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }

float valueNoise(float x, float y, std::uint32_t seed) {
    const float fx0 = std::floor(x), fy0 = std::floor(y);
    const int x0 = int(fx0), y0 = int(fy0);
    const float sx = fade(x - fx0), sy = fade(y - fy0);
    const float v00 = lattice(x0, y0, seed), v10 = lattice(x0 + 1, y0, seed);
    const float v01 = lattice(x0, y0 + 1, seed), v11 = lattice(x0 + 1, y0 + 1, seed);
    return lerpf(lerpf(v00, v10, sx), lerpf(v01, v11, sx), sy);
}

float fbm(float x, float y, std::uint32_t seed, int octaves) {
    float sum = 0.f, amp = 1.f, norm = 0.f, freq = 1.f;
    for (int o = 0; o < octaves; ++o) {
        // per-octave offsets break up lattice alignment between octaves
        sum += amp * valueNoise(x * freq + 31.7f * float(o), y * freq - 17.3f * float(o),
                                seed + std::uint32_t(o) * 0x9e3779b9u);
        norm += amp;
        amp *= 0.5f;
        freq *= 2.03f;
    }
    return sum / norm;
}

float quantile(std::vector<float> v, float q) {
    if (v.empty()) return 0.f;
    const std::size_t k = std::min(v.size() - 1, std::size_t(std::max(0.f, q) * float(v.size())));
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(k), v.end());
    return v[k];
}

bool isWater(Terrain t) { return t == Terrain::DeepWater || t == Terrain::ShallowWater; }
bool isWalkableTerrain(Terrain t) {
    return t == Terrain::Sand || t == Terrain::Grass || t == Terrain::Forest || t == Terrain::Hills;
}

float fertilityFactor(float fertility) { return 0.5f + fertility; }

// ===========================================================================
// Ring iteration (chebyshev rings, clipped to the map)
// ===========================================================================
template <class F>
void forRing(Vec2i c, int r, int w, int h, F&& f) {
    if (r == 0) {
        if (c.x >= 0 && c.y >= 0 && c.x < w && c.y < h) f(c);
        return;
    }
    const int x0 = c.x - r, x1 = c.x + r, y0 = c.y - r, y1 = c.y + r;
    const int xa = std::max(x0, 0), xb = std::min(x1, w - 1);
    for (int x = xa; x <= xb; ++x) {
        if (y0 >= 0 && y0 < h) f(Vec2i{x, y0});
        if (y1 >= 0 && y1 < h) f(Vec2i{x, y1});
    }
    const int ya = std::max(y0 + 1, 0), yb = std::min(y1 - 1, h - 1);
    for (int y = ya; y <= yb; ++y) {
        if (x0 >= 0 && x0 < w) f(Vec2i{x0, y});
        if (x1 >= 0 && x1 < w) f(Vec2i{x1, y});
    }
}

// True when a ring of radius r around c lies entirely outside the map (so do all larger rings).
bool ringOutside(Vec2i c, int r, int w, int h) {
    return c.x - r < 0 && c.x + r >= w && c.y - r < 0 && c.y + r >= h;
}

int dist2(Vec2i a, Vec2i b) {
    const int dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// Nearest tile (chebyshev rings; euclidean tie-break inside a ring, then scan order) satisfying pred.
template <class Pred>
std::optional<Vec2i> ringSearch(Vec2i from, int maxRadius, int w, int h, Pred&& pred) {
    for (int r = 0; r <= maxRadius; ++r) {
        if (r > 0 && ringOutside(from, r, w, h)) break;
        Vec2i best{};
        int bestD = INT_MAX;
        forRing(from, r, w, h, [&](Vec2i p) {
            const int d = dist2(p, from);
            if (d < bestD && pred(p)) {
                bestD = d;
                best = p;
            }
        });
        if (bestD != INT_MAX) return best;
    }
    return std::nullopt;
}

const Blueprint kBlueprints[kStructureTypeCount] = {
    //  type                    name        glyph  {Food,Wood,Stone,Seeds,Herbs,Tools}  work  blocks  cap
    {StructureType::Campfire, "campfire", '&', ItemCounts{0, 3, 0, 0, 0, 0}, 30.f, false, 0},
    {StructureType::Hut, "hut", 'H', ItemCounts{0, 10, 2, 0, 0, 0}, 160.f, false, 12},
    {StructureType::Storage, "storage", 'S', ItemCounts{0, 8, 4, 0, 0, 0}, 140.f, false, 80},
    {StructureType::Farm, "farm", '%', ItemCounts{0, 2, 0, 0, 0, 0}, 60.f, false, 0},
    {StructureType::Wall, "wall", '#', ItemCounts{0, 0, 3, 0, 0, 0}, 50.f, true, 0},
    {StructureType::Workshop, "workshop", 'W', ItemCounts{0, 8, 6, 0, 0, 0}, 200.f, false, 0},
    {StructureType::Monument, "monument", 'M', ItemCounts{0, 4, 14, 0, 0, 0}, 400.f, false, 0},
};

}  // namespace

// ===========================================================================
// Free functions
// ===========================================================================
Item resourceYield(ResourceKind k) {
    switch (k) {
        case ResourceKind::BerryBush: return Item::Food;
        case ResourceKind::Tree: return Item::Wood;
        case ResourceKind::Rock: return Item::Stone;
        case ResourceKind::FishShoal: return Item::Food;
        case ResourceKind::HerbPatch: return Item::Herbs;
        case ResourceKind::WildGrain: return Item::Seeds;
        default: return Item::Food;
    }
}

const char* terrainName(Terrain t) {
    switch (t) {
        case Terrain::DeepWater: return "deep water";
        case Terrain::ShallowWater: return "shallow water";
        case Terrain::Sand: return "sand";
        case Terrain::Grass: return "grass";
        case Terrain::Forest: return "forest";
        case Terrain::Hills: return "hills";
        case Terrain::Mountain: return "mountain";
        default: return "?";
    }
}

const char* resourceName(ResourceKind k) {
    switch (k) {
        case ResourceKind::None: return "nothing";
        case ResourceKind::BerryBush: return "berry bush";
        case ResourceKind::Tree: return "tree";
        case ResourceKind::Rock: return "rock";
        case ResourceKind::FishShoal: return "fish shoal";
        case ResourceKind::HerbPatch: return "herb patch";
        case ResourceKind::WildGrain: return "wild grain";
        default: return "?";
    }
}

const char* seasonName(Season s) {
    switch (s) {
        case Season::Spring: return "spring";
        case Season::Summer: return "summer";
        case Season::Autumn: return "autumn";
        case Season::Winter: return "winter";
        default: return "?";
    }
}

const Blueprint& blueprint(StructureType t) {
    const int i = static_cast<int>(t);
    return kBlueprints[(i >= 0 && i < kStructureTypeCount) ? i : 0];
}

int Structure::materialsMissing(Item i) const {
    const std::size_t k = std::size_t(i);
    return std::max(0, bp().cost[k] - delivered[k]);
}

bool Structure::materialsComplete() const {
    for (int i = 0; i < kItemCount; ++i)
        if (materialsMissing(Item(i)) > 0) return false;
    return true;
}

// ===========================================================================
// World: construction & generation
// ===========================================================================
World::World(int width, int height)
    : w_(std::max(1, width)), h_(std::max(1, height)), tiles_(std::size_t(w_) * std::size_t(h_)) {}

void World::generate(Random& rng) {
    const int N = w_ * h_;
    tiles_.assign(std::size_t(w_) * std::size_t(h_), Tile{});
    structures_.clear();
    nextStructureId_ = 1;
    lastUpdateTick_ = now_;
    lastRegrowTick_ = now_;

    const auto idx = [this](int x, int y) { return std::size_t(y * w_ + x); };
    const std::uint32_t elevSeed = std::uint32_t(rng.next());
    const std::uint32_t moistSeed = std::uint32_t(rng.next());
    const std::uint32_t fertSeed = std::uint32_t(rng.next());
    const std::uint32_t ruggedSeed = std::uint32_t(rng.next());
    const float ox = rng.range(0.f, 512.f), oy = rng.range(0.f, 512.f);

    // ---- 1. noise fields ----
    const std::size_t NN = std::size_t(N);
    std::vector<float> elev(NN), moist(NN);
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x) {
            const float fx = float(x) + ox, fy = float(y) + oy;
            elev[idx(x, y)] = fbm(fx * kElevationFreq, fy * kElevationFreq, elevSeed, kElevationOctaves);
            moist[idx(x, y)] = fbm(fx * kMoistureFreq, fy * kMoistureFreq, moistSeed, kMoistureOctaves);
        }

    // ---- 2. elevation bands (quantiles => stable terrain mix for any seed) ----
    const float qDeep = quantile(elev, kDeepWaterFrac);
    const float qWater = quantile(elev, kWaterFrac);
    const float qBeach = quantile(elev, kBeachElevFrac);
    const float qDryBeach = quantile(elev, kDryBeachElevFrac);
    std::vector<float> high(NN);  // elevation + detail noise: ragged hill/mountain outlines
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x)
            high[idx(x, y)] = elev[idx(x, y)] +
                              kRuggedAmp * (fbm(float(x) * kRuggedFreq, float(y) * kRuggedFreq, ruggedSeed, 2) - 0.5f);
    const float qHill = quantile(high, 1.f - kMountainFrac - kHillFrac);
    const float qMountain = quantile(high, 1.f - kMountainFrac);
    for (int i = 0; i < N; ++i) {
        const float e = elev[std::size_t(i)];
        const float eh = high[std::size_t(i)];
        Terrain t = Terrain::Grass;
        if (e < qDeep) t = Terrain::DeepWater;
        else if (e < qWater) t = Terrain::ShallowWater;
        else if (eh >= qMountain) t = Terrain::Mountain;
        else if (eh >= qHill) t = Terrain::Hills;
        tiles_[std::size_t(i)].terrain = t;
    }

    const auto waterWithin = [&](int x, int y, int r) {
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= w_ || ny >= h_) continue;
                if (isWater(tiles_[idx(nx, ny)].terrain)) return true;
            }
        return false;
    };

    // ---- 3. beaches ----
    {
        std::vector<std::size_t> sand;
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x) {
                const std::size_t i = idx(x, y);
                if (tiles_[i].terrain != Terrain::Grass) continue;
                if ((elev[i] < qBeach && waterWithin(x, y, 1)) || (elev[i] < qDryBeach && waterWithin(x, y, 2)))
                    sand.push_back(i);
            }
        for (std::size_t i : sand) tiles_[i].terrain = Terrain::Sand;
    }

    // ---- 4. forests: the wettest share of the remaining lowland ----
    {
        std::vector<std::pair<float, std::size_t>> cand;
        for (int i = 0; i < N; ++i)
            if (tiles_[std::size_t(i)].terrain == Terrain::Grass)
                cand.push_back({moist[std::size_t(i)] + rng.range(-0.015f, 0.015f), std::size_t(i)});
        const std::size_t want = std::min(cand.size(), std::size_t(kForestFrac * float(N) + 0.5f));
        std::sort(cand.begin(), cand.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second < b.second;
        });
        for (std::size_t k = 0; k < want; ++k) tiles_[cand[k].second].terrain = Terrain::Forest;
    }

    // ---- 5. one connected walkable landmass ----
    ensureConnectedLand();

    // Shallow shore ring: no deep water touches land.
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x) {
            Tile& t = tiles_[idx(x, y)];
            if (t.terrain != Terrain::DeepWater) continue;
            for (const Vec2i d : kDirs8) {
                const int nx = x + d.x, ny = y + d.y;
                if (nx < 0 || ny < 0 || nx >= w_ || ny >= h_) continue;
                if (!isWater(tiles_[idx(nx, ny)].terrain)) {
                    t.terrain = Terrain::ShallowWater;
                    break;
                }
            }
        }

    // ---- 6. fertility from moisture rank (+ water proximity, + local noise) ----
    {
        std::vector<std::pair<float, std::size_t>> land;
        for (int i = 0; i < N; ++i)
            if (!isWater(tiles_[std::size_t(i)].terrain)) land.push_back({moist[std::size_t(i)], std::size_t(i)});
        std::sort(land.begin(), land.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first < b.first : a.second < b.second;
        });
        const float denom = land.size() > 1 ? float(land.size() - 1) : 1.f;
        for (std::size_t k = 0; k < land.size(); ++k) {
            const std::size_t i = land[k].second;
            const int x = int(i) % w_, y = int(i) / w_;
            const float rank = float(k) / denom;
            const float n = fbm(float(x) * kFertilityNoiseFreq, float(y) * kFertilityNoiseFreq, fertSeed, 2) - 0.5f;
            float f = 0.18f + 0.72f * rank + 0.22f * n;
            if (waterWithin(x, y, 2)) f += 0.08f;
            switch (tiles_[i].terrain) {
                case Terrain::Sand: f *= 0.35f; break;
                case Terrain::Hills: f *= 0.70f; break;
                case Terrain::Mountain: f = 0.05f; break;
                default: break;
            }
            tiles_[i].fertility = clampf(f, 0.05f, 1.f);
        }
        for (Tile& t : tiles_)
            if (isWater(t.terrain)) t.fertility = 0.6f;
    }

    // ---- 7. natural resources ----
    const auto terrainAt = [&](int x, int y) {
        return (x < 0 || y < 0 || x >= w_ || y >= h_) ? Terrain::DeepWater : tiles_[idx(x, y)].terrain;
    };
    const auto touches = [&](int x, int y, Terrain t) {
        for (const Vec2i d : kDirs8)
            if (terrainAt(x + d.x, y + d.y) == t) return true;
        return false;
    };
    const auto place = [](Tile& t, ResourceKind k, int amount) {
        t.resource = k;
        t.maxAmount = float(amount);
        t.amount = float(amount);
    };
    // Fish: exactly kFishShare x (shallow tiles), on shallow tiles an agent can stand next to.
    {
        std::vector<std::size_t> coastal;
        int shallow = 0;
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x) {
                if (tiles_[idx(x, y)].terrain != Terrain::ShallowWater) continue;
                ++shallow;
                for (const Vec2i d : kDirs8)
                    if (isWalkableTerrain(terrainAt(x + d.x, y + d.y))) {
                        coastal.push_back(idx(x, y));
                        break;
                    }
            }
        const std::size_t want = std::min(std::size_t(kFishShare * float(shallow) + 0.5f),
                                          std::size_t(kFishMaxCoastalShare * float(coastal.size()) + 0.5f));
        rng.shuffle(coastal.begin(), coastal.end());
        for (std::size_t k = 0; k < want; ++k) place(tiles_[coastal[k]], ResourceKind::FishShoal, rng.rangeInt(kFishMin, kFishMax));
    }
    // Berries: exactly kBerryShare x (grass + forest-edge tiles), weighted toward forest edges
    // (Efraimidis-Spirakis weighted sampling without replacement).
    {
        std::vector<std::pair<float, std::size_t>> cand;
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x) {
                const Terrain t = tiles_[idx(x, y)].terrain;
                float wgt = 0.f;
                if (t == Terrain::Grass) wgt = touches(x, y, Terrain::Forest) ? kBerryEdgeWeight : 1.f;
                else if (t == Terrain::Forest && touches(x, y, Terrain::Grass)) wgt = kBerryEdgeWeight;
                if (wgt <= 0.f) continue;
                cand.push_back({std::pow(rng.uniform(), 1.f / wgt), idx(x, y)});
            }
        const std::size_t want = std::min(cand.size(), std::size_t(kBerryShare * float(cand.size()) + 0.5f));
        std::sort(cand.begin(), cand.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second < b.second;
        });
        for (std::size_t k = 0; k < want; ++k)
            place(tiles_[cand[k].second], ResourceKind::BerryBush, rng.rangeInt(kBerryMin, kBerryMax));
    }
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x) {
            Tile& t = tiles_[idx(x, y)];
            if (t.resource != ResourceKind::None) continue;  // fish / berries already placed
            switch (t.terrain) {
                case Terrain::Hills: {
                    const float p = touches(x, y, Terrain::Mountain) ? kRockChanceNearMountain : kRockChanceHills;
                    if (rng.chance(p)) place(t, ResourceKind::Rock, rng.rangeInt(kRockMin, kRockMax));
                    break;
                }
                case Terrain::Forest: {
                    if (rng.chance(kHerbChance))
                        place(t, ResourceKind::HerbPatch, rng.rangeInt(kHerbMin, kHerbMax));
                    else if (rng.chance(kTreeChanceForest))
                        place(t, ResourceKind::Tree, rng.rangeInt(kTreeMin, kTreeMax));
                    break;
                }
                case Terrain::Grass: {
                    if (rng.chance(kGrainChance))
                        place(t, ResourceKind::WildGrain, rng.rangeInt(kGrainMin, kGrainMax));
                    else if (rng.chance(kHerbChance))
                        place(t, ResourceKind::HerbPatch, rng.rangeInt(kHerbMin, kHerbMax));
                    else if (touches(x, y, Terrain::Mountain) && rng.chance(kRockChanceMountainFoot))
                        place(t, ResourceKind::Rock, rng.rangeInt(kRockMin, kRockMax));
                    else if (rng.chance(kTreeChanceGrass))
                        place(t, ResourceKind::Tree, rng.rangeInt(kTreeMin, kTreeMax));
                    break;
                }
                case Terrain::Sand:
                    if (touches(x, y, Terrain::Mountain) && rng.chance(kRockChanceMountainFoot))
                        place(t, ResourceKind::Rock, rng.rangeInt(kRockMin, kRockMax));
                    break;
                default: break;
            }
        }

    recomputeWildFood();
}

// Makes the walkable land one 4-connected region: tiny islands are drowned, larger separate regions are
// joined to the main one by carving the cheapest ford (water -> sand) or pass (mountain -> hills).
void World::ensureConnectedLand() {
    const int N = w_ * h_;
    const std::size_t NN = std::size_t(N);
    std::vector<int> comp(NN);
    std::vector<int> stack;
    for (int pass = 0; pass < kMaxConnectPasses; ++pass) {
        // label 4-connected walkable components
        std::fill(comp.begin(), comp.end(), -1);
        std::vector<int> sizes;
        for (int s = 0; s < N; ++s) {
            if (comp[std::size_t(s)] != -1 || !isWalkableTerrain(tiles_[std::size_t(s)].terrain)) continue;
            const int id = int(sizes.size());
            sizes.push_back(0);
            stack.assign(1, s);
            comp[std::size_t(s)] = id;
            while (!stack.empty()) {
                const int c = stack.back();
                stack.pop_back();
                ++sizes[std::size_t(id)];
                const int cx = c % w_, cy = c / w_;
                for (const Vec2i d : kDirs4) {
                    const int nx = cx + d.x, ny = cy + d.y;
                    if (nx < 0 || ny < 0 || nx >= w_ || ny >= h_) continue;
                    const int n = ny * w_ + nx;
                    if (comp[std::size_t(n)] != -1 || !isWalkableTerrain(tiles_[std::size_t(n)].terrain)) continue;
                    comp[std::size_t(n)] = id;
                    stack.push_back(n);
                }
            }
        }
        if (sizes.size() <= 1) return;
        const int mainId = int(std::max_element(sizes.begin(), sizes.end()) - sizes.begin());

        // Drown every tiny island that touches water.
        std::vector<char> touchesWater(sizes.size(), 0);
        for (int i = 0; i < N; ++i) {
            const int id = comp[std::size_t(i)];
            if (id < 0 || touchesWater[std::size_t(id)]) continue;
            const int cx = i % w_, cy = i / w_;
            for (const Vec2i d : kDirs8) {
                const int nx = cx + d.x, ny = cy + d.y;
                if (nx < 0 || ny < 0 || nx >= w_ || ny >= h_) continue;
                if (isWater(tiles_[std::size_t(ny * w_ + nx)].terrain)) {
                    touchesWater[std::size_t(id)] = 1;
                    break;
                }
            }
        }
        bool drowned = false;
        for (int i = 0; i < N; ++i) {
            const int id = comp[std::size_t(i)];
            if (id < 0 || id == mainId) continue;
            if (sizes[std::size_t(id)] < kMinIslandTiles && touchesWater[std::size_t(id)]) {
                tiles_[std::size_t(i)].terrain = Terrain::ShallowWater;
                drowned = true;
            }
        }
        if (drowned) continue;  // relabel

        // Join the largest secondary region to the main one along the cheapest carve path.
        int target = -1;
        for (int id = 0; id < int(sizes.size()); ++id)
            if (id != mainId && (target < 0 || sizes[std::size_t(id)] > sizes[std::size_t(target)])) target = id;
        const auto carveCost = [&](Terrain t) {
            switch (t) {
                case Terrain::ShallowWater: return 2.0f;
                case Terrain::Mountain: return 2.5f;
                case Terrain::DeepWater: return 4.0f;
                default: return 0.05f;  // walkable: nearly free, but prefer short
            }
        };
        std::vector<float> dist(NN, 1e30f);
        std::vector<int> parent(NN, -1);
        using QE = std::pair<float, int>;
        std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
        for (int i = 0; i < N; ++i)
            if (comp[std::size_t(i)] == target) {
                dist[std::size_t(i)] = 0.f;
                pq.push({0.f, i});
            }
        int reached = -1;
        while (!pq.empty()) {
            const auto [dc, c] = pq.top();
            pq.pop();
            if (dc > dist[std::size_t(c)]) continue;
            if (comp[std::size_t(c)] == mainId) {
                reached = c;
                break;
            }
            const int cx = c % w_, cy = c / w_;
            for (const Vec2i d : kDirs4) {
                const int nx = cx + d.x, ny = cy + d.y;
                if (nx < 0 || ny < 0 || nx >= w_ || ny >= h_) continue;
                const int n = ny * w_ + nx;
                const float nd = dc + carveCost(tiles_[std::size_t(n)].terrain);
                if (nd < dist[std::size_t(n)]) {
                    dist[std::size_t(n)] = nd;
                    parent[std::size_t(n)] = c;
                    pq.push({nd, n});
                }
            }
        }
        if (reached < 0) return;  // cannot happen on a finite grid, but be safe
        for (int c = reached; c >= 0; c = parent[std::size_t(c)]) {
            Tile& t = tiles_[std::size_t(c)];
            if (t.terrain == Terrain::Mountain) t.terrain = Terrain::Hills;
            else if (isWater(t.terrain)) t.terrain = Terrain::Sand;
        }
    }
}

void World::recomputeWildFood() {
    float food = 0.f;
    for (const Tile& t : tiles_)
        if (t.resource == ResourceKind::BerryBush || t.resource == ResourceKind::FishShoal) food += t.amount;
    wildFood_ = food;
}

// ===========================================================================
// Movement
// ===========================================================================
bool World::walkable(Vec2i p) const {
    if (!inBounds(p)) return false;
    const Tile& t = at(p);
    if (!isWalkableTerrain(t.terrain)) return false;
    if (t.structure != kNoStructure) {
        const auto it = structures_.find(t.structure);
        if (it != structures_.end() && it->second.complete && it->second.bp().blocksMovement) return false;
    }
    return true;
}

float World::moveCost(Vec2i p) const {
    if (!inBounds(p)) return kImpassableCost;
    switch (at(p).terrain) {
        case Terrain::Grass: return 1.0f;
        case Terrain::Sand: return 1.2f;
        case Terrain::Forest: return 1.4f;
        case Terrain::Hills: return 1.8f;
        case Terrain::ShallowWater: return 4.0f;
        default: return kImpassableCost;
    }
}

// ===========================================================================
// Time & climate
// ===========================================================================
int World::day() const { return int(now_ / Tick(cfg::kTicksPerDay)); }

float World::timeOfDay() const { return float(now_ % Tick(cfg::kTicksPerDay)) / float(cfg::kTicksPerDay); }

bool World::isNight() const {
    const float t = timeOfDay();
    return t >= cfg::kNightStart || t < cfg::kNightEnd;
}

Season World::season() const { return Season((day() / cfg::kDaysPerSeason) % cfg::kSeasonsPerYear); }

float World::seasonProgress() const {
    return (float(day() % cfg::kDaysPerSeason) + timeOfDay()) / float(cfg::kDaysPerSeason);
}

float World::temperature() const {
    // A season holds its base temperature, then blends (smoothstep) into the next one over its final
    // kSeasonBlend share, so every season starts at its own base (spring day 0 is mild, not wintry).
    const int s0 = int(season());
    const int s1 = (s0 + 1) % cfg::kSeasonsPerYear;
    const float frac = clamp01((seasonProgress() - (1.f - kSeasonBlend)) / kSeasonBlend);
    const float k = frac * frac * (3.f - 2.f * frac);
    const float base = lerpf(kSeasonTemp[s0], kSeasonTemp[s1], k);
    const float swing = kDaySwing * std::cos(2.f * kPi * (timeOfDay() - kWarmestTimeOfDay));
    return clamp01(base + swing);
}

void World::update(Tick now, Random& rng) {
    now_ = now;
    if (lastUpdateTick_ > now_) lastUpdateTick_ = now_;
    if (lastRegrowTick_ > now_) lastRegrowTick_ = now_;

    const Tick dtStruct = now_ - lastUpdateTick_;
    if (dtStruct > 0) {
        updateStructures(int(std::min<Tick>(dtStruct, Tick(cfg::kTicksPerDay))));
        lastUpdateTick_ = now_;
    }
    const Tick dtRegrow = now_ - lastRegrowTick_;
    if (dtRegrow >= Tick(kRegrowInterval)) {
        regrowResources(int(std::min<Tick>(dtRegrow, Tick(cfg::kTicksPerDay) * 4)));
        lastRegrowTick_ = now_;
    }
}

void World::regrowResources(int dtTicks) {
    const int si = int(season());
    const float days = float(dtTicks) / float(cfg::kTicksPerDay);
    const float wither = (Season(si) == Season::Winter) ? std::min(1.f, kBerryWinterWitherPerDay * days) : 0.f;
    float food = 0.f;
    for (Tile& t : tiles_) {
        if (t.resource == ResourceKind::None) continue;
        float rate = 0.f;  // units per day
        switch (t.resource) {
            case ResourceKind::BerryBush:
                rate = kBerryRegrowPerDay * kBerrySeason[si] * fertilityFactor(t.fertility);
                break;
            case ResourceKind::Tree: rate = kTreeRegrowPerDay * kTreeSeason[si] * fertilityFactor(t.fertility); break;
            case ResourceKind::Rock: rate = kRockRegrowPerDay * kRockSeason[si]; break;
            case ResourceKind::FishShoal: rate = kFishRegrowPerDay * kFishSeason[si]; break;
            case ResourceKind::HerbPatch: rate = kHerbRegrowPerDay * kHerbSeason[si] * fertilityFactor(t.fertility); break;
            case ResourceKind::WildGrain:
                rate = kGrainRegrowPerDay * kGrainSeason[si] * fertilityFactor(t.fertility);
                break;
            default: break;
        }
        if (rate > 0.f && t.amount < t.maxAmount) t.amount = std::min(t.maxAmount, t.amount + rate * days);
        if (t.resource == ResourceKind::BerryBush) {
            if (wither > 0.f) t.amount -= t.amount * wither;
            food += t.amount;
        } else if (t.resource == ResourceKind::FishShoal) {
            food += t.amount;
        }
    }
    wildFood_ = food;
}

void World::updateStructures(int dtTicks) {
    const float dt = float(dtTicks);
    const float tpd = float(cfg::kTicksPerDay);
    const int si = int(season());
    const float farmBase = 1.f / (kFarmDaysToMature * tpd);
    std::vector<StructureId> collapsed;
    for (auto& [id, s] : structures_) {
        if (s.type == StructureType::Campfire) {
            s.fuel = std::max(0.f, s.fuel - dt);
            if (s.complete) continue;  // lit or not, a finished fire pit doesn't decay
        }
        if (s.type == StructureType::Farm && s.complete) {
            if (s.planted && s.growth < 1.f) {
                const float fert = inBounds(s.pos) ? at(s.pos).fertility : 0.5f;
                const float g = farmBase * (0.55f + 0.9f * fert) * kFarmSeason[si] *
                                (kFarmUntendedFactor + (1.f - kFarmUntendedFactor) * clamp01(s.tended));
                s.growth = std::min(1.f, s.growth + g * dt);
            }
        }
        // decay
        float decayDays = kDecayDays;
        if (!s.complete) decayDays = kSiteDecayDays;
        else if (s.type == StructureType::Monument || s.type == StructureType::Wall) decayDays = kSlowDecayDays;
        float decay = dt / (decayDays * tpd);
        if (s.type == StructureType::Farm && s.complete) decay *= 1.f - clamp01(s.tended);  // fields in use keep
        if (s.type == StructureType::Farm)
            s.tended = std::max(0.f, s.tended - dt / (kTendedDecayDays * tpd));
        s.condition -= decay;
        if (s.condition <= 0.f) {
            s.condition = 0.f;
            collapsed.push_back(id);
        }
    }
    for (StructureId id : collapsed) destroyStructure(id);
}

// ===========================================================================
// Resources
// ===========================================================================
int World::harvest(Vec2i p, int want) {
    if (want <= 0 || !inBounds(p)) return 0;
    Tile& t = at(p);
    if (t.resource == ResourceKind::None || t.amount < 1.f) return 0;
    const int got = std::min(want, int(std::floor(t.amount)));
    t.amount -= float(got);
    if (t.resource == ResourceKind::BerryBush || t.resource == ResourceKind::FishShoal)
        wildFood_ = std::max(0.f, wildFood_ - float(got));
    return got;
}

float World::wildFoodTotal() const { return wildFood_; }

// ===========================================================================
// Structures
// ===========================================================================
bool World::canBuildAt(Vec2i p, StructureType type) const {
    if (!inBounds(p)) return false;
    const Tile& t = at(p);
    if (t.structure != kNoStructure) return false;
    switch (t.terrain) {
        case Terrain::Grass: break;
        case Terrain::Sand:
        case Terrain::Forest:
        case Terrain::Hills:
            if (type == StructureType::Farm) return false;
            break;
        default: return false;
    }
    if (type == StructureType::Farm && t.fertility < kMinFarmFertility) return false;
    if (t.resource != ResourceKind::None) {
        // Only cleared ground: a felled tree or a quarried-out rock may be built over; living food
        // sources (bushes, grain, herbs) never are.
        const bool clearable = (t.resource == ResourceKind::Tree || t.resource == ResourceKind::Rock) && t.amount < 1.f;
        if (!clearable) return false;
    }
    return true;
}

StructureId World::beginStructure(StructureType type, Vec2i pos, AgentId founder, AgentId owner) {
    if (!canBuildAt(pos, type)) return kNoStructure;
    const StructureId id = nextStructureId_++;
    Structure s;
    s.id = id;
    s.type = type;
    s.pos = pos;
    s.owner = owner;
    s.founder = founder;
    s.condition = 1.f;
    if (founder != kNoAgent) s.contributors.push_back(founder);
    structures_.emplace(id, std::move(s));
    Tile& t = at(pos);
    t.structure = id;
    t.resource = ResourceKind::None;  // clear the stump / quarry
    t.amount = 0.f;
    t.maxAmount = 0.f;
    return id;
}

Structure* World::structure(StructureId id) {
    if (id == kNoStructure) return nullptr;
    const auto it = structures_.find(id);
    return it == structures_.end() ? nullptr : &it->second;
}

const Structure* World::structure(StructureId id) const {
    if (id == kNoStructure) return nullptr;
    const auto it = structures_.find(id);
    return it == structures_.end() ? nullptr : &it->second;
}

Structure* World::structureAt(Vec2i p) {
    if (!inBounds(p)) return nullptr;
    return structure(at(p).structure);
}

const Structure* World::structureAt(Vec2i p) const {
    if (!inBounds(p)) return nullptr;
    return structure(at(p).structure);
}

void World::destroyStructure(StructureId id) {
    const auto it = structures_.find(id);
    if (it == structures_.end()) return;
    const Vec2i p = it->second.pos;
    if (inBounds(p) && at(p).structure == id) at(p).structure = kNoStructure;
    structures_.erase(it);
}

int World::deliverMaterials(Structure& s, Inventory& from) {
    if (s.complete) return 0;
    int moved = 0;
    for (int i = 0; i < kItemCount; ++i) {
        const Item item = Item(i);
        const int n = std::min(s.materialsMissing(item), from.get(item));
        if (n <= 0) continue;
        from.take(item, n);
        s.delivered[std::size_t(i)] += n;
        moved += n;
    }
    if (moved > 0) s.condition = 1.f;  // an active site doesn't rot
    return moved;
}

bool World::applyWork(Structure& s, float labour, AgentId worker) {
    if (labour <= 0.f) return false;
    if (worker != kNoAgent && std::find(s.contributors.begin(), s.contributors.end(), worker) == s.contributors.end())
        s.contributors.push_back(worker);
    if (s.complete) {
        s.condition = std::min(1.f, s.condition + labour * kRepairPerLabour);
        return false;
    }
    const float work = s.bp().work;
    s.progress = std::min(work, s.progress + labour);
    s.condition = 1.f;
    if (s.progress >= work && s.materialsComplete()) {
        s.complete = true;
        s.condition = 1.f;
        if (s.type == StructureType::Campfire) s.fuel = kCampfireInitialFuel;
        if (s.type == StructureType::Farm) {
            s.planted = false;
            s.growth = 0.f;
            s.tended = 0.f;
        }
        return true;
    }
    return false;
}

std::optional<Vec2i> World::findBuildSite(Vec2i near, StructureType type, int maxRadius, Random& rng) const {
    // A site is "preferred" when no other structure touches it (walls next to walls and farms next to farms
    // are fine: those cluster naturally).
    const auto preferred = [&](Vec2i p) {
        for (const Vec2i d : kDirs8) {
            const Vec2i n = p + d;
            if (!inBounds(n)) continue;
            const Structure* o = structureAt(n);
            if (!o) continue;
            if (o->type == type && (type == StructureType::Wall || type == StructureType::Farm)) continue;
            return false;
        }
        return true;
    };
    std::vector<Vec2i> good, fallback;
    for (int r = 0; r <= maxRadius; ++r) {
        if (r > 0 && ringOutside(near, r, w_, h_)) break;
        forRing(near, r, w_, h_, [&](Vec2i p) {
            if (!canBuildAt(p, type)) return;
            if (preferred(p)) good.push_back(p);
            else if (fallback.size() < std::size_t(kBuildSiteCandidates) * 4) fallback.push_back(p);
        });
        if (good.size() >= std::size_t(kBuildSiteCandidates)) break;
    }
    std::vector<Vec2i>& pool = good.empty() ? fallback : good;
    if (pool.empty()) return std::nullopt;
    std::stable_sort(pool.begin(), pool.end(), [&](Vec2i a, Vec2i b) { return dist2(a, near) < dist2(b, near); });
    const int n = std::min(int(pool.size()), kBuildSiteCandidates);
    return pool[std::size_t(rng.rangeInt(0, n - 1))];
}

// ===========================================================================
// Spatial queries
// ===========================================================================
std::optional<Vec2i> World::findNearestResource(Vec2i from, ResourceKind kind, int maxRadius, float minAmount) const {
    return ringSearch(from, maxRadius, w_, h_, [&](Vec2i p) {
        const Tile& t = at(p);
        return t.resource == kind && t.amount >= minAmount;
    });
}

std::optional<Vec2i> World::findNearest(Vec2i from, int maxRadius,
                                        const std::function<bool(Vec2i, const Tile&)>& pred) const {
    if (!pred) return std::nullopt;
    return ringSearch(from, maxRadius, w_, h_, [&](Vec2i p) { return pred(p, at(p)); });
}

std::vector<StructureId> World::structuresNear(Vec2i p, int radius, std::optional<StructureType> type) const {
    std::vector<StructureId> out;
    for (const auto& [id, s] : structures_) {
        if (type && s.type != *type) continue;
        if (chebyshev(s.pos, p) <= radius) out.push_back(id);
    }
    return out;
}

StructureId World::nearestStructure(Vec2i p, int radius, StructureType type,
                                    const std::function<bool(const Structure&)>& pred) const {
    StructureId best = kNoStructure;
    int bestC = INT_MAX, bestE = INT_MAX;
    for (const auto& [id, s] : structures_) {
        if (s.type != type) continue;
        const int c = chebyshev(s.pos, p);
        if (c > radius) continue;
        const int e = dist2(s.pos, p);
        if (c > bestC || (c == bestC && e >= bestE)) continue;
        if (pred && !pred(s)) continue;
        best = id;
        bestC = c;
        bestE = e;
    }
    return best;
}

bool World::isSheltered(Vec2i p) const {
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const Structure* s = structureAt(Vec2i{p.x + dx, p.y + dy});
            if (s && s->type == StructureType::Hut && s->complete) return true;
        }
    return false;
}

// Hot path (called per agent per tick): scan tile ids directly and only touch the structure map for
// occupied tiles.
template <class Pred>
static bool anyStructureWithin(const std::vector<Tile>& tiles, int w, int h, Vec2i p, int r,
                               const std::map<StructureId, Structure>& structures, Pred&& pred) {
    const int x0 = std::max(0, p.x - r), x1 = std::min(w - 1, p.x + r);
    const int y0 = std::max(0, p.y - r), y1 = std::min(h - 1, p.y + r);
    for (int y = y0; y <= y1; ++y) {
        const Tile* row = &tiles[std::size_t(y * w)];
        for (int x = x0; x <= x1; ++x) {
            const StructureId sid = row[x].structure;
            if (sid == kNoStructure) continue;
            const auto it = structures.find(sid);
            if (it != structures.end() && pred(it->second)) return true;
        }
    }
    return false;
}

bool World::nearWarmth(Vec2i p) const {
    return anyStructureWithin(tiles_, w_, h_, p, 3, structures_, [](const Structure& s) { return s.isLit(); });
}

bool World::nearMonument(Vec2i p) const {
    return anyStructureWithin(tiles_, w_, h_, p, 6, structures_, [](const Structure& s) {
        return s.type == StructureType::Monument && s.complete;
    });
}

std::optional<Vec2i> World::randomWalkableNear(Vec2i p, int radius, Random& rng) const {
    radius = std::max(0, radius);
    for (int tries = 0; tries < 20; ++tries) {
        const Vec2i q{p.x + rng.rangeInt(-radius, radius), p.y + rng.rangeInt(-radius, radius)};
        if (walkable(q)) return q;
    }
    return std::nullopt;
}

std::optional<Vec2i> World::nearestWalkable(Vec2i p, int maxRadius) const {
    return ringSearch(p, maxRadius, w_, h_, [&](Vec2i q) { return walkable(q); });
}

}  // namespace heur
