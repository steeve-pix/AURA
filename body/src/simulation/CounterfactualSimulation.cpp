#include "simulation/CounterfactualSimulation.hpp"

#include <cstdlib>

#include "bridge/ActionUtils.hpp"
#include "navigation/Pathfinder.hpp"

namespace aura::simulation {
    PhysicalSimulationBranch createPhysicalSimulationBranch(const world::World &world, const agent::Agent &agent) {
        return PhysicalSimulationBranch{world, agent};
    }

    CounterfactualResult simulateBranchAction(PhysicalSimulationBranch &branch, const bridge::Action &action,
                                              world::CellType investigationOutcome) {
        auto &world = branch.world;
        auto &agent = branch.agent;

        bool succeeded = false;
        std::string result = "failed";
        std::optional<int> pathLengthBefore;
        std::optional<int> pathLengthAfter;
        std::optional<world::CellType> outcome;

        switch (action.type) {
            case bridge::ActionType::Move:
                succeeded = agent.moveBy(bridge::directionOffset(action.direction), world);
                result = succeeded ? "completed" : "failed";
                break;
            case bridge::ActionType::MoveTo: {
                const auto path = navigation::findPath(world, agent.position(), action.target);

                if (path.empty() && agent.position() != action.target) {
                    result = "unreachable";
                    break;
                }

                pathLengthBefore = static_cast<int>(path.size());
                succeeded = true;

                if (!path.empty()) {
                    const auto nextStep = path.front();
                    succeeded = agent.moveBy({
                                                 nextStep.x - agent.position().x,
                                                 nextStep.y - agent.position().y
                                             }, world);
                }

                result = succeeded ? "completed" : "failed";

                if (succeeded) {
                    const auto remainingPath = navigation::findPath(world, agent.position(),
                                                                    action.target);
                    pathLengthAfter = static_cast<int>(remainingPath.size());
                }
                break;
            }

            case bridge::ActionType::Investigate: {
                const auto position = agent.position();
                const int distance =
                        std::abs(action.target.x - position.x) +
                        std::abs(action.target.y - position.y);

                succeeded = world.isInside(action.target) && distance == 1 && world.cellAt(action.target)
                            == world::CellType::Unknown;

                if (succeeded) {
                    world.setCell(action.target, investigationOutcome);
                    outcome = investigationOutcome;
                }

                result = succeeded ? "completed" : "failed";
                break;
            }

            case bridge::ActionType::Idle:
                succeeded = true;
                result = "completed";
                break;
        }
        return CounterfactualResult{
            succeeded,
            result,
            agent.position(),
            agent.energy(),
            pathLengthBefore,
            pathLengthAfter,
            outcome
        };
    }

    CounterfactualResult simulateAction(
        world::World &world,
        agent::Agent &agent,
        const bridge::Action &action,
        world::CellType investigationOutcome
    ) {
        auto branch = createPhysicalSimulationBranch(world, agent);

        return simulateBranchAction(branch, action, investigationOutcome);
    }
}
