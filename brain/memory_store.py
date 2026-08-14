import json
import re
from pathlib import Path

from brain.memory import Memory, BatteryMemory


def memory_path_for_world(directory: Path, world_id: str) -> Path:
    if not world_id:
        raise ValueError("Cannot persist memory without a world_id")

    filename = re.sub(r"[^A-Za-z0-9._-]+", "_", world_id).strip("_")
    return directory / f"{filename}.json"


def save_memory(memory: Memory, path: Path, world_id: str) -> None:
    data = {
        "world_id": world_id,
        "step": memory.step,
        "known_batteries": [{
            "position": list(battery_memory.position),
            "status": battery_memory.status,
            "last_seen_step": battery_memory.last_seen_step,
            "time_confirmed": battery_memory.time_confirmed,
        } for battery_memory in memory.known_batteries.values()],

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

    for item in data.get("known_batteries", []):
        x, y = item["position"]
        key = (x, y)

        memory.known_batteries[key] = BatteryMemory(position=key, status=item.get("status", "confirmed"),
                                                    last_seen_step=item.get("last_seen_step", 0),
                                                    time_confirmed=item.get("time_confirmed", 1))

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
