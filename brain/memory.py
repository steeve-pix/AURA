from typing import Optional, Sequence, Literal
from dataclasses import dataclass
from brain.world_memory import WorldMemory
from brain.planning import Plan
from brain.experience import (
    Experience,
    RESULT_COMPLETED,
    RESULT_FAILED,
    detect_outcome,
)
from brain.reward import calculate_reward


class Memory:
    def __init__(self) -> None:
        self.known_cells: dict[tuple[int, int], str] = {}
        self.visit_counts: dict[tuple[int, int], int] = {}
        self.failed_targets: set[tuple[int, int]] = set()
        self.active_goal: Optional[str] = None
        self.step = 0
        self.investigation_history: dict[tuple[int, int], str] = {}
        self.world_memory: WorldMemory = WorldMemory()
        self.active_plan: Plan | None = None
        self.experiences: list[Experience] = []
        self.pending_experience: dict | None = None
        self.pending_value_prediction: float | None = None
        self.plan_failure_count = 0
        self.replan_count = 0
        self.body_action_failure_count = 0

    def remember_cell(self, position: list[int], cell_type: str) -> None:
        self.known_cells[(position[0], position[1])] = cell_type

    def remember_entity(self, position: list[int] | tuple[int, int], entity_type: str, ) -> None:
        self.world_memory.remember_entity(
            position=position,
            entity_type=entity_type,
            step=self.step,
        )

    def remember_battery(self, position: list[int] | tuple[int, int]) -> None:
        self.remember_entity(position, "Battery")

    def mark_entity_stale(self, position: list[int] | tuple[int, int], ) -> None:
        self.world_memory.mark_stale(position)

    def forget_battery(self, position: tuple[int, int]) -> None:
        self.mark_entity_stale(position)

    def batteries(self) -> list[tuple[int, int]]:
        return [
            entity.position for entity in self.world_memory.entities_of_type("Battery")
        ]

    def record_visit(self, position: list[int]) -> None:
        key = (position[0], position[1])

        self.visit_counts[key] = self.visit_counts.get(key, 0) + 1

    def visit_count(self, position: tuple[int, int]) -> int:
        return self.visit_counts.get(position, 0)

    def mark_target_failed(self, position: Sequence[int]) -> None:
        key = (position[0], position[1])
        self.failed_targets.add(key)

    def record_failed_target(self, position: Sequence[int]) -> None:
        self.mark_target_failed(position)

    def failed_target_count(self, position: tuple[int, int]) -> int:
        return 1 if position in self.failed_targets else 0

    def is_failed_target(self, position: tuple[int, int]) -> bool:
        return self.failed_target_count(position) >= 1

    def least_visited_position(self) -> Optional[tuple[int, int]]:
        walkable_positions = [
            position
            for position, cell_type in self.known_cells.items()
            if cell_type != "Wall" and not self.is_failed_target(position)
        ]

        if not walkable_positions:
            return None

        return min(
            walkable_positions,
            key=lambda position: (self.visit_count(position), position),
        )

    def set_active_goal(self, goal: str) -> None:
        self.active_goal = goal

    def clear_active_goal(self) -> None:
        self.active_goal = None

    def mark_battery_stale(self, position: tuple[int, int]) -> None:
        self.mark_entity_stale(position)

    def advance_step(self) -> None:
        self.step += 1

    def battery_trust(self, position: tuple[int, int]) -> float:
        if not self.world_memory.has_entity(position, "Battery"):
            return 0.0
        entity = self.world_memory.entity_at(position)

        if entity is None:
            return 0.0

        age = max(0, self.step - entity.last_seen_step)

        recency = 1.0 / (1.0 + age * 0.05)

        confirmation = min(1.0, 0.5 + entity.times_confirmed * 0.1)

        return recency * confirmation

    def remember_investigation_result(self, position: list[int] | tuple[int, int], revealed_type: str) -> None:
        self.investigation_history[(position[0], position[1])] = revealed_type

    def previous_investigation_result(self, position: list[int] | tuple[int, int]) -> str | None:
        return self.investigation_history.get((position[0], position[1]))

    def remember_unknown(self, position: list[int] | tuple[int, int]) -> None:
        self.remember_entity(position, "Unknown")

    def unknowns(self) -> list[tuple[int, int]]:
        return [
            entity.position for entity in self.world_memory.entities_of_type("Unknown")
        ]

    def set_active_plan(self, plan: Plan) -> None:
        self.active_plan = plan

    def clear_active_plan(self) -> None:
        self.active_plan = None

    def clear_plan(self) -> None:
        self.clear_active_plan()

    def record_experience(self, experience: Experience) -> None:
        self.experiences.append(experience)

        if experience.kind == "action" and not experience.succeeded:
            self.body_action_failure_count += 1

    def record_plan_event(
            self,
            *,
            event: str,
            goal: str,
            target: tuple[int, int] | None,
            observation: dict,
            reward: float,
    ) -> Experience:
        position = (observation["position"][0], observation["position"][1])
        energy = observation["energy"]
        experience = Experience(
            step=self.step,
            kind="plan",
            event=event,
            goal=goal,
            action="",
            target=target,
            position_before=position,
            position_after=position,
            energy_before=energy,
            energy_after=energy,
            succeeded=event != "plan_failed",
            result=event,
            visited_new_cell=False,
            navigation_progress=None,
            outcome=None,
            reward=reward,
        )
        self.record_experience(experience)
        return experience

    def record_plan_failure(self) -> None:
        self.plan_failure_count += 1

    def record_replan(self) -> None:
        self.replan_count += 1

    def failure_debug(self) -> dict[str, int]:
        return {
            "plan_failures": self.plan_failure_count,
            "replans": self.replan_count,
            "failed_targets": len(self.failed_targets),
            "body_action_failures": self.body_action_failure_count,
        }

    def begin_experience(self, *, goal: str, action: dict, observation: dict) -> dict | None:
        if action["action"] == "idle":
            self.pending_experience = None
            return None

        target = action.get("target")
        target_position = (
            None if target is None else (target[0], target[1])
        )

        current_position = tuple(observation["position"])
        memory_trust_before = None

        if (
                target_position is not None
                and self.world_memory.has_entity(
            target_position,
            "Battery",
        )
        ):
            memory_trust_before = self.battery_trust(
                target_position
            )

        self.pending_experience = {
            "step": self.step,
            "goal": goal,
            "action": action["action"],
            "target": target_position,
            "position_before": current_position,
            "energy_before": observation["energy"],
            "visited_before": set(self.visit_counts.keys()),
            "memory_trust_before": memory_trust_before,
        }

        return self.pending_experience

    def finish_pending_experience(
            self,
            observation: dict,
    ) -> Experience | None:
        if self.pending_experience is None:
            return None

        last_action = observation.get("last_action")

        if last_action is None:
            return None

        pending = self.pending_experience

        if last_action.get("type") != pending["action"]:
            return None

        reported_target = last_action.get("target")

        if (
                pending["target"] is not None
                and (
                reported_target is None
                or tuple(reported_target) != pending["target"]
        )
        ):
            return None

        outcome = detect_outcome(pending, observation)
        succeeded = last_action.get("succeeded", False)
        result = last_action.get(
            "result",
            RESULT_COMPLETED if succeeded else RESULT_FAILED,
        )
        target = pending["target"]
        position_after = (observation["position"][0], observation["position"][1])
        visited_new_cell = (
                position_after not in pending["visited_before"]
        )
        path_length_before = last_action.get("path_length_before")
        path_length_after = last_action.get("path_length_after")
        navigation_progress = (
            None
            if path_length_before is None or path_length_after is None
            else path_length_before - path_length_after
        )

        experience = Experience(
            step=pending["step"],
            kind="action",
            event=pending["action"],
            goal=pending["goal"],
            action=pending["action"],
            target=target,
            position_before=pending["position_before"],
            position_after=position_after,
            energy_before=pending["energy_before"],
            energy_after=observation["energy"],
            succeeded=succeeded,
            result=result,
            path_length_before=path_length_before,
            memory_trust_before=pending["memory_trust_before"],
            visited_new_cell=visited_new_cell,
            navigation_progress=navigation_progress,
            outcome=outcome,
        )

        experience.reward = calculate_reward(experience)

        self.record_experience(experience)

        self.pending_experience = None
        return experience


MemoryStatus = Literal[
    "confirmed",
    "stale",
]


@dataclass
class BatteryMemory:
    position: tuple[int, int]
    status: MemoryStatus = "confirmed"
    last_seen_step: int = 0
    time_confirmed: int = 1
