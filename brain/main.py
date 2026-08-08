import json
from typing import Any

from brain.decision import decide  # pyright: ignore[reportUnknownVariableType]

raw = input()

observation: dict[str, Any] = json.loads(raw)  # pyright: ignore[reportExplicitAny, reportAny]
decision = decide(observation)

print(json.dumps(decision))
