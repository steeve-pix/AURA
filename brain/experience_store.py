import json
from dataclasses import asdict
from pathlib import Path

from brain.experience import Experience


def experience_path_for_world(
        directory: Path,
        world_id: str,
) -> Path:
    safe_world_id = world_id.replace(":", "_")

    return (
            directory
            / "experiences"
            / f"{safe_world_id}.jsonl"
    )


def append_experience(experience: Experience, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

    data = asdict(experience)

    if experience.target is not None:
        data["target"] = list(experience.target)

    data["position_before"] = list(experience.position_before)
    data["position_after"] = list(experience.position_after)

    with path.open("a", encoding="utf-8") as file:
        file.write(json.dumps(data) + "\n")
