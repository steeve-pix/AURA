"""Shared deterministic test fixtures; no production behavior."""


def observation(**changes):
    result = dict(termination_mode='balance_v2', termination_version=2,
                  steps=0, balance_state='BALANCED', terminated=False, truncated=False, done=False,
                  pelvis_uprightness=1., torso_uprightness=1., pelvis_height=4.17, torso_height=6.,
                  center_of_mass_velocity=[0., 0., 0.], pelvis_angular_velocity=[0., 0., 0.],
                  torso_angular_velocity=[0., 0., 0.], left_foot_grounded=True, right_foot_grounded=True,
                  left_foot_velocity=[0., 0., 0.], right_foot_velocity=[0., 0., 0.],
                  support_margin=.2, support_polygon=[[0, 0], [1, 0], [0, 1]],
                  neutral_height_references=dict(pelvis=4.17, torso=6., center_of_mass=3.75625))
    result.update(changes)
    return result
