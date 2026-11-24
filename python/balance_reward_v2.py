"""Diagnostic-only physical reward; no policy, optimizer or classifier decisions."""
from dataclasses import asdict, dataclass
from math import acos, exp, expm1, hypot, isfinite, pi, sqrt

REWARD_VERSION = 2
REWARD_NAME = "balance_reward_v2"


@dataclass(frozen=True)
class RewardV2Config:
    posture_scale_degrees: float = 30.
    height_zero_ratio: float = .25
    height_full_ratio: float = .75
    vertical_deadband: float = .25
    horizontal_deadband: float = .35
    angular_deadband: float = .4
    linear_dynamic_fraction: float = .25
    angular_dynamic_fraction: float = .5
    support_margin_scale: float = .1
    support_time_constant: float = .08
    gravity: float = 9.81
    time_step: float = 1. / 120
    discount: float = .995
    posture_weight: float = .25
    height_weight: float = .15
    motion_weight: float = .25
    support_weight: float = 1.
    recovery_weight: float = .1
    terminal_weight: float = 1.

    def __post_init__(self):
        for key, value in asdict(self).items():
            if not isfinite(value) or value < 0: raise ValueError(f"Invalid reward setting: {key}")
        for key in ("posture_scale_degrees", "linear_dynamic_fraction", "angular_dynamic_fraction",
                    "support_margin_scale", "support_time_constant", "gravity", "time_step"):
            if getattr(self, key) == 0: raise ValueError(f"Reward scale must be positive: {key}")
        if not 0 < self.discount <= 1: raise ValueError("Reward shaping discount must be in (0,1]")
        if not self.height_zero_ratio < self.height_full_ratio <= 1:
            raise ValueError("Height plateau must follow the collapse region and be at most neutral height")

    @property
    def weights(self):
        return {name: getattr(self, name + "_weight") for name in
                ("posture", "height", "motion", "support", "recovery", "terminal")}


def smoothstep(value):
    x = max(0., min(1., value))
    return x * x * (3. - 2. * x)


def number(value, label):
    result = float(value)
    if not isfinite(result): raise ValueError(f"Nonfinite reward measurement: {label}")
    return result


def vector(value, label):
    if len(value) != 3: raise ValueError(f"Expected three coordinates: {label}")
    result = tuple(number(x, label) for x in value)
    if not isfinite(hypot(*result)): raise ValueError(f"Invalid vector magnitude: {label}")
    return result


def physical_qualities(obs, config):
    """All four qualities are in [0,1]; head/joint angles are intentionally absent."""
    refs = {k: number(obs["neutral_height_references"][k], k) for k in ("pelvis", "torso", "center_of_mass")}
    if any(x <= 0 for x in refs.values()): raise ValueError("Neutral body heights must be positive")
    angles = [acos(max(-1., min(1., number(obs[k + "_uprightness"], k + " uprightness"))))
              for k in ("pelvis", "torso")]
    scale = config.posture_scale_degrees * pi / 180
    posture = exp(-sum((a / scale) * (a / scale) for a in angles) / 2.)
    heights = [smoothstep((number(obs[k + "_height"], k + " height") / refs[k] - config.height_zero_ratio) /
                          (config.height_full_ratio - config.height_zero_ratio)) for k in ("pelvis", "torso")]
    height = sqrt(heights[0] * heights[1])
    velocity = vector(obs["center_of_mass_velocity"], "COM velocity")
    omega = [hypot(*vector(obs[k + "_angular_velocity"], k + " angular velocity")) for k in ("pelvis", "torso")]
    linear_scale = config.linear_dynamic_fraction * sqrt(config.gravity * refs["center_of_mass"])
    angular_scale = config.angular_dynamic_fraction * sqrt(config.gravity / refs["center_of_mass"])
    vertical = max(0., abs(velocity[1]) - config.vertical_deadband) / linear_scale
    horizontal = max(0., hypot(velocity[0], velocity[2]) - config.horizontal_deadband) / linear_scale
    angular = [max(0., w - config.angular_deadband) / angular_scale for w in omega]
    # Multiplication may saturate to +inf for huge finite inputs; exp(-inf)=0.
    motion = exp(-(vertical * vertical + horizontal * horizontal + sum(w * w for w in angular) / 2.))
    any_foot = bool(obs["left_foot_grounded"] or obs["right_foot_grounded"])
    margin = number(obs["support_margin"], "support margin")
    # Simulator convexHull removes collinear vertices: >=3 means valid area.
    valid = len(obs["support_polygon"]) >= 3
    margin_quality = smoothstep((margin / config.support_margin_scale + 1.) / 2.)
    target = (.25 + .75 * margin_quality if valid else .25) if any_foot else 0.
    return {"posture": posture, "height": height, "motion": motion, "support_target": target,
            "linear_speed_scale": linear_scale, "angular_speed_scale": angular_scale}


class BalanceRewardV2:
    def __init__(self, config=None):
        self.config = config or RewardV2Config()
        self.previous_step = None;
        self.previous_potential = 0.;
        self.support = 0.;
        self.ended = False

    @staticmethod
    def validate_mode(obs):
        if obs.get("termination_mode") != "balance_v2" or obs.get("termination_version") != 2:
            raise ValueError("Reward V2 requires explicit balance_v2 termination version 2")
        if bool(obs["terminated"]) != (obs["balance_state"] == "FALLEN"):
            raise ValueError("Reward V2 requires the existing FALLEN termination signal")
        if bool(obs["done"]) != bool(obs["terminated"] or obs["truncated"]):
            raise ValueError("Inconsistent episode end flags")

    def reset(self, obs):
        self.validate_mode(obs)
        q = physical_qualities(obs, self.config)
        self.support = q["support_target"]
        self.previous_potential = q["posture"] * q["motion"] * self.support
        self.previous_step = obs["steps"];
        self.ended = bool(obs["done"])

    def update(self, obs):
        self.validate_mode(obs)
        if self.previous_step is None: raise ValueError("Reset reward evaluator before stepping")
        if self.ended: raise ValueError("Episode ended; reset reward evaluator")
        if obs["steps"] != self.previous_step + 1: raise ValueError("Reward requires exactly one new physics tick")
        q = physical_qualities(obs, self.config)
        alpha = -expm1(-self.config.time_step / self.config.support_time_constant)
        support = self.support + alpha * (q["support_target"] - self.support)
        p, h, m = q["posture"], q["height"], q["motion"]
        # No height in the potential: a pure upward position correction is not
        # recovery progress. End potential is zero for either episode ending.
        potential = 0. if obs["done"] else p * m * support
        terms = {"posture": p - 1., "height": h - 1., "motion": m - 1.,
                 "support": 0. if obs["terminated"] else p * h * m * support,
                 "recovery": self.config.discount * potential - self.previous_potential,
                 "terminal": -1. if obs["terminated"] else 0.}
        components = {key: value * self.config.weights[key] for key, value in terms.items()}
        total = sum(components.values())
        if not all(isfinite(v) for v in (*terms.values(), *components.values(), total)):
            raise ValueError("Nonfinite reward calculation")
        self.support = support;
        self.previous_potential = potential;
        self.previous_step = obs["steps"]
        self.ended = bool(obs["done"])
        return {"reward_total": total, "reward_version": REWARD_VERSION, "reward_name": REWARD_NAME,
                "reward_components": components, "reward_terms": terms,
                "reward_qualities": {**q, "support": support, "recovery_potential": potential},
                "reward_weights": self.config.weights}
