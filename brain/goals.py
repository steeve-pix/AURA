from brain import memory

ENERGY_RESERVE = 5
RECHARGE_CONSIDERATION_THRESHOLD = 70


def recharge_score(observation):
    energy = observation["energy"]

    shortest_path = shortest_battery_path(observation)

    if shortest_path is not None and energy <= RECHARGE_CONSIDERATION_THRESHOLD:
        required_energy = shortest_path + ENERGY_RESERVE

        if energy <= required_energy:
            return 1.0

    return 1.0 - energy / 100.0


def explore_score(observation, memory):
    current = tuple(observation["position"])

    visits = memory.visit_count(current)

    base = 0.30
    repetition_bonus = min(visits * 0.05, 0.30)

    return base + repetition_bonus


def investigation_score(observation, memory):
    unknown_objects = [
        obj for obj in observation["nearby_objects"]
        if obj["type"] == "Unknown"
        and not memory.is_failed_target(tuple(obj["position"]))
    ]

    if not unknown_objects:
        return 0.0

    return 0.65


def goal_scores(observation, memory):
    return {
        "recharge": recharge_score(observation),
        "explore": explore_score(observation, memory),
        "investigate": investigation_score(observation, memory),
    }


GOAL_SWITCH_MARGIN = 0.1
CRITICAL_ENERGY = 8


def choose_goal(observation, memory):
    current_goal = memory.active_goal

    if current_goal is not None and goal_completed(current_goal, observation, memory):
        if current_goal == "investigate":
            memory.clear_investigation_target()
        memory.clear_active_goal()
        current_goal = None

    energy = observation["energy"]

    if energy <= CRITICAL_ENERGY:
        memory.clear_investigation_target()
        memory.set_active_goal("recharge")
        return "recharge"

    # Once recharge wins, do not allow another goal to interrupt it.
    if memory.active_goal == "recharge" and energy < 100:
        return "recharge"

    scores = goal_scores(observation, memory)
    best_goal = max(scores, key=lambda goal: scores[goal])
    current_goal = memory.active_goal

    if current_goal is None:
        memory.set_active_goal(best_goal)
        return best_goal

    current_score = scores.get(current_goal, 0.0)
    best_score = scores[best_goal]

    if best_goal == current_goal:
        return current_goal

    if best_score >= current_score + GOAL_SWITCH_MARGIN:
        if current_goal == "investigate":
            memory.clear_investigation_target()
        memory.set_active_goal(best_goal)
        return best_goal

    return current_goal


def shortest_battery_path(observation):
    path_lengths = [
        obj["path_length"] for obj in observation["nearby_objects"] if
        (obj["type"] == "Battery" and obj["reachable"] and obj["path_length"] >= 0)
    ]

    if not path_lengths:
        return None

    return min(path_lengths)


def goal_completed(goal, observation, memory):
    if goal == "recharge":
        return observation["energy"] >= 100

    if goal == "investigate":
        target = memory.active_investigation_target
        if target is None:
            return not any(
                obj["type"] == "Unknown"
                and not memory.is_failed_target(tuple(obj["position"]))
                for obj in observation["nearby_objects"]
            )

        target_is_still_unknown = any(
            obj["type"] == "Unknown"
            and tuple(obj["position"]) == target
            for obj in observation["nearby_objects"]
        )

        return not target_is_still_unknown

    if goal == "explore":
        return False

    return True
