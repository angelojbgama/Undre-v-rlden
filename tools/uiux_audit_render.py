"""Render every Content Studio screen to PNGs for the UI/UX audit.

Read-only usage of the repository content: the autosave timer is stopped
and nothing is saved. Windows use WA_DontShowOnScreen so the native
platform (real fonts) renders without flashing windows on the desktop.
"""

from __future__ import annotations

import os
import sys
import traceback
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "windows" if os.name == "nt" else "offscreen")

REPOSITORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPOSITORY))

from PySide6.QtCore import Qt  # noqa: E402
from PySide6.QtWidgets import QApplication, QDialog  # noqa: E402

from tools.content_studio.model.world_project import WorldProject  # noqa: E402
from tools.content_studio.services.localization import Translator  # noqa: E402
from tools.content_studio.ui.main_window import MainWindow, _open_repository_workspace  # noqa: E402
from tools.content_studio.ui import theme  # noqa: E402


def grab(widget, output_dir: Path, name: str) -> None:
    pixmap = widget.grab()
    pixmap.save(str(output_dir / f"{name}.png"))
    print(f"{name}: {pixmap.width()}x{pixmap.height()}")


def show_dialog(dialog: QDialog) -> None:
    dialog.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen, True)
    dialog.show()


def main() -> int:
    output_dir = REPOSITORY / "build" / "uiux_audit"
    output_dir.mkdir(parents=True, exist_ok=True)

    app = QApplication.instance() or QApplication([])
    repository_root = REPOSITORY
    workspace = _open_repository_workspace(repository_root)
    project_path = repository_root / "content" / "world.uworld"
    project, _ = WorldProject.open(project_path)
    asset_root = repository_root / "assets"

    window = MainWindow(project, workspace, asset_root)
    window.autosave_timer.stop()  # audit is read-only
    window.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen, True)
    window.resize(1440, 900)
    window.show()
    app.processEvents()
    # Deterministic panel widths: saved user preferences may collapse panels.
    window._map_split.setSizes([320, 860, 340])
    window._content_split.setSizes([320, 860, 340])
    app.processEvents()

    # --- Map mode: every section tab -------------------------------------
    theme.apply_theme("light")
    app.processEvents()
    window._select_mode(0)
    sections = window._section_labels(0)
    for index in range(len(sections)):
        window._section_tabs.setCurrentIndex(index)
        app.processEvents()
        panel = window._map_panels.currentWidget()
        grab(panel, output_dir, f"map_{index:02d}_{panel.__class__.__name__}")
    grab(window, output_dir, "mapmode_full")

    # --- Map inspector with an object selected (door/transition editors) --
    objects = window.project.active_map.data.get("objects", [])
    if objects:
        window.map_canvas.selected_entity = ("objects", objects[0].get("id"))
        window._map_selection_changed(("objects", objects[0].get("id")))
        app.processEvents()
        inspector_panel = window.map_inspector.parentWidget()
        grab(inspector_panel, output_dir, "map_inspector_with_object")
    else:
        print("no objects in active map; skipping inspector selection")

    # --- Validation diagnostics populated ---------------------------------
    window.validate()
    app.processEvents()
    grab(window.diagnostics_view, output_dir, "diagnostics")

    # --- Content mode ------------------------------------------------------
    window._select_mode(1)
    app.processEvents()
    for index in range(window._section_tabs.count()):
        window._section_tabs.setCurrentIndex(index)
        app.processEvents()
        panel = window._content_panels.currentWidget()
        grab(panel, output_dir, f"content_{index}_{panel.__class__.__name__}")
    # content selection: first definition for the preview widget
    for category in ("tilesets", "items", "objects"):
        found = workspace.definitions(category)
        if found:
            break
    grab(window.preview, output_dir, "content_preview")

    # --- Standalone dialogs -------------------------------------------------
    from tools.content_studio.model.map_document import MapDocument
    from tools.content_studio.services.tileset_library import TilesetLibrary
    from tools.content_studio.ui.animated_collision_editor import AnimatedCollisionEditorDialog
    from tools.content_studio.ui.animation_frame_alignment_dialog import AnimationFrameAlignmentDialog
    from tools.content_studio.ui.attack_library_widget import AttackDefinitionDialog, AttackManagerDialog
    from tools.content_studio.ui.item_library_widget import ItemDefinitionDialog
    from tools.content_studio.ui.item_stack_editor import ItemStackEditor
    from tools.content_studio.ui.map_properties_dialog import MapPropertiesDialog
    from tools.content_studio.ui.player_library_widget import FrameSequenceDialog
    from tools.content_studio.ui.scene_editor import SceneEditorWidget
    from tools.content_studio.ui.spritesheet_import_dialog import SpritesheetImportDialog
    from tools.content_studio.ui.tileset_import_dialog import TilesetImportDialog
    from tools.content_studio.ui.tilesets.batch_tileset_import_dialog import BatchTilesetImportDialog
    from tools.content_studio.ui.tilesets.tileset_properties_dialog import TilesetPropertiesDialog
    from tools.content_studio.ui.terrain.terrain_rule_dialog import TerrainRuleDialog
    from tools.content_studio.ui.tilesets.tileset_library_widget import TilesetLibraryWidget

    translator = Translator("pt-BR")
    tileset_ids = [entry.definition_id for entry in workspace.definitions("tilesets")]
    animation_ids = [entry.definition_id for entry in workspace.definitions("animations")]
    item_ids = [entry.definition_id for entry in workspace.definitions("items")]
    attack_ids = [entry.definition_id for entry in workspace.definitions("attacks")]

    def try_dialog(name: str, factory) -> None:
        try:
            dialog = factory()
            dialog.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen, True)
            dialog.show()
            app.processEvents()
            grab(dialog, output_dir, f"dialog_{name}")
            dialog.close()
            dialog.deleteLater()
        except Exception:
            print(f"FAILED {name}:")
            traceback.print_exc()

    try_dialog("map_properties_new", lambda: MapPropertiesDialog(translator, suggested_id="map.new", folders=["Floresta"], parent=None))
    document = MapDocument.new("map.edit", 32, 24, 16)
    try_dialog("map_properties_edit", lambda: MapPropertiesDialog(translator, document=document, folders=["Floresta"], parent=None))
    try_dialog("tileset_import", lambda: TilesetImportDialog(workspace, None))
    try_dialog("tileset_properties", lambda: TilesetPropertiesDialog(TilesetLibrary(workspace), tileset_ids[0] if tileset_ids else "tileset.none", asset_root, translator, None))
    try_dialog("batch_tileset_import", lambda: BatchTilesetImportDialog(TilesetLibrary(workspace), asset_root, translator, None))
    try_dialog("spritesheet_import", lambda: SpritesheetImportDialog(workspace, asset_root, translator, None))
    try_dialog("terrain_rule", lambda: TerrainRuleDialog(workspace, asset_root, tileset_ids[0] if tileset_ids else "tileset.none", translator, None))
    try_dialog("item_definition", lambda: ItemDefinitionDialog(workspace, asset_root, translator, None))
    try_dialog("item_stack_editor_add", lambda: ItemStackEditor(workspace, add_mode=True))
    try_dialog("attack_manager", lambda: AttackManagerDialog(workspace, translator, None, asset_root=asset_root))
    if attack_ids:
        try_dialog("attack_definition", lambda: AttackDefinitionDialog(workspace, asset_root, attack_ids[0], translator, None))
    if tileset_ids and animation_ids:
        try_dialog("animated_collision", lambda: AnimatedCollisionEditorDialog(workspace, asset_root, animation_ids[0], translator, None))
        try_dialog("frame_alignment", lambda: AnimationFrameAlignmentDialog(workspace, asset_root, animation_ids[0], translator, None))
    if item_ids:
        from tools.content_studio.ui.item_visual_picker import ItemVisualPickerDialog
        try_dialog("item_visual_picker", lambda: ItemVisualPickerDialog(workspace, item_ids[0]))
    try_dialog("player_frame_sequence", lambda: FrameSequenceDialog(workspace, None, translator, "Idle — Baixo"))

    scene = SceneEditorWidget()
    scene.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen, True)
    scene.resize(700, 300)
    scene.show()
    app.processEvents()
    grab(scene, output_dir, "dialog_scene_editor_empty")
    scene.close()

    # --- Dark theme context shot -------------------------------------------
    theme.apply_theme("dark")
    window._select_mode(0)
    window._section_tabs.setCurrentIndex(2)  # tilesets section, busy panel
    app.processEvents()
    grab(window, output_dir, "mapmode_full_dark")

    window.close()
    app.processEvents()
    print("done")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
