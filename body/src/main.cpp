#include <iostream>
#include <optional>
#include <ostream>
#include <vector>
#include <GLFW/glfw3.h>

#include "bridge/Action.hpp"
#include "bridge/ActionParser.hpp"
#include "bridge/ActionUtils.hpp"
#include "bridge/BrainProcess.hpp"
#include "bridge/BrainResponseParser.hpp"
#include "bridge/Observation.hpp"
#include "bridge/ObservationSerializer.hpp"
#include "navigation/Pathfinder.hpp"
#include "render/GridRenderer.hpp"
#include "render/Window.hpp"
#include "sensors/LocalSensor.hpp"
#include "sensors/RangeSensor.hpp"

#include <vector>
#include <stack>
#include <random>
#include <algorithm>

namespace {

// Generates a fully traversable maze with guaranteed path connectivity
void generateWalkableMaze(aura::world::World& world, int width, int height, int numBatteries, unsigned int seed = 42) {
    // Maze grid carving works best with odd dimensions
    if (width % 2 == 0) width++;
    if (height % 2 == 0) height++;

    world.addBoundaryWalls();

    // Track walls internally (true = wall, false = passage)
    std::vector<std::vector<bool>> isWall(width, std::vector<bool>(height, true));

    int gridW = (width - 1) / 2;
    int gridH = (height - 1) / 2;
    std::vector<std::vector<bool>> visited(gridW, std::vector<bool>(gridH, false));

    std::mt19937 rng(seed);
    std::stack<std::pair<int, int>> stack;

    // Start carving from grid cell (0, 0) -> world coordinate {1, 1}
    visited[0][0] = true;
    isWall[1][1] = false;
    stack.push({0, 0});

    const int dx[] = {0, 0, 1, -1};
    const int dy[] = {1, -1, 0, 0};

    while (!stack.empty()) {
        auto [gx, gy] = stack.top();

        // Check for unvisited neighbor grid nodes
        std::vector<int> neighbors;
        for (int i = 0; i < 4; ++i) {
            int nx = gx + dx[i];
            int ny = gy + dy[i];
            if (nx >= 0 && nx < gridW && ny >= 0 && ny < gridH && !visited[nx][ny]) {
                neighbors.push_back(i);
            }
        }

        if (!neighbors.empty()) {
            std::uniform_int_distribution<size_t> dist(0, neighbors.size() - 1);
            int dir = neighbors[dist(rng)];

            int nx = gx + dx[dir];
            int ny = gy + dy[dir];

            // Convert grid nodes to world coordinates
            int currentWX = gx * 2 + 1;
            int currentWY = gy * 2 + 1;
            int nextWX = nx * 2 + 1;
            int nextWY = ny * 2 + 1;

            // Carve path through target node and intermediate wall
            int wallWX = (currentWX + nextWX) / 2;
            int wallWY = (currentWY + nextWY) / 2;

            isWall[nextWX][nextWY] = false;
            isWall[wallWX][wallWY] = false;

            visited[nx][ny] = true;
            stack.push({nx, ny});
        } else {
            stack.pop();
        }
    }

    // Set remaining solid walls in the world
    for (int x = 1; x < width - 1; ++x) {
        for (int y = 1; y < height - 1; ++y) {
            if (isWall[x][y]) {
                world.setCell({x, y}, aura::world::CellType::Wall);
            }
        }
    }

    // Collect open passage cells (excluding start position)
    std::vector<std::pair<int, int>> passageCells;
    for (int x = 1; x < width - 1; ++x) {
        for (int y = 1; y < height - 1; ++y) {
            if (!isWall[x][y] && !(x == 1 && y == 1)) {
                passageCells.push_back({x, y});
            }
        }
    }

    // Scatter batteries strictly on valid passage cells
    std::shuffle(passageCells.begin(), passageCells.end(), rng);
    int placed = 0;
    for (const auto& [bx, by] : passageCells) {
        if (placed >= numBatteries) break;
        world.setCell({bx, by}, aura::world::CellType::Battery);
        placed++;
    }
}
}

int main() {
    using aura::render::GridRenderer;
    using aura::render::Window;

    Window window{1280, 720, "AURA"};

    constexpr int WIDTH = 41;
    constexpr int HEIGHT = 21;
    constexpr int NUM_BATTERIES = 12;

    aura::world::World world{WIDTH, HEIGHT};

    // Build a walkable maze seeded with batteries
    generateWalkableMaze(world, WIDTH, HEIGHT, NUM_BATTERIES, 1337);

    // Agent starts safely at guaranteed open position {1, 1}
    aura::agent::Agent agent{{1, 1}};

    aura::sensors::RangeSensor rangeSensor{10};

#if defined(_WIN32)
    const std::string pythonExecutable = "python";
    const std::string scriptPath = "-m brain.main";
    const std::string workingDirectory =
            R"(C:\Users\Steeve Dim\Documents\AURA)";
#else
    const std::string pythonExecutable = "python3";
    const std::string scriptPath = "brain/main.py";
    const std::string workingDirectory =
            "/Users/steeve.dimitry/Developer/AURA";
#endif

    aura::bridge::BrainProcess brain{
        pythonExecutable,
        scriptPath,
        workingDirectory
    };

    if (!brain.launch()) {
        std::cerr << "Failed to launch Python brain\n";
        return 1;
    }

    double lastUpdateTime = glfwGetTime();

    constexpr double updateInterval = 0.25;

    std::vector<aura::world::Position> currentPath;

    aura::world::Position currentTarget{};
    bool hasTarget = false;

    aura::bridge::BrainDebugState brainDebug;

    std::optional<aura::bridge::LastAction> lastAction;

    while (!window.shouldClose()) {
        Window::pollEvents();

        const double now = glfwGetTime();

        if (now - lastUpdateTime >= updateInterval) {
            const auto local =
                    aura::sensors::LocalSensor::observe(
                        world,
                        agent
                    );

            const auto nearby =
                    rangeSensor.observe(
                        world,
                        agent
                    );

            const aura::bridge::Observation observation{
                agent.position(),
                agent.energy(),
                local,
                nearby,
                rangeSensor.radius(),
                lastAction
            };

            const std::string observationJson =
                    aura::bridge::serializedObservation(
                        observation
                    );

            lastAction.reset();

            const std::string actionJson =
                    brain.exchange(
                        observationJson
                    );

            if (!actionJson.empty()) {
                try {
                    const auto response =
                            aura::bridge::parseBrainResponse(actionJson);

                    brainDebug = response.debug;

                    const auto action =
                            response.action;

                    if (action.type == aura::bridge::ActionType::Move) {
                        currentPath.clear();
                        hasTarget = false;

                        const bool moved =
                                agent.moveBy(
                                    aura::bridge::directionOffset(action.direction),
                                    world
                                );

                        lastAction = {
                            aura::bridge::ActionType::Move,
                            std::nullopt,
                            moved
                        };
                    }

                    if (action.type == aura::bridge::ActionType::MoveTo) {
                        currentTarget = action.target;
                        hasTarget = true;

                        currentPath =
                                aura::navigation::findPath(
                                    world,
                                    agent.position(),
                                    currentTarget
                                );

                        bool moved = currentTarget == agent.position();

                        if (!currentPath.empty()) {
                            const auto current =
                                    agent.position();

                            const std::string title =
                                    "AURA | Energy: " + std::to_string(agent.energy()) + " | Goal: " + brainDebug.goal +
                                    " | Position: (" +
                                    std::to_string(current.x) + "," + std::to_string(current.y) + ")";
                            window.setTitle(title);

                            const auto next =
                                    currentPath.front();

                            const aura::world::Position offset{
                                next.x - current.x,
                                next.y - current.y
                            };

                            moved = agent.moveBy(
                                offset,
                                world
                            );
                        }

                        lastAction = {
                            aura::bridge::ActionType::MoveTo,
                            currentTarget,
                            moved
                        };
                    }

                    if (action.type == aura::bridge::ActionType::Idle) {
                        currentPath.clear();
                        hasTarget = false;

                        lastAction = {
                            aura::bridge::ActionType::Idle,
                            std::nullopt,
                            true
                        };
                    }
                } catch (const std::exception &error) {
                    std::cerr
                            << "Invalid JSON from brain: "
                            << actionJson
                            << '\n';

                    std::cerr
                            << "Reason: "
                            << error.what()
                            << '\n';
                }
            }

            lastUpdateTime = now;
        }

        window.clear();

        GridRenderer::render(
            world,
            agent,
            rangeSensor.radius(),
            currentPath,
            hasTarget
                ? &currentTarget
                : nullptr,
            brainDebug
        );

        window.display();
    }

    return 0;
}
