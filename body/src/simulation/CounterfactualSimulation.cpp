#include "simulation/CounterfactualSimulation.hpp"

#include <cstdlib>

#include "bridge/ActionUtils.hpp"
#include "navigation/Pathfinder.hpp"
#include "simulation/SimulationSnapshot.hpp"

namespace aura::simulation {
    CounterfactualResult simulateAction(
            world::World &world,
            agent::Agent &agent,
            const bridge::Action &action,
            world::CellType investigationOutcome
    ) {
        const auto snapshot = captureSimulationSnapshot(world, agent);

        bool succeeded = false;
        std::string result = "failed";

        switch (action.type) {
            case bridge::ActionType::Move:
                succeeded = agent.moveBy(
                        bridge::directionOffset(action.direction),
                        world
                );
                result = succeeded ? "completed" : "failed";
                break;

            case bridge::ActionType::MoveTo: {
                const auto path = navigation::findPath(
                        world,
                        agent.position(),
                        action.target
                );

                if (path.empty() && agent.position() != action.target) {
                    result = "unreachable";
                    break;
                }

                succeeded = true;

                if (!path.empty()) {
                    const auto nextStep = path.front();
                    succeeded = agent.moveBy(
                            {
                                nextStep.x - agent.position().x,
                                nextStep.y - agent.position().y
                            },
                            world
                    );
                }

                result = succeeded ? "completed" : "failed";
                break;
            }

            case bridge::ActionType::Investigate: {
                const auto position = agent.position();
                const int distance =
                        std::abs(action.target.x - position.x) +
                        std::abs(action.target.y - position.y);

                succeeded = world.isInside(action.target) &&
                            distance == 1 &&
                            world.cellAt(action.target) == world::CellType::Unknown;

                if (succeeded) {
                    world.setCell(action.target, investigationOutcome);
                }

                result = succeeded ? "completed" : "failed";
                break;
            }

            case bridge::ActionType::Idle:
                succeeded = true;
                result = "completed";
                break;
        }

        const CounterfactualResult counterfactual{
            succeeded,
            result,
            agent.position(),
            agent.energy()
        };

        restoreSimulationSnapshot(world, agent, snapshot);

        return counterfactual;
    }
}
