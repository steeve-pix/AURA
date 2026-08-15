import unittest

from brain.world_memory import WorldMemory


class WorldMemoryTests(unittest.TestCase):
    def setUp(self):
        self.memory = WorldMemory()

    def test_entities_of_type_returns_only_matching_entities(self):
        self.memory.remember_entity([1, 2], "Battery", step=1)
        self.memory.remember_entity([3, 4], "Unknown", step=1)
        self.memory.remember_entity([5, 6], "Food", step=1)
        self.memory.remember_entity([7, 8], "Battery", step=1)

        batteries = self.memory.entities_of_type("Battery")

        self.assertEqual(
            {entity.position for entity in batteries},
            {(1, 2), (7, 8)},
        )

    def test_entities_of_type_excludes_stale_entities_by_default(self):
        self.memory.remember_entity([1, 2], "Battery", step=1)
        self.memory.remember_entity([3, 4], "Battery", step=1)
        self.memory.mark_stale([3, 4])

        batteries = self.memory.entities_of_type("Battery")

        self.assertEqual(
            [entity.position for entity in batteries],
            [(1, 2)],
        )

    def test_entities_of_type_can_include_stale_entities(self):
        self.memory.remember_entity([1, 2], "Battery", step=1)
        self.memory.mark_stale([1, 2])

        batteries = self.memory.entities_of_type(
            "Battery",
            confirmed_only=False,
        )

        self.assertEqual(
            [entity.position for entity in batteries],
            [(1, 2)],
        )
        self.assertEqual(batteries[0].status, "stale")

    def test_has_entity_returns_true_for_matching_confirmed_entity(self):
        self.memory.remember_entity([12, 5], "Battery", step=1)

        self.assertTrue(
            self.memory.has_entity((12, 5), "Battery")
        )

    def test_has_entity_returns_false_for_wrong_type(self):
        self.memory.remember_entity([12, 5], "Battery", step=1)

        self.assertFalse(
            self.memory.has_entity((12, 5), "Food")
        )

    def test_has_entity_returns_false_for_missing_position(self):
        self.assertFalse(
            self.memory.has_entity((12, 5), "Battery")
        )


if __name__ == "__main__":
    unittest.main()
