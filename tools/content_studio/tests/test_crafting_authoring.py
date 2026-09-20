from __future__ import annotations

import importlib
import tempfile
import unittest
from pathlib import Path

from tools.content_studio.formats.content_json import CONTENT_CATEGORIES, CONTENT_VERSION
from tools.content_studio.formats.json_io import encode_json
from tools.content_studio.model.content_workspace import ContentWorkspace


def content_root(version: int = 5) -> dict[str, object]:
    result: dict[str, object] = {
        "format": "dungeon-underworld-content",
        "version": version,
    }
    result.update({
        category: []
        for category in CONTENT_CATEGORIES
    })
    result["visualImages"] = [{
        "id": "image.items",
        "root": "gameAssets",
        "relativePath": "items.png",
    }]
    result["staticSprites"] = [{
        "id": "visual.item.herb",
        "imageId": "image.items",
        "source": {"x": 0, "y": 0, "width": 16, "height": 16},
        "anchor": {"x": 0, "y": 0},
    }]
    result["items"] = [
        {"id": "item.red_herb", "visualId": "visual.item.herb", "category": "consumable", "stackLimit": 66},
        {"id": "item.empty_bottle", "visualId": "visual.item.herb", "category": "misc", "stackLimit": 66},
        {"id": "item.coal", "visualId": "visual.item.herb", "category": "misc", "stackLimit": 66},
        {"id": "item.wood", "visualId": "visual.item.herb", "category": "misc", "stackLimit": 66},
        {"id": "item.life_potion", "visualId": "visual.item.herb", "category": "consumable", "stackLimit": 66},
        {"id": "item.slag", "visualId": "visual.item.herb", "category": "misc", "stackLimit": 66},
    ]
    return result


def make_workspace(root: Path, version: int = 5) -> ContentWorkspace:
    root.mkdir(parents=True, exist_ok=True)
    (root / "content.json").write_text(
        encode_json(content_root(version)),
        encoding="utf-8",
    )
    return ContentWorkspace.open(root)


def crafting_service(workspace: ContentWorkspace):
    module = importlib.import_module(
        "tools.content_studio.services.crafting_authoring_service"
    )
    return module.CraftingAuthoringService(workspace)


HERB = "item.red_herb"
BOTTLE = "item.empty_bottle"
POTION = "item.life_potion"
COAL = "item.coal"

BASE_INPUTS = [
    {"itemId": HERB, "quantity": 2},
    {"itemId": BOTTLE, "quantity": 1},
]
BASE_OUTPUTS = [{"itemId": POTION, "quantity": 1}]


class CraftingAuthoringServiceTests(unittest.TestCase):
    def test_create_recipe_writes_inputs_outputs_and_descriptor(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(Path(directory))
            service = crafting_service(workspace)

            created = service.create_recipe(
                "Poção de Vida",
                "recipe.life_potion",
                BASE_INPUTS,
                BASE_OUTPUTS,
            )

            self.assertEqual("recipe.life_potion", created.definition_id)
            self.assertEqual(HERB, created.data["inputs"][0]["itemId"])
            self.assertEqual(2, created.data["inputs"][0]["quantity"])
            self.assertEqual(POTION, created.data["outputs"][0]["itemId"])

            descriptor = workspace.find("authoringDescriptors", "recipe.life_potion")
            self.assertIsNotNone(descriptor)
            self.assertEqual("Poção de Vida", descriptor.data["displayName"])
            self.assertEqual("craftingRecipe", descriptor.data["category"])

            indexed = workspace.find("craftingRecipes", "recipe.life_potion")
            self.assertEqual("Poção de Vida", indexed.display_name)

    def test_create_recipe_rejects_invalid_shapes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(Path(directory))
            service = crafting_service(workspace)

            with self.assertRaises(ValueError):
                service.create_recipe("R", "life_potion", BASE_INPUTS, BASE_OUTPUTS)
            with self.assertRaises(ValueError):
                service.create_recipe("R", "recipe.x", [BASE_INPUTS[0]], BASE_OUTPUTS)
            with self.assertRaises(ValueError):
                service.create_recipe("R", "recipe.x", [], BASE_OUTPUTS)
            too_many = BASE_INPUTS + [
                {"itemId": COAL, "quantity": 1},
                {"itemId": "item.wood", "quantity": 1},
                {"itemId": POTION, "quantity": 1},
            ]
            with self.assertRaises(ValueError):
                service.create_recipe("R", "recipe.x", too_many, BASE_OUTPUTS)
            with self.assertRaises(ValueError):
                service.create_recipe("R", "recipe.x", BASE_INPUTS, [])
            with self.assertRaises(ValueError):
                service.create_recipe(
                    "R", "recipe.x",
                    [BASE_INPUTS[0], {"itemId": HERB, "quantity": 1}],
                    BASE_OUTPUTS)
            with self.assertRaises(ValueError):
                service.create_recipe(
                    "R", "recipe.x",
                    [BASE_INPUTS[0], {"itemId": BOTTLE, "quantity": 0}],
                    BASE_OUTPUTS)
            with self.assertRaises(ValueError):
                service.create_recipe(
                    "R", "recipe.x",
                    [BASE_INPUTS[0], {"itemId": "item.ghost", "quantity": 1}],
                    BASE_OUTPUTS)
            with self.assertRaises(ValueError):
                service.create_recipe(
                    "R", "recipe.x",
                    BASE_INPUTS,
                    [{"itemId": POTION, "quantity": 1}, {"itemId": POTION, "quantity": 1}])
            self.assertIsNone(workspace.find("craftingRecipes", "recipe.x"))

    def test_create_recipe_promotes_workspace_file_to_version_6(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(Path(directory), version=5)
            service = crafting_service(workspace)
            self.assertEqual(5, workspace.files[0].data["version"])

            service.create_recipe("Poção", "recipe.potion", BASE_INPUTS, BASE_OUTPUTS)

            self.assertEqual(6, workspace.files[0].data["version"])
            workspace.save_all()
            reloaded = ContentWorkspace.open(Path(directory))
            self.assertEqual(6, reloaded.files[0].data["version"])
            self.assertIsNotNone(reloaded.find("craftingRecipes", "recipe.potion"))

    def test_new_workspace_initializes_crafting_category_at_version_6(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = ContentWorkspace.new(Path(directory) / "content.json")
            self.assertEqual(CONTENT_VERSION, 6)
            self.assertIn("craftingRecipes", workspace.files[0].data)
            self.assertEqual([], workspace.files[0].data["craftingRecipes"])
            self.assertEqual(6, workspace.files[0].data["version"])

    def test_update_recipe_replaces_ingredients(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(Path(directory))
            service = crafting_service(workspace)
            service.create_recipe("Poção", "recipe.potion", BASE_INPUTS, BASE_OUTPUTS)

            updated = service.update_recipe(
                "recipe.potion",
                display_name="Poção Forte",
                inputs=[{"itemId": HERB, "quantity": 3}, {"itemId": COAL, "quantity": 1}],
                outputs=[{"itemId": POTION, "quantity": 2}, {"itemId": "item.slag", "quantity": 1}],
            )

            self.assertEqual(COAL, updated.data["inputs"][1]["itemId"])
            self.assertEqual(2, len(updated.data["outputs"]))
            descriptor = workspace.find("authoringDescriptors", "recipe.potion")
            self.assertEqual("Poção Forte", descriptor.data["displayName"])

    def test_delete_recipe_releases_item_dependency(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(Path(directory))
            service = crafting_service(workspace)
            item_service = importlib.import_module(
                "tools.content_studio.services.item_authoring_service"
            ).ItemAuthoringService(workspace)

            service.create_recipe("Poção", "recipe.potion", BASE_INPUTS, BASE_OUTPUTS)
            usages = workspace.find_usages(HERB)
            self.assertTrue(any(
                usage.category == "craftingRecipes" and usage.definition_id == "recipe.potion"
                for usage in usages
            ))

            with self.assertRaises(ValueError):
                item_service.delete_item(HERB)

            service.delete_recipe("recipe.potion")
            self.assertIsNone(workspace.find("craftingRecipes", "recipe.potion"))
            self.assertIsNone(workspace.find("authoringDescriptors", "recipe.potion"))

            # Without the recipe the Item can be removed again.
            self.assertTrue(item_service.delete_item(HERB))
            self.assertIsNone(workspace.find("items", HERB))

    def test_undo_redo_round_trip(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(Path(directory))
            service = crafting_service(workspace)

            service.create_recipe("Poção", "recipe.potion", BASE_INPUTS, BASE_OUTPUTS)
            self.assertIsNotNone(workspace.find("craftingRecipes", "recipe.potion"))
            self.assertTrue(workspace.undo())
            self.assertIsNone(workspace.find("craftingRecipes", "recipe.potion"))
            self.assertTrue(workspace.redo())
            self.assertIsNotNone(workspace.find("craftingRecipes", "recipe.potion"))

            service.update_recipe("recipe.potion", display_name="Renomeada")
            self.assertTrue(workspace.undo())
            recipe = workspace.find("craftingRecipes", "recipe.potion")
            self.assertEqual("Poção", recipe.display_name)

    def test_rename_recipe_updates_id_and_descriptor(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(Path(directory))
            service = crafting_service(workspace)
            service.create_recipe("Poção", "recipe.potion", BASE_INPUTS, BASE_OUTPUTS)

            renamed = service.rename_recipe("recipe.potion", "recipe.life_potion")
            self.assertEqual("recipe.life_potion", renamed.definition_id)
            self.assertIsNone(workspace.find("craftingRecipes", "recipe.potion"))
            descriptor = workspace.find("authoringDescriptors", "recipe.life_potion")
            self.assertIsNotNone(descriptor)

    def test_save_and_reload_preserves_recipes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(Path(directory), version=5)
            service = crafting_service(workspace)
            service.create_recipe("Poção", "recipe.potion", BASE_INPUTS, BASE_OUTPUTS)
            workspace.save_all()

            reloaded = ContentWorkspace.open(Path(directory))
            recipe = reloaded.find("craftingRecipes", "recipe.potion")
            self.assertIsNotNone(recipe)
            self.assertEqual(HERB, recipe.data["inputs"][0]["itemId"])
            self.assertEqual(6, reloaded.files[0].data["version"])

    def test_recipes_support_three_and_four_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(Path(directory))
            service = crafting_service(workspace)

            service.create_recipe(
                "Espada",
                "recipe.sword",
                [
                    {"itemId": HERB, "quantity": 2},
                    {"itemId": COAL, "quantity": 1},
                    {"itemId": "item.wood", "quantity": 1},
                ],
                [{"itemId": POTION, "quantity": 1}],
            )
            service.create_recipe(
                "Quatro",
                "recipe.four",
                [
                    {"itemId": HERB, "quantity": 1},
                    {"itemId": BOTTLE, "quantity": 1},
                    {"itemId": COAL, "quantity": 1},
                    {"itemId": "item.wood", "quantity": 1},
                ],
                [
                    {"itemId": POTION, "quantity": 1},
                    {"itemId": "item.slag", "quantity": 2},
                ],
            )
            four = workspace.find("craftingRecipes", "recipe.four")
            self.assertEqual(4, len(four.data["inputs"]))
            self.assertEqual(2, len(four.data["outputs"]))


if __name__ == "__main__":
    unittest.main()
