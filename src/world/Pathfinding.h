// Heuristica — A* pathfinding over the world grid (8-connected, octile heuristic).
// FROZEN CONTRACT.
#pragma once

#include <vector>

#include "core/Types.h"

namespace heur {

class World;

// Finds a path from `start` toward `goal` using A* with an octile-distance heuristic and terrain move costs.
// Success when the path ends on a walkable tile with chebyshev(tile, goal) <= range.
// (range 0 = stand on goal; range 1 = adjacent, e.g. to harvest a tree or talk to an agent.)
// Diagonal moves must not cut corners between two unwalkable orthogonal neighbours.
// Returned path EXCLUDES start and is ordered start->goal. Empty vector if start already satisfies range
// OR no path found within maxExpansions node expansions — callers distinguish using chebyshev(start,goal).
std::vector<Vec2i> findPath(const World& world, Vec2i start, Vec2i goal, int range = 0, int maxExpansions = 6000);

}  // namespace heur
