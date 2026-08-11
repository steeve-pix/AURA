import json
import sys
from typing import Any

from decision import decide
from goals import choose_goal
from memory import Memory


def main() -> None:
    memory = Memory()

    for raw in sys.stdin:
        raw = raw.strip()

        if not raw:
            continue

        observation: dict[str, Any] = json.loads(raw)

        last_action = observation.get("last_action")

        if last_action is not None and last_action["type"] == "move_to" and not last_action["succeeded"]:
            memory.record_failed_target(last_action["target"])

        for visible_cell in observation["visible_cells"]:
            memory.remember_cell(
                visible_cell["position"],
                visible_cell["type"],
            )

        memory.record_visit(observation["position"])

        for visible_object in observation["nearby_objects"]:
            if visible_object["type"] == "Battery":
                memory.remember_battery(visible_object["position"])

        visible_batteries = {
            tuple(obj["position"]) for obj in observation["nearby_objects"] if obj["type"] == "Battery"
        }

        aura_x, aura_y = observation["position"]
        sensor_radius = observation["sensor_radius"]

        for battery in memory.batteries():
            battery_x, battery_y = battery

            within_sensor_range = (
                    abs(battery_x - aura_x) <= sensor_radius and abs(battery_y - aura_y) <= sensor_radius
            )

            if (within_sensor_range and battery not in visible_batteries):
                memory.forget_battery(
                    battery
                )

        goal = choose_goal(observation)

        decision = decide(observation, goal, memory)

        decision["debug"] = {
            "goal": goal,
            "known_cells": [
                list(position) for position in memory.known_cells.keys()
            ],
            "visited_cells": [
                list(position) for position in memory.visit_counts.keys()
            ],
        }

        print(json.dumps(decision), flush=True)


if __name__ == "__main__":
    main()
