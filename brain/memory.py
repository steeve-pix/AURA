class Memory:
    def __init__(self) -> None:
        self.known_batteries: set[tuple[int, int]] = set()
        self.vist_counts: dict[tuple[int, int], int] = {}

    def remember_battery(self, position: list[int]) -> None:
        self.known_batteries.add(tuple(position))

    def forget_battery(self, position: tuple[int, int]) -> None:
        self.known_batteries.discard(position)

    def batteries(self) -> list[tuple[int, int]]:
        return list(self.known_batteries)

    def record_visit(self, position: list[int]) -> None:
        key = tuple(position)

        self.vist_counts[key] = (self.vist_counts.get(key, 0) + 1)

    def visit_count(self, position: tuple[int, int]) -> int:
        return self.vist_counts.get(position, 0)
