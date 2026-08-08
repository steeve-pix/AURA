"""Run one newline-delimited observation/action exchange for the AURA brain."""

import json
from typing import Any

from brain.decision import decide  # pyright: ignore[reportUnknownVariableType]

# input() reads the observation line redirected from the C++ body's pipe.
raw = input()

observation: dict[str, Any] = json.loads(raw)  # pyright: ignore[reportExplicitAny, reportAny]
decision = decide(observation)

# print() adds the newline that tells the C++ exchange loop the action is complete.
print(json.dumps(decision))
