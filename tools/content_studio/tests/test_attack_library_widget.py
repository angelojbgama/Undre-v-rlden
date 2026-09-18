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

        self.assertNotIn(attack_label, labels)

        items_tab = window.translator("items_tab")

        items_index = labels.index(items_tab)

        window._section_tabs.setCurrentIndex(
            items_index
        )

        self.assertIs(
            window._map_panels.currentWidget(),
            window.item_library,
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

        manager = widget._build_attack_manager()

        editor = manager.library._build_editor(
            "attack.player.sword"
        )

        self.assertEqual(
            1,
            editor.damage_amount.value(),
        )

        editor.damage_amount.setValue(4)

        editor._save()

        editor.exec = lambda: 1  # Accepted, without a modal loop.

        # The library rebuilds its editor internally; inject the
        # prepared one so no real modal loop starts.
        manager.library._build_editor = (
            lambda attack_id: editor
        )

        manager.library._open_editor(
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

    def test_attack_manager_lists_future_attacks_automatically(self) -> None:
        widget = self._widget_with_player()

        manager = widget._build_attack_manager()

        self.assertEqual(
            5,
            manager.library.attacks.count(),
        )

        # A newly authored attack shows up without any code change.
        widget.workspace.create_definition(
            "attacks",
            "attack.future.punch",
        )

        manager.library.refresh()

        self.assertEqual(
            6,
            manager.library.attacks.count(),
        )

        labels = [
            manager.library.attacks.item(row).text()
            for row in range(manager.library.attacks.count())
        ]

        self.assertTrue(
            any(
                "attack.future.punch" in label
                for label in labels
            )
        )


@unittest.skipIf(
    QApplication is None,
    "PySide6 is not installed",
)
class ProjectileSpawnEditorDialogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        assert QApplication is not None

        cls.application = (
            QApplication.instance()
            or QApplication([])
        )

    def _workspace(self):
        from tools.content_studio.tests.test_projectile_authoring_service import (
            workspace_data,
        )

        temporary, workspace = make_workspace(workspace_data())

        self.addCleanup(temporary.cleanup)

        looping = workspace.create_definition(
            "animations",
            "anim.loop.poof",
        )

        workspace.update(looping, "loop", True)

        arrow_pickup = workspace.find(
            "pickups",
            "pickup.arrow",
        )

        if arrow_pickup is None:
            arrow_pickup = workspace.create_definition(
                "pickups",
                "pickup.arrow",
            )

        workspace.replace_definition(arrow_pickup, {
            "id": "pickup.arrow",
            "visualId": "visual.projectile.player.arrow",
            "collectionBounds": {"x": -5, "y": -5, "width": 10, "height": 10},
            "payload": {"kind": "item", "itemId": "item.arrow", "quantity": 1},
        })

        projectile = workspace.find(
            "projectiles",
            "projectile.player.arrow",
        )

        assert projectile is not None

        workspace.replace_definition(projectile, {
            **projectile.data,
            "animationId": "animation.arrow",
        })

        return workspace

    def _dialog(self, workspace):
        from tools.content_studio.ui.attack_library_widget import (
            AttackVisualResolver,
            ProjectileSpawnEditorDialog,
        )

        dialog = ProjectileSpawnEditorDialog(
            workspace,
            Translator("en-US"),
            "projectile.player.arrow",
            AttackVisualResolver(workspace, None),
            "attack.player.bow",
            {"kind": "projectile"},
        )

        self.addCleanup(dialog.deleteLater)

        return dialog

    def test_end_animation_combo_lists_looping_animations(self) -> None:
        workspace = self._workspace()

        dialog = self._dialog(workspace)

        self.assertIsNotNone(
            dialog.end_animation.findData("anim.loop.poof"),
        )

        self.assertIsNotNone(
            dialog.expire_animation.findData("anim.loop.poof"),
        )

    def test_impact_loop_removed_on_save(self) -> None:
        workspace = self._workspace()

        dialog = self._dialog(workspace)

        dialog.end_animation.setCurrentIndex(
            dialog.end_animation.findData("anim.loop.poof"),
        )

        # The checkbox reflects the pending loop flag of the selected
        # animation and is editable right in the dialog.
        self.assertTrue(dialog.impact_loop.isEnabled())

        self.assertTrue(dialog.impact_loop.isChecked())

        dialog.impact_loop.setChecked(False)

        dialog._save()

        projectile = workspace.find(
            "projectiles",
            "projectile.player.arrow",
        )

        assert projectile is not None

        self.assertEqual(
            {"down": "anim.loop.poof"},
            projectile.data["impactAnimations"],
        )

        animation = workspace.find(
            "animations",
            "anim.loop.poof",
        )

        assert animation is not None

        self.assertIs(False, animation.data["loop"])

    def test_flight_loop_toggles_animation_definition(self) -> None:
        workspace = self._workspace()

        dialog = self._dialog(workspace)

        self.assertTrue(dialog.flight_loop.isEnabled())

        self.assertFalse(dialog.flight_loop.isChecked())

        dialog.flight_loop.setChecked(True)

        dialog._save()

        animation = workspace.find(
            "animations",
            "animation.arrow",
        )

        assert animation is not None

        self.assertIs(True, animation.data["loop"])

    def test_loop_controls_disabled_without_animation(self) -> None:
        workspace = self._workspace()

        dialog = self._dialog(workspace)

        self.assertFalse(dialog.impact_loop.isEnabled())

        self.assertFalse(dialog.expire_loop.isEnabled())

    def test_drop_selection_persists(self) -> None:
        workspace = self._workspace()

        dialog = self._dialog(workspace)

        self.assertIsNotNone(
            dialog.expire_drop.findData("pickup.arrow"),
        )

        dialog.expire_drop.setCurrentIndex(
            dialog.expire_drop.findData("pickup.arrow"),
        )

        dialog.expire_drop_chance.setValue(100)

        dialog.impact_drop.setCurrentIndex(
            dialog.impact_drop.findData("pickup.arrow"),
        )

        dialog.impact_drop_chance.setValue(50)

        dialog._save()

        projectile = workspace.find(
            "projectiles",
            "projectile.player.arrow",
        )

        assert projectile is not None

        self.assertEqual(
            {"pickupId": "pickup.arrow", "chancePercent": 100},
            projectile.data["expireDrop"],
        )

        self.assertEqual(
            {"pickupId": "pickup.arrow", "chancePercent": 50},
            projectile.data["impactDrop"],
        )


if __name__ == "__main__":
    unittest.main()
