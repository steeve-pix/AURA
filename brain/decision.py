from typing import Any

def decide(observation: dict[str, Any]) -> dict[str, Any]:
    if observation["east"] == "Battery":
        return {
            "action": "move",
            "direction": "east"
        }
        
    return{
        "action": "idle"
    }