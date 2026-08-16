import json
from dataclasses import asdict
from pathlib import Path

from brain.experience import Experience


def append_experience(experience: Experience, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

    data = asdict(experience)

    if experience.target is not None:
        data["target"] = list(experience.target)

    data["position_before"] = list(experience.position_before)
    data["position_after"] = list(experience.position_after)

    with path.open("a", encoding="utf-8") as f:
        f.write(json.dumps(data) + "\n")
