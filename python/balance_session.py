"""Viewer pacing and progress reporting for the CLI's Balance Lab."""

import argparse
import random
import os
import select
import sys
import time
from collections.abc import Callable

import aura_python

EPISODE_LIMIT = 1200
TRIALS = 20
FIXED_TARGET_RECORD = 346  # Historical result, not reevaluated by this search.


def run_search(evaluate: Callable) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--events", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--interactive", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()

    def event(kind: str, *values: object) -> None:
        if args.events:
            print("AURA_TRAIN", kind, *values, sep="\t", flush=True)

    def text(message: str = "") -> None:
        if not args.events:
            print(message, flush=True)

    simulation = aura_python.AuraSimulation()
    viewer = None
    rng = random.Random(42)
    candidates = [(0.0, 0.0),
                  *((rng.uniform(-0.5, 0.5), rng.uniform(-0.15, 0.15))
                    for _ in range(TRIALS))]

    event("start", len(candidates), EPISODE_LIMIT, FIXED_TARGET_RECORD)
    text("AURA  /  BALANCE FEEDBACK SEARCH")
    text("Method: search Kp/Kd feedback gains; symmetric hips, neutral knees; score = steps standing.")
    text(f"Episodes: {len(candidates)}  |  Limit: {EPISODE_LIMIT} steps (10 simulated seconds)")
    text("Search runs headlessly. Select a completed trial in the CLI to replay it in 3D.")

    # The CLI sends trial indices over stdin; gains remain at full Python precision.
    command_buffer = ""
    input_closed = False
    if args.interactive:
        os.set_blocking(sys.stdin.fileno(), False)

    def commands() -> list[str]:
        nonlocal command_buffer, input_closed
        if not args.interactive or input_closed:
            return []
        while select.select([sys.stdin], [], [], 0)[0]:
            try:
                data = os.read(sys.stdin.fileno(), 4096)
            except BlockingIOError:
                break
            if not data:
                input_closed = True
                break
            command_buffer += data.decode("ascii", errors="ignore")
        # Dispatch one command at a time so replay can consume a queued Esc/stop.
        line, separator, remaining = command_buffer.partition("\n")
        if not separator:
            command_buffer = command_buffer[-256:]
            return []
        command_buffer = remaining
        return [line]

    scores: list[tuple[float, float, int]] = []

    def replay(index: int) -> None:
        nonlocal viewer
        if not 0 <= index < len(scores):
            return
        replay_sim = aura_python.AuraSimulation()
        kp, kd, _ = scores[index]
        event("replay_start", index + 1)
        stopped = False
        last_report = 0.0

        def show(obs: dict, correction: float, clamped: bool) -> bool:
            nonlocal stopped, last_report
            for command in commands():
                if command in {"stop_replay", "quit"}:
                    stopped = True
            if stopped or input_closed or not viewer.is_open():
                return False
            viewer.render(replay_sim)
            now = time.monotonic()
            if now - last_report >= 0.1:
                event("replay_progress", obs["steps"], obs["time"], obs["pelvis_height"],
                      obs["pelvis_uprightness"], correction, int(clamped))
                last_report = now
            if obs["steps"]:
                time.sleep(1 / 120)
            return True

        try:
            if viewer is None:
                viewer = aura_python.AuraViewer(replay_sim)
            viewer.show()
            label = "Baseline" if index == 0 else f"Trial {index}"
            viewer.set_title(f"AURA - Replay {label} - Esc returns to trials")
            score = evaluate(replay_sim, kp, kd, show)
            if score is not None:
                obs = replay_sim.observation()
                event("replay_progress", obs["steps"], obs["time"], obs["pelvis_height"],
                      obs["pelvis_uprightness"], replay_sim.action()["left_hip_target"], 0)
                event("replay_result", score, obs["termination_reason"])
                viewer.set_title(f"AURA - Replay complete: {score} steps")
                # Briefly show the outcome, then return automatically to the table.
                deadline = time.monotonic() + 0.75
                while time.monotonic() < deadline and show(
                        obs, replay_sim.action()["left_hip_target"], False):
                    pass
        except Exception as error:
            event("replay_error", str(error).replace("\n", " "))
        finally:
            try:
                if viewer is not None:
                    viewer.hide()
            finally:
                event("replay_end")

    searching = True

    def service_commands() -> None:
        for command in commands():
            if command == "stop_search" and searching:
                raise KeyboardInterrupt
            if command.startswith("replay "):
                try:
                    replay(int(command.split()[1]) - 1)
                except (ValueError, IndexError):
                    pass

    try:
        for index, (kp, kd) in enumerate(candidates, start=1):
            def report(observation: dict, correction: float, clamped: bool) -> None:
                event("progress", index, observation["steps"], observation["time"],
                      observation["pelvis_height"], observation["pelvis_uprightness"],
                      kp, kd, correction, int(clamped))

            last_report = 0.0

            def observe(observation: dict, correction: float, clamped: bool) -> bool:
                nonlocal last_report, viewer
                service_commands()
                if args.interactive and input_closed:
                    raise KeyboardInterrupt
                if viewer is not None:
                    viewer.is_open()  # Keep hidden Cocoa windows responsive after a replay.
                now = time.monotonic()
                if args.events and (now - last_report >= 0.1 or observation["done"]):
                    report(observation, correction, clamped)
                    last_report = now
                return True

            score = evaluate(simulation, kp, kd, observe)
            observation = simulation.observation()
            scores.append((kp, kd, score))
            best_score = max(value for _, _, value in scores)
            label = "baseline" if index == 1 else f"trial {index - 1}/{TRIALS}"
            event("result", index, score, observation["time"], kp, kd,
                  observation["termination_reason"])
            text(f"[{index:02}/{len(candidates):02}] {label:<12} "
                 f"Kp {kp:+.3f}  Kd {kd:+.3f}   survived {score:4}/{EPISODE_LIMIT} steps "
                 f"({observation['time']:.2f} s)   best {best_score:4}")
        else:
            event("finished")
    except KeyboardInterrupt:
        event("stopped")
        text("Search stopped.")

    searching = False
    if scores:
        best_kp, best_kd, best_score = max(scores, key=lambda item: item[2])
        text()
        text(f"Best: Kp {best_kp:+.6f}  Kd {best_kd:+.6f}  |  {best_score}/{EPISODE_LIMIT} steps")
        text(f"Against neutral: {best_score - scores[0][2]:+d} steps  |  "
             f"Against historical fixed record ({FIXED_TARGET_RECORD}): "
             f"{best_score - FIXED_TARGET_RECORD:+d} steps")


    # Retain one hidden viewer after GUI use. On macOS the process must keep
    # pumping Cocoa events while it waits for another CLI replay command.
    if viewer is not None:
        viewer.hide()

    # Keep completed results available for selection and repeated replay in the CLI.
    if args.interactive:
        try:
            while not input_closed:
                service_commands()
                if viewer is not None:
                    viewer.is_open()  # Poll macOS events even while hidden.
                time.sleep(0.03)
        except KeyboardInterrupt:
            pass
