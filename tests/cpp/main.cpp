#include <iostream>

#include "agent/Agent.hpp"
#include "world/World.hpp"
#include "world/Position.hpp"
#include "navigation/Pathfinder.hpp"

int main() {
    int failures = 0;

    aura::world::World world{10, 5};
    world.addBoundaryWalls();

    aura::agent::Agent agent(aura::world::Position{2, 2});

    if (!world.isInside({0, 0})) {
        std::cout << "FAIL: (0,0) should be inside the world\n";
        ++failures;
    }
    if (!world.isInside({9, 4})) {
        std::cout << "FAIL: (9,4) should be inside the world\n";
        ++failures;
    }

    if (world.isInside({10, 4})) {
        std::cout << "FAIL: (10,4) should be outside the world\n";
        ++failures;
    }

    if (world.isInside({9, 5})) {
        std::cout << "FAIL: (9,5) should be outside the world\n";
        ++failures;
    }

    if (world.isInside({-1, 0})) {
        std::cout << "FAIL: (-1,0) should be outside the world\n";
        ++failures;
    }

    if (!agent.moveBy({1, 0}, world)) {
        std::cout << "FAIL: valid movement should succeed\n";
        ++failures;
    }

    if (!(agent.position() == aura::world::Position{3, 2})) {
        std::cout << "FAIL: agent should move to (3,2)\n";
        ++failures;
    }

    static_cast<void>(agent.moveBy({1, 0}, world));
    if (agent.energy() != 98) {
        std::cout << "FAIL: successful movement should cost 1 energy\n";
        ++failures;
    }

    aura::agent::Agent blockedAgent{{1, 1}};

    const int energyBefore = blockedAgent.energy();

    if (blockedAgent.moveBy({-1, 0}, world)) {
        std::cout << "FAIL: movement into wall should fail\n";
        ++failures;
    }

    if (!(blockedAgent.position() == aura::world::Position{1, 1})) {
        std::cout << "FAIL: blocked movement should not change position\n";
        ++failures;
    }

    if (blockedAgent.energy() != energyBefore) {
        std::cout << "FAIL: blocked movement should not cost energy\n";
        ++failures;
    }

    world.setCell({2, 1}, aura::world::CellType::Battery);

    static_cast<void>(blockedAgent.moveBy({1, 0}, world));

    if (blockedAgent.energy() != blockedAgent.maxEnergy()) {
        std::cout << "FAIL: battery should restore maximum energy\n";
        ++failures;
    }

    aura::world::World pathWorld{7, 5};
    pathWorld.addBoundaryWalls();

    pathWorld.setCell({3, 1}, aura::world::CellType::Wall);
    pathWorld.setCell({3, 2}, aura::world::CellType::Wall);
    pathWorld.setCell({3, 3}, aura::world::CellType::Wall);

    // Open a gap at the bottom.
    pathWorld.setCell({3, 3}, aura::world::CellType::Empty);

    const auto path =
            aura::navigation::findPath(
                pathWorld,
                {1, 2},
                {5, 2}
            );

    if (!path.empty() &&
        path.back() != aura::world::Position{5, 2}) {
        std::cout << "FAIL: path should end at requested goal\n";
        ++failures;
    }

    aura::world::World blockedWorld{7, 5};
    blockedWorld.addBoundaryWalls();

    blockedWorld.setCell({3, 1}, aura::world::CellType::Wall);
    blockedWorld.setCell({3, 2}, aura::world::CellType::Wall);
    blockedWorld.setCell({3, 3}, aura::world::CellType::Wall);

    const auto blockedPath =
            aura::navigation::findPath(
                blockedWorld,
                {1, 2},
                {5, 2}
            );

    if (!blockedPath.empty()) {
        std::cout << "FAIL: unreachable target should return empty path\n";
        ++failures;
    }

    if (failures == 0) {
        std::cout << "All tests passed\n";
        return 0;
    }

    std::cout << failures << " test(s) failed\n";
    return 1;
}
