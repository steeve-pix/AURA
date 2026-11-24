"""Pure diagnostic failure utility; not imported by any production trainer.

Steps are one-based transitions: r_t is the existing terminal-transition reward.
The tail adds virtual utilities at t+1,...,H only. The returned correction is
valued at transition t, so it includes one leading gamma. It may be added once
to r_t before the existing discounted_returns() recurrence. No virtual actions,
observations, optimizer samples, or physical simulation steps are needed.
"""
import math


def discounted_failure_tail(terminal_step, *, fallen, failure_utility,
                            horizon=1200, gamma=.995):
    """Return gamma * r_f * sum(gamma**k, k=0,...,H-t-1).

    A time-limit truncation without FALLEN passes fallen=False and gets zero.
    FALLEN at the cap (both flags) gets zero remaining tail, retaining r_t.
    Utility is a chosen task value, not a measured physical quantity. Its sign
    is unrestricted here to keep the geometric-series primitive independent.
    Invalid/nonfinite inputs or an unrepresentable finite result raise ValueError.
    """
    for name, value in (('terminal_step', terminal_step), ('horizon', horizon)):
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise ValueError(f'{name} must be a nonnegative integer')
    if not isinstance(fallen, bool):
        raise ValueError('fallen must be a confirmed boolean outcome')
    try:
        gamma = float(gamma)
        failure_utility = float(failure_utility)
    except (TypeError, ValueError, OverflowError) as error:
        raise ValueError('gamma and failure utility must be finite numbers') from error
    if not math.isfinite(gamma) or not 0 <= gamma <= 1:
        raise ValueError('gamma must be finite and in [0, 1]')
    if not math.isfinite(failure_utility):
        raise ValueError('failure utility must be finite')
    remaining = max(0, horizon - terminal_step)
    if not fallen or not remaining or gamma == 0 or failure_utility == 0:
        return 0.
    try:
        if gamma == 1:
            result = failure_utility * remaining
        else:
            log_gamma = math.log(gamma)
            # Avoid converting arbitrarily large integer horizons to a float.
            exponent = -math.inf if remaining > 745 / -log_gamma else remaining * log_gamma
            series = -math.expm1(exponent) / -math.expm1(log_gamma)
            result = gamma * failure_utility * series
    except OverflowError as error:
        raise ValueError('Failure-tail value exceeds finite numeric range') from error
    if not math.isfinite(result):
        raise ValueError('Failure-tail value exceeds finite numeric range')
    return result
