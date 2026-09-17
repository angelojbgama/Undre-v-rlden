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

from tools.content_studio.services.localization import (
    Translator,
)
from tools.content_studio.tests.test_attack_authoring_service import (
    attack_content,
    make_workspace,
)


@unittest.skipIf(
    QApplication is None,
    "PySide6 is not installed",
)
class AttackLibraryWidgetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        assert QApplication is not None

        cls.application = (
            QApplication.instance()
            or QApplication([])
        )

    def _widget(self, workspace):
        from tools.content_studio.ui.attack_library_widget import (
            AttackLibraryWidget,
        )

        widget = AttackLibraryWidget(
            workspace,
            Translator("en-US"),
        )

        self.addCleanup(widget.close)

        return widget

    def test_library_lists_builtin_and_authored_attacks(self) -> None:
        temporary, workspace = make_workspace(
            attack_content()
        )

        self.addCleanup(temporary.cleanup)

        widget = self._widget(workspace)

        labels = [
            widget.attacks.item(row).text()
            for row in range(widget.attacks.count())
        ]

        self.assertEqual(
            5,
            len(labels),
        )

        self.assertTrue(
            any(
                "attack.player.sword" in label
                for label in labels
            )
        )

        self.assertTrue(
            any(
                "attack.slime.bounce" in label
                for label in labels
            )
        )

    def test_edit_dialog_round_trip_updates_workspace(self) -> None:
        temporary, workspace = make_workspace(
            attack_content()
        )

        self.addCleanup(temporary.cleanup)

        widget = self._widget(workspace)

        dialog = widget._build_editor(
            "attack.player.sword"
        )

        self.assertEqual(
            1,
            dialog.damage_amount.value(),
        )

        dialog.damage_amount.setValue(3)

        dialog._save()

        stored = workspace.find(
            "attacks",
            "attack.player.sword",
        )

        self.assertIsNotNone(stored)

        assert stored is not None

        self.assertEqual(
            3,
            stored.data["damage"]["amount"],
        )

        self.assertEqual(
            24,
            stored.data["totalTicks"],
        )

    def test_main_window_exposes_attacks_tab(self) -> None:
        from tools.content_studio.ui.attack_library_widget import (
            AttackLibraryWidget,
        )
        from tools.content_studio.ui.main_window import (
            MainWindow,
        )
        from tools.content_studio.model.world_project import (
            WorldProject,
        )

        temporary, workspace = make_workspace(
            attack_content()
        )

        self.addCleanup(temporary.cleanup)

        project = WorldProject.new()

        window = MainWindow(
            project,
            workspace,
        )

        self.addCleanup(window.close)

        self.assertIsInstance(
            window.attack_library,
            AttackLibraryWidget,
        )

        window.mode_tabs.setCurrentIndex(0)

        labels = [
            window._section_tabs.tabText(index)
            for index in range(
                window._section_tabs.count()
            )
        ]

        attack_label = window.translator("attacks_tab")

        self.assertIn(attack_label, labels)

        attack_index = labels.index(attack_label)

        window._section_tabs.setCurrentIndex(
            attack_index
        )

        self.assertIs(
            window._map_panels.currentWidget(),
            window.attack_library,
        )


@unittest.skipIf(
    QApplication is None,
    "PySide6 is not installed",
)
class PlayerAttackContextMenuTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        assert QApplication is not None

        cls.application = (
            QApplication.instance()
            or QApplication([])
        )

    def _widget_with_player(self):
        from tools.content_studio.ui.player_library_widget import (
            PlayerLibraryWidget,
        )

        data = attack_content()

        data["players"].append({  # type: ignore[union-attr]
            "id": "player.hero",
            "visualSetId": "visual.player.hero",
            "progressionId": "progression.default",
        })

        temporary, workspace = make_workspace(data)

        self.addCleanup(temporary.cleanup)

        widget = PlayerLibraryWidget(
            workspace,
            None,
            Translator("en-US"),
        )

        self.addCleanup(widget.close)

        return widget

    def test_context_menu_policy_and_attack_flow(self) -> None:
        assert QApplication is not None

        from PySide6.QtCore import Qt

        widget = self._widget_with_player()

        self.assertEqual(
            Qt.ContextMenuPolicy.CustomContextMenu,
            widget.list.contextMenuPolicy(),
        )

        self.assertEqual(
            1,
            widget.list.count(),
        )

        changed: list = []

        statuses: list[str] = []

        widget.changed.connect(
            lambda: changed.append(True)
        )

        widget.status_changed.connect(
            statuses.append
        )

        dialog = widget._build_attack_editor(
            "attack.player.sword"
        )

        self.assertEqual(
            1,
            dialog.damage_amount.value(),
        )

        dialog.damage_amount.setValue(4)

        # Save first (persists + accept()); the patched exec then
        # replays the widget's post-accept signal handling.
        dialog._save()

        dialog.exec = lambda: 1  # Accepted, without a modal loop.

        widget._build_attack_editor = (
            lambda attack_id: dialog
        )

        widget._open_attack_editor(
            "attack.player.sword"
        )

        self.assertTrue(changed)

        stored = widget.workspace.find(
            "attacks",
            "attack.player.sword",
        )

        self.assertIsNotNone(stored)

        assert stored is not None

        self.assertEqual(
            4,
            stored.data["damage"]["amount"],
        )

        self.assertIn(
            "attack.player.sword",
            statuses[-1],
        )

    def test_player_attack_menu_offers_both_slots(self) -> None:
        from tools.content_studio.services.attack_authoring_service import (
            PLAYER_ATTACK_IDS,
        )

        self.assertEqual(
            {"attack.player.sword", "attack.player.bow"},
            set(PLAYER_ATTACK_IDS),
        )


if __name__ == "__main__":
    unittest.main()
