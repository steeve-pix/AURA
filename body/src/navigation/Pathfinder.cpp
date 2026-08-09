#include "navigation/Pathfinder.hpp"
#include "world/Position.hpp"

#include <algorithm>
#include <array>
#include <queue>
#include <vector>

namespace aura::navigation {
    std::vector<aura::world::Position> findPath(
        const aura::world::World &world,
        aura::world::Position start,
        aura::world::Position goal
    ) {
        const int totalCells =
                world.width() * world.height();

        std::vector<bool> visited(totalCells, false);
        std::vector<world::Position> parent(totalCells);

        std::queue<world::Position> frontier;

        const auto indexOf =
                [&world](world::Position position) {
            return position.y * world.width() + position.x;
        };

        frontier.push(start);
        visited[indexOf(start)] = true;

        while (!frontier.empty()) {
            const world::Position current =
                    frontier.front();

            frontier.pop();

            if (current == goal) {
                break;
            }

            const world::Position directions[] = {
                {0, -1},
                {1, 0},
                {0, 1},
                {-1, 0},
            };

            for (const auto direction: directions) {
                const world::Position next{
                    current.x + direction.x,
                    current.y + direction.y
                };

                if (!world.canEnter(next)) {
                    continue;
                }

                const int nextIndex =
                        indexOf(next);

                if (visited[nextIndex]) {
                    continue;
                }

                visited[nextIndex] = true;
                parent[nextIndex] = current;
                frontier.push(next);
            }
        }

        if (!visited[indexOf(goal)]) {
            return {};
        }

        std::vector<world::Position> path;

        world::Position current = goal;

        while (!(current == start)) {
            path.push_back(current);
            current = parent[indexOf(current)];
        }

        std::reverse(path.begin(), path.end());

        return path;
    }
}