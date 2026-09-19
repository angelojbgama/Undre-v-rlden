from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QPoint, QSize, Qt, Signal
from PySide6.QtGui import QFontMetrics, QImage, QIcon, QPixmap
from PySide6.QtWidgets import (
    QButtonGroup, QFrame, QGridLayout, QLabel, QPushButton, QScrollArea,
    QSizePolicy, QSpinBox, QToolButton, QVBoxLayout, QWidget,
)

from ...model.content_workspace import ContentWorkspace
from ...model.tile_semantics import TerrainFamily, TerrainProfile, TerrainSelection
from ...services.localization import Translator
from ...services.terrain_rule_service import RULE_SLOTS, TerrainRuleService
from ...services.tile_semantic_catalog import TileSemanticCatalog
from ..tile_thumbnails import tile_pixmap


class TerrainFamilyCard(QToolButton):
    preview_requested = Signal(str, object)
    preview_finished = Signal()

    def __init__(self, family: str, icon: QIcon, tooltip: str,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.family = family
        self.setCheckable(True)
        self.setFixedSize(96, 96)
        self.setIcon(icon)
        self.setIconSize(QSize(48, 48))
        self.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextUnderIcon)
        # A native tooltip would compete with the richer 3x3 preview window.
        self.setStatusTip(tooltip)
        self.setAccessibleDescription(tooltip)
        self.setFamilyLabel(family)
        self.setStyleSheet(
            "QToolButton { border: 1px solid palette(mid); border-radius: 7px; padding: 5px; }"
            "QToolButton:hover { border-color: palette(highlight); background: palette(alternate-base); }"
            "QToolButton:checked { border: 2px solid palette(highlight); background: palette(alternate-base); }"
        )

    def setFamilyLabel(self, label: str) -> None:  # noqa: N802 - Qt naming
        # Long family ids wrapped and overlapped inside the fixed card
        # (audit ST1): elide to a single line and keep the full name in
        # the hover preview.
        display = label.removeprefix("terrain.") or label
        metrics = QFontMetrics(self.font())
        self.setText(metrics.elidedText(display, Qt.TextElideMode.ElideRight, self.width() - 14))

    def enterEvent(self, event: object) -> None:
        self.preview_requested.emit(self.family, self)
        super().enterEvent(event)  # type: ignore[arg-type]

    def leaveEvent(self, event: object) -> None:
        self.preview_finished.emit()
        super().leaveEvent(event)  # type: ignore[arg-type]


class TerrainFamilyPreview(QFrame):
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent, Qt.WindowType.ToolTip)
        self.setFrameShape(QFrame.Shape.StyledPanel)
        self.title = QLabel()
        grid = QGridLayout()
        grid.setSpacing(3)
        self.cells: list[QLabel] = []
        for index in range(9):
            cell = QLabel("·")
            cell.setFixedSize(44, 44)
            cell.setAlignment(Qt.AlignmentFlag.AlignCenter)
            cell.setStyleSheet("border: 1px solid palette(mid); background: palette(base);")
            self.cells.append(cell)
            grid.addWidget(cell, index // 3, index % 3)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.addWidget(self.title)
        layout.addLayout(grid)

    def show_family(self, title: str, tiles: list[QPixmap | None], anchor: QWidget) -> None:
        self.title.setText(title)
        for index, cell in enumerate(self.cells):
            pixmap = tiles[index] if index < len(tiles) else None
            if pixmap is not None and not pixmap.isNull():
                cell.setText("")
                cell.setPixmap(pixmap.scaled(
                    40, 40, Qt.AspectRatioMode.IgnoreAspectRatio,
                    Qt.TransformationMode.FastTransformation))
            else:
                cell.setPixmap(QPixmap())
                cell.setText("·")
        self.adjustSize()
        self.move(anchor.mapToGlobal(QPoint(anchor.width() + 8, 0)))
        self.show()


class SmartTerrainPalette(QWidget):
    terrain_selected = Signal(object)
    room_requested = Signal(object)

    def __init__(self, catalog: TileSemanticCatalog | None = None,
                 translator: Translator | None = None, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.translate = translator or Translator()
        self.catalog = catalog or TileSemanticCatalog()
        self.rule_service = TerrainRuleService()
        self.workspace: ContentWorkspace | None = None
        self.asset_root: Path | None = None
        self._selected_family = ""
        self._image_cache: dict[str, QImage] = {}
        self.family_cards: dict[str, TerrainFamilyCard] = {}
        self.family_group = QButtonGroup(self)
        self.family_group.setExclusive(True)

        self.title = QLabel(self.translate("smart_terrain"))
        self.family_label = QLabel(self.translate("terrain_family_cards"))
        self.family_label.setWordWrap(True)
        self.family_panel = QWidget()
        self.family_grid = QGridLayout(self.family_panel)
        self.family_grid.setContentsMargins(2, 2, 2, 2)
        self.family_grid.setSpacing(8)
        self.family_scroll = QScrollArea()
        self.family_scroll.setWidgetResizable(True)
        self.family_scroll.setFrameShape(QFrame.Shape.NoFrame)
        self.family_scroll.setWidget(self.family_panel)
        self.family_scroll.setMinimumHeight(126)
        self.family_scroll.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.preview = TerrainFamilyPreview(self)

        self.seed_label = QLabel(self.translate("terrain_seed"))
        self.seed = QSpinBox()
        self.seed.setRange(-2_147_483_648, 2_147_483_647)
        self.seed.setValue(0)
        self.seed.setToolTip(self.translate("terrain_seed_tip"))
        self.seed.valueChanged.connect(self._selection_changed)
        self.room = QPushButton(self.translate("room_brush"))
        self.room.setEnabled(False)
        self.room.setToolTip(self.translate("room_brush_tip"))
        self.room.clicked.connect(self._room_requested)
        self.status = QLabel()
        self.status.setWordWrap(True)

        layout = QVBoxLayout(self)
        layout.addWidget(self.title)
        layout.addWidget(self.family_label)
        layout.addWidget(self.family_scroll, 1)
        layout.addWidget(self.seed_label)
        layout.addWidget(self.seed)
        layout.addWidget(self.room)
        layout.addWidget(self.status)

    def set_asset_root(self, asset_root: Path | None) -> None:
        if asset_root != self.asset_root:
            self.asset_root = asset_root
            self._image_cache.clear()
            self._rebuild_family_cards()

    def set_workspace(self, workspace: ContentWorkspace | None) -> None:
        self.workspace = workspace
        self.catalog.set_workspace(workspace)
        self._image_cache.clear()
        self._rebuild_family_cards()
        self._selection_changed()

    def refresh(self) -> None:
        self.catalog.invalidate()
        self._image_cache.clear()
        self._rebuild_family_cards()
        self._selection_changed()

    def retranslate(self, translator: Translator) -> None:
        self.translate = translator
        self.title.setText(self.translate("smart_terrain"))
        self.family_label.setText(self.translate("terrain_family_cards"))
        self.seed_label.setText(self.translate("terrain_seed"))
        self.room.setText(self.translate("room_brush"))
        self._rebuild_family_cards()
        self._selection_changed()

    def select_family(self, family: str) -> None:
        card = self.family_cards.get(family)
        if card is None:
            return
        card.setChecked(True)
        self._selected_family = family
        self._selection_changed()

    def selection(self) -> TerrainSelection | None:
        terrain = self._family(self._selected_family)
        if terrain is None:
            return None
        wall_only = "wall" in terrain.roles and "floor" not in terrain.roles
        return TerrainSelection(
            terrain.family,
            "wall" if wall_only else "floor",
            self.seed.value())

    def profile(self) -> TerrainProfile | None:
        terrain = self._family(self._selected_family)
        if terrain is None or not {"floor", "wall"}.issubset(terrain.roles):
            return None
        return TerrainProfile(
            f"terrain.{terrain.family}.room",
            TerrainSelection(terrain.family, "floor", self.seed.value()),
            TerrainSelection(terrain.family, "wall", self.seed.value()),
        )

    def preview_tiles(self, family: str) -> list[QPixmap | None]:
        terrain = self._family(family)
        if terrain is None:
            return [None] * 9
        assignments = self._preview_assignments(terrain)
        return [self._tile_pixmap(*assignments[slot]) if slot in assignments else None for slot in RULE_SLOTS]

    def _family(self, family: str) -> TerrainFamily | None:
        return next((item for item in self.catalog.families() if item.family == family), None)

    def _rebuild_family_cards(self) -> None:
        previous = self._selected_family
        while self.family_grid.count():
            item = self.family_grid.takeAt(0)
            widget = item.widget()
            if widget is not None:
                # Detach now: deleteLater alone leaves the old card visible
                # (stacked over the new one) until deferred deletion runs.
                widget.setParent(None)
                widget.deleteLater()
        self.family_cards.clear()
        self.family_group.deleteLater()
        self.family_group = QButtonGroup(self)
        self.family_group.setExclusive(True)
        families = tuple(
            terrain for terrain in self.catalog.families()
            if "floor" in terrain.roles or "wall" in terrain.roles
        )
        for index, terrain in enumerate(families):
            tiles = self.preview_tiles(terrain.family)
            ordered = [tiles[4], *tiles]
            representative = next((tile for tile in ordered if tile is not None and not tile.isNull()), None)
            behavior = self._behavior_text(terrain)
            card = TerrainFamilyCard(
                terrain.family, QIcon(representative) if representative else QIcon(),
                f"{terrain.family}\n{behavior}", self.family_panel)
            card.clicked.connect(lambda checked=False, value=terrain.family: self.select_family(value))
            card.preview_requested.connect(self._show_preview)
            card.preview_finished.connect(self.preview.hide)
            self.family_group.addButton(card)
            self.family_cards[terrain.family] = card
            self.family_grid.addWidget(card, index // 2, index % 2)
        self.family_grid.setRowStretch((len(families) + 1) // 2, 1)
        if previous not in self.family_cards:
            previous = ""
        self._selected_family = previous
        if previous:
            self.family_cards[previous].setChecked(True)

    def _preview_assignments(self, terrain: TerrainFamily) -> dict[str, tuple[str, int]]:
        best: dict[str, tuple[str, int]] = {}
        # Preview the rule that clicking the card will activate.  Mixed
        # families paint their floor in freehand mode; wall-only families
        # naturally fall through to their colliding rule.
        for role in ("floor", "wall"):
            for tileset_id in terrain.tileset_ids:
                assignments = self.rule_service.load_assignments(
                    self.workspace, tileset_id, terrain.family, role)
                candidate = {slot: (tileset_id, source_index) for slot, source_index in assignments.items()}
                if len(candidate) > len(best):
                    best = candidate
        if best:
            return best
        for index, semantic in enumerate(terrain.semantics[:9]):
            best[RULE_SLOTS[index]] = semantic.reference
        return best

    def _tile_pixmap(self, tileset_id: str, source_index: int) -> QPixmap | None:
        return tile_pixmap(self.workspace, self.asset_root, tileset_id, source_index, self._image_cache)

    def _show_preview(self, family: str, anchor: object) -> None:
        if isinstance(anchor, QWidget):
            terrain = self._family(family)
            title = f"{family}\n{self._behavior_text(terrain)}" if terrain else family
            self.preview.show_family(title, self.preview_tiles(family), anchor)

    def _behavior_text(self, terrain: TerrainFamily) -> str:
        has_floor = "floor" in terrain.roles
        has_wall = "wall" in terrain.roles
        if has_floor and has_wall:
            return self.translate("terrain_family_mixed_behavior")
        if has_wall:
            return self.translate("terrain_family_wall_behavior")
        return self.translate("terrain_family_floor_behavior")

    def _selection_changed(self) -> None:
        selection = self.selection()
        terrain = self._family(self._selected_family)
        self.room.setEnabled(
            terrain is not None and {"floor", "wall"}.issubset(terrain.roles))
        if selection:
            self.terrain_selected.emit(selection)
            self.status.setText(self.translate(
                "terrain_active",
                family=selection.family,
                role=self.translate(selection.role),
            ))
        elif self.family_cards:
            # Families exist but none is picked yet (audit ST2): the old
            # "none available" message contradicted the visible cards.
            self.status.setText(self.translate("terrain_no_selection"))
        else:
            self.status.setText(self.translate("no_terrain_family"))

    def _room_requested(self) -> None:
        profile = self.profile()
        if profile:
            self.room_requested.emit(profile)
