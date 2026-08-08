import json

from brain.decision import decide

raw = input()
observation = json.loads(raw)
decision = decide(observation)
print(json.dumps(decision))
