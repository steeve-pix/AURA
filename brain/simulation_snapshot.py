from collections.abc import Callable
from dataclasses import dataclass, field

from brain.decision import decide
from brain.goals import propose_goal
from brain.learning.counterfactual import (
    build_single_counterfactual_request,
    counterfactual_action_reward,
    single_counterfactual_result,
)
from brain.memory import Memory
from brain.perception import update_memory_from_observation
from brain.plan_supervisor import supervise_goal


@dataclass
class BrainSimulationSnapshot:
    memory: Memory


@dataclass
class HorizonResult:
    cumulative_reward: float
    steps_completed: int


@dataclass(frozen=True)
class ForcedBranchDecision:
    goal: str
    action: dict


@dataclass
class SimulationBranch:
    memory: Memory
    forced_first_decision: ForcedBranchDecision | None = None
    forced_first_decision_consumed: bool = False

    @property
    def forced_first_action(self) -> dict | None:
        """Compatibility view while horizon code migrates to forced decisions."""
        if self.forced_first_decision is None:
            return None

        return self.forced_first_decision.action

    @property
    def forced_first_action_consumed(self) -> bool:
        """Compatibility view while horizon code migrates to forced decisions."""
        return self.forced_first_decision_consumed


@dataclass
class CompletedBranchStep:
    branch: SimulationBranch
    choice: str
    observation_before: dict
    action: dict
    result: dict
    observation_after: dict
    reward: float


@dataclass
class PendingBranchStep:
    branch: SimulationBranch
    choice: str
    observation_before: dict
    action: dict


@dataclass
class BranchHorizonState:
    branch: SimulationBranch
    choice: str
    step_limit: int
    pending_step: PendingBranchStep | None = None
    completed_steps: list[CompletedBranchStep] = field(default_factory=list)
    stopped_early: bool = False
    initial_observation: dict | None = None


@dataclass
class HorizonComparison:
    rule_state: BranchHorizonState
    model_state: BranchHorizonState


@dataclass
class HorizonComparisonRequests:
    rule: dict | None
    model: dict | None


@dataclass(frozen=True)
class HorizonComparisonResult:
    rule: HorizonResult
    model: HorizonResult


def horizon_comparison_complete(comparison: HorizonComparison) -> bool:
    return (
            branch_horizon_complete(comparison.rule_state)
            and branch_horizon_complete(comparison.model_state)
    )


def horizon_comparison_result(comparison: HorizonComparison) -> HorizonComparisonResult:
    if not horizon_comparison_complete(comparison):
        raise ValueError("Both branch horizons must be complete before obtaining comparison results.")

    return HorizonComparisonResult(
        rule=branch_horizon_result(comparison.rule_state),
        model=branch_horizon_result(comparison.model_state),
    )


def begin_horizon_comparison(comparison: HorizonComparison) -> HorizonComparisonRequests:
    rule_next = next_horizon_request(comparison.rule_state)
    model_next = next_horizon_request(comparison.model_state)

    rule_request = None if rule_next is None else rule_next[1]
    model_request = None if model_next is None else model_next[1]

    return HorizonComparisonRequests(
        rule=rule_request,
        model=model_request,
    )


def handle_rule_horizon_response(
        comparison: HorizonComparison,
        response: dict,
) -> dict | None:
    return handle_horizon_response(comparison.rule_state, response)


def handle_model_horizon_response(
        comparison: HorizonComparison,
        response: dict,
) -> dict | None:
    return handle_horizon_response(comparison.model_state, response)


def stop_branch_horizon(state: BranchHorizonState) -> None:
    state.stopped_early = True


def branch_horizon_complete(state: BranchHorizonState) -> bool:
    return state.stopped_early or len(state.completed_steps) >= state.step_limit


def create_comparison_branches(snapshot: BrainSimulationSnapshot, horizon: int, rule_action: dict,
                               model_action: dict, rule_goal: str = "rule",
                               model_goal: str = "model") -> tuple[BranchHorizonState, BranchHorizonState]:
    rule_branch = create_branch(
        snapshot,
        forced_first_decision=ForcedBranchDecision(goal=rule_goal, action=rule_action),
    )
    model_branch = create_branch(
        snapshot,
        forced_first_decision=ForcedBranchDecision(goal=model_goal, action=model_action),
    )

    return (
        BranchHorizonState(
            branch=rule_branch,
            choice="rule",
            step_limit=horizon,
        ),
        BranchHorizonState(
            branch=model_branch,
            choice="model",
            step_limit=horizon,
        ),
    )


def create_horizon_comparison(
        snapshot: BrainSimulationSnapshot,
        initial_observation: dict,
        horizon: int,
        rule_action: dict,
        model_action: dict,
        rule_goal: str = "rule",
        model_goal: str = "model",
) -> HorizonComparison:
    rule_state, model_state = create_comparison_branches(
        snapshot=snapshot,
        horizon=horizon,
        rule_action=rule_action,
        model_action=model_action,
        rule_goal=rule_goal,
        model_goal=model_goal,
    )
    rule_state.initial_observation = initial_observation
    model_state.initial_observation = initial_observation

    return HorizonComparison(rule_state=rule_state, model_state=model_state)


def branch_horizon_result(state: BranchHorizonState) -> HorizonResult:
    return HorizonResult(
        cumulative_reward=sum(step.reward for step in state.completed_steps),
        steps_completed=len(state.completed_steps),
    )


def consume_forced_first_decision(branch: SimulationBranch) -> ForcedBranchDecision | None:
    if branch.forced_first_decision_consumed:
        return None

    branch.forced_first_decision_consumed = True

    return branch.forced_first_decision


def consume_forced_first_action(branch: SimulationBranch) -> dict | None:
    """Return only the action for callers not yet using the forced goal."""
    decision = consume_forced_first_decision(branch)

    return None if decision is None else decision.action


def choose_rule_action_for_branch(branch: SimulationBranch, observation: dict) -> dict | None:
    """Run AURA's rule policy using only the branch's simulated memory."""
    proposal = propose_goal(observation, branch.memory)
    goal = supervise_goal(branch.memory, proposal=proposal)

    return decide(observation, goal, branch.memory)


def choose_branch_action(branch: SimulationBranch, observation: dict) -> dict | None:
    forced = consume_forced_first_action(branch)

    if forced is not None:
        return forced

    return choose_rule_action_for_branch(branch, observation)


def create_branch(
        snapshot: BrainSimulationSnapshot,
        forced_first_action: dict | None = None,
        *,
        forced_first_decision: ForcedBranchDecision | None = None,
) -> SimulationBranch:
    if forced_first_action is not None and forced_first_decision is not None:
        raise ValueError("Provide either a forced first action or decision, not both.")

    if forced_first_action is not None:
        forced_first_decision = ForcedBranchDecision(
            goal="unknown",
            action=forced_first_action,
        )

    return SimulationBranch(
        memory=restore_brain_snapshot(snapshot),
        forced_first_decision=forced_first_decision,
    )


def begin_branch_horizon(
        branch: SimulationBranch,
        observation: dict,
        *,
        choice: str,
        step_limit: int,
) -> tuple[BranchHorizonState, dict]:
    if step_limit < 1:
        raise ValueError("Branch horizon step_limit must be at least 1.")

    pending_step, request = begin_branch_step(branch, observation, choice=choice)
    state = BranchHorizonState(
        branch=branch,
        choice=choice,
        step_limit=step_limit,
        pending_step=pending_step,
        initial_observation=observation,
    )

    return state, request


def handle_horizon_response(
        state: BranchHorizonState,
        response: dict,
) -> dict | None:
    """Complete the pending step and return the next protocol request, if any."""
    return continue_branch_horizon(state, response)


def continue_branch_horizon(
        state: BranchHorizonState,
        response: dict,
) -> dict | None:
    if state.pending_step is None:
        raise ValueError("Branch horizon is not waiting for a response.")

    completed_step = complete_branch_step(state.pending_step, response)
    state.branch.memory.advance_step()
    update_memory_from_observation(
        state.branch.memory,
        completed_step.observation_after,
    )
    state.completed_steps.append(completed_step)

    next_step = next_horizon_request(state)
    if next_step is None:
        state.pending_step = None
        return None

    pending_step, request = next_step
    state.pending_step = pending_step
    return request


def begin_branch_step(branch: SimulationBranch, observation: dict, *, choice: str) -> tuple[PendingBranchStep, dict]:
    action: dict = choose_branch_action(branch, observation)
    pending_step = PendingBranchStep(
        branch=branch,
        choice=choice,
        observation_before=observation,
        action=action
    )

    request = build_single_counterfactual_request(action, choice=choice)

    return pending_step, request


def next_horizon_request(state: BranchHorizonState) -> tuple[PendingBranchStep, dict] | None:
    if branch_horizon_complete(state):
        return None

    observation = (
        state.completed_steps[-1].observation_after
        if state.completed_steps
        else state.initial_observation
    )
    if observation is None:
        raise ValueError("Branch horizon requires an initial observation.")

    next_step = begin_next_branch_step(state, observation)
    if next_step is not None:
        state.pending_step = next_step[0]
    return next_step


def begin_next_branch_step(
        state: BranchHorizonState,
        observation: dict | None = None,
) -> tuple[PendingBranchStep, dict] | None:
    if branch_horizon_complete(state):
        return None

    if observation is None:
        return next_horizon_request(state)

    action = choose_branch_action(state.branch, observation)
    if action is None:
        stop_branch_horizon(state)
        return None

    pending_step = PendingBranchStep(
        branch=state.branch,
        choice=state.choice,
        observation_before=observation,
        action=action,
    )
    request = build_single_counterfactual_request(action, choice=state.choice)
    return pending_step, request


def complete_branch_step(pending_step: PendingBranchStep, response: dict) -> CompletedBranchStep:
    result = single_counterfactual_result(response, choice=pending_step.choice)
    observation_after: dict = result.get("observation_after")

    if not isinstance(observation_after, dict):
        raise ValueError(
            "Counterfactual result requires observation_after."
        )

    return CompletedBranchStep(
        branch=pending_step.branch,
        choice=pending_step.choice,
        observation_before=pending_step.observation_before,
        action=pending_step.action,
        result=result,
        observation_after=observation_after,
        reward=counterfactual_action_reward(
            pending_step.action,
            result,
            pending_step.observation_before,
            pending_step.branch.memory,
            goal=pending_step.branch.memory.active_goal,
        ),
    )


def accumulate_rewards(rewards: list[float]) -> HorizonResult:
    return HorizonResult(
        cumulative_reward=sum(rewards),
        steps_completed=len(rewards)
    )


def capture_brain_snapshot(memory: Memory) -> BrainSimulationSnapshot:
    return BrainSimulationSnapshot(memory=memory.clone_for_simulation())


def restore_brain_snapshot(snapshot: BrainSimulationSnapshot) -> Memory:
    return snapshot.memory.clone_for_simulation()


def run_horizon(branch: SimulationBranch, horizon: int,
                step_fn: Callable[[SimulationBranch], float | None]) -> HorizonResult:
    rewards: list[float] = []

    for _ in range(horizon):
        reward: float | None = step_fn(branch)

        if reward is None:
            break

        rewards.append(reward)

    return accumulate_rewards(rewards)
