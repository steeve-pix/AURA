"""Episode measurements, independent of reward and classifier decisions."""
from math import isfinite


class BalanceMetrics:
    def __init__(self):
        self.steps = self.balanced = self.unbalanced = self.longest = self.interval = 0
        self.recoveries = 0
        self.established = self.imbalance_after_balance = False
        self.quality_sum = self.reward_sum = self.seconds = self.balanced_seconds = 0.
        self.fallen = False
        self.reason = "none"

    def update(self, obs, reward):
        if self.fallen:
            raise ValueError("Cannot extend metrics after FALLEN")
        if obs['steps'] != self.steps + 1:
            raise ValueError("Metrics require consecutive physical transitions")
        quality = obs.get('reward_qualities', {}).get('balance_quality', reward)
        time = float(obs['time'])
        if not all(isfinite(x) for x in (reward, quality, time)) or time < self.seconds:
            raise ValueError("Invalid balance metrics")
        duration = time - self.seconds
        self.steps += 1
        self.seconds = time
        state = obs['balance_state']
        if state == 'BALANCED':
            if self.imbalance_after_balance:
                self.recoveries += 1
                self.imbalance_after_balance = False
            self.established = True
            self.balanced += 1
            self.balanced_seconds += duration
            self.interval += 1
            self.longest = max(self.longest, self.interval)
        else:
            self.interval = 0
            if state == 'UNBALANCED':
                self.unbalanced += 1
                if self.established:
                    self.imbalance_after_balance = True
        self.fallen = state == 'FALLEN'
        self.reason = obs['termination_reason']
        self.quality_sum += quality
        self.reward_sum += reward

    def result(self):
        return dict(steps=self.steps, balanced_steps=self.balanced, balanced_seconds=self.balanced_seconds,
                    unbalanced_steps=self.unbalanced, longest_balanced_steps=self.longest,
                    recoveries=self.recoveries, fall_rate=float(self.fallen),
                    balanced_fraction=self.balanced / self.steps if self.steps else 0.,
                    average_balance_quality=self.quality_sum / self.steps if self.steps else 0.,
                    total_reward=self.reward_sum, termination_reason=self.reason)
