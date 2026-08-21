import json
import re
from pathlib import Path

from brain.memory import Memory
from brain.world_memory import RememberedEntity


def memory_path_for_world(directory: Path, world_id: str) -> Path:
    if not world_id:
        raise ValueError("Cannot persist memory without a world_id")

    filename = re.sub(r"[^A-Za-z0-9._-]+", "_", world_id).strip("_")
    return directory / "world_memory" / f"{filename}.json"


def save_memory(memory: Memory, path: Path, world_id: str) -> None:
    data = {
        "world_id": world_id,
        "step": memory.step,
        "entities": [{
            "position": list(entity.position),
            "entity_type": entity.entity_type,
            "status": entity.status,
            "last_seen_step": entity.last_seen_step,
            "times_confirmed": entity.times_confirmed,
        } for entity in memory.world_memory.entities.values()],

        "investigation_history": [{
            "position": list(position),
            "revealed_type": reveal_type,
        } for position, reveal_type in memory.investigation_history.items()],

        "known_cells": [{
            "position": list(position),
            "type": cell_type,
        } for position, cell_type in memory.known_cells.items()],

        "visit_counts": [{
            "position": list(position),
            "count": count,
        } for position, count in memory.visit_counts.items()],
    }
    text = json.dumps(data, indent=2)

    text = re.sub(r'\[\s*(-?\d+),\s*(-?\d+)\s*]', r'[\1,\2]', text)

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def load_memory(path: Path, world_id: str) -> Memory:
    memory = Memory()

    if not path.exists():
        return memory

    text = path.read_text().strip()

    if not text:
        return memory

    data = json.loads(text)

    if data.get("world_id") != world_id:
        return memory

    memory.step = int(data.get("step", 0))

    entity_items = data.get("entities")
    if entity_items is None:
        entity_items = []

        for battery in data.get("known_batteries", []):
            if isinstance(battery, dict):
                entity_items.append({
                    "position": battery["position"],
                    "entity_type": "Battery",
                    "status": battery.get("status", "confirmed"),
                    "last_seen_step": battery.get("last_seen_step", 0),
                    "times_confirmed": battery.get(
                        "times_confirmed",
                        battery.get("time_confirmed", 1),
                    ),
                })
            else:
                entity_items.append({
                    "position": battery,
                    "entity_type": "Battery",
                })

    for item in entity_items:
        x, y = item["position"]
        entity = RememberedEntity(
            position=(int(x), int(y)),
            entity_type=item["entity_type"],
            status=item.get("status", "confirmed"),
            last_seen_step=int(item.get("last_seen_step", 0)),
            times_confirmed=int(
                item.get("times_confirmed", item.get("time_confirmed", 1))
            ),
        )

        memory.world_memory.restore_entity(entity)

    for item in data.get("investigation_history", []):
        memory.remember_investigation_result(item["position"], item["revealed_type"])

    for cell in data.get("known_cells", []):
        x, y = cell["position"]
        position = (int(x), int(y))

        memory.known_cells[position] = cell["type"]

    for visit in data.get("visit_counts", []):
        x, y = visit["position"]
        position = (int(x), int(y))

        memory.visit_counts[position] = visit["count"]

    return memory
