"""Tests for the UI screen authoring service and the registry manifest sync."""

from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.content_studio.formats.content_json import CONTENT_CATEGORIES
from tools.content_studio.formats.json_io import encode_json
from tools.content_studio.model.content_workspace import ContentWorkspace
from tools.content_studio.services.ui_authoring_service import UiAuthoringService
from tools.content_studio.services import ui_registry

REPOSITORY = Path(__file__).resolve().parents[3]


def content_root(version: int = 5) -> dict[str, object]:
    result: dict[str, object] = {
        "format": "dungeon-underworld-content",
        "version": version,
    }
    result.update({category: [] for category in CONTENT_CATEGORIES})
    result["visualImages"] = [{
        "id": "image.hud",
        "root": "gameAssets",
        "relativePath": "ui.png",
    }]
    result["staticSprites"] = [
        {"id": "spr.heart.full", "imageId": "image.hud",
         "source": {"x": 0, "y": 0, "width": 11, "height": 10},
         "anchor": {"x": 0, "y": 0}},
        {"id": "spr.liquid", "imageId": "image.hud",
         "source": {"x": 16, "y": 0, "width": 20, "height": 8},
         "anchor": {"x": 0, "y": 0}},
    ]
    return result


def make_workspace(root: Path, version: int = 5) -> ContentWorkspace:
    root.mkdir(parents=True, exist_ok=True)
    (root / "content.json").write_text(encode_json(content_root(version)), encoding="utf-8")
    return ContentWorkspace.open(root)


def find_cpp_tool(repository: Path, name: str) -> Path | None:
    candidates = [repository / "build" / "bin" / f"{name}.exe", repository / "build" / f"{name}"]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


class UiAuthoringServiceTests(unittest.TestCase):
    def make_service(self, directory: Path, version: int = 5) -> UiAuthoringService:
        return UiAuthoringService(make_workspace(directory, version))

    def test_create_screen_normalizes_prefix_and_promotes_version(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            service = self.make_service(Path(directory), version=5)
            screen = service.create_screen("hud", kind="hud")
            self.assertEqual("screen.hud", screen.data["id"])
            self.assertEqual(7, service.workspace.files[0].data["version"])
            self.assertEqual("group", screen.data["root"]["component"])

    def test_node_tree_editing_round_trips(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            service = self.make_service(root)
            service.create_screen("screen.hud")
            service.add_node("screen.hud", "hud.health", "meter", offset=(3, 2))
            service.set_meter("screen.hud", "hud.health", mode="segmented",
                              segment_value=2, spacing=1,
                              sprites={"full": "spr.heart.full"})
            service.set_meter("screen.hud", "hud.health",
                              empty_rect={"color": {"r": 54, "g": 30, "b": 38, "a": 255},
                                          "offsetX": 0, "offsetY": 1, "width": 9, "height": 8})
            service.bind_property("screen.hud", "hud.health", "value",
                                  "player.health.current")
            service.bind_property("screen.hud", "hud.health", "maximum",
                                  "player.health.max")
            service.add_state("screen.hud", "hud.health", "lowHealth",
                              visual={"tint": {"r": 255, "g": 64, "b": 64, "a": 255}},
                              condition={"source": "player.health.percentage",
                                         "operator": "lessOrEqual", "value": 30})
            service.workspace.save_all()

            reloaded = ContentWorkspace.open(root)
            other = UiAuthoringService(reloaded)
            screen = other.find("screen.hud")
            self.assertIsNotNone(screen)
            node = screen.data["root"]["children"][0]
            self.assertEqual("meter", node["component"])
            self.assertEqual("player.health.current", node["bindings"][0]["source"])
            self.assertEqual(1, node["meter"]["spacing"])
            self.assertEqual("lowHealth", node["states"][0]["id"])

    def test_bar_and_orb_variants_author_through_the_same_service(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            service = self.make_service(Path(directory))
            service.create_screen("screen.hud")
            service.add_node("screen.hud", "hud.health", "meter", offset=(10, 10))
            service.set_meter("screen.hud", "hud.health", mode="fillHorizontal",
                              sprites={"fill": "spr.liquid"})
            service.bind_property("screen.hud", "hud.health", "value", "player.health.current")
            service.bind_property("screen.hud", "hud.health", "maximum", "player.health.max")
            node = service.find("screen.hud").data["root"]["children"][0]
            self.assertEqual("fillHorizontal", node["meter"]["mode"])

            service.set_meter("screen.hud", "hud.health", mode="fillVertical")
            node = service.find("screen.hud").data["root"]["children"][0]
            self.assertEqual("fillVertical", node["meter"]["mode"])

    def test_unknown_references_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            service = self.make_service(Path(directory))
            service.create_screen("screen.hud")
            service.add_node("screen.hud", "hud.health", "meter")
            with self.assertRaises(ValueError):
                service.add_node("screen.hud", "hud.other", "slider")
            with self.assertRaises(ValueError):
                service.bind_property("screen.hud", "hud.health", "value",
                                      "player.mana.current")
            with self.assertRaises(ValueError):
                service.bind_property("screen.hud", "hud.health", "text",
                                      "player.gold")
            with self.assertRaises(ValueError):
                service.set_meter("screen.hud", "hud.health",
                                  sprites={"full": "spr.ghost"})
            with self.assertRaises(ValueError):
                service.set_meter("screen.hud", "hud.health", mode="fillVertical")
            with self.assertRaises(ValueError):
                service.add_action("screen.hud", "hud.health", "game.quit")
            with self.assertRaises(ValueError):
                service.add_node("screen.hud", "hud.health", "panel")

    def test_node_removal_and_states(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            service = self.make_service(Path(directory))
            service.create_screen("screen.hud")
            service.add_node("screen.hud", "hud.frame", "panel")
            service.add_node("screen.hud", "hud.gold", "text", parent_id="hud.frame")
            service.set_text("screen.hud", "hud.gold", "0")
            service.bind_property("screen.hud", "hud.gold", "text", "player.gold")
            service.add_state("screen.hud", "hud.frame", "hidden",
                              visual={"visible": False},
                              condition={"source": "player.health.percentage",
                                         "operator": "equal", "value": 0})
            with self.assertRaises(ValueError):
                service.remove_node("screen.hud", "screen.hud.root")
            service.remove_node("screen.hud", "hud.frame")
            screen = service.find("screen.hud")
            self.assertEqual([], screen.data["root"]["children"])


class BuiltinScreensTests(unittest.TestCase):
    def test_builtin_asset_matches_cpp_export(self) -> None:
        tool = find_cpp_tool(REPOSITORY, "ui_manifest")
        if tool is None:
            self.skipTest("ui_manifest tool is not built")
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "builtin.json"
            subprocess.run([str(tool), "--screens", str(out)],
                           capture_output=True, text=True, check=True)
            generated = json.loads(out.read_text(encoding="utf-8"))
        from tools.content_studio.services.ui_authoring_service import BUILTIN_SCREEN_ASSET
        checked_in = json.loads(BUILTIN_SCREEN_ASSET.read_text(encoding="utf-8"))
        self.assertEqual(generated, checked_in)

    def test_override_builtin_creates_editable_workspace_copy(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            service = UiAuthoringService(make_workspace(root))
            self.assertTrue(service.is_builtin_only("screen.hud"))
            override = service.override_builtin("screen.hud")
            self.assertEqual("hud.root", override.data["root"]["id"])
            self.assertFalse(service.is_builtin_only("screen.hud"))
            service.set_layout("screen.hud", "hud.health", offset_x=20)
            hearts = service.find("screen.hud").data["root"]["children"][2]
            self.assertEqual(20, hearts["layout"]["offsetX"])
            self.assertEqual(201, service.builtin_find("screen.hud")["root"]["children"][2]["layout"]["offsetX"])
            # builtin visuals resolve for overrides, like the C++ overlay
            service.set_meter("screen.hud", "hud.health",
                              sprites={"full": "spr.hud.heart"})
            service.workspace.save_all()
            reloaded = ContentWorkspace.open(root)
            self.assertIsNotNone(reloaded.find("uiScreens", "screen.hud"))


class UiManifestSyncTests(unittest.TestCase):
    def test_python_registry_mirror_matches_cpp_manifest(self) -> None:
        tool = find_cpp_tool(REPOSITORY, "ui_manifest")
        if tool is None:
            self.skipTest("ui_manifest tool is not built")
        result = subprocess.run([str(tool)], capture_output=True, text=True, check=True)
        manifest = json.loads(result.stdout)
        self.assertEqual(list(ui_registry.COMPONENTS), manifest["components"])
        self.assertEqual(list(ui_registry.ANCHORS), manifest["anchors"])
        self.assertEqual(list(ui_registry.METER_MODES), manifest["meterModes"])
        self.assertEqual(list(ui_registry.CONDITION_OPERATORS), manifest["conditionOperators"])
        self.assertEqual(list(ui_registry.SCREEN_KINDS), manifest["screenKinds"])
        self.assertEqual(list(ui_registry.BINDING_PATHS), manifest["bindings"])
        self.assertEqual(list(ui_registry.ACTIONS), manifest["actions"])
        properties = {entry["component"]: entry["properties"]
                      for entry in manifest["componentProperties"]}
        for component, expected in ui_registry.COMPONENT_PROPERTIES.items():
            self.assertEqual(list(expected), properties[component])


if __name__ == "__main__":
    unittest.main()
