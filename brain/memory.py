from __future__ import annotations
from typing import Optional, Sequence, Tuple


class Memory:
    def __init__(self) -> None:
        self.known_cells: dict[tuple[int, int], str] = {}
        self.known_batteries: set[tuple[int, int]] = set()
        self.visit_counts: dict[tuple[int, int], int] = {}
        self.failed_targets: set[tuple[int, int]] = set()
        self.active_recharge_target: Optional[Tuple[int, int]] = None

    def remember_cell(self, position: list[int], cell_type: str) -> None:
        self.known_cells[tuple(position)] = cell_type

    def remember_battery(self, position: list[int]) -> None:
        self.known_batteries.add(tuple(position))

    def forget_battery(self, position: tuple[int, int]) -> None:
        self.known_batteries.discard(position)

    def batteries(self) -> list[tuple[int, int]]:
        return list(self.known_batteries)

    def record_visit(self, position: list[int]) -> None:
        key = tuple(position)

        self.visit_counts[key] = self.visit_counts.get(key, 0) + 1

    def visit_count(self, position: tuple[int, int]) -> int:
        return self.visit_counts.get(position, 0)

    def mark_target_failed(self, position: Sequence[int]) -> None:
        key = tuple(position)
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
        self.active_recharge_target = tuple(position)

    def clear_recharge_target(self) -> None:
        self.active_recharge_target = None
