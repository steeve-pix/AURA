import tempfile
import unittest
from pathlib import Path

from brain.memory import Memory
from brain.memory_store import (
    load_memory,
    memory_path_for_world,
    save_memory,
)


class MemoryStoreTests(unittest.TestCase):
    def test_builds_separate_safe_path_for_each_world(self):
        directory = Path("data")

        first = memory_path_for_world(directory, "maze:1337:82x42:b60:u80")
        second = memory_path_for_world(directory, "maze:42:82x42:b60:u80")

        self.assertEqual(first, directory / "maze_1337_82x42_b60_u80.json")
        self.assertNotEqual(first, second)

    def test_rejects_empty_world_id(self):
        with self.assertRaises(ValueError):
            memory_path_for_world(Path("data"), "")

    def test_blank_file_loads_as_empty_memory(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "memory.json"
            path.write_text("")

            memory = load_memory(path, "maze:1337")

            self.assertEqual(memory.known_cells, {})
            self.assertEqual(memory.visit_counts, {})

    def test_round_trip_is_scoped_to_world_id(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "memory.json"
            memory = Memory()
            memory.remember_cell([2, 3], "Empty")
            memory.record_visit([2, 3])

            save_memory(memory, path, "maze:1337")

            loaded = load_memory(path, "maze:1337")
            other_world = load_memory(path, "maze:42")

            self.assertEqual(loaded.known_cells[(2, 3)], "Empty")
            self.assertEqual(loaded.visit_count((2, 3)), 1)
            self.assertEqual(other_world.known_cells, {})


if __name__ == "__main__":
    unittest.main()
