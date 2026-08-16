import json
from pathlib import Path

from brain.experience import Experience


def load_experiences(
        path: Path,
) -> list[Experience]:
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
                    goal=data["goal"],
                    action=data["action"],
                    target=(None if data["target"] is None else (data["target"][0], data["target"][1])),
                    position_before=(data["position_before"][0], data["position_before"][1]),
                    position_after=(data["position_after"][0], data["position_after"][1]),
                    energy_before=data["energy_before"],
                    energy_after=data["energy_after"],
                    succeeded=data["succeeded"],
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