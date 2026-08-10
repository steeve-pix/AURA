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
    {"position": [6, 2], "type": "Battery"}
  ]
}
```

`visible_cells` contains every in-bounds cell currently covered by the range sensor. The Python brain can remember these observations as a partial map. `nearby_objects` remains a focused list of visible batteries.

## Action

Sent from the Python brain to the C++ body.

Fields:

- action
- direction (for `move`)
- target (for `move_to`)

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
