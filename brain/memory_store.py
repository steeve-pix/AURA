import json
import re
from pathlib import Path

from brain.memory import Memory


def save_memory(memory: Memory, path: Path) -> None:
    data = {
        "known_batteries": [
            list(position) for position in memory.batteries()
        ],

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


def load_memory(path: Path) -> Memory:
    memory = Memory()

    if not path.exists():
        return memory

    text = path.read_text().strip()

    if not text:
        return memory

    data = json.loads(path.read_text())

    for position in data.get("known_batteries", []):
        memory.remember_battery(position)

    for cell in data.get("known_cells", []):
        x, y = cell["position"]
        position = (int(x), int(y))

        memory.known_cells[position] = cell["type"]

    for visit in data.get("visit_counts", []):
        x, y = visit["position"]
        position = (int(x), int(y))

        memory.visit_counts[position] = visit["count"]

    return memory


