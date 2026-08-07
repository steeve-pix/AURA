from brain.decision import decide

observation = {
    "position": [2, 2],
    "energy": 42,
    "north": "Empty",
    "east": "Battery",
    "south": "Empty",
    "west": "Wall",
}

decision = decide(observation)
print(decision)