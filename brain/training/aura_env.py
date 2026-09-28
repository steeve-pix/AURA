import json
import subprocess


class AuraEnv:
    def __init__(self, executable_path):
        self.process = subprocess.Popen(
            [str(executable_path), "--training-loop"],
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
        return json.loads(line)

    def reset(self):
        message = {"type": "reset"}

        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()

        line = self.process.stdout.readline()
        return json.loads(line)

    def close(self):
        self.process.stdin.close()
        self.process.wait()
