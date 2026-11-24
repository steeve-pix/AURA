"""Shared deterministic test fixtures; no production behavior."""
import copy
from python.aura_env import AuraEnv
from python.arena_env import ArenaEnv

class ScriptedArena:
    episode_limit = 8
    agent_count = 4

    def __init__(self):
        self.base = AuraEnv().simulation.reset()
        self.ticks = [0] * 4

    def reset(self, count=0):
        self.count = count or 4
        self.ticks = [0] * 4
        return self.observation()

    def observation(self):
        result = []
        for i, tick in enumerate(self.ticks):
            obs = copy.deepcopy(self.base)
            done = tick == 5 + i
            state = 'FALLEN' if done else 'UNBALANCED' if tick in (0, 1, 3) else 'BALANCED'
            obs.update(steps=tick, time=tick/120., terminated=done, done=done, truncated=False,
                       termination_reason='fallen' if done else 'none', balance_state=state,
                       left_foot_grounded=tick>=2, right_foot_grounded=False,
                       support_polygon=[[0., 0.], [1., 0.], [0., 1.]], support_margin=.2,
                       arena_contacts=0, agent_name=f'AURA {i+1}')
            result.append(obs)
        return result

    def step_actions(self, actions):
        before = self.observation()
        for i in range(self.count):
            if not before[i]['done']:
                self.ticks[i] += 1
        return self.observation()


def fixture(agents=4, *, spacing=12., quality_version=None):
    env = object.__new__(ArenaEnv)
    env._configure_quality(quality_version)
    env.agents = agents
    env.spacing = spacing
    env.simulation = ScriptedArena()
    env.info = None
    return env
