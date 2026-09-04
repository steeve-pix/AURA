import json
import sys
from pathlib import Path
from typing import Any

from brain.decision import (
    DecisionProposal,
    choose_local_exploration_action,
    commit_decision,
    decide,
    propose_exploration_decision,
    propose_investigation_decision,
    propose_recharge_decision,
    replan_failed_investigation,
    replan_failed_recharge,
)
from brain.experience import Experience
from brain.experience_store import append_experience, experience_path_for_world
from brain.goals import goal_scores, propose_goal
from brain.learning.disagreement_analysis import DisagreementAnalysis
from brain.learning.counterfactual import (
    CounterfactualSelection,
    build_counterfactual_request,
    counterfactual_results,
    counterfactual_reward,
)
from brain.learning.candidates import (
    decision_key,
    rule_scored_candidate,
    score_candidates,
    select_model_candidate, CandidateDecision, format_decision_proposals,
    propose_decisions,
)
from brain.learning.model_io import load_model
from brain.learning.reporting import LiveValueReporter
from brain.memory_store import load_memory, memory_path_for_world, save_memory
from brain.navigation_preview import (
    build_navigation_preview_request,
    navigation_previews_by_target,
)
from brain.navigation_safety import navigation_decision_is_energy_safe
from brain.perception import update_memory_from_observation
from brain.plan_supervisor import supervise_goal
from brain.planning import plan_debug, update_plan_from_observation

PLAN_FAILED_REWARD = -0.40
REPLAN_REWARD = 0.05
PLAN_COMPLETED_REWARD = 0.75


def persist_experience(experience, memory_directory: Path, world_id: str) -> None:
    append_experience(
        experience,
        experience_path_for_world(memory_directory, world_id),
    )


def update_active_plan_and_record_events(memory, observation: dict) -> list[Experience]:
    active_plan = memory.active_plan

    if active_plan is None:
        return []

    events = []
    update_plan_from_observation(active_plan, observation, current_step=memory.step)

    if active_plan.is_complete():
        events.append(memory.record_plan_event(
            event="plan_completed",
            goal=active_plan.goal,
            target=active_plan.goal_target,
            observation=observation,
            reward=PLAN_COMPLETED_REWARD,
        ))
        memory.clear_active_plan()
        return events

    if not active_plan.has_failed():
        return events

    failed_step = active_plan.current_step()

    if failed_step is not None and failed_step.target is not None:
        memory.mark_target_failed(failed_step.target)

    events.append(memory.record_plan_event(
        event="plan_failed",
        goal=active_plan.goal,
        target=active_plan.goal_target,
        observation=observation,
        reward=PLAN_FAILED_REWARD,
    ))

    replanned = (
            replan_failed_investigation(observation, memory)
            or replan_failed_recharge(observation, memory)
    )

    if not replanned:
        memory.clear_active_plan()
        return events

    replacement_plan = memory.active_plan
    events.append(memory.record_plan_event(
        event="replan",
        goal=replacement_plan.goal,
        target=replacement_plan.goal_target,
        observation=observation,
        reward=REPLAN_REWARD,
    ))
    return events


def score_and_report_value_candidates(
        *, value_model, candidates, observation: dict, memory, goal: str, decision: dict,
        value_reporter: LiveValueReporter,
        navigation_previews: dict | None = None,
        goal_target: tuple[int, int] | None = None,
        disagreement_analysis: DisagreementAnalysis | None = None,
) -> CounterfactualSelection | None:
    if value_model is None or not candidates:
        return None

    scored_candidates = score_candidates(
        value_model,
        candidates,
        observation,
        memory,
        navigation_previews=navigation_previews,
    )
    rule_scored = rule_scored_candidate(
        scored_candidates,
        rule_goal=goal,
        rule_action=decision,
    )
    eligible_candidates = [
        candidate
        for candidate in candidates
        if navigation_decision_is_energy_safe(
            goal=candidate.goal,
            goal_target=(
                goal_target
                if candidate.goal == "recharge"
                else None
            ),
            action=candidate.action,
            observation=observation,
            navigation_previews=navigation_previews or {},
        )
    ]
    eligible_keys = {
        decision_key(candidate.goal, candidate.action)
        for candidate in eligible_candidates
    }
    eligible_scored_candidates = [
        scored
        for scored in scored_candidates
        if decision_key(
            scored.candidate.goal,
            scored.candidate.action,
        ) in eligible_keys
    ]

    value_reporter.report_candidates(
        scored_candidates,
        eligible_keys=eligible_keys,
    )

    if not eligible_scored_candidates:
        memory.pending_value_prediction = rule_scored.predicted_reward
        memory.pending_candidate_comparison = None
        memory.pending_disagreement = None
        return None

    model_scored = select_model_candidate(
        eligible_scored_candidates,
        rule_goal=goal,
        rule_action=decision,
    )
    memory.pending_value_prediction = rule_scored.predicted_reward

    rule_key = decision_key(goal, decision)
    model_key = decision_key(
        model_scored.candidate.goal,
        model_scored.candidate.action,
    )

    if model_key == rule_key:
        memory.pending_candidate_comparison = None
        memory.pending_disagreement = None
        return None

    value_reporter.report_disagreement(
        scored_candidates,
        rule_key=rule_key,
        model_key=model_key,
    )
    memory.pending_candidate_comparison = {
        "rule": rule_key,
        "rule_prediction": rule_scored.predicted_reward,
        "model": model_key,
        "model_prediction": model_scored.predicted_reward,
    }

    if disagreement_analysis is not None:
        memory.pending_disagreement = disagreement_analysis.begin(
            step=memory.step,
            rule_goal=goal,
            rule_action=decision,
            rule_predicted_value=rule_scored.predicted_reward,
            model_goal=model_scored.candidate.goal,
            model_action=model_scored.candidate.action,
            model_predicted_value=model_scored.predicted_reward,
        )

    return CounterfactualSelection(
        rule=rule_scored,
        model=model_scored,
    )


def main() -> None:
    memory_directory = Path("data")

    memory = None
    active_world_id = None
    memory_path = None

    model_path = Path("data/models/value_model_2.pt")
    value_model = None

    if model_path.exists():
        value_model = load_model(model_path)

    value_reporter = LiveValueReporter()
    disagreement_analysis = DisagreementAnalysis()
    pending_preview_cycle = None
    pending_counterfactual_cycle = None

    for raw in sys.stdin:
        raw = raw.strip()

        if not raw:
            continue

        observation: dict[str, Any] = json.loads(raw)

        if observation.get("type") == "counterfactual_response":
            if pending_counterfactual_cycle is None:
                raise ValueError(
                    "Received an unexpected counterfactual response."
                )

            results = counterfactual_results(observation)
            selection = pending_counterfactual_cycle["selection"]
            cached_observation = pending_counterfactual_cycle[
                "observation"
            ]
            rule_reward = counterfactual_reward(
                selection.rule,
                results["rule"],
                cached_observation,
                memory,
            )
            model_reward = counterfactual_reward(
                selection.model,
                results["model"],
                cached_observation,
                memory,
            )

            record = memory.pending_disagreement

            if record is None:
                raise ValueError(
                    "Counterfactual response has no pending disagreement."
                )

            disagreement_analysis.complete_counterfactual(
                record,
                rule_actual_reward=rule_reward,
                model_actual_reward=model_reward,
            )
            value_reporter.report_counterfactual_outcome(
                record,
                disagreement_analysis.statistics(),
            )

            decision_proposal = pending_counterfactual_cycle[
                "decision_proposal"
            ]

            if decision_proposal is not None:
                commit_decision(memory, decision_proposal)

            print(
                json.dumps(
                    pending_counterfactual_cycle["decision"]
                ),
                flush=True,
            )
            pending_counterfactual_cycle = None
            continue

        if observation.get("type") == "preview_response":
            if pending_preview_cycle is None:
                raise ValueError("Received an unexpected preview response.")

            navigation_previews = navigation_previews_by_target(
                pending_preview_cycle["request"],
                observation,
            )

            cached_decision = (
                pending_preview_cycle["decision"]
            )

            cached_observation = (
                pending_preview_cycle["observation"]
            )

            cached_goal = (
                pending_preview_cycle["goal"]
            )

            cached_goal_target = (
                pending_preview_cycle["goal_target"]
            )

            if (
                    cached_decision.get("action")
                    == "move_to"
                    and cached_decision.get("target")
                    is not None
            ):
                target = tuple(
                    cached_decision["target"]
                )

                preview = (
                    navigation_previews.get(target)
                )

                if preview is not None:
                    memory.apply_navigation_preview_to_pending(
                        preview
                    )

            if not navigation_decision_is_energy_safe(
                    goal=cached_goal,
                    goal_target=cached_goal_target,
                    action=cached_decision,
                    observation=cached_observation,
                    navigation_previews=
                    navigation_previews,
            ):
                target = tuple(
                    cached_decision["target"]
                )

                memory.mark_target_failed(target)
                memory.clear_active_plan()
                memory.cancel_pending_experience()

                fallback = (
                    choose_local_exploration_action(
                        cached_observation,
                        memory,
                    )
                )

                memory.begin_experience(
                    goal=cached_goal,
                    action=fallback,
                    observation=cached_observation,
                )

                debug = dict(
                    cached_decision.get(
                        "debug",
                        {},
                    )
                )

                debug["plan"] = None

                debug["navigation_safety"] = {
                    "rejected_target": list(target),
                    "reason":
                        "insufficient_round_trip_energy",
                }

                fallback["debug"] = debug

                print(
                    json.dumps(fallback),
                    flush=True,
                )

                pending_preview_cycle = None
                continue

            counterfactual_selection = score_and_report_value_candidates(
                value_model=value_model,
                candidates=pending_preview_cycle["candidates"],
                observation=pending_preview_cycle["observation"],
                memory=memory,
                goal=pending_preview_cycle["goal"],
                decision=pending_preview_cycle["decision"],
                value_reporter=value_reporter,
                navigation_previews=navigation_previews,
                goal_target=cached_goal_target,
                disagreement_analysis=disagreement_analysis,
            )

            if counterfactual_selection is not None:
                request = build_counterfactual_request(
                    counterfactual_selection
                )
                pending_counterfactual_cycle = {
                    "selection": counterfactual_selection,
                    "observation": cached_observation,
                    "decision": pending_preview_cycle["decision"],
                    "decision_proposal": pending_preview_cycle[
                        "decision_proposal"
                    ],
                }
                pending_preview_cycle = None
                print(json.dumps(request), flush=True)
                continue

            decision_proposal = pending_preview_cycle[
                "decision_proposal"
            ]

            if decision_proposal is not None:
                commit_decision(
                    memory,
                    decision_proposal,
                )

            # Scoring is advisory. The cached rule decision remains authoritative.
            print(
                json.dumps(pending_preview_cycle["decision"]),
                flush=True,
            )
            pending_preview_cycle = None
            continue

        if pending_preview_cycle is not None:
            raise ValueError("Expected a preview response before a new observation.")

        if pending_counterfactual_cycle is not None:
            raise ValueError(
                "Expected a counterfactual response before a new observation."
            )

        world_id = observation["world_id"]

        if memory is None or world_id != active_world_id:
            memory_path = memory_path_for_world(memory_directory, world_id)
            memory = load_memory(memory_path, world_id)
            active_world_id = world_id

        memory.advance_step()

        completed_experience = memory.finish_pending_experience(observation)

        if completed_experience is not None:
            prediction = memory.pending_value_prediction
            candidate_comparison = memory.pending_candidate_comparison
            pending_disagreement = memory.pending_disagreement

            value_reporter.record_completed(
                completed_experience,
                prediction=prediction,
                candidate_comparison=(
                    None
                    if pending_disagreement is not None
                    else candidate_comparison
                ),
            )

            if pending_disagreement is not None:
                completed_disagreement = disagreement_analysis.complete(
                    pending_disagreement,
                    rule_actual_reward=completed_experience.reward,
                )
                value_reporter.report_disagreement_outcome(
                    completed_disagreement,
                    disagreement_analysis.statistics(),
                )

            memory.pending_value_prediction = None
            memory.pending_candidate_comparison = None
            memory.pending_disagreement = None

        if completed_experience is not None:
            persist_experience(completed_experience, memory_directory, world_id)

        update_memory_from_observation(memory, observation)

        plan_events = update_active_plan_and_record_events(
            memory,
            observation,
        )

        for plan_event in plan_events:
            persist_experience(
                plan_event,
                memory_directory,
                world_id,
            )

        save_memory(memory, memory_path, world_id)

        score = goal_scores(observation, memory)
        proposal = propose_goal(observation, memory)

        recharge_urgent_now = proposal.goal_type == "recharge" and proposal.urgency > 0.0

        goal = supervise_goal(memory, proposal=proposal)

        decision_proposal: DecisionProposal | None = None

        if goal == "investigate" and (
                memory.active_plan is None
                or memory.active_plan.goal != "investigate"
        ):
            decision_proposal = propose_investigation_decision(
                observation,
                memory,
            )
            decision = decision_proposal.action
        elif goal == "recharge" and (
                memory.active_plan is None
                or memory.active_plan.goal != "recharge"
        ):
            decision_proposal = propose_recharge_decision(
                observation,
                memory,
            )
            decision = decision_proposal.action
        elif goal == "explore" and (
                memory.active_plan is None
                or memory.active_plan.goal != "explore"
        ):
            decision_proposal = propose_exploration_decision(
                observation,
                memory,
            )
            decision = decision_proposal.action
        else:
            decision = decide(observation, goal, memory)

        plan_is_committed = memory.active_plan is not None

        pending = memory.begin_experience(goal=goal, action=decision, observation=observation)

        if pending is None:
            decision_proposals = []
        else:
            decision_proposals = propose_decisions(
                observation,
                memory,
                rule_goal=goal,
                rule_action=decision,
                recharge_urgent=recharge_urgent_now,
                plan_is_committed=plan_is_committed,
            )

        if pending is None:
            candidates = []
        elif value_model is None:
            # The rule system still needs a C++ preview
            # to validate move_to energy safety.
            candidates = [CandidateDecision(goal=goal, action=dict(decision))]
        else:
            candidates = decision_proposals

        if decision_proposals:
            print(
                format_decision_proposals(
                    decision_proposals,
                    rule_goal=goal,
                    rule_action=decision,
                ),
                file=sys.stderr,
            )

        # Debug metadata shares the response but is never used to execute the action.
        decision["debug"] = {
            "goal": goal,
            "goal_scores": score,
            "goal_proposal": {
                "type": proposal.goal_type,
                "target": (None if proposal.target is None else list(proposal.target)),
                "score": proposal.score,
                "urgency": proposal.urgency,
                "reason": proposal.reason,
            },
            "known_cells": [
                list(position) for position in memory.known_cells.keys()
            ],
            "visited_cells": [
                list(position) for position in memory.visit_counts.keys()
            ],
            "plan": plan_debug(
                memory.active_plan
                if decision_proposal is None
                else decision_proposal.plan
            ),
            "failures": memory.failure_debug(),
        }

        preview_request = build_navigation_preview_request(
            candidates
        )

        if preview_request is None:
            counterfactual_selection = score_and_report_value_candidates(
                value_model=value_model,
                candidates=candidates,
                observation=observation,
                memory=memory,
                goal=goal,
                decision=decision,
                value_reporter=value_reporter,
                goal_target=(
                    memory.active_plan.goal_target
                    if memory.active_plan is not None
                    else (
                        None
                        if decision_proposal is None
                        or decision_proposal.plan is None
                        else decision_proposal.plan.goal_target
                    )
                ),
                disagreement_analysis=disagreement_analysis,
            )

            if counterfactual_selection is not None:
                pending_counterfactual_cycle = {
                    "selection": counterfactual_selection,
                    "observation": observation,
                    "decision": decision,
                    "decision_proposal": decision_proposal,
                }
                print(
                    json.dumps(
                        build_counterfactual_request(
                            counterfactual_selection
                        )
                    ),
                    flush=True,
                )
                continue

            if decision_proposal is not None:
                commit_decision(
                    memory,
                    decision_proposal,
                )

            print(json.dumps(decision), flush=True)
            continue

        pending_preview_cycle = {
            "request": preview_request,
            "decision": decision,
            "candidates": candidates,
            "observation": observation,
            "goal": goal,
            "decision_proposal": decision_proposal,
            "goal_target": (
                memory.active_plan.goal_target
                if memory.active_plan is not None
                else (
                    None
                    if decision_proposal is None
                    or decision_proposal.plan is None
                    else decision_proposal.plan.goal_target
                )
            ),
        }
        print(json.dumps(preview_request), flush=True)


if __name__ == "__main__":
    main()
