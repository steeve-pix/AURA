import aura_python

sim = aura_python.AuraSimulation()
obs = sim.reset()

pelvis = obs["pelvis_position"]

left_foot = obs["left_foot_position"]
right_foot = obs["right_foot_position"]

left_relative = tuple(
    left_foot[i] - pelvis[i] for i in range(3)
)

right_relative = tuple(
    right_foot[i] - pelvis[i] for i in range(3)
)

print("Left foot relative to pelvis:", left_relative)
print("Right foot relative to pelvis:", right_relative)