from brain.goals import shortest_battery_path, ENERGY_RESERVE, BATTERY_ARRIVAL_RESERVE


def exploration_route_is_energy_safe(observation: dict, preview: dict) -> bool:
    if not preview.get("reachable", False):
        return False

    route_to_frontier: int | None = preview.get("path_length")

    if route_to_frontier is None:
        return False

    route_to_battery = shortest_battery_path(observation)

    if route_to_battery is None:
        return False

    required_energy = route_to_frontier * 2 + route_to_battery + ENERGY_RESERVE

    return required_energy <= observation["energy"]


def exploration_decision_is_energy_safe(*, goal: str, action: dict, observation: dict,
                                        navigation_previews: dict) -> bool:
    if goal != "explore" or action.get("action") != "move_to":
        return True

    target = action.get("target")

    if target is None:
        return False

    preview = navigation_previews.get((target[0], target[1]))

    if preview is None:
        return False

    return exploration_route_is_energy_safe(observation, preview)


def recharge_route_is_energy_safe(observation: dict, preview: dict) -> bool:
    if not preview.get("reachable", False):
        return False

    path_length =  preview.get("path_length")

    if path_length is None:
        return False

    required_energy = path_length+BATTERY_ARRIVAL_RESERVE

    return required_energy <= observation["energy"]


def navigation_decision_is_energy_safe(
        *,
        goal: str,
        goal_target: tuple[int, int] | None,
        action: dict,
        observation: dict,
        navigation_previews: dict,
) -> bool:
    if action.get("action") != "move_to":
        return True

    target = action.get("target")

    if target is None:
        return False

    preview = navigation_previews.get(
        tuple(target)
    )

    if preview is None:
        return False

    if not preview.get("reachable", False):
        return False

    if goal == "explore":
        return exploration_route_is_energy_safe(
            observation,
            preview,
        )

    if goal == "recharge":
        if goal_target is None:
            return exploration_route_is_energy_safe(
                observation,
                preview,
            )

        return recharge_route_is_energy_safe(
            observation,
            preview,
        )

    return True
