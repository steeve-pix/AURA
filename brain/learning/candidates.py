import math
from dataclasses import dataclass

from brain.learning.features import ValueInput, encode_value_input
from brain.learning.inference import predict_value
from brain.memory import Memory


@dataclass(frozen=True)
class CandidateDecision:
    goal: str
    action: dict


@dataclass(frozen=True)
class ScoredCandidate:
    candidate: CandidateDecision
    predicted_reward: float
    value_input: ValueInput | None = None
    reachable: bool | None = None


def decision_key(goal: str, action: dict) -> tuple:
    target = action.get("target")

    return (
        goal,
        action.get("action"),
        action.get("direction"),
        None if target is None else (target[0], target[1]),
    )


def _copy_action(action: dict) -> dict:
    copied = {
        key: value
        for key, value in action.items()
        if key != "debug"
    }

    if copied.get("target") is not None:
        copied["target"] = list(copied["target"])

    return copied


def _append_unique(candidates: list[CandidateDecision], candidate: CandidateDecision) -> None:
    key = decision_key(candidate.goal, candidate.action)

    if any(
            decision_key(existing.goal, existing.action) == key
            for existing in candidates
    ):
        return

    candidates.append(candidate)


def candidate_decisions(observation: dict, memory: Memory, *, rule_goal: str, rule_action: dict, recharge_urgent: bool,
                        plan_is_committed: bool) -> list[CandidateDecision]:
    candidates = [
        CandidateDecision(
            goal=rule_goal,
            action=_copy_action(rule_action),
        )
    ]

    # Advisory alternatives still respect the rule system's safety and
    # commitment boundaries.
    if recharge_urgent or rule_goal == "recharge" or plan_is_committed:
        return candidates

    for direction in (
            "north",
            "east",
            "south",
            "west",
    ):
        if observation.get(direction) in {None, "Wall", "Unknown"}:
            continue

        _append_unique(
            candidates,
            CandidateDecision(
                goal="explore",
                action={
                    "action": "move",
                    "direction": direction,
                },
            ),
        )

    aura_position = tuple(observation["position"])

    for visible_object in observation.get("nearby_objects", []):
        if visible_object.get("type") != "Unknown":
            continue

        target = (visible_object["position"][0], visible_object["position"][1])
        distance = (
                abs(target[0] - aura_position[0])
                + abs(target[1] - aura_position[1])
        )

        if (
                distance != 1
                or not visible_object.get("reachable", False)
                or memory.is_failed_target(target)
        ):
            continue

        _append_unique(
            candidates,
            CandidateDecision(
                goal="investigate",
                action={
                    "action": "investigate",
                    "target": list(target),
                },
            ),
        )

    return candidates


def value_input_for_candidate(candidate: CandidateDecision, observation: dict, memory: Memory,
                              navigation_preview: dict | None = None) -> ValueInput:
    context = memory.pre_action_context(
        action=candidate.action,
        observation=observation,
    )

    path_length = context["path_length_before"]
    next_step_was_visited = context["next_step_was_visited"]
    reachable = None

    if candidate.action.get("action") == "move_to" and navigation_preview is not None:
        reachable = navigation_preview["reachable"]

        if navigation_preview["reachable"]:
            path_length = navigation_preview["path_length"]
            next_step = navigation_preview["next_step"]
            next_step_was_visited = (
                None
                if next_step is None
                else memory.visit_count((next_step[0], next_step[1])) > 0
            )
        else:
            path_length = None
            next_step_was_visited = None

    return ValueInput(
        energy=context["energy_before"],
        goal=candidate.goal,
        action=candidate.action["action"],
        target=context["target"],
        position=context["position_before"],
        path_length=path_length,
        memory_trust=context["memory_trust_before"],
        next_step_was_visited=next_step_was_visited,
        reachable=reachable,
    )


def score_candidates(model, candidates: list[CandidateDecision], observation: dict, memory: Memory,
                     navigation_previews: dict[tuple[int, int], dict] | None = None) -> list[ScoredCandidate]:
    scored = []

    for candidate in candidates:
        target = candidate.action.get("target")
        navigation_preview = (
            None
            if navigation_previews is None or target is None
            else navigation_previews.get((target[0], target[1]))
        )
        value_input = value_input_for_candidate(
            candidate,
            observation,
            memory,
            navigation_preview,
        )
        feature_vector = encode_value_input(value_input)
        prediction = predict_value(model, feature_vector)

        scored.append(
            ScoredCandidate(
                candidate=candidate,
                predicted_reward=prediction,
                value_input=value_input,
                reachable=(
                    None
                    if navigation_preview is None
                    else navigation_preview["reachable"]
                ),
            )
        )

    return scored


def rule_scored_candidate(scored_candidates: list[ScoredCandidate], *, rule_goal: str,
                          rule_action: dict) -> ScoredCandidate:
    rule_key = decision_key(rule_goal, rule_action)

    for scored in scored_candidates:
        if decision_key(
                scored.candidate.goal,
                scored.candidate.action,
        ) == rule_key:
            return scored

    raise ValueError("The rule-selected decision is missing from the candidates.")


def select_model_candidate(scored_candidates: list[ScoredCandidate], *, rule_goal: str,
                           rule_action: dict) -> ScoredCandidate:
    if not scored_candidates:
        raise ValueError("At least one scored candidate is required.")

    best_reward = max(
        scored.predicted_reward
        for scored in scored_candidates
    )
    rule_scored = rule_scored_candidate(
        scored_candidates,
        rule_goal=rule_goal,
        rule_action=rule_action,
    )

    # Prefer the rule choice when scores tie so indistinguishable directions do
    # not create meaningless disagreements.
    if math.isclose(
            rule_scored.predicted_reward,
            best_reward,
            rel_tol=1e-9,
            abs_tol=1e-9,
    ):
        return rule_scored

    return max(
        scored_candidates,
        key=lambda scored: scored.predicted_reward,
    )
