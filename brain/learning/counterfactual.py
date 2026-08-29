from dataclasses import dataclass

from brain.experience import Experience
from brain.learning.candidates import ScoredCandidate
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
    action = scored.candidate.action
    target = action.get("target")
    position_before = tuple(observation["position"])
    position_after = tuple(result["position_after"])
    path_length_before = result.get("path_length_before")
    path_length_after = result.get("path_length_after")

    navigation_progress = (
        None
        if path_length_before is None or path_length_after is None
        else path_length_before - path_length_after
    )

    value_input = scored.value_input
    experience = Experience(
        kind="action",
        event=action["action"],
        step=memory.step,
        goal=scored.candidate.goal,
        action=action["action"],
        target=None if target is None else tuple(target),
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
