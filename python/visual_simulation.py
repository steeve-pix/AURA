import aura_python
import time

# Create ONE simulation
sim = aura_python.AuraSimulation()

# Create a viewer for that simulation
viewer = aura_python.AuraViewer(sim)

obs = sim.reset()

while viewer.is_open() and not obs["done"]:
    # Advance physics
    obs = sim.step_action({
        "left_hip_target": -0.19,
        "right_hip_target": -0.19,
        "left_knee_target": 0.0,
        "right_knee_target": 0.0,
    })

    # Display the SAME simulation
    viewer.render(sim)

    # Slow down so we can watch
    time.sleep(1 / 120)

print("Steps survived:", obs["steps"])
print("Termination:", obs["termination_reason"])
