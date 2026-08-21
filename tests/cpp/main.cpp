#include <algorithm>
#include <cstdint>
#include <iostream>

#include "agent/Agent.hpp"
#include "bridge/Observation.hpp"
#include "bridge/ObservationSerializer.hpp"
#include "bridge/BrainResponseParser.hpp"
#include "world/MazeGenerator.hpp"
#include "world/World.hpp"
#include "world/Position.hpp"
#include "navigation/Pathfinder.hpp"
#include "scenario/Scenario.hpp"
#include "sensors/RangeSensor.hpp"

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

    // The gap makes the destination reachable while preserving walls on three sides.
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

    aura::world::World sensorWorld{7, 5};
    sensorWorld.addBoundaryWalls();

    aura::agent::Agent sensorAgent{{1, 2}};

    const aura::world::Position batteryPosition{5, 2};

    sensorWorld.setCell(batteryPosition, aura::world::CellType::Battery);

    aura::sensors::RangeSensor sensor{6};

    const auto sensorObservation =
            sensor.observe(sensorWorld, sensorAgent);

    const auto batteryObject =
            std::ranges::find_if(sensorObservation.objects,
                                 [batteryPosition](const auto &object) {
                                     return object.position == batteryPosition;
                                 });

    const auto expectedPath =
            aura::navigation::findPath(sensorWorld, sensorAgent.position(), batteryPosition);

    if (batteryObject == sensorObservation.objects.end() || expectedPath.empty() || !batteryObject->nextStep.has_value()
        || batteryObject->nextStep.value() != expectedPath.front()) {
        std::cout
                << "FAIL: visible object should expose "
                << "the first BFS route step\n";

        ++failures;
    }

    aura::world::World mazeWorld{11, 9};
    aura::world::MazeGenerator mazeGenerator{1337};
    mazeGenerator.generate(mazeWorld, 3, 0);

    int batteryCount = 0;

    for (int y = 0; y < mazeWorld.height(); ++y) {
        for (int x = 0; x < mazeWorld.width(); ++x) {
            const aura::world::Position position{x, y};

            if (mazeWorld.cellAt(position) == aura::world::CellType::Battery) {
                ++batteryCount;
            }

            if (position == aura::world::Position{1, 1} ||
                !mazeWorld.canEnter(position)) {
                continue;
            }

            if (aura::navigation::findPath(mazeWorld, {1, 1}, position).empty()) {
                std::cout << "FAIL: maze passages should connect to the start\n";
                ++failures;
                break;
            }
        }
    }

    if (batteryCount != 3) {
        std::cout << "FAIL: maze should contain the requested battery count\n";
        ++failures;
    }

    aura::world::World scenarioWorld{5, 5};
    scenarioWorld.addBoundaryWalls();
    aura::agent::Agent scenarioAgent{{1, 1}};
    aura::scenario::PeriodicMoveToBlock challenge{2};
    const aura::bridge::Action moveToAction{
        aura::bridge::ActionType::MoveTo,
        aura::bridge::Direction::North,
        {3, 3}
    };

    challenge.beforeAction(scenarioWorld, scenarioAgent, moveToAction);

    if (scenarioWorld.cellAt({3, 3}) == aura::world::CellType::Wall) {
        std::cout << "FAIL: challenge should leave the first move_to unchanged\n";
        ++failures;
    }

    challenge.beforeAction(scenarioWorld, scenarioAgent, moveToAction);

    if (scenarioWorld.cellAt({3, 3}) != aura::world::CellType::Wall) {
        std::cout << "FAIL: challenge should block the configured move_to interval\n";
        ++failures;
    }

    challenge.afterAction(scenarioWorld, scenarioAgent, moveToAction);

    if (scenarioWorld.cellAt({3, 3}) == aura::world::CellType::Wall) {
        std::cout << "FAIL: challenge should restore its temporary obstacle\n";
        ++failures;
    }

    for (std::uint32_t seed = 2001; seed <= 2012; ++seed) {
        aura::world::World safeEnergyWorld{42, 21};
        aura::world::MazeGenerator safeEnergyGenerator{seed};
        safeEnergyGenerator.generate(safeEnergyWorld, 12, 50, 30);
        int shortestBatteryPath = safeEnergyWorld.width() * safeEnergyWorld.height();

        for (int x = 0; x < safeEnergyWorld.width(); ++x) {
            for (int y = 0; y < safeEnergyWorld.height(); ++y) {
                const aura::world::Position target{x, y};

                if (safeEnergyWorld.cellAt(target) != aura::world::CellType::Battery) {
                    continue;
                }

                const auto batteryPath = aura::navigation::findPath(
                    safeEnergyWorld,
                    {1, 1},
                    target
                );
                shortestBatteryPath = std::min(
                    shortestBatteryPath,
                    static_cast<int>(batteryPath.size())
                );
            }
        }

        if (shortestBatteryPath > 30) {
            std::cout << "FAIL: configured seed should have an energy-safe battery\n";
            ++failures;
            break;
        }
    }

    aura::sensors::RangeObservation nearby;

    nearby.objects.push_back({aura::world::CellType::Battery, {8, 3}, true, 6, aura::world::Position{3, 2}});

    const aura::bridge::Observation observation{
        {2, 2},
        100,
        {
            aura::world::CellType::Empty,
            aura::world::CellType::Empty,
            aura::world::CellType::Empty,
            aura::world::CellType::Empty
        },
        nearby,
        3,
        aura::bridge::LastAction{
            aura::bridge::ActionType::MoveTo,
            aura::world::Position{8, 3},
            true,
            "completed",
            1,
            0
        }
    };

    const auto serialized =
            aura::bridge::serializedObservation(observation);

    if (serialized.find(R"("last_action":)") == std::string::npos ||
        serialized.find(R"("type":"move_to")") == std::string::npos ||
        serialized.find(R"("target":[8,3])") == std::string::npos ||
        serialized.find(R"("succeeded":true)") == std::string::npos ||
        serialized.find(R"("next_step":[3,2])") == std::string::npos ||
        serialized.find(R"("result":"completed")") == std::string::npos ||
        serialized.find(R"("path_length_before":1)") == std::string::npos ||
        serialized.find(R"("path_length_after":0)") == std::string::npos) {
        std::cout << "FAIL: observation should include"
                << " the visible object's next BFS step\n";
        
        ++failures;
    }

    const auto response = aura::bridge::parseBrainResponse(R"({
        "action":"move_to",
        "target":[7,3],
        "debug":{
            "goal":"recharge",
            "failures":{
                "plan_failures":3,
                "replans":2,
                "failed_targets":4,
                "body_action_failures":1
            },
            "plan":{
                "goal":"recharge",
                "current_step":0,
                "step_count":1,
                "failed":false,
                "step":{"type":"move_to","target":[7,3]}
            }
        }
    })");

    if (response.debug.planGoal != "recharge" ||
        response.debug.planCurrentStep != 0 ||
        response.debug.planStepCount != 1 ||
        response.debug.planFailed ||
        response.debug.planStepType != "move_to" ||
        !response.debug.hasPlanStepTarget ||
        response.debug.planStepTarget != aura::world::Position{7, 3} ||
        response.debug.planFailures != 3 ||
        response.debug.replans != 2 ||
        response.debug.failedTargets != 4 ||
        response.debug.bodyActionFailures != 1) {
        std::cout << "FAIL: brain response should expose active plan debug state\n";
        ++failures;
    }

    if (failures == 0) {
        std::cout << "All tests passed\n";
        return 0;
    }

    std::cout << failures << " test(s) failed\n";
    return 1;
}
