import json
from pathlib import Path

from brain.experience import Experience, RESULT_COMPLETED, RESULT_FAILED


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
                    result=data.get(
                        "result",
                        RESULT_COMPLETED if data["succeeded"] else RESULT_FAILED,
                    ),
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

if __name__ == "__main__":
    from collections import Counter
    import sys

    path = Path(sys.argv[1])

    experiences = load_experiences(path)

    print(f"Experiences: {len(experiences)}")
    print(f"Success rate: {success_rate(experiences):.2%}")
    print(f"Average reward: {average_reward(experiences):.3f}")

    print("\n")

    summary = {
        "Goals":Counter(e.goal for e in experiences),
        "Actions":Counter(e.action for e in experiences),
        "Results":Counter(e.result for e in experiences),
        "Outcomes":Counter(e.outcome for e in experiences),
    }

    print(json.dumps(summary, indent=2))
