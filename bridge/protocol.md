# AURA Brain–Body Protocol

## Observation

Sent from the C++ body to the Python brain.

Fields:

- position
- energy
- north
- east
- south
- west
- sensor_radius
- visible_cells
- nearby_objects
- last_action

Example:

```json
{
  "position": [
    4,
    2
  ],
  "energy": 100,
  "sensor_radius": 2,
  "north": "Empty",
  "east": "Battery",
  "south": "Empty",
  "west": "Empty",
  "visible_cells": [
    {"position": [3, 2], "type": "Empty"},
    {"position": [4, 2], "type": "Empty"},
    {"position": [5, 2], "type": "Wall"}
  ],
  "nearby_objects": [
    {
      "position": [6, 2],
      "type": "Battery",
      "reachable": true,
      "path_length": 4
    }
  ],
  "last_action": {
    "type": "move_to",
    "target": [6, 2],
    "succeeded": true
  }
}
```

`visible_cells` contains every in-bounds cell currently covered by the range sensor. The Python brain can remember these
observations as a partial map.

`nearby_objects` is the focused list of visible batteries. `reachable` and `path_length` describe the C++ body's current
pathfinding result for each visible object. An unreachable object has `reachable: false` and `path_length: null`.

`last_action` reports the outcome of the action executed after the previous observation. It is `null` before the body
has executed an action. A `move_to` result includes the requested target so the brain can associate a failure with the
correct remembered location.

```json
{
  "last_action": {
    "type": "move_to",
    "target": [8, 3],
    "succeeded": false
  }
}
```

The body reports each result once, in the next observation. Python should use failed `move_to` results to avoid
repeatedly selecting unreachable targets.

## Action

Sent from the Python brain to the C++ body.

Fields:

- action
- direction (for `move`)
- target (for `move_to`)
- target (for `investigate`)
- debug (optional rendering information, including active-plan state)

Example:

```json
{
  "action": "move",
  "direction": "east"
}
```

### Move to a target

The `move_to` action asks the C++ body to navigate to a target position. The target uses the `[x, y]` coordinate format.

```json
{"action":"move_to","target":[7,3]}
```

### Investigate a target

`investigate` asks the body to move toward a target selected for exploration. It also uses an `[x, y]` target.

```json
{"action":"investigate","target":[7,3]}
```

### Optional debug data

The brain may include a `debug` object with its current goal, goal scores, known cells, visited cells, and active plan. The C++ body
uses this only for visualization; it does not affect physical simulation.

```json
{
  "action": "move_to",
  "target": [7, 3],
  "debug": {
    "goal": "recharge",
    "goal_scores": {"recharge": 80.0, "explore": 20.0},
    "known_cells": [[1, 1], [2, 1]],
    "visited_cells": [[1, 1]],
    "plan": {
      "goal": "recharge",
      "current_step": 0,
      "step_count": 1,
      "failed": false,
      "step": {"type": "move_to", "target": [7, 3]}
    }
  }
}
```
