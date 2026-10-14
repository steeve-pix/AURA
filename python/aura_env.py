"""Python-side, fixed-size observation contract for AURA's balance policy."""

from math import isfinite, sqrt

import aura_python
import torch


OBSERVATION_SCHEMA_VERSION = 2

# The order matches the named records emitted by AuraSimulation::observation().
JOINT_ORDER = (
    "waist", "neck", "head",
    "leftShoulder", "leftElbow", "leftWrist",
    "rightShoulder", "rightElbow", "rightWrist",
    "leftHip", "leftKnee", "leftAnkle",
    "rightHip", "rightKnee", "rightAnkle",
)
BODY_PART_ORDER = (
    "head", "neck", "torso", "pelvis",
    "left_upper_arm", "left_forearm", "left_hand",
    "right_upper_arm", "right_forearm", "right_hand",
    "left_thigh", "left_shin", "left_foot",
    "right_thigh", "right_shin", "right_foot",
)

# AURA's nominal geometry runs from the floor to the top of the head:
# 7.24 m head center + half of its 1.05 m height = 7.765 m.
# Gravity is 9.81 m/s^2 in SimulationStep.cpp. These produce physically
# meaningful characteristic scales: sqrt(g H) for linear speed and
# sqrt(g / H) for angular speed (the inverse pendulum time scale).
CHARACTER_HEIGHT = 7.765
GRAVITY = 9.81
LINEAR_SPEED_SCALE = sqrt(GRAVITY * CHARACTER_HEIGHT)
ANGULAR_SPEED_SCALE = sqrt(GRAVITY / CHARACTER_HEIGHT)
MAX_CONTACT_POINTS_PER_FOOT = 8  # A box has eight corners in floorContactPoints().
MAX_SUPPORT_VERTICES = 2 * MAX_CONTACT_POINTS_PER_FOOT


def _feature(name, size, frame, scale):
    return {"name": name, "size": size, "frame": frame, "scale": scale}


_FEATURES = (
    _feature("joint_relative_orientations", len(JOINT_ORDER) * 4, "parent-relative quaternion (w,x,y,z)", "unit quaternion; canonical sign"),
    _feature("joint_relative_angular_velocities", len(JOINT_ORDER) * 3, "parent-local", f"rad/s ÷ {ANGULAR_SPEED_SCALE:.6g} rad/s"),
    _feature("pelvis_up", 3, "world direction", "unit vector"),
    _feature("pelvis_angular_velocity", 3, "pelvis-local", f"rad/s ÷ {ANGULAR_SPEED_SCALE:.6g} rad/s"),
    _feature("com_position_relative_to_pelvis", 3, "pelvis-local", f"m ÷ {CHARACTER_HEIGHT:g} m"),
    _feature("com_velocity_relative_to_pelvis", 3, "pelvis rotating frame", f"m/s ÷ {LINEAR_SPEED_SCALE:.6g} m/s"),
    _feature("com_world_horizontal_velocity", 2, "world ground X,Z", f"m/s ÷ {LINEAR_SPEED_SCALE:.6g} m/s"),
    _feature("foot_positions_relative_to_pelvis", 6, "pelvis-local; left XYZ then right XYZ", f"m ÷ {CHARACTER_HEIGHT:g} m"),
    _feature("foot_grounded", 2, "left then right", "boolean → 0 or 1"),
    _feature("body_part_linear_velocities", len(BODY_PART_ORDER) * 3, "pelvis orientation frame; canonical body-part order", f"m/s ÷ {LINEAR_SPEED_SCALE:.6g} m/s"),
    _feature("foot_velocities_relative_to_pelvis", 6, "pelvis rotating frame; left XYZ then right XYZ", f"m/s ÷ {LINEAR_SPEED_SCALE:.6g} m/s"),
    _feature("support_state", 3, "world-ground support polygon", "valid mask, COM-inside flag, signed margin ÷ character height"),
    _feature("support_polygon_geometry", 7, "world ground X,Z relative to projected COM", f"centroid/extent/perimeter ÷ {CHARACTER_HEIGHT:g} m; area ÷ H²; vertex count ÷ {MAX_SUPPORT_VERTICES}"),
    _feature("foot_contact_geometry", 14, "per-foot pelvis-local summary; left then right", f"valid mask, point count ÷ {MAX_CONTACT_POINTS_PER_FOOT}, centroid offset and X/Z extent ÷ {CHARACTER_HEIGHT:g} m"),
)


def _make_layout():
    layout = []
    start = 0
    for feature in _FEATURES:
        end = start + feature["size"]
        layout.append({**feature, "start": start, "end": end})
        start = end
    return tuple(layout)


OBSERVATION_LAYOUT = _make_layout()
OBSERVATION_SIZE = sum(feature["size"] for feature in _FEATURES)


def _named_records(records, expected_names, label):
    by_name = {record["name"]: record for record in records}
    if len(by_name) != len(records) or set(by_name) != set(expected_names):
        raise ValueError(f"Unexpected {label} names; expected {expected_names}")
    return by_name


def _vector3(value, label):
    if len(value) != 3:
        raise ValueError(f"{label} must contain three coordinates")
    return tuple(float(component) for component in value)


def _unit_quaternion(value, label):
    if len(value) != 4:
        raise ValueError(f"{label} must be a quaternion in (w, x, y, z) order")
    components = tuple(float(component) for component in value)
    norm = sqrt(sum(component * component for component in components))
    if not isfinite(norm) or norm == 0.0:
        raise ValueError(f"{label} must be a finite, nonzero quaternion")
    normalized = tuple(component / norm for component in components)
    # q and -q encode the same rotation. A canonical sign prevents an arbitrary
    # sign flip in an equivalent C++ quaternion from appearing as a state jump.
    if normalized[0] < 0.0:
        normalized = tuple(-component for component in normalized)
    return normalized


def _rotate(quaternion, vector):
    """Rotate a vector by a unit (w,x,y,z) quaternion."""
    w, x, y, z = quaternion
    vx, vy, vz = vector
    tx = 2.0 * (y * vz - z * vy)
    ty = 2.0 * (z * vx - x * vz)
    tz = 2.0 * (x * vy - y * vx)
    return (
        vx + w * tx + y * tz - z * ty,
        vy + w * ty + z * tx - x * tz,
        vz + w * tz + x * ty - y * tx,
    )


def _inverse_rotate(quaternion, vector):
    w, x, y, z = quaternion
    return _rotate((w, -x, -y, -z), vector)


def _subtract(a, b):
    return tuple(first - second for first, second in zip(a, b))


def _cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def _relative_position(position, pelvis_position, pelvis_orientation):
    return _inverse_rotate(pelvis_orientation, _subtract(position, pelvis_position))


def _relative_velocity(position, velocity, pelvis_position, pelvis_velocity,
                       pelvis_orientation, pelvis_angular_velocity_local):
    relative_position = _relative_position(position, pelvis_position, pelvis_orientation)
    relative_velocity_world = _subtract(velocity, pelvis_velocity)
    relative_velocity_local = _inverse_rotate(pelvis_orientation, relative_velocity_world)
    frame_velocity = _cross(pelvis_angular_velocity_local, relative_position)
    return _subtract(relative_velocity_local, frame_velocity)


def _polygon_geometry(vertices, com_position):
    points = [(float(point[0]), float(point[1])) for point in vertices]
    if not points:
        return False, (0.0,) * 7

    xs = [point[0] for point in points]
    zs = [point[1] for point in points]
    signed_area_twice = 0.0
    centroid_x_numerator = 0.0
    centroid_z_numerator = 0.0
    perimeter = 0.0
    if len(points) >= 3:
        for index, (x1, z1) in enumerate(points):
            x2, z2 = points[(index + 1) % len(points)]
            edge_cross = x1 * z2 - x2 * z1
            signed_area_twice += edge_cross
            centroid_x_numerator += (x1 + x2) * edge_cross
            centroid_z_numerator += (z1 + z2) * edge_cross
            perimeter += sqrt((x2 - x1) ** 2 + (z2 - z1) ** 2)

    area = abs(signed_area_twice) * 0.5
    valid = len(points) >= 3 and area > 0.0
    if valid:
        centroid_x = centroid_x_numerator / (3.0 * signed_area_twice)
        centroid_z = centroid_z_numerator / (3.0 * signed_area_twice)
    else:
        centroid_x = sum(xs) / len(xs)
        centroid_z = sum(zs) / len(zs)

    # COM-relative centroid keeps the geometry invariant to world translation.
    geometry = (
        (centroid_x - com_position[0]) / CHARACTER_HEIGHT,
        (centroid_z - com_position[2]) / CHARACTER_HEIGHT,
        (max(xs) - min(xs)) / (2.0 * CHARACTER_HEIGHT),
        (max(zs) - min(zs)) / (2.0 * CHARACTER_HEIGHT),
        area / (CHARACTER_HEIGHT * CHARACTER_HEIGHT),
        perimeter / CHARACTER_HEIGHT,
        len(points) / MAX_SUPPORT_VERTICES,
    )
    return valid, geometry


def _foot_contact_geometry(contact_points, foot_position, pelvis_position, pelvis_orientation):
    if not contact_points:
        return (0.0,) * 7

    foot_local = _relative_position(foot_position, pelvis_position, pelvis_orientation)
    contact_local = [
        _relative_position(_vector3(point, "foot contact point"), pelvis_position, pelvis_orientation)
        for point in contact_points
    ]
    offsets = [_subtract(point, foot_local) for point in contact_local]
    centroid = tuple(sum(point[axis] for point in offsets) / len(offsets) for axis in range(3))
    extent_x = max(point[0] for point in offsets) - min(point[0] for point in offsets)
    extent_z = max(point[2] for point in offsets) - min(point[2] for point in offsets)
    return (
        1.0,
        len(offsets) / MAX_CONTACT_POINTS_PER_FOOT,
        *(component / CHARACTER_HEIGHT for component in centroid),
        extent_x / CHARACTER_HEIGHT,
        extent_z / CHARACTER_HEIGHT,
    )


def encode_observation(obs):
    """Encode a simulator dictionary under the versioned fixed-size contract."""
    joints = _named_records(obs["joints"], JOINT_ORDER, "joint")
    body_parts = _named_records(obs["body_part_states"], BODY_PART_ORDER, "body-part")
    pelvis_position = _vector3(obs["pelvis_position"], "pelvis_position")
    pelvis_velocity = _vector3(obs["pelvis_velocity"], "pelvis_velocity")
    pelvis_orientation = _unit_quaternion(obs["pelvis_orientation"], "pelvis_orientation")
    pelvis_angular_velocity_world = _vector3(obs["pelvis_angular_velocity"], "pelvis_angular_velocity")
    pelvis_angular_velocity_local = _inverse_rotate(pelvis_orientation, pelvis_angular_velocity_world)
    com_position = _vector3(obs["center_of_mass_position"], "center_of_mass_position")
    com_velocity = _vector3(obs["center_of_mass_velocity"], "center_of_mass_velocity")
    left_foot_position = _vector3(obs["left_foot_position"], "left_foot_position")
    right_foot_position = _vector3(obs["right_foot_position"], "right_foot_position")
    left_foot_velocity = _vector3(obs["left_foot_velocity"], "left_foot_velocity")
    right_foot_velocity = _vector3(obs["right_foot_velocity"], "right_foot_velocity")

    com_relative_position = _relative_position(com_position, pelvis_position, pelvis_orientation)
    com_relative_velocity = _relative_velocity(
        com_position, com_velocity, pelvis_position, pelvis_velocity,
        pelvis_orientation, pelvis_angular_velocity_local,
    )
    foot_positions = (left_foot_position, right_foot_position)
    foot_velocities = (left_foot_velocity, right_foot_velocity)

    feature_values = {
        "joint_relative_orientations": [],
        "joint_relative_angular_velocities": [],
        "pelvis_up": list(_vector3(obs["pelvis_up"], "pelvis_up")),
        "pelvis_angular_velocity": [value / ANGULAR_SPEED_SCALE for value in pelvis_angular_velocity_local],
        "com_position_relative_to_pelvis": [value / CHARACTER_HEIGHT for value in com_relative_position],
        "com_velocity_relative_to_pelvis": [value / LINEAR_SPEED_SCALE for value in com_relative_velocity],
        "com_world_horizontal_velocity": [com_velocity[0] / LINEAR_SPEED_SCALE,
                                           com_velocity[2] / LINEAR_SPEED_SCALE],
        "foot_positions_relative_to_pelvis": [],
        "foot_grounded": [float(bool(obs["left_foot_grounded"])),
                           float(bool(obs["right_foot_grounded"]))],
        "body_part_linear_velocities": [],
        "foot_velocities_relative_to_pelvis": [],
    }

    for name in JOINT_ORDER:
        joint = joints[name]
        orientation = _unit_quaternion(joint["relative_orientation"], f"{name}.relative_orientation")
        angular_velocity = _vector3(joint["relative_angular_velocity"], f"{name}.relative_angular_velocity")
        feature_values["joint_relative_orientations"].extend(orientation)
        feature_values["joint_relative_angular_velocities"].extend(
            value / ANGULAR_SPEED_SCALE for value in angular_velocity
        )

    feature_values["foot_positions_relative_to_pelvis"].extend(
        value / CHARACTER_HEIGHT
        for position in foot_positions
        for value in _relative_position(position, pelvis_position, pelvis_orientation)
    )
    feature_values["foot_velocities_relative_to_pelvis"].extend(
        value / LINEAR_SPEED_SCALE
        for position, velocity in zip(foot_positions, foot_velocities)
        for value in _relative_velocity(
            position, velocity, pelvis_position, pelvis_velocity,
            pelvis_orientation, pelvis_angular_velocity_local,
        )
    )
    # These are each body's world-space linear velocity expressed in pelvis
    # orientation coordinates. They are not relative-position derivatives.
    for name in BODY_PART_ORDER:
        world_velocity = _vector3(body_parts[name]["velocity"], f"{name}.velocity")
        feature_values["body_part_linear_velocities"].extend(
            value / LINEAR_SPEED_SCALE for value in _inverse_rotate(pelvis_orientation, world_velocity)
        )

    support_vertices = obs["support_polygon"]
    support_valid, polygon_geometry = _polygon_geometry(support_vertices, com_position)
    feature_values["support_state"] = [
        float(support_valid),
        float(support_valid and bool(obs["center_of_mass_inside_support_polygon"])),
        float(obs["support_margin"]) / CHARACTER_HEIGHT if support_valid else 0.0,
    ]
    feature_values["support_polygon_geometry"] = list(polygon_geometry)
    feature_values["foot_contact_geometry"] = [
        *_foot_contact_geometry(obs["left_foot_contact_points"], left_foot_position,
                                pelvis_position, pelvis_orientation),
        *_foot_contact_geometry(obs["right_foot_contact_points"], right_foot_position,
                                pelvis_position, pelvis_orientation),
    ]

    values = []
    for feature in OBSERVATION_LAYOUT:
        group_values = feature_values[feature["name"]]
        if len(group_values) != feature["size"]:
            raise ValueError(
                f"Observation group {feature['name']} has {len(group_values)} values; "
                f"expected {feature['size']} (schema {OBSERVATION_SCHEMA_VERSION})"
            )
        values.extend(group_values)

    if len(values) != OBSERVATION_SIZE or not all(isfinite(value) for value in values):
        raise ValueError("Encoded observation has an invalid size or non-finite values")
    encoded = torch.tensor(values, dtype=torch.float32)
    if not torch.isfinite(encoded).all():
        raise ValueError("Encoded observation is not finite in float32")
    return encoded


def balance_reward(observation):
    if observation["terminated"]:
        return 0.0

    uprightness = observation["pelvis_uprightness"]
    uprightness = max(0.0, min(1.0, uprightness))
    return 0.5 + 0.5 * uprightness


class AuraEnv:
    def __init__(self):
        self.simulation = aura_python.AuraSimulation()

    @property
    def episode_limit(self):
        return self.simulation.episode_limit

    @property
    def observation_size(self):
        return OBSERVATION_SIZE

    def reset(self):
        obs = self.simulation.reset()
        return encode_observation(obs), obs

    def step(self, action):
        obs = self.simulation.step_action(action)
        return encode_observation(obs), balance_reward(obs), bool(obs["terminated"]), bool(obs["truncated"]), obs
