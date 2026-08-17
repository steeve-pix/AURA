import json
from collections import Counter
from pathlib import Path

from brain.experience import Experience, RESULT_COMPLETED, RESULT_FAILED


def load_experiences(path: Path) -> list[Experience]:
    experiences = []

    if not path.exists():
        return experiences

    with path.open(
            "r",
            encoding="utf-8",
    ) as file:
        for line in file:
            line = line.strip()

            if not line:
                continue

            data = json.loads(line)

            experiences.append(
                Experience(
                    step=data["step"],
                    kind=data.get("kind", "action"),
                    event=data.get("event", data["action"]),
                    goal=data["goal"],
                    action=data["action"],
                    target=(None if data["target"] is None else (data["target"][0], data["target"][1])),
                    position_before=(data["position_before"][0], data["position_before"][1]),
                    position_after=(data["position_after"][0], data["position_after"][1]),
                    energy_before=data["energy_before"],
                    energy_after=data["energy_after"],
                    succeeded=data["succeeded"],
                    result=data.get(
                        "result",
                        RESULT_COMPLETED if data["succeeded"] else RESULT_FAILED,
                    ),
                    visited_new_cell=data.get(
                        "visited_new_cell",
                        data.get("discovered_new_cell", False),
                    ),
                    navigation_progress=data.get("navigation_progress"),
                    outcome=data.get("outcome"),
                    reward=data.get("reward", 0.0),
                )
            )

    return experiences


def success_rate(experiences: list[Experience]) -> float:
    if not experiences:
        return 0.0

    successes = sum(experience.succeeded for experience in experiences)

    return successes / len(experiences)

def average_reward(experiences: list[Experience]) -> float:
    if not experiences:
        return 0.0

    return sum(experience.reward for experience in experiences) / len(experiences)


def movement_counts(experiences: list[Experience]) -> tuple[int, int]:
    movements = [
        experience
        for experience in experiences
        if (
            experience.kind == "action"
            and experience.action in {"move", "move_to"}
        )
    ]
    new_cells = sum(
        experience.visited_new_cell
        for experience in movements
    )
    revisited_cells = sum(
        not experience.visited_new_cell
        for experience in movements
    )

    return new_cells, revisited_cells


def navigation_progress_counts(experiences: list[Experience],) -> tuple[int, int, int]:
    progress_values = [
        experience.navigation_progress
        for experience in experiences
        if experience.navigation_progress is not None
    ]
    positive = sum(progress > 0 for progress in progress_values)
    zero = sum(progress == 0 for progress in progress_values)
    negative = sum(progress < 0 for progress in progress_values)

    return positive, zero, negative


def average_navigation_progress(experiences: list[Experience],) -> float | None:
    progress_values = [
        experience.navigation_progress
        for experience in experiences
        if experience.navigation_progress is not None
    ]

    if not progress_values:
        return None

    return sum(progress_values) / len(progress_values)


def result_distribution(experiences: list[Experience]) -> Counter[str]:
    return Counter(
        experience.result
        for experience in experiences
        if experience.kind == "action"
    )


def reward_distribution(experiences: list[Experience]) -> Counter[float]:
    return Counter(round(experience.reward, 2) for experience in experiences)


def body_action_failure_count(experiences: list[Experience]) -> int:
    return sum(
        experience.kind == "action" and not experience.succeeded
        for experience in experiences
    )


def experience_kind_distribution(
        experiences: list[Experience],
) -> Counter[str]:
    return Counter(experience.kind for experience in experiences)


def plan_event_distribution(experiences: list[Experience],) -> Counter[str]:
    return Counter(
        experience.event
        for experience in experiences
        if experience.kind == "plan"
    )


def print_distribution(title: str, distribution: Counter) -> None:
    print(f"\n{title}:")

    for value, count in distribution.items():
        print(f"{value}: {count}")

if __name__ == "__main__":
    import sys

    path = Path(sys.argv[1])

    experiences = load_experiences(path)

    print(f"Experiences: {len(experiences)}")
    print(f"Success rate: {success_rate(experiences):.2%}")
    print(f"Average reward: {average_reward(experiences):.3f}")

    goals = Counter(experience.goal for experience in experiences)
    kinds = experience_kind_distribution(experiences)
    actions = Counter(
        experience.event
        for experience in experiences
        if experience.kind == "action"
    )
    plan_events = plan_event_distribution(experiences)
    results = result_distribution(experiences)
    rewards = reward_distribution(experiences)
    outcomes = Counter(
        "None" if experience.outcome is None else experience.outcome
        for experience in experiences
    )
    new_cells, revisited_cells = movement_counts(experiences)
    positive, zero, negative = navigation_progress_counts(experiences)
    navigation_average = average_navigation_progress(experiences)

    print_distribution("Goals", goals)
    print_distribution("Experience kinds", kinds)
    print_distribution("Actions", actions)
    print_distribution("Results", results)
    print_distribution("Plan events", plan_events)

    print("\nFailures:")
    print(f"body action failures: {body_action_failure_count(experiences)}")
    print(f"plan failures: {plan_events['plan_failed']}")
    print(f"replans: {plan_events['replan']}")
    print("failed targets: available in runtime P/R/T/B debug only")

    print("\nMovement:")
    print(f"newly visited: {new_cells}")
    print(f"revisited cells: {revisited_cells}")

    print("\nNavigation progress:")
    print(f"positive: {positive}")
    print(f"zero: {zero}")
    print(f"negative: {negative}")
    print(
        "average: n/a"
        if navigation_average is None
        else f"average: {navigation_average:.3f}"
    )

    print_distribution("Rewards", rewards)
    print_distribution("Outcomes", outcomes)
