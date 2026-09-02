import unittest

from brain.memory import Memory
from brain.simulation_snapshot import (
    capture_brain_snapshot,
    restore_brain_snapshot,
)


class BrainSimulationSnapshotTests(unittest.TestCase):
    def test_restored_branches_do_not_share_mutable_memory(self):
        battery_position = (2, 1)
        memory = Memory()
        memory.remember_battery(battery_position)
        snapshot = capture_brain_snapshot(memory)

        branch_a = restore_brain_snapshot(snapshot)
        branch_b = restore_brain_snapshot(snapshot)

        branch_a.mark_battery_stale(battery_position)

        self.assertNotIn(battery_position, branch_a.batteries())
        self.assertIn(battery_position, branch_b.batteries())
        self.assertIn(battery_position, snapshot.memory.batteries())


if __name__ == "__main__":
    unittest.main()
