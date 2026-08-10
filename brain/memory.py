from typing import Optional


class Memory:
    def __init__(self) -> None:
        self.known_cells: dict[tuple[int, int], str] = {}
        self.known_batteries: set[tuple[int, int]] = set()
        self.visit_counts: dict[tuple[int, int], int] = {}

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

    def least_visited_position(self) -> Optional[tuple[int, int]]:
        walkable_positions = [
            position
            for position, cell_type in self.known_cells.items()
            if cell_type != "Wall"
        ]

        if not walkable_positions:
            return None

        return min(
            walkable_positions,
            key=lambda position: (self.visit_count(position), position),
        )
