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

    def test_recent_battery_is_more_trusted_than_old_battery(self):
        memory = Memory()
        old_battery = (3, 2)
        recent_battery = (7, 4)

        memory.remember_battery(old_battery)
        for _ in range(20):
            memory.advance_step()
        memory.remember_battery(recent_battery)

        self.assertGreater(
            memory.battery_trust(recent_battery),
            memory.battery_trust(old_battery),
        )

    def test_repeated_confirmation_increases_trust(self):
        memory = Memory()
        repeatedly_confirmed = (3, 2)
        confirmed_once = (7, 4)

        memory.remember_battery(repeatedly_confirmed)
        memory.remember_battery(confirmed_once)
        memory.remember_battery(repeatedly_confirmed)

        self.assertGreater(
            memory.battery_trust(repeatedly_confirmed),
            memory.battery_trust(confirmed_once),
        )

    def test_stale_battery_has_zero_trust(self):
        memory = Memory()
        stale_battery = (3, 2)

        memory.remember_battery(stale_battery)
        memory.mark_battery_stale(stale_battery)

        self.assertEqual(memory.battery_trust(stale_battery), 0.0)

    def test_world_memory_mirrors_confirmed_battery(self):
        memory = Memory()

        memory.remember_battery([3, 2])

        entity = memory.world_memory.entity_at((3, 2))
        self.assertIsNotNone(entity)
        self.assertEqual(entity.entity_type, "Battery")
        self.assertEqual(entity.status, "confirmed")

    def test_world_memory_tracks_repeated_battery_confirmation(self):
        memory = Memory()
        position = (3, 2)
        memory.remember_battery(position)

        memory.advance_step()
        memory.remember_battery(position)

        entity = memory.world_memory.entity_at(position)
        self.assertIsNotNone(entity)
        self.assertEqual(entity.last_seen_step, memory.step)
        self.assertEqual(entity.times_confirmed, 2)

    def test_world_memory_tracks_stale_battery(self):
        memory = Memory()
        position = (3, 2)
        memory.remember_battery(position)

        memory.mark_battery_stale(position)

        entity = memory.world_memory.entity_at(position)
        self.assertIsNotNone(entity)
        self.assertEqual(entity.status, "stale")

    def test_confirmed_battery_query_matches_world_memory(self):
        memory = Memory()
        memory.remember_battery([3, 2])
        memory.remember_battery([7, 4])
        memory.mark_battery_stale((7, 4))

        self.assertEqual(
            set(memory.batteries()),
            {
                entity.position
                for entity in memory.world_memory.entities.values()
                if (
                    entity.entity_type == "Battery"
                    and entity.status == "confirmed"
                )
            },
        )

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
