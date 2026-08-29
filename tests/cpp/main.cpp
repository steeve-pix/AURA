#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>

#include "agent/Agent.hpp"
#include "bridge/Observation.hpp"
#include "bridge/ObservationSerializer.hpp"
#include "bridge/BrainResponseParser.hpp"
#include "bridge/Counterfactual.hpp"
#include "bridge/NavigationPreview.hpp"
#include "world/MazeGenerator.hpp"
#include "world/World.hpp"
#include "world/Position.hpp"
#include "navigation/Pathfinder.hpp"
#include "scenario/Scenario.hpp"
#include "sensors/LocalSensor.hpp"
#include "sensors/RangeSensor.hpp"
#include "simulation/CounterfactualSimulation.hpp"
#include "simulation/SimulationSnapshot.hpp"

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

    aura::world::World counterfactualWorld{5, 5};
    counterfactualWorld.addBoundaryWalls();
    counterfactualWorld.setCell({2, 1}, aura::world::CellType::Unknown);

    aura::agent::Agent counterfactualAgent{{1, 1}, 10};

    const auto assertCounterfactualRollback =
            [&counterfactualWorld, &counterfactualAgent, &failures](
                    const aura::simulation::CounterfactualResult &result,
                    aura::world::Position expectedPosition,
                    int expectedEnergy,
                    const char *actionName
            ) {
                if (!result.succeeded ||
                    result.result != "completed" ||
                    result.positionAfter != expectedPosition ||
                    result.energyAfter != expectedEnergy) {
                    std::cout << "FAIL: counterfactual " << actionName
                              << " should report its hypothetical result\n";
                    ++failures;
                }

                if (counterfactualAgent.position() != aura::world::Position{1, 1} ||
                    counterfactualAgent.energy() != 10) {
                    std::cout << "FAIL: counterfactual " << actionName
                              << " should restore agent state\n";
                    ++failures;
                }
            };

    const auto moveResult = aura::simulation::simulateAction(
            counterfactualWorld,
            counterfactualAgent,
            {
                aura::bridge::ActionType::Move,
                aura::bridge::Direction::East,
                {}
            }
    );
    assertCounterfactualRollback(moveResult, {2, 1}, 9, "move");

    const auto moveToResult = aura::simulation::simulateAction(
            counterfactualWorld,
            counterfactualAgent,
            {
                aura::bridge::ActionType::MoveTo,
                aura::bridge::Direction::North,
                {3, 1}
            }
    );
    assertCounterfactualRollback(moveToResult, {2, 1}, 9, "move_to");

    const auto investigateResult = aura::simulation::simulateAction(
            counterfactualWorld,
            counterfactualAgent,
            {
                aura::bridge::ActionType::Investigate,
                aura::bridge::Direction::North,
                {2, 1}
            },
            aura::world::CellType::Battery
    );
    assertCounterfactualRollback(investigateResult, {1, 1}, 10, "investigate");

    if (
        moveToResult.pathLengthBefore != 2
        || moveToResult.pathLengthAfter != 1
        || investigateResult.outcome != aura::world::CellType::Battery
    ) {
        std::cout << "FAIL: counterfactual results should include reward inputs\n";
        ++failures;
    }

    if (counterfactualWorld.cellAt({2, 1}) != aura::world::CellType::Unknown) {
        std::cout << "FAIL: counterfactual investigate should restore world cells\n";
        ++failures;
    }

    aura::world::World snapshotWorld{5, 5};
    snapshotWorld.addBoundaryWalls();
    snapshotWorld.setCell({2, 1}, aura::world::CellType::Battery);

    aura::agent::Agent snapshotAgent{{1, 1}, 50};
    aura::sensors::RangeSensor snapshotSensor{2};

    const auto serializedSnapshotObservation =
            [&snapshotWorld, &snapshotAgent, &snapshotSensor] {
                const aura::bridge::Observation snapshotObservation{
                    snapshotAgent.position(),
                    snapshotAgent.energy(),
                    aura::sensors::LocalSensor::observe(
                        snapshotWorld,
                        snapshotAgent
                    ),
                    snapshotSensor.observe(snapshotWorld, snapshotAgent),
                    snapshotSensor.radius(),
                    std::nullopt,
                    "snapshot-test"
                };

                return aura::bridge::serializedObservation(
                    snapshotObservation
                );
            };

    const auto originalObservation =
            serializedSnapshotObservation();
    const auto snapshot =
            aura::simulation::captureSimulationSnapshot(
                snapshotWorld,
                snapshotAgent
            );

    if (!snapshotAgent.moveBy({1, 0}, snapshotWorld)) {
        std::cout << "FAIL: snapshot test movement should succeed\n";
        ++failures;
    }

    snapshotWorld.setCell({3, 1}, aura::world::CellType::Unknown);

    aura::simulation::restoreSimulationSnapshot(
        snapshotWorld,
        snapshotAgent,
        snapshot
    );

    if (snapshotAgent.position() != aura::world::Position{1, 1} ||
        snapshotAgent.energy() != 50) {
        std::cout << "FAIL: snapshot restore should recover agent state\n";
        ++failures;
    }

    if (snapshotWorld.cellAt({2, 1}) != aura::world::CellType::Battery ||
        snapshotWorld.cellAt({3, 1}) != aura::world::CellType::Empty) {
        std::cout << "FAIL: snapshot restore should recover world cells\n";
        ++failures;
    }

    if (serializedSnapshotObservation() != originalObservation) {
        std::cout << "FAIL: observation after restore should match original state\n";
        ++failures;
    }

    aura::world::World scenarioWorld{5, 5};

    scenarioWorld.addBoundaryWalls();

    aura::agent::Agent scenarioAgent{{1, 1}};

    aura::scenario::PeriodicMoveToBlock challenge{2};

    const aura::bridge::Action firstTarget{
        aura::bridge::ActionType::MoveTo,
        aura::bridge::Direction::North,
        {3, 3}
    };

    challenge.beforeAction(scenarioWorld, scenarioAgent, firstTarget);

    if (scenarioWorld.cellAt({2, 1}) == aura::world::CellType::Wall) {
        std::cout
                << "FAIL: first distinct target "
                << "should not be obstructed\n";
        ++failures;
    }

    // Continuing the same plan must not count as
    // another target attempt.
    challenge.beforeAction(scenarioWorld, scenarioAgent, firstTarget);

    if (scenarioWorld.cellAt({2, 1}) == aura::world::CellType::Wall) {
        std::cout
                << "FAIL: repeated move_to ticks "
                << "should not retrigger challenge\n";
        ++failures;
    }

    const aura::bridge::Action secondTarget{aura::bridge::ActionType::MoveTo, aura::bridge::Direction::North, {3, 1}};

    challenge.beforeAction(scenarioWorld, scenarioAgent, secondTarget);

    // The first BFS step is obstructed.
    if (scenarioWorld.cellAt({2, 1}) != aura::world::CellType::Wall) {
        std::cout
                << "FAIL: configured target attempt "
                << "should obstruct its route\n";
        ++failures;
    }

    // The final objective must remain valid.
    if (scenarioWorld.cellAt({3, 1}) == aura::world::CellType::Wall) {
        std::cout
                << "FAIL: challenge should not turn "
                << "the objective into a wall\n";
        ++failures;
    }

    challenge.afterAction(scenarioWorld, scenarioAgent, secondTarget);

    if (scenarioWorld.cellAt({2, 1}) == aura::world::CellType::Wall) {
        std::cout
                << "FAIL: challenge should restore "
                << "its temporary route obstacle\n";
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
            .type = aura::bridge::ActionType::MoveTo,
            .target = aura::world::Position{8, 3},
            .succeeded = true,
            .result = "completed",
            .pathLengthBefore = 1,
            .pathLengthAfter = 0,
            .nextStepBefore = aura::world::Position{3, 2},
            .nextStepAfter = aura::world::Position{4, 2},
            .reachableBefore = true
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
        serialized.find(R"("path_length_after":0)") == std::string::npos ||
        serialized.find(R"("next_step_before":[3,2])") == std::string::npos ||
        serialized.find(R"("next_step_after":[4,2])") == std::string::npos ||
        serialized.find(R"("reachable_before":true)") == std::string::npos) {
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

    const auto previewRequest = aura::bridge::parseBrainResponse(R"({
        "type":"preview_request",
        "candidates":[
            {"id":1,"action":"move_to","target":[5,2]},
            {"id":2,"action":"move_to","target":[0,0]}
        ]
    })");

    if (
        previewRequest.type != aura::bridge::BrainResponseType::PreviewRequest
        || previewRequest.previewCandidates.size() != 2
        || previewRequest.previewCandidates[0].id != 1
        || previewRequest.previewCandidates[0].target !=
        aura::world::Position{5, 2}
    ) {
        std::cout << "FAIL: brain response should parse preview candidates\n";
        ++failures;
    }

    const auto counterfactualRequest = aura::bridge::parseBrainResponse(R"({
        "type":"counterfactual_request",
        "candidates":[
            {
                "choice":"rule",
                "decision":{"action":"move","direction":"east"}
            },
            {
                "choice":"model",
                "decision":{"action":"move_to","target":[5,2]}
            }
        ]
    })");

    if (
        counterfactualRequest.type !=
            aura::bridge::BrainResponseType::CounterfactualRequest
        || counterfactualRequest.counterfactualCandidates.size() != 2
        || counterfactualRequest.counterfactualCandidates[0].choice != "rule"
        || counterfactualRequest.counterfactualCandidates[1].action.target !=
            aura::world::Position{5, 2}
    ) {
        std::cout << "FAIL: body should parse counterfactual candidates\n";
        ++failures;
    }

    const auto counterfactualJson =
        aura::bridge::serializedCounterfactualResponse({
            {
                "rule",
                moveResult
            },
            {
                "model",
                investigateResult
            }
        });

    if (
        counterfactualJson.find(R"("type":"counterfactual_response")") ==
            std::string::npos
        || counterfactualJson.find(R"("choice":"rule")") ==
            std::string::npos
        || counterfactualJson.find(R"("outcome":"Battery")") ==
            std::string::npos
    ) {
        std::cout << "FAIL: body should serialize counterfactual outcomes\n";
        ++failures;
    }

    aura::world::World previewWorld{7, 5};
    previewWorld.addBoundaryWalls();

    const auto previews = aura::bridge::previewNavigation(
        previewWorld,
        {1, 1},
        previewRequest.previewCandidates
    );
    const auto previewJson =
            aura::bridge::serializedNavigationPreviewResponse(previews);

    if (
        previews.size() != 2
        || !previews[0].reachable
        || previews[0].pathLength != 5
        || previews[0].nextStep != aura::world::Position{2, 1}
        || previews[1].reachable
        || previews[1].pathLength.has_value()
        || previews[1].nextStep.has_value()
        || previewJson.find(R"("type":"preview_response")") ==
        std::string::npos
        || previewJson.find(R"("id":1)") == std::string::npos
        || previewJson.find(R"("path_length":5)") == std::string::npos
        || previewJson.find(R"("next_step":[2,1])") == std::string::npos
        || previewJson.find(R"("reachable":false)") == std::string::npos
    ) {
        std::cout << "FAIL: navigation preview should preserve IDs and BFS truth\n";
        ++failures;
    }

    if (failures == 0) {
        std::cout << "All tests passed\n";
        return 0;
    }

    std::cout << failures << " test(s) failed\n";
    return 1;
}
