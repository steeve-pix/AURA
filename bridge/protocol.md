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

Example:

```json
{
  "position": [
    4,
    2
  ],
  "energy": 100,
  "north": "Empty",
  "east": "Battery",
  "south": "Empty",
  "west": "Empty"
}
```

## Action

Sent from the Python brain to the C++ body.

Fields:

- action
- direction

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
