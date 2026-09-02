from dataclasses import dataclass

from brain.memory import Memory


@dataclass
class BrainSimulationSnapshot:
    memory: Memory


def capture_brain_snapshot(memory: Memory) -> BrainSimulationSnapshot:
    return BrainSimulationSnapshot(memory=memory.clone_for_simulation())


def restore_brain_snapshot(snapshot: BrainSimulationSnapshot) -> Memory:
    return snapshot.memory.clone_for_simulation()
