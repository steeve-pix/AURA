import json
import subprocess
import time


class AuraEnv:
    def __init__(self, executable_path, render=False, render_delay=0.0):
        self.render = render
        self.render_delay = render_delay

        command = [str(executable_path), "--training-loop"]
        if render:
            command.append("--render")

        self.process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
        )

    def step(self, left_ankle_torque, right_ankle_torque):
        message = {
            "type": "action",
            "left_ankle_torque": left_ankle_torque,
            "right_ankle_torque": right_ankle_torque,
        }

        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()

        line = self.process.stdout.readline()
        observation = json.loads(line)

        if self.render and self.render_delay > 0.0:
            time.sleep(self.render_delay)

        return observation

    def reset(self, push_x=0.0):
        message = {
            "type": "reset",
            "push_x": push_x,
        }

        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()

        line = self.process.stdout.readline()
        return json.loads(line)

    def close(self):
        self.process.stdin.close()
        self.process.wait()
