// Heuristica — the physical world: terrain, resources, structures, time & climate.
// FROZEN CONTRACT for the public API. The world module owner may add PRIVATE members/helpers only.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <vector>

#include "core/Random.h"
#include "core/Types.h"

namespace heur {

// ---------------------------------------------------------------------------
// Terrain & natural resources
// ---------------------------------------------------------------------------
enum class Terrain : std::uint8_t { DeepWater, ShallowWater, Sand, Grass, Forest, Hills, Mountain, Count };

// Natural resource sitting on a tile. Harvested from the tile itself or any of its 8 neighbours.
enum class ResourceKind : std::uint8_t {
    None,
    BerryBush,  // -> Food. Regrows fast in spring/summer, slow in autumn, not in winter.
    Tree,       // -> Wood. Regrows slowly all year except winter.
    Rock,       // -> Stone. Very slow regeneration.
    FishShoal,  // -> Food. Lives on ShallowWater; slow regrowth all year (winter fallback food).
    HerbPatch,  // -> Herbs (healing / calming). Regrows spring-autumn.
    WildGrain,  // -> Seeds (plant at farms) and can be eaten in a pinch by the forage logic.
    Count
};

Item resourceYield(ResourceKind k);
const char* terrainName(Terrain t);
const char* resourceName(ResourceKind k);

enum class Season : std::uint8_t { Spring, Summer, Autumn, Winter };
const char* seasonName(Season s);

// ---------------------------------------------------------------------------
// Structures
// ---------------------------------------------------------------------------
enum class StructureType : std::uint8_t {
    Campfire,  // warmth radius when lit (burns fuel; fed with wood). Social hub.
    Hut,       // home: shelter (warmth + better sleep), small private stock.
    Storage,   // big stock of items. Owned (private) or communal (owner == kNoAgent).
    Farm,      // plant seeds -> crop grows (needs tending) -> harvest lots of food.
    Wall,      // blocks movement. Defensive.
    Workshop,  // lets agents craft Tools (Wood+Stone -> Tools) faster/at all.
    Monument,  // pure expression: creative agents build it for pride/fun; boosts nearby mood.
    Count
};
inline constexpr int kStructureTypeCount = static_cast<int>(StructureType::Count);

struct Blueprint {
    StructureType type;
    const char* name;
    char glyph;              // ASCII glyph used by the renderer
    ItemCounts cost;         // materials that must be delivered before completion
    float work;              // labour units required (a builder contributes ~1 unit/tick, scaled by skill)
    bool blocksMovement;
    int capacity;            // stock capacity (Storage/Hut); sleeping slots for Hut
};
const Blueprint& blueprint(StructureType t);

struct Structure {
    StructureId id = kNoStructure;
    StructureType type = StructureType::Campfire;
    Vec2i pos;
    AgentId owner = kNoAgent;    // kNoAgent => communal
    AgentId founder = kNoAgent;  // agent who started construction
    ItemCounts delivered{};      // materials delivered so far
    float progress = 0.f;        // labour done so far (complete when >= blueprint.work and materials delivered)
    bool complete = false;
    float condition = 1.f;       // 0..1, decays slowly; repaired by building work; collapses at 0
    Inventory stock;             // Storage / Hut contents
    bool planted = false;        // Farm: seeds planted
    float growth = 0.f;          // Farm: 0..1 crop growth; harvestable at 1
    float tended = 0.f;          // Farm: 0..1 recent tending; growth rate scales with it, decays daily
    float fuel = 0.f;            // Campfire: ticks of burn remaining
    std::vector<AgentId> contributors;

    const Blueprint& bp() const { return blueprint(type); }
    bool isCommunal() const { return owner == kNoAgent; }
    int materialsMissing(Item i) const;   // cost - delivered (>= 0)
    bool materialsComplete() const;
    bool isLit() const { return type == StructureType::Campfire && complete && fuel > 0.f; }
    bool harvestable() const { return type == StructureType::Farm && complete && planted && growth >= 1.f; }
    int stockFree() const { return std::max(0, bp().capacity - stock.total()); }
};

// ---------------------------------------------------------------------------
// Tiles
// ---------------------------------------------------------------------------
struct Tile {
    Terrain terrain = Terrain::Grass;
    ResourceKind resource = ResourceKind::None;
    float amount = 0.f;      // harvestable units currently available
    float maxAmount = 0.f;   // regrowth cap
    float fertility = 0.5f;  // 0..1: regrowth speed and farm yield
    StructureId structure = kNoStructure;
};

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------
class World {
public:
    World(int width, int height);

    // Procedural generation (value-noise / fBm). Must produce: water bodies with shallow shores,
    // grassland, forests (trees), hills with rocks, a few mountains, berry bushes near grass/forest edges,
    // herb patches, wild grain in grassland, fish shoals in shallow water. Deterministic for a given rng.
    void generate(Random& rng);

    int width() const { return w_; }
    int height() const { return h_; }
    bool inBounds(Vec2i p) const { return p.x >= 0 && p.y >= 0 && p.x < w_ && p.y < h_; }
    Tile& at(Vec2i p) { return tiles_[std::size_t(p.y * w_ + p.x)]; }
    const Tile& at(Vec2i p) const { return tiles_[std::size_t(p.y * w_ + p.x)]; }

    // Passable for agents: in bounds, not water/mountain, and not occupied by a blocking (complete) structure.
    bool walkable(Vec2i p) const;
    // Movement cost multiplier for A* (>= 1): grass 1, sand 1.2, forest 1.4, hills 1.8 ...
    float moveCost(Vec2i p) const;

    // ---- Time & climate ----------------------------------------------------
    // Called once per tick by Simulation. Handles resource regrowth (season & fertility dependent),
    // farm growth, campfire fuel burn, structure decay/collapse.
    void update(Tick now, Random& rng);
    Tick now() const { return now_; }
    int day() const;                // days since start
    float timeOfDay() const;        // 0..1 (0 = midnight, 0.5 = noon)
    bool isNight() const;
    Season season() const;
    float seasonProgress() const;   // 0..1 within the current season
    // 0 = freezing .. 1 = hot. Winter nights ~0.05, summer noon ~0.95.
    float temperature() const;

    // ---- Resources ----------------------------------------------------------
    // Removes up to `want` whole units from the resource on tile p. Returns units removed.
    int harvest(Vec2i p, int want);

    // ---- Structures -----------------------------------------------------------
    // Places a construction site (incomplete). Returns kNoStructure if canBuildAt is false.
    StructureId beginStructure(StructureType type, Vec2i pos, AgentId founder, AgentId owner);
    Structure* structure(StructureId id);
    const Structure* structure(StructureId id) const;
    Structure* structureAt(Vec2i p);
    const Structure* structureAt(Vec2i p) const;
    void destroyStructure(StructureId id);
    // Deliver materials from an inventory into a construction site. Returns number of units moved.
    int deliverMaterials(Structure& s, Inventory& from);
    // Apply labour; completes the structure when work + materials are satisfied. Returns true if it just completed.
    bool applyWork(Structure& s, float labour, AgentId worker);
    const std::map<StructureId, Structure>& structures() const { return structures_; }
    std::map<StructureId, Structure>& structures() { return structures_; }

    // Tile is free (no structure, no resource for non-farm types), walkable terrain, in bounds.
    bool canBuildAt(Vec2i p, StructureType type) const;
    // Random suitable site within maxRadius of `near` (prefers closer). nullopt if none.
    std::optional<Vec2i> findBuildSite(Vec2i near, StructureType type, int maxRadius, Random& rng) const;

    // ---- Spatial queries (search outward in rings; nearest by chebyshev distance) ----
    std::optional<Vec2i> findNearestResource(Vec2i from, ResourceKind kind, int maxRadius, float minAmount = 1.f) const;
    std::optional<Vec2i> findNearest(Vec2i from, int maxRadius,
                                     const std::function<bool(Vec2i, const Tile&)>& pred) const;
    std::vector<StructureId> structuresNear(Vec2i p, int radius, std::optional<StructureType> type = std::nullopt) const;
    // Nearest structure of a type satisfying pred (pred may be empty). kNoStructure if none.
    StructureId nearestStructure(Vec2i p, int radius, StructureType type,
                                 const std::function<bool(const Structure&)>& pred = {}) const;
    // Tile is inside/adjacent (chebyshev <= 1) to a complete Hut.
    bool isSheltered(Vec2i p) const;
    // Within 3 tiles of a lit campfire.
    bool nearWarmth(Vec2i p) const;
    // Within 6 tiles of a complete Monument.
    bool nearMonument(Vec2i p) const;
    // Random walkable tile within radius of p (nullopt if none found after a few tries).
    std::optional<Vec2i> randomWalkableNear(Vec2i p, int radius, Random& rng) const;
    // Nearest walkable tile to p (p itself if walkable).
    std::optional<Vec2i> nearestWalkable(Vec2i p, int maxRadius = 8) const;

    // Total food units currently available in the wild (for stats/UI).
    float wildFoodTotal() const;

private:
    int w_;
    int h_;
    std::vector<Tile> tiles_;
    std::map<StructureId, Structure> structures_;  // node-based: Structure* stay valid until destroyed
    StructureId nextStructureId_ = 1;
    Tick now_ = 0;

    // ---- private (world module internals) ----
    Tick lastUpdateTick_ = 0;   // last tick structures were advanced
    Tick lastRegrowTick_ = 0;   // last tick resources were regrown (batched every few ticks)
    float wildFood_ = 0.f;      // cached wildFoodTotal(); refreshed on regrowth, decremented on harvest
    void regrowResources(int dtTicks);
    void updateStructures(int dtTicks);
    void ensureConnectedLand();
    void recomputeWildFood();
};

}  // namespace heur
