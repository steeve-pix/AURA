from dataclasses import dataclass
from typing import Literal

EntityStatus = Literal[
    "confirmed",
    "stale",
]


@dataclass
class RememberedEntity:
    position: tuple[int, int]
    entity_type: str
    status: EntityStatus = "confirmed"
    last_seen_step: int = 0
    times_confirmed: int = 1


class WorldMemory:
    def __init__(self):
        self.entities: dict[tuple[int, int], RememberedEntity] = {}

    def remember_entity(self, position: list[int] | tuple[int, int], entity_type: str, step: int) -> None:
        key = (position[0], position[1])

        existing = self.entities.get(key)

        if existing is None:
            self.entities[key] = RememberedEntity(position=key, entity_type=entity_type, status="confirmed",
                                                  last_seen_step=step, times_confirmed=1)
            return

        existing.entity_type = entity_type
        existing.status = "confirmed"
        existing.last_seen_step = step
        existing.times_confirmed += 1

    def mark_stale(self, position: list[int] | tuple[int, int]) -> None:
        entity = self.entities.get((position[0], position[1]))

        if entity is not None:
            entity.status = "stale"

    def entity_at(self, position: list[int] | tuple[int, int]) -> RememberedEntity | None:
        return self.entities.get((position[0], position[1]))

    def restore_entity(self, entity: RememberedEntity) -> None:
        self.entities[entity.position] = entity

    def entities_of_type(self, entity_type: str, *, confirmed_only: bool = True) -> list[RememberedEntity]:
        entities = [entity for entity in self.entities.values() if entity.entity_type == entity_type]

        if confirmed_only:
            entities = [entity for entity in entities if entity.status == "confirmed"]

        return entities

    def has_entity(self, position: list[int] | tuple[int, int], entity_type: str, *,
                   confirmed_only: bool = True) -> bool:
        entity = self.entity_at(position)

        if entity is None:
            return False

        if entity.entity_type != entity_type:
            return False

        if confirmed_only and not entity.status == "confirmed":
            return False

        return True
