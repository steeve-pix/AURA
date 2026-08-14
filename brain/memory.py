from __future__ import annotations
from typing import Optional, Sequence, Tuple, Literal
from dataclasses import dataclass


class Memory:
    def __init__(self) -> None:
        self.known_cells: dict[tuple[int, int], str] = {}
        self.known_batteries: dict[tuple[int, int], BatteryMemory] = {}
        self.visit_counts: dict[tuple[int, int], int] = {}
        self.failed_targets: set[tuple[int, int]] = set()
        self.active_recharge_target: Optional[Tuple[int, int]] = None
        self.active_investigation_target: Optional[Tuple[int, int]] = None
        self.active_investigation_approach: Optional[Tuple[int, int]] = None
        self.active_goal: Optional[str] = None

    def remember_cell(self, position: list[int], cell_type: str) -> None:
        self.known_cells[(position[0], position[1])] = cell_type

    def remember_battery(self, position: list[int]) -> None:
        key = (position[0], position[1])

        self.known_batteries[key] = BatteryMemory(position=key, status="confirmed")

    def forget_battery(self, position: tuple[int, int]) -> None:
        self.known_batteries.pop(position, None)

    def batteries(self) -> list[tuple[int, int]]:
        return [
            memory.position for memory in self.known_batteries.values() if memory.status == "confirmed"
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

    def set_recharge_target(self, position) -> None:
        self.active_recharge_target = (position[0], position[1])

    def clear_recharge_target(self) -> None:
        self.active_recharge_target = None

    def set_investigation_target(self, position: Sequence[int]) -> None:
        target = (position[0], position[1])
        # An approach is valid only for the unknown object that selected it.
        if target != self.active_investigation_target:
            self.active_investigation_approach = None
        self.active_investigation_target = target

    def set_investigation_approach(self, position: Sequence[int]) -> None:
        self.active_investigation_approach = (position[0], position[1])

    def clear_investigation_approach(self) -> None:
        self.active_investigation_approach = None

    def clear_investigation_target(self) -> None:
        # Target and approach form one lock and must be released together.
        self.active_investigation_target = None
        self.active_investigation_approach = None

    def set_active_goal(self, goal: str) -> None:
        self.active_goal = goal

    def clear_active_goal(self) -> None:
        self.active_goal = None

    def mark_battery_stale(self, position: tuple[int, int]) -> None:
        memory = self.known_batteries.get(position)

        if memory is not None:
            memory.status = "stale"


MemoryStatus = Literal[
    "confirmed",
    "stale",
]


@dataclass
class BatteryMemory:
    position: tuple[int, int]
    status: MemoryStatus = "confirmed"
