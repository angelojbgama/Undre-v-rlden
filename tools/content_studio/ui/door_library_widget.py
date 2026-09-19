from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QPoint, QRect, QTimer, Qt, Signal
from PySide6.QtGui import QIcon, QImage, QPainter, QPixmap
from PySide6.QtWidgets import (
    QComboBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMenu,
    QPushButton,
    QSplitter,
    QVBoxLayout,
    QWidget,
)

from ..model.content_workspace import ContentWorkspace
from ..services.door_authoring_service import (
    DoorAuthoringService,
    DoorCatalogEntry,
    DoorFamilyMetadata,
)
from ..services.localization import Translator
from .icon_registry import icon


def _frame_pixmap(
        image: QImage,
        frame: dict[str, object],
        size: int = 360,
) -> QPixmap:
    source = frame.get(
        "source",
        {},
    )

    anchor = frame.get(
        "anchor",
        {},
    )

    offset = frame.get(
        "drawOffset",
        {},
    )

    if (
        image.isNull()
        or not isinstance(source, dict)
    ):
        return QPixmap()

    if not isinstance(
        anchor,
        dict,
    ):
        anchor = {}

    if not isinstance(
        offset,
        dict,
    ):
        offset = {}

    width = max(
        1,
        int(
            source.get(
                "width",
                1,
            )
        ),
    )

    height = max(
        1,
        int(
            source.get(
                "height",
                1,
            )
        ),
    )

    padding = max(
        4,
        max(
            width,
            height,
        ) // 6,
    )

    canvas = QImage(
        width + padding * 2,
        height + padding * 2,
        QImage.Format.Format_ARGB32,
    )

    canvas.fill(
        Qt.GlobalColor.transparent
    )

    logical = QPoint(
        canvas.width() // 2,
        canvas.height()
        - padding
        - 1,
    )

    destination = QPoint(
        logical.x()
        - int(
            anchor.get(
                "x",
                0,
            )
        )
        + int(
            offset.get(
                "x",
                0,
            )
        ),
        logical.y()
        - int(
            anchor.get(
                "y",
                0,
            )
        )
        + int(
            offset.get(
                "y",
                0,
            )
        ),
    )

    painter = QPainter(
        canvas
    )

    painter.drawImage(
        destination,
        image,
        QRect(
            int(
                source.get(
                    "x",
                    0,
                )
            ),
            int(
                source.get(
                    "y",
                    0,
                )
            ),
            width,
            height,
        ),
    )

    painter.end()

    return QPixmap.fromImage(
        canvas
    ).scaled(
        size,
        size,
        Qt.AspectRatioMode.KeepAspectRatio,
        Qt.TransformationMode.FastTransformation,
    )


class DoorLibraryWidget(QWidget):
    """Specialized authoring view for WorldObjects with door capability.

    This widget intentionally does not perform map placement yet.
    Placement is a wall-aware operation and must not fall back to the
    generic ObjectPlacement mouse pivot path.
    """

    selected = Signal(object)
    place_requested = Signal(str)
    animated_collision_requested = Signal(str)
    status_changed = Signal(str)

    definition_id_role = (
        Qt.ItemDataRole.UserRole
    )

    # Authoring-only sentinel for the "no family" filter choice.
    # Family ids are validated non-empty, so "" cannot collide.
    no_family_choice = ""

    def __init__(
        self,
        workspace: ContentWorkspace | None,
        asset_root: Path | None,
        tile_size: int,
        translator: Translator | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(
            parent
        )

        self.workspace = workspace
        self.asset_root = asset_root
        self.tile_size = max(
            1,
            int(
                tile_size
            ),
        )

        self.translate = (
            translator
            or Translator()
        )

        self.family_label = QLabel()

        self.family_filter = QComboBox()

        self.family_filter.currentIndexChanged.connect(
            self.refresh
        )

        self.search = QLineEdit()

        self.search.textChanged.connect(
            self.refresh
        )

        self.doors = QListWidget()

        self.doors.currentItemChanged.connect(
            self._selection_changed
        )

        self.doors.itemDoubleClicked.connect(
            self._request_place
        )

        self.doors.setContextMenuPolicy(
            Qt.ContextMenuPolicy.CustomContextMenu
        )

        self.doors.customContextMenuRequested.connect(
            self._context_menu
        )

        self.place_button = QPushButton()

        self.place_button.setIcon(
            icon("place")
        )

        self.place_button.setEnabled(
            False
        )

        self.place_button.clicked.connect(
            self._request_place
        )

        self.preview = QLabel(
            self.translate(
                "no_image"
            )
        )

        self.preview.setAlignment(
            Qt.AlignmentFlag.AlignCenter
        )

        self.preview.setMinimumSize(
            360,
            360,
        )

        self.preview.setStyleSheet(
            "background: #161b22; "
            "color: #aeb8c4; "
            "border: 1px solid #34404d;"
        )

        self.details = QLabel()

        self.details.setWordWrap(
            True
        )

        self.help = QLabel()

        self.help.setWordWrap(
            True
        )

        self.help.setStyleSheet(
            "color: #aeb8c4;"
        )

        self._image = QImage()

        self._frames: list[
            dict[str, object]
        ] = []

        self._frame_index = 0
        self._preview_loops = False

        self._timer = QTimer(
            self
        )

        self._timer.setSingleShot(
            True
        )

        self._timer.timeout.connect(
            self._advance
        )

        left = QWidget()

        left_layout = QVBoxLayout(
            left
        )

        family_row = QHBoxLayout()

        family_row.addWidget(
            self.family_label
        )

        family_row.addWidget(
            self.family_filter,
            1,
        )

        left_layout.addLayout(
            family_row
        )

        left_layout.addWidget(
            self.search
        )

        left_layout.addWidget(
            self.doors,
            1,
        )

        left_layout.addWidget(
            self.place_button
        )

        right = QWidget()

        right_layout = QVBoxLayout(
            right
        )

        right_layout.addWidget(
            self.preview,
            1,
        )

        right_layout.addWidget(
            self.details
        )

        right_layout.addWidget(
            self.help
        )

        splitter = QSplitter(
            Qt.Orientation.Horizontal
        )

        splitter.addWidget(
            left
        )

        splitter.addWidget(
            right
        )

        splitter.setStretchFactor(
            1,
            1,
        )

        splitter.setSizes(
            [
                280,
                620,
            ]
        )

        layout = QVBoxLayout(
            self
        )

        layout.addWidget(
            splitter
        )

        self.retranslate(
            self.translate
        )

    def set_context(
        self,
        workspace: ContentWorkspace | None,
        asset_root: Path | None,
        tile_size: int,
    ) -> None:
        self.workspace = workspace
        self.asset_root = asset_root

        self.set_map_tile_size(
            tile_size,
            refresh=False,
        )

        self.refresh()

    def set_map_tile_size(
        self,
        tile_size: int,
        refresh: bool = True,
    ) -> None:
        value = int(
            tile_size
        )

        if value <= 0:
            raise ValueError(
                "tile_size must be positive"
            )

        self.tile_size = value

        if refresh:
            self.refresh()

    def retranslate(
        self,
        translator: Translator,
    ) -> None:
        self.translate = translator

        self.family_label.setText(
            self.translate(
                "door_family_filter"
            )
        )

        self.search.setPlaceholderText(
            self.translate(
                "search_doors"
            )
        )

        self.place_button.setText(
            self.translate(
                "door_place_button"
            )
        )

        self.help.setText(
            self.translate(
                "door_library_help"
            )
        )

        self.refresh()

    def refresh(self) -> None:
        current_id = (
            self._current_id()
        )

        errors: list[str] = []

        visible: list[DoorCatalogEntry] = []

        if self.workspace is not None:
            service = DoorAuthoringService(
                self.workspace
            )

            family_options = (
                self._family_options(
                    service,
                    errors,
                )
            )

            self._reload_family_filter(
                family_options
            )

            query = (
                self.search.text()
                .strip()
                .casefold()
            )

            family_id = (
                self.family_filter.currentData()
            )

            for definition in self.workspace.definitions(
                "objects"
            ):
                if not isinstance(
                    definition.data.get(
                        "door"
                    ),
                    dict,
                ):
                    continue

                try:
                    entry = service.entry(
                        definition.definition_id,
                        self.tile_size,
                    )
                except ValueError as error:
                    errors.append(
                        str(
                            error
                        )
                    )

                    continue

                if entry is None:
                    continue

                if not self._matches_family(
                    entry,
                    family_id,
                ):
                    continue

                if (
                    query
                    and query
                    not in self._search_text(
                        entry
                    )
                ):
                    continue

                visible.append(
                    entry
                )

            visible.sort(
                key=self._entry_sort_key
            )
        else:
            self._reload_family_filter(
                []
            )

        self.doors.blockSignals(
            True
        )

        self.doors.clear()

        last_family_id: str | None = None
        started_family_group = False

        for entry in visible:
            family = entry.family

            if (
                family is not None
                and (
                    not started_family_group
                    or family.family_id
                    != last_family_id
                )
            ):
                started_family_group = True
                last_family_id = (
                    family.family_id
                )

                self.doors.addItem(
                    self._family_header(
                        family
                    )
                )

            item = QListWidgetItem(
                f"{entry.display_name}  "
                f"[{entry.definition_id}]"
            )

            item.setData(
                self.definition_id_role,
                entry.definition_id,
            )

            icon = self._door_icon(
                entry
            )

            if not icon.isNull():
                item.setIcon(
                    QIcon(
                        icon
                    )
                )

            self.doors.addItem(
                item
            )

            if (
                entry.definition_id
                == current_id
            ):
                self.doors.setCurrentItem(
                    item
                )

        if (
            self.doors.currentItem()
            is None
        ):
            first_door = (
                self._first_door_item()
            )

            if first_door is not None:
                self.doors.setCurrentItem(
                    first_door
                )

        self.doors.blockSignals(
            False
        )

        self._selection_changed(
            self.doors.currentItem(),
            None,
            emit_selected=False,
        )

        if errors:
            self.status_changed.emit(
                errors[0]
            )

    def _family_options(
        self,
        service: DoorAuthoringService,
        errors: list[str],
    ) -> list[DoorFamilyMetadata]:
        # families() cross-checks family metadata between doors and
        # reports conflicts; the library stays usable by falling back
        # to the per-door metadata while the conflict is surfaced.
        try:
            return service.families(
                self.tile_size
            )
        except ValueError as error:
            errors.append(
                str(error)
            )

            return []

    @staticmethod
    def _entry_sort_key(
        entry: DoorCatalogEntry,
    ) -> tuple[object, ...]:
        family = entry.family

        family_key = (
            (
                0,
                family.sort_order,
                family.display_name.casefold(),
                family.family_id,
            )
            if family is not None
            else (1, 0, "", "")
        )

        return (
            family_key,
            entry.display_name.casefold(),
            entry.definition_id,
        )

    def _reload_family_filter(
        self,
        families: list[DoorFamilyMetadata],
    ) -> None:
        selection = (
            self.family_filter.currentData()
        )

        self.family_filter.blockSignals(
            True
        )

        self.family_filter.clear()

        self.family_filter.addItem(
            self.translate(
                "door_family_all"
            ),
            None,
        )

        for family in families:
            self.family_filter.addItem(
                family.display_name,
                family.family_id,
            )

        self.family_filter.addItem(
            self.translate(
                "door_family_uncategorized"
            ),
            self.no_family_choice,
        )

        index = self.family_filter.findData(
            selection
        )

        if index < 0:
            index = 0

        self.family_filter.setCurrentIndex(
            index
        )

        self.family_filter.blockSignals(
            False
        )

    @staticmethod
    def _matches_family(
        entry: DoorCatalogEntry,
        family_id: object,
    ) -> bool:
        if family_id is None:
            return True

        if family_id == DoorLibraryWidget.no_family_choice:
            return entry.family is None

        return (
            entry.family is not None
            and entry.family.family_id
            == family_id
        )

    @staticmethod
    def _search_text(
        entry: DoorCatalogEntry,
    ) -> str:
        parts = [
            entry.display_name,
            entry.definition_id,
        ]

        family = entry.family

        if family is not None:
            parts.extend(
                (
                    family.family_id,
                    family.display_name,
                    family.description,
                )
            )

        return " ".join(
            parts
        ).casefold()

    @staticmethod
    def _family_header(
        family: DoorFamilyMetadata,
    ) -> QListWidgetItem:
        header = QListWidgetItem(
            family.display_name
        )

        header.setFlags(
            header.flags()
            & ~Qt.ItemFlag.ItemIsSelectable
        )

        font = header.font()

        font.setBold(
            True
        )

        header.setFont(
            font
        )

        return header

    def _first_door_item(
        self,
    ) -> QListWidgetItem | None:
        for row in range(
            self.doors.count()
        ):
            item = self.doors.item(
                row
            )

            if (
                item is not None
                and item.data(
                    self.definition_id_role
                )
            ):
                return item

        return None

    def current_entry(
        self,
    ) -> DoorCatalogEntry | None:
        if self.workspace is None:
            return None

        definition_id = (
            self._current_id()
        )

        if not definition_id:
            return None

        return DoorAuthoringService(
            self.workspace
        ).entry(
            definition_id,
            self.tile_size,
        )

    def _current_id(
        self,
    ) -> str:
        item = (
            self.doors.currentItem()
        )

        if item is None:
            return ""

        return str(
            item.data(
                self.definition_id_role
            )
        )

    def _selection_changed(
        self,
        current: QListWidgetItem | None,
        unused: QListWidgetItem | None,
        emit_selected: bool = True,
    ) -> None:
        del unused

        definition_id = (
            str(
                current.data(
                    self.definition_id_role
                )
            )
            if current is not None
            else ""
        )

        definition = (
            self.workspace.find(
                "objects",
                definition_id,
            )
            if (
                self.workspace
                and definition_id
            )
            else None
        )

        if emit_selected:
            self.selected.emit(
                definition
            )

        entry = None

        if (
            definition is not None
            and self.workspace is not None
        ):
            try:
                entry = DoorAuthoringService(
                    self.workspace
                ).entry(
                    definition.definition_id,
                    self.tile_size,
                )
            except ValueError as error:
                self.status_changed.emit(
                    str(
                        error
                    )
                )

        self.place_button.setEnabled(
            entry is not None
        )

        self._show_entry(
            entry
        )

    def _request_place(
        self,
        unused: object = None,
    ) -> None:
        del unused

        try:
            entry = self.current_entry()
        except ValueError as error:
            self.status_changed.emit(
                str(error)
            )
            return

        if entry is None:
            return

        self.place_requested.emit(
            entry.definition_id
        )

    def _context_menu(
        self,
        position: QPoint,
    ) -> None:
        item = self.doors.itemAt(
            position
        )

        if item is None:
            return

        definition_id = str(
            item.data(
                self.definition_id_role
            )
            or ""
        )

        # Family headers carry no definition and must never
        # become the context target.
        if not definition_id:
            return

        self.doors.setCurrentItem(
            item
        )

        menu = QMenu(
            self
        )

        animated_collision = menu.addAction(
            self.translate(
                "door_edit_animated_collision"
            )
        )

        chosen = menu.exec(
            self.doors.viewport().mapToGlobal(
                position
            )
        )

        if chosen == animated_collision:
            self._request_animated_collision(
                definition_id
            )

    def _request_animated_collision(
        self,
        definition_id: str = "",
    ) -> None:
        target_id = (
            definition_id
            or self._current_id()
        )

        if not target_id:
            return

        self.status_changed.emit(
            self.translate(
                "door_animated_collision_selected"
            ).format(
                definition_id=target_id,
            )
        )

        self.animated_collision_requested.emit(
            target_id
        )

    def _show_entry(
        self,
        entry: DoorCatalogEntry | None,
    ) -> None:
        self._timer.stop()

        self._image = QImage()
        self._frames = []
        self._frame_index = 0
        self._preview_loops = False

        if (
            entry is None
            or self.workspace is None
        ):
            self.preview.setPixmap(
                QPixmap()
            )

            self.preview.setText(
                self.translate(
                    "no_doors"
                )
            )

            self.details.setText(
                ""
            )

            return

        animation = self.workspace.find(
            "animations",
            entry.source_animation_id,
        )

        if animation is None:
            self.preview.setPixmap(
                QPixmap()
            )

            self.preview.setText(
                self.translate(
                    "image_unavailable"
                )
            )

            self._show_details(
                entry
            )

            return

        self._image = (
            self._animation_image(
                animation
            )
        )

        frames = animation.data.get(
            "frames",
            [],
        )

        if isinstance(
            frames,
            list,
        ):
            self._frames = [
                frame
                for frame in frames
                if isinstance(
                    frame,
                    dict,
                )
            ]

        self._preview_loops = bool(
            animation.data.get(
                "loop",
                False,
            )
        )

        if (
            self._image.isNull()
            or not self._frames
        ):
            self.preview.setPixmap(
                QPixmap()
            )

            self.preview.setText(
                self.translate(
                    "image_unavailable"
                )
            )
        else:
            self.preview.setText(
                ""
            )

            self._show_frame()
            self._schedule()

        self._show_details(
            entry
        )

    def _show_details(
        self,
        entry: DoorCatalogEntry,
    ) -> None:
        orientation_names = {
            "horizontal":
                self.translate(
                    "door_orientation_horizontal"
                ),
            "vertical":
                self.translate(
                    "door_orientation_vertical"
                ),
        }

        orientations = ", ".join(
            orientation_names.get(
                value,
                value,
            )
            for value
            in entry.placement.orientations
        )

        lines = [
            entry.display_name,
            entry.definition_id,
            "",
            self.translate(
                "door_placement_wall"
            ),
            self.translate(
                "door_span_summary"
            ).format(
                span=entry.placement.span_tiles,
                thickness=entry.placement.thickness_tiles,
            ),
            self.translate(
                "door_anchor_summary"
            ).format(
                anchor=entry.placement.anchor,
            ),
            self.translate(
                "door_orientation_summary"
            ).format(
                orientations=orientations,
            ),
            self.translate(
                "door_animation_summary"
            ).format(
                animation=entry.source_animation_id,
            ),
        ]

        if entry.family is not None:
            lines.append(
                self.translate(
                    "door_family_label"
                ).format(
                    name=entry.family.display_name,
                )
            )

        self.details.setText(
            "\n".join(
                lines
            )
        )

    def _animation_image(
        self,
        animation: object,
    ) -> QImage:
        if (
            self.workspace is None
            or not hasattr(
                animation,
                "data",
            )
        ):
            return QImage()

        data = animation.data

        image_id = str(
            data.get(
                "imageId",
                "",
            )
        )

        image = self.workspace.find(
            "visualImages",
            image_id,
        )

        if image is None:
            return QImage()

        relative = image.data.get(
            "relativePath"
        )

        if not isinstance(
            relative,
            str,
        ):
            return QImage()

        root = (
            self.asset_root
            if (
                image.data.get(
                    "root"
                )
                == "gameAssets"
            )
            else self.workspace.root
        )

        if root is None:
            return QImage()

        return QImage(
            str(
                root
                / relative
            )
        )

    def _door_icon(
        self,
        entry: DoorCatalogEntry,
    ) -> QPixmap:
        if self.workspace is None:
            return QPixmap()

        animation = self.workspace.find(
            "animations",
            entry.source_animation_id,
        )

        if animation is None:
            return QPixmap()

        image = self._animation_image(
            animation
        )

        frames = animation.data.get(
            "frames",
            [],
        )

        first = (
            frames[0]
            if (
                isinstance(
                    frames,
                    list,
                )
                and frames
            )
            else None
        )

        if (
            image.isNull()
            or not isinstance(
                first,
                dict,
            )
        ):
            return QPixmap()

        source = first.get(
            "source"
        )

        if not isinstance(
            source,
            dict,
        ):
            return QPixmap()

        crop = image.copy(
            int(
                source.get(
                    "x",
                    0,
                )
            ),
            int(
                source.get(
                    "y",
                    0,
                )
            ),
            max(
                1,
                int(
                    source.get(
                        "width",
                        1,
                    )
                ),
            ),
            max(
                1,
                int(
                    source.get(
                        "height",
                        1,
                    )
                ),
            ),
        )

        return QPixmap.fromImage(
            crop
        ).scaled(
            48,
            48,
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.FastTransformation,
        )

    def _show_frame(
        self,
    ) -> None:
        if not self._frames:
            return

        self.preview.setPixmap(
            _frame_pixmap(
                self._image,
                self._frames[
                    self._frame_index
                ],
            )
        )

    def _schedule(
        self,
    ) -> None:
        if len(
            self._frames
        ) <= 1:
            return

        duration = max(
            1,
            int(
                self._frames[
                    self._frame_index
                ].get(
                    "durationTicks",
                    1,
                )
            ),
        )

        self._timer.start(
            max(
                16,
                round(
                    duration
                    * 1000
                    / 60
                ),
            )
        )

    def _advance(
        self,
    ) -> None:
        if not self._frames:
            return

        if (
            self._frame_index + 1
            < len(
                self._frames
            )
        ):
            self._frame_index += 1
            self._show_frame()
            self._schedule()

            return

        if self._preview_loops:
            self._frame_index = 0
            self._show_frame()
            self._schedule()
