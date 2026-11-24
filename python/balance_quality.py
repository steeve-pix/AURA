"""Continuous balance-quality reward for the active Balance System.

Uses instantaneous physical measurements, independently of classifier labels.
Small corrections remain unpenalized. Planted-foot slip, sustained drift and
rapid body rotation reduce quality even before BALANCED is reached.
"""
from dataclasses import asdict, dataclass
from math import acos, exp, hypot, isfinite, pi, prod, sqrt

QUALITY_VERSION = 3


@dataclass(frozen=True)
class BalanceQualityConfig:
    # Initial scales are not calibrated against trained-policy outcomes.
    posture_scale_degrees: float = 30.
    height_zero_ratio: float = .25
    height_full_ratio: float = .75
    vertical_deadband: float = .25
    horizontal_deadband: float = .35
    # A balanced agent may make small corrections, but sustained COM drift and
    # planted-foot slip reduce its hold quality. These are initial tolerances,
    # expressed in world m/s; evaluation traces should guide later calibration.
    stationary_com_deadband: float = .08
    stationary_com_scale: float = .30
    foot_slip_deadband: float = .03
    foot_slip_scale: float = .15
    # Calibrate continuous control quality to the classifier's existing entry /
    # stay speeds (.4 / .65 rad/s), without changing those thresholds. Both body
    # parts at those speeds score approximately .80 / .20 respectively. This
    # standing-task scale is a design choice, not a measured recovery guarantee.
    angular_deadband: float = .25
    angular_control_scale: float = .315
    linear_dynamic_fraction: float = .25
    # Historical dimensional reference; rotation now uses angular_control_scale.
    angular_dynamic_fraction: float = .5
    gravity: float = 9.81
    support_margin_scale: float = .1
    marginal_support_quality: float = .25

    def __post_init__(self):
        for key, value in asdict(self).items():
            if not isfinite(value) or value < 0:
                raise ValueError(f"Invalid quality setting: {key}")
        for key in ("posture_scale_degrees", "linear_dynamic_fraction",
                    "angular_dynamic_fraction", "gravity", "support_margin_scale",
                    "stationary_com_scale", "foot_slip_scale", "angular_control_scale"):
            if getattr(self, key) == 0:
                raise ValueError(f"Quality scale must be positive: {key}")
        if not self.height_zero_ratio < self.height_full_ratio <= 1:
            raise ValueError("Invalid height quality interval")
        if self.marginal_support_quality > 1:
            raise ValueError("Marginal support quality must be in [0,1]")


def _number(value, name):
    result = float(value)
    if not isfinite(result):
        raise ValueError(f"Nonfinite physical measurement: {name}")
    return result


def _vector(value, name):
    if len(value) != 3:
        raise ValueError(f"Expected three coordinates: {name}")
    result = tuple(_number(x, name) for x in value)
    if not isfinite(hypot(*result)):
        raise ValueError(f"Invalid vector magnitude: {name}")
    return result


def _smoothstep(value):
    x = max(0., min(1., value))
    return x * x * (3. - 2. * x)


def balance_quality_components(obs, config=None):
    """Return [0,1] quality components and their product, without mutating obs.

    Support assumes the simulator's convex hull (collinear points removed).
    Grounded means geometric floor proximity, not measured load bearing. Outside
    the polygon retains marginal quality; it is never interpreted as FALLEN.
    """
    c = config or BalanceQualityConfig()
    refs = {k: _number(obs["neutral_height_references"][k], k)
            for k in ("pelvis", "torso", "center_of_mass")}
    if any(value <= 0 for value in refs.values()):
        raise ValueError("Neutral height references must be positive")
    scale = c.posture_scale_degrees * pi / 180
    posture = []
    heights = []
    rotation = []
    angular_scale = c.angular_control_scale
    for part in ("pelvis", "torso"):
        up = _number(obs[part + "_uprightness"], part + " uprightness")
        # Acos clipping accommodates numerical roundoff at upright/inverted.
        angle = acos(max(-1., min(1., up)))
        posture.append(exp(-((angle / scale) ** 2)))
        ratio = _number(obs[part + "_height"], part + " height") / refs[part]
        heights.append(_smoothstep((ratio - c.height_zero_ratio) /
                                   (c.height_full_ratio - c.height_zero_ratio)))
        speed = hypot(*_vector(obs[part + "_angular_velocity"], part + " rotation"))
        excess = max(0., speed - c.angular_deadband) / angular_scale
        rotation.append(excess * excess)
    velocity = _vector(obs["center_of_mass_velocity"], "COM velocity")
    linear_scale = c.linear_dynamic_fraction * sqrt(c.gravity * refs["center_of_mass"])
    vertical = max(0., abs(velocity[1]) - c.vertical_deadband) / linear_scale
    horizontal = max(0., hypot(velocity[0], velocity[2]) - c.horizontal_deadband) / linear_scale
    margin = _number(obs["support_margin"], "support margin")
    supported = bool(obs["left_foot_grounded"] or obs["right_foot_grounded"])
    polygon = obs["support_polygon"]
    for point in polygon:
        if len(point) != 2:
            raise ValueError("Expected two coordinates per support vertex")
        for coordinate in point:
            _number(coordinate, "support vertex")
    support = 0.
    if supported:
        support = c.marginal_support_quality
        if len(polygon) >= 3:
            support += (1. - support) * _smoothstep((margin / c.support_margin_scale + 1.) / 2.)
    com_motion = exp(-(vertical * vertical + horizontal * horizontal))
    stationary_hold = 1.
    foot_grip = 1.
    # Labels cannot exempt an unstable body from physical motion costs.
    # Do not reward walking-like drift as standing. The deadband permits
    # tiny pose corrections; the smooth penalty avoids a hard threshold.
    com_speed = hypot(velocity[0], velocity[2])
    com_excess = max(0., com_speed - c.stationary_com_deadband) / c.stationary_com_scale
    stationary_hold = exp(-(com_excess * com_excess))

    # Only feet in geometric floor contact count. A lifted foot can move
    # during a brief adjustment without being mislabeled as slipping.
    slip_excesses = []
    for side in ("left", "right"):
        velocity_key = side + "_foot_velocity"
        if bool(obs[side + "_foot_grounded"]) and velocity_key in obs:
            foot_velocity = _vector(obs[velocity_key], side + " foot velocity")
            slip_speed = hypot(foot_velocity[0], foot_velocity[2])
            slip_excesses.append(max(0., slip_speed - c.foot_slip_deadband) / c.foot_slip_scale)
    if slip_excesses:
        foot_grip = exp(-sum(value * value for value in slip_excesses) / len(slip_excesses))

    components = dict(posture=sum(posture) / 2., height=sqrt(prod(heights)),
                      com_motion=com_motion, body_rotation=exp(-sum(rotation) / 2.), support=support,
                      stationary_hold=stationary_hold, foot_grip=foot_grip)
    # Product prevents high height/support from cancelling uncontrolled motion.
    components["balance_quality"] = prod(components.values())
    if not all(isfinite(x) and 0 <= x <= 1 for x in components.values()):
        raise ValueError("Invalid balance quality result")
    return components


def balance_quality(obs, config=None):
    """Continuous nonnegative quality; terminal/truncation semantics belong elsewhere."""
    return balance_quality_components(obs, config)["balance_quality"]


def quality_implementation(version=QUALITY_VERSION):
    """Select a known objective explicitly; never reinterpret a saved reward."""
    if type(version) is not int:
        raise ValueError("Quality version must be an integer")
    if version == QUALITY_VERSION:
        return BalanceQualityConfig, balance_quality_components
    if version == 2:
        if __package__:
            from .balance_quality_v2 import BalanceQualityConfig as Config, balance_quality_components as components
        else:
            from balance_quality_v2 import BalanceQualityConfig as Config, balance_quality_components as components
        return Config, components
    raise ValueError(f"Unsupported balance quality version: {version}")
