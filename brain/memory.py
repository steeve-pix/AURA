from typing import Optional, Sequence, Literal
from dataclasses import dataclass
from brain.world_memory import WorldMemory
from brain.planning import Plan
from brain.experience import Experience, detect_outcome
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
        self.investigation_history[tuple(position)] = revealed_type

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

    def begin_experience(self, *, goal: str, action: dict, observation: dict) -> None:
        if action["action"] == "idle":
            self.pending_experience = None
            return

        target = action.get("target")

        self.pending_experience = {
            "step": self.step,
            "goal": goal,
            "action": action["action"],
            "target": (
                None if target is None else tuple(target)
            ),
            "position_before": tuple(observation["position"]),
            "energy_before": observation["energy"],
        }

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

        experience = Experience(
            step=pending["step"],
            goal=pending["goal"],
            action=pending["action"],
            target=pending["target"],
            position_before=pending["position_before"],
            position_after=tuple(observation["position"]),
            energy_before=pending["energy_before"],
            energy_after=observation["energy"],
            succeeded=last_action.get("succeeded", False),
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
