"""Central access to the Tabler Icons used by the Content Studio UI.

Icons are addressed by semantic name; widgets never reference the SVG
files directly, so the Tabler layout stays an implementation detail and
the UI can evolve into a broader StudioVisualSystem (IconRegistry +
Theme + Metrics) without touching call sites.

The Tabler sources use ``stroke="currentColor"``. A QIconEngine resolves
that color from the live application palette at paint time, so existing
QIcon instances follow the Studio light/dark theme automatically.

Icons are UI chrome only. Thumbnails of authored game content keep using
the real game assets and must never be replaced by these icons.
"""

from __future__ import annotations

from pathlib import Path
import sys

from PySide6.QtCore import QByteArray, QPoint, QRect, QRectF, QSize, Qt
from PySide6.QtGui import (
    QColor, QIcon, QIconEngine, QImage, QGuiApplication, QPainter, QPalette, QPixmap,
)
from PySide6.QtSvg import QSvgRenderer

#: Versioned copy of the Tabler Icons (outline) SVGs used by the studio.
ICONS_DIR = Path(__file__).resolve().parent.parent / "assets" / "icons" / "tabler"

#: Semantic name -> Tabler Icons (outline) file under ``assets/icons/tabler``.
TABLER_ICONS: dict[str, str] = {
    # File menu
    "new": "file-plus.svg",
    "open": "folder-open.svg",
    "save": "device-floppy.svg",
    "save_as": "file-pencil.svg",
    "save_all": "stack-2.svg",
    "validate": "circle-check.svg",
    "export": "file-export.svg",
    "playtest": "player-play.svg",
    "stop": "player-stop.svg",
    "import": "file-import.svg",
    "exit": "logout.svg",
    # Edit / view
    "undo": "arrow-back-up.svg",
    "redo": "arrow-forward-up.svg",
    "grid": "grid-dots.svg",
    "snap": "magnet.svg",
    "frame_map": "maximize.svg",
    "select": "pointer.svg",
    "erase": "eraser.svg",
    # Theme
    "theme_system": "device-desktop.svg",
    "theme_light": "sun.svg",
    "theme_dark": "moon.svg",
    # Section navigation
    "map": "map.svg",
    "layers": "stack-2.svg",
    "tiles": "layout-grid.svg",
    "crafting": "hammer.svg",
    "presentation": "sparkles.svg",
    "spritesheet": "movie.svg",
    "object": "box.svg",
    "door": "door.svg",
    "player": "user.svg",
    "items": "backpack.svg",
    "terrain": "texture.svg",
    "tag": "tag.svg",
    "stamp": "bookmark.svg",
    "entities": "users.svg",
    "scenes": "video.svg",
    "links": "link.svg",
    "definitions": "list.svg",
    "assets": "folder.svg",
    # Canvas HUD
    "zoom_in": "zoom-in.svg",
    "zoom_out": "zoom-out.svg",
    # Diagnostics severity
    "diag_error": "circle-x.svg",
    "diag_warning": "alert-triangle.svg",
    "diag_info": "info-circle.svg",
    # Playback
    "play": "player-play.svg",
    "pause": "player-pause.svg",
    # Ordering arrows
    "chevron_up": "chevron-up.svg",
    "chevron_down": "chevron-down.svg",
    "chevron_left": "chevron-left.svg",
    "chevron_right": "chevron-right.svg",
    # Frequent actions
    "add": "plus.svg",
    "delete": "trash.svg",
    "edit": "pencil.svg",
    "duplicate": "copy-plus.svg",
    "rename": "pencil.svg",
    "find_usages": "search.svg",
    "back": "arrow-left.svg",
    "place": "map-pin.svg",
    "configure": "settings.svg",
    "apply": "check.svg",
    "refresh": "refresh.svg",
    "new_folder": "folder-plus.svg",
    "set_entry": "flag.svg",
    "move_up": "arrow-up.svg",
    "move_down": "arrow-down.svg",
    "visibility": "eye.svg",
    "new_map": "map-plus.svg",
    "close": "x.svg",
    "unbind": "link-off.svg",
}


class IconSize:
    """Reusable icon sizes so widgets do not scatter magic numbers."""

    SMALL = QSize(16, 16)  # menus / small controls
    NORMAL = QSize(20, 20)  # normal controls
    TOOLBAR = QSize(24, 24)  # main toolbar
    LARGE = QSize(32, 32)  # larger tool buttons


class TablerIconEngine(QIconEngine):
    """Paints one Tabler SVG recolored by the current theme palette.

    ``currentColor`` is replaced at paint time, so icons stay visible on
    both the light and dark palettes without widgets re-setting icons
    when the theme changes.
    """

    def __init__(self, source: str) -> None:
        super().__init__()
        self._source = source
        self._renderers: dict[str, QSvgRenderer] = {}

    def _renderer(self, color: str) -> QSvgRenderer:
        renderer = self._renderers.get(color)
        if renderer is None:
            data = QByteArray(self._source.replace("currentColor", color).encode("utf-8"))
            renderer = QSvgRenderer(data)
            self._renderers[color] = renderer
        return renderer

    @staticmethod
    def _color(mode: QIcon.Mode) -> QColor:
        palette = QGuiApplication.palette()
        if mode == QIcon.Mode.Disabled:
            return palette.color(QPalette.ColorGroup.Disabled, QPalette.ColorRole.WindowText)
        if mode == QIcon.Mode.Selected:
            return palette.color(QPalette.ColorGroup.Active, QPalette.ColorRole.HighlightedText)
        return palette.color(QPalette.ColorGroup.Active, QPalette.ColorRole.WindowText)

    def paint(self, painter: QPainter, rect: QRect, mode: QIcon.Mode,
              state: QIcon.State) -> None:  # noqa: N802 - Qt naming
        color = self._color(mode).name(QColor.NameFormat.HexRgb)
        self._renderer(color).render(painter, QRectF(rect))

    def pixmap(self, size: QSize, mode: QIcon.Mode,
               state: QIcon.State) -> QPixmap:  # noqa: N802 - Qt naming
        image = QImage(size, QImage.Format.Format_ARGB32_Premultiplied)
        image.fill(Qt.GlobalColor.transparent)
        painter = QPainter(image)
        self.paint(painter, QRect(QPoint(0, 0), size), mode, state)
        painter.end()
        return QPixmap.fromImage(image)

    def cacheKey(self) -> int:  # noqa: N802 - Qt naming
        # Bust icon pixmap caches whenever the themed color changes.
        return hash((id(self), self._color(QIcon.Mode.Normal).name()))

    def clone(self) -> QIconEngine:  # noqa: N802 - Qt naming
        return TablerIconEngine(self._source)


class IconRegistry:
    """Resolves semantic icon names to the local Tabler SVG files.

    Loaded QIcons are cached per semantic name, so repeated requests
    (menus, toolbars, rebuilt inspector rows) share one engine instead of
    re-reading the SVG.
    """

    def __init__(self, root: Path = ICONS_DIR) -> None:
        self._root = root
        self._cache: dict[str, QIcon] = {}
        self._warned_missing: set[str] = set()

    def icon(self, name: str) -> QIcon:
        loaded = self._cache.get(name)
        if loaded is None:
            resolved = self.resolve(name)
            if resolved is None:
                self._warn_missing(name)
                loaded = QIcon()
            else:
                loaded = QIcon(TablerIconEngine(resolved.read_text(encoding="utf-8")))
            self._cache[name] = loaded
        return loaded

    def resolve(self, name: str) -> Path | None:
        """Return the SVG file for a semantic name, or None when missing."""
        file_name = TABLER_ICONS.get(name)
        if file_name is None:
            return None
        candidate = self._root / file_name
        return candidate if candidate.is_file() else None

    def _warn_missing(self, name: str) -> None:
        if name in self._warned_missing:
            return
        self._warned_missing.add(name)
        print(f"icon_registry: missing icon asset for '{name}'", file=sys.stderr)


_default_registry = IconRegistry()


def icon(name: str) -> QIcon:
    """Return the studio-wide icon for a semantic name.

    Unknown names or missing files yield a null QIcon instead of raising,
    so a broken asset tree degrades to icon-less controls.
    """
    return _default_registry.icon(name)
