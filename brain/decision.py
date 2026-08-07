def decide(observation):
    if observation["east"] == "Battery":
        return {
            "action": "move",
            "direction": "east"
        }
        
    return{
        "action": "idle"
    }