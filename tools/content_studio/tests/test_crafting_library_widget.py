from __future__ import annotations

import os
import unittest

os.environ.setdefault(
    "QT_QPA_PLATFORM",
    "offscreen",
)

try:
    from PySide6.QtWidgets import QApplication
except ImportError:  # pragma: no cover
    QApplication = None  # type: ignore[assignment]

from tools.content_studio.services.crafting_authoring_service import CraftingAuthoringService
from tools.content_studio.services.localization import Translator
from tools.content_studio.tests.test_crafting_authoring import (
    BASE_INPUTS,
    BASE_OUTPUTS,
    make_workspace,
)

HERB = "item.red_herb"
BOTTLE = "item.empty_bottle"
POTION = "item.life_potion"


@unittest.skipIf(
    QApplication is None,
    "PySide6 is not installed",
)
class CraftingLibraryWidgetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        assert QApplication is not None

        cls.application = (
            QApplication.instance()
            or QApplication([])
        )

    def _workspace(self):
        import tempfile
        from pathlib import Path

        directory = tempfile.TemporaryDirectory()  # noqa: SIM115
        self.addCleanup(directory.cleanup)
        return make_workspace(Path(directory.name))

    def _widget(self, workspace):
        from tools.content_studio.ui.crafting_library_widget import CraftingLibraryWidget

        widget = CraftingLibraryWidget(
            workspace,
            None,
            Translator("en-US"),
        )

        self.addCleanup(widget.close)

        return widget

    def test_library_lists_created_recipes_and_details(self) -> None:
        workspace = self._workspace()
        service = CraftingAuthoringService(workspace)
        service.create_recipe("Life Potion", "recipe.life_potion", BASE_INPUTS, BASE_OUTPUTS)

        widget = self._widget(workspace)
        widget.refresh()

        self.assertEqual(1, widget.recipes.count())
        item = widget.recipes.item(0)
        self.assertIn("recipe.life_potion", item.text())

        widget.recipes.setCurrentItem(item)
        details = widget.details.text()
        self.assertIn("recipe.life_potion", details)
        self.assertIn("2x Red Herb", details)
        self.assertIn("1x Life Potion", details)

    def test_search_filters_recipes(self) -> None:
        workspace = self._workspace()
        service = CraftingAuthoringService(workspace)
        service.create_recipe("Life Potion", "recipe.life_potion", BASE_INPUTS, BASE_OUTPUTS)
        service.create_recipe(
            "Slag Burn",
            "recipe.slag",
            [BASE_INPUTS[0], {"itemId": "item.coal", "quantity": 1}],
            [{"itemId": "item.slag", "quantity": 1}],
        )

        widget = self._widget(workspace)
        self.assertEqual(2, widget.recipes.count())

        widget.search.setText("life")
        self.assertEqual(1, widget.recipes.count())

        widget.search.setText("nothing-matches")
        self.assertEqual(0, widget.recipes.count())

    def test_widget_refreshes_after_service_change(self) -> None:
        workspace = self._workspace()
        widget = self._widget(workspace)
        self.assertEqual(0, widget.recipes.count())

        service = CraftingAuthoringService(workspace)
        service.create_recipe("Life Potion", "recipe.potion", BASE_INPUTS, BASE_OUTPUTS)
        widget.refresh()
        self.assertEqual(1, widget.recipes.count())

        service.delete_recipe("recipe.potion")
        widget.refresh()
        self.assertEqual(0, widget.recipes.count())


if __name__ == "__main__":
    unittest.main()
