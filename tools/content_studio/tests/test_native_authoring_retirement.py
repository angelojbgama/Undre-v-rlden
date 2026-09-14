from __future__ import annotations

import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]


class NativeAuthoringRetirementTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.windows_build = (REPOSITORY / "build.bat").read_text(encoding="utf-8")
        cls.linux_build = (REPOSITORY / "docker" / "linux_build.mk").read_text(encoding="utf-8")
        cls.cpp_tests = (REPOSITORY / "tests" / "test_main.cpp").read_text(encoding="utf-8")
        cls.playtest_runner = (
            REPOSITORY / "src" / "tools" / "playtest_runner.cpp"
        ).read_text(encoding="utf-8")

    def test_cpp_editor_subsystem_stays_retired(self) -> None:
        self.assertFalse((REPOSITORY / "src" / "editor").exists())
        for source in (self.cpp_tests, self.playtest_runner):
            self.assertNotIn('"editor/', source)
            self.assertNotIn("underworld::editor", source)
        self.assertNotIn("src\\editor", self.windows_build)
        self.assertNotIn("TEST_EDITOR_", self.linux_build)
        self.assertNotIn("src/editor", self.linux_build)

    def test_cpp_map_compile_and_map_editor_targets_stay_retired(self) -> None:
        self.assertFalse((REPOSITORY / "src" / "tools" / "map_compile.cpp").exists())
        self.assertFalse((REPOSITORY / "src" / "tools" / "map_compile_options.h").exists())
        self.assertNotIn('/OUT:"build\\bin\\map_compile.exe"', self.windows_build)
        self.assertNotIn('/OUT:"build\\bin\\map_editor.exe"', self.windows_build)
        self.assertNotIn("map_compile:", self.linux_build)
        self.assertNotIn("map_editor:", self.linux_build)

    def test_builds_remove_stale_native_authoring_binaries(self) -> None:
        self.assertIn(
            'del /q "build\\bin\\map_compile.exe" "build\\bin\\map_editor.exe"',
            self.windows_build,
        )
        self.assertLess(
            self.windows_build.index(
                'del /q "build\\bin\\map_compile.exe" "build\\bin\\map_editor.exe"'
            ),
            self.windows_build.index("where cl.exe"),
        )
        self.assertIn("retire_legacy_authoring:", self.linux_build)
        self.assertIn(
            "rm -f $(BUILD_DIR)/map_compile $(BUILD_DIR)/map_editor",
            self.linux_build,
        )
        self.assertIn("all: retire_legacy_authoring", self.linux_build)


if __name__ == "__main__":
    unittest.main()
