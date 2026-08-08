"""Run one newline-delimited observation/action exchange for the AURA brain."""

import json
import sys
from typing import Any

from brain.decision import decide
from brain.goals import choose_goal


def main() -> None:
    for raw in sys.stdin:
        raw: str = raw.strip()

        observation: dict[str, Any] = json.loads(raw)  # pyright: ignore[reportExplicitAny, reportAny]
        goal = choose_goal(observation)
        decision = decide(observation, goal)

        # print() adds the newline that tells the C++ exchange loop the action is complete.
        print(json.dumps(decision), flush=True)


if __name__ == "__main__":
    main()
