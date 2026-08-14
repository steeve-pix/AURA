import unittest

from brain.memory import Memory


class MemoryTests(unittest.TestCase):
    def test_remembers_battery(self):
        memory = Memory()

        memory.remember_battery([3, 2])

        self.assertIn(
            (3, 2),
            memory.batteries()
        )

    def test_does_not_duplicate_battery(self):
        memory = Memory()

        memory.remember_battery([3, 2])
        memory.remember_battery([3, 2])

        self.assertEqual(
            len(memory.batteries()),
            1
        )

    def test_forgets_battery(self):
        memory = Memory()

        memory.remember_battery([3, 2])
        memory.forget_battery((3, 2))

        self.assertNotIn(
            (3, 2),
            memory.batteries()
        )

    def test_forgetting_unknown_battery_is_safe(self):
        memory = Memory()

        memory.forget_battery((3, 2))

        self.assertEqual(memory.batteries(), [])

    def test_stale_battery_is_not_returned_as_confirmed(self):
        memory = Memory()
        memory.remember_battery([3, 2])

        memory.mark_battery_stale((3, 2))

        self.assertNotIn((3, 2), memory.batteries())

    def test_marking_unknown_battery_stale_is_safe(self):
        memory = Memory()

        memory.mark_battery_stale((3, 2))

        self.assertEqual(memory.batteries(), [])

    def test_records_visits(self):
        memory = Memory()

        memory.record_visit([2, 2])
        memory.record_visit([2, 2])

        self.assertEqual(
            memory.visit_count((2, 2)),
            2
        )

    def test_records_failed_targets(self):
        memory = Memory()

        memory.record_failed_target([8, 3])

        self.assertIn(
            (8, 3),
            memory.failed_targets,
        )

        self.assertEqual(
            memory.failed_target_count((8, 3)),
            1
        )


if __name__ == "__main__":
    unittest.main()
