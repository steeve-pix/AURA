from brain.memory import Memory


def update_memory_from_observation(
    memory: Memory,
    observation: dict,
) -> None:
    last_action = observation.get("last_action")

    # Failed destinations are excluded from later planning so the brain cannot
    # alternate forever between equivalent approaches to the same obstacle.
    if (last_action and last_action.get("type")
            in {"move_to", "investigate"} and not
            last_action.get("succeeded", False)
            and last_action.get("target") is not None):
        target = tuple(last_action["target"])
        memory.mark_target_failed(target)

    if last_action and last_action.get("type") == "investigate" and last_action.get("succeeded", False):
        x, y = last_action["target"]
        target = (x, y)

        revealed_cell = next((cell for cell in observation["visible_cells"] if tuple(cell["position"]) == target),
                             None, )

        if revealed_cell is not None:
            memory.remember_investigation_result(target, revealed_cell["type"])

    for visible_cell in observation["visible_cells"]:
        memory.remember_cell(
            visible_cell["position"],
            visible_cell["type"],
        )

    memory.record_visit(observation["position"])

    for visible_object in observation["nearby_objects"]:
        memory.remember_entity(
            visible_object["position"],
            visible_object["type"],
        )

    # Sensor truth supersedes remembered batteries when a previously known coordinate
    # is inside the current scan but no longer contains a battery.
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

        if within_sensor_range and battery not in visible_batteries:
            memory.forget_battery(
                battery
            )
