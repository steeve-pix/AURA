#include "world/MazeGenerator.hpp"

#include <algorithm>
#include <random>
#include <stack>
#include <utility>
#include <vector>

namespace aura::world {
    MazeGenerator::MazeGenerator(std::uint32_t seed)
        : seed_(seed) {
    }

void MazeGenerator::generate(World &world, int batteryCount, int unknownCount) const {
    const int width = world.width();
    const int height = world.height();

    world.addBoundaryWalls();

    std::vector<std::vector<bool>> isWall(
            width,
            std::vector<bool>(height, true)
    );

    const int gridWidth = (width - 1) / 2;
    const int gridHeight = (height - 1) / 2;

    std::vector<std::vector<bool>> visited(
            gridWidth,
            std::vector<bool>(gridHeight, false)
    );

    std::mt19937 random(seed_);
    std::stack<std::pair<int, int>> stack;

    visited[0][0] = true;
    isWall[1][1] = false;
    stack.push({0, 0});

    constexpr int horizontalOffsets[] = {0, 0, 1, -1};
    constexpr int verticalOffsets[] = {1, -1, 0, 0};

    while (!stack.empty()) {
        const auto [gridX, gridY] = stack.top();

        std::vector<int> neighborDirections;

        for (int direction = 0; direction < 4; ++direction) {
            const int nextGridX = gridX + horizontalOffsets[direction];
            const int nextGridY = gridY + verticalOffsets[direction];

            if (nextGridX >= 0 && nextGridX < gridWidth &&
                nextGridY >= 0 && nextGridY < gridHeight &&
                !visited[nextGridX][nextGridY]) {
                neighborDirections.push_back(direction);
            }
        }

        if (neighborDirections.empty()) {
            stack.pop();
            continue;
        }

        std::uniform_int_distribution<std::size_t> distribution(
                0,
                neighborDirections.size() - 1
        );

        const int direction = neighborDirections[distribution(random)];

        const int nextGridX = gridX + horizontalOffsets[direction];
        const int nextGridY = gridY + verticalOffsets[direction];

        const int currentWorldX = gridX * 2 + 1;
        const int currentWorldY = gridY * 2 + 1;
        const int nextWorldX = nextGridX * 2 + 1;
        const int nextWorldY = nextGridY * 2 + 1;

        isWall[nextWorldX][nextWorldY] = false;
        isWall[(currentWorldX + nextWorldX) / 2]
              [(currentWorldY + nextWorldY) / 2] = false;

        visited[nextGridX][nextGridY] = true;
        stack.push({nextGridX, nextGridY});
    }

    std::vector<Position> passageCells;

    for (int x = 1; x < width - 1; ++x) {
        for (int y = 1; y < height - 1; ++y) {
            if (isWall[x][y]) {
                world.setCell({x, y}, CellType::Wall);
            } else if (x != 1 || y != 1) {
                passageCells.push_back({x, y});
            }
        }
    }

    std::shuffle(passageCells.begin(), passageCells.end(), random);

    int cellIndex = 0;

    const int batteriesToPlace = std::min(
            batteryCount,
            static_cast<int>(passageCells.size())
    );

    for (int i = 0; i < batteriesToPlace; ++i) {
        world.setCell(passageCells[cellIndex++], CellType::Battery);
    }

    const int cellsRemaining =
            static_cast<int>(passageCells.size()) - cellIndex;

    const int unknownsToPlace = std::min(
            unknownCount,
            cellsRemaining
    );

    for (int i = 0; i < unknownsToPlace; ++i) {
        world.setCell(passageCells[cellIndex++], CellType::Unknown);
    }
}}
