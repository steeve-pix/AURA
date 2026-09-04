from dataclasses import dataclass

from brain.experience import Experience
from brain.learning.candidates import ScoredCandidate
from brain.learning.features import ValueInput
from brain.reward import calculate_reward


@dataclass(frozen=True)
class CounterfactualSelection:
    rule: ScoredCandidate
    model: ScoredCandidate


def build_counterfactual_request(
        selection: CounterfactualSelection,
) -> dict:
    return {
        "type": "counterfactual_request",
        "candidates": [
            {
                "choice": choice,
                "decision": dict(scored.candidate.action),
            }
            for choice, scored in (
                ("rule", selection.rule),
                ("model", selection.model),
            )
        ],
    }


def counterfactual_results(response: dict) -> dict[str, dict]:
    if response.get("type") != "counterfactual_response":
        raise ValueError("Expected a counterfactual response.")

    results = {
        item["choice"]: item
        for item in response.get("results", [])
    }

    if set(results) != {"rule", "model"}:
        raise ValueError(
            "Counterfactual response must contain rule and model results."
        )

    return results


def counterfactual_reward(
        scored: ScoredCandidate,
        result: dict,
        observation: dict,
        memory,
) -> float:
    return counterfactual_action_reward(
        scored.candidate.action,
        result,
        observation,
        memory,
        goal=scored.candidate.goal,
        value_input=scored.value_input,
    )


def counterfactual_action_reward(
        action: dict,
        result: dict,
        observation: dict,
        memory,
        *,
        goal: str | None = None,
        value_input: ValueInput | None = None,
) -> float:
    """Score a simulated action against memory before its observation is applied."""
    target = action.get("target")
    position_before = (observation["position"][0], observation["position"][1])
    position_after = (result["position_after"][0], result["position_after"][1])
    path_length_before = result.get("path_length_before")
    path_length_after = result.get("path_length_after")

    navigation_progress = (
        None
        if path_length_before is None or path_length_after is None
        else path_length_before - path_length_after
    )

    experience = Experience(
        kind="action",
        event=action["action"],
        step=memory.step,
        goal=goal,
        action=action["action"],
        target=None if target is None else (target[0], target[1]),
        position_before=position_before,
        position_after=position_after,
        energy_before=observation["energy"],
        energy_after=result["energy_after"],
        succeeded=result["succeeded"],
        result=result["result"],
        path_length_before=path_length_before,
        memory_trust_before=(
            None if value_input is None else value_input.memory_trust
        ),
        next_step_was_visited=(
            None if value_input is None else value_input.next_step_was_visited
        ),
        reachable_before=(
            None
            if action["action"] != "move_to"
            else result["result"] != "unreachable"
        ),
        visited_new_cell=memory.visit_count(position_after) == 0,
        navigation_progress=navigation_progress,
        outcome=result.get("outcome"),
    )

    return calculate_reward(experience)


def build_single_counterfactual_request(action: dict, *, choice: str) -> dict:
    return {
        "type": "counterfactual_request",
        "candidates": [
            {
                "choice": choice,
                "decision": dict(action),
            }
        ]
    }


def single_counterfactual_result(response: dict, *, choice: str) -> dict:
    if response.get("type") != "counterfactual_response":
        raise ValueError("Expected a counterfactual response.")

    results = response.get("results", [])
    if len(results) != 1:
        raise ValueError(f"Expected exactly one item in 'results', found {len(results)}.")

    result_item = results[0]
    if result_item.get("choice") != choice:
        raise ValueError(f"Result choice '{result_item.get('choice')}' does not match requested choice '{choice}'.")

    return result_item
