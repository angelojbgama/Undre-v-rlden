"""Studio-wide light/dark theming.

The Studio uses the Fusion style with explicit palettes so light and
dark themes are deterministic on every platform (Windows, Linux, docker
and offscreen tests). The default mode is "system": it follows the OS
color scheme and keeps following it live through ``colorSchemeChanged``.

Chrome surfaces (map canvas surround, muted hint labels) read their
colors from here; authored content wells (sprite previews, scene and
animation canvases) stay dark on purpose because they frame game assets
authored for the dungeon's dark rooms.
"""

from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor, QGuiApplication, QPalette
from PySide6.QtWidgets import QApplication, QLabel

THEME_SYSTEM = "system"
THEME_LIGHT = "light"
THEME_DARK = "dark"
THEME_MODES = (THEME_SYSTEM, THEME_LIGHT, THEME_DARK)

_applied_scheme: str | None = None

#: Secondary/hint label styling, re-applied per scheme via one app-level
#: stylesheet so the rules are guaranteed to re-polish on theme changes.
_MUTED_QSS = {
    THEME_LIGHT: 'QLabel[muted="true"] { color: #5a626b; }',
    THEME_DARK: 'QLabel[muted="true"] { color: #aeb8c4; }',
}

#: Chrome surfaces of the map canvas, per resolved scheme.
_CANVAS_CHROME = {
    THEME_DARK: {
        "canvas": QColor(32, 37, 43),
        "canvas_text": QColor(184, 194, 204),
        "map_area": QColor(50, 59, 66),
        "grid_line": QColor(255, 255, 255, 28),
        "contrast": QColor(255, 255, 255),
    },
    THEME_LIGHT: {
        "canvas": QColor(214, 218, 223),
        "canvas_text": QColor(59, 67, 76),
        "map_area": QColor(244, 246, 248),
        "grid_line": QColor(0, 0, 0, 40),
        "contrast": QColor(43, 49, 56),
    },
}


def normalize_mode(mode: object) -> str:
    """Return a valid theme mode, falling back to the system default."""
    return mode if mode in THEME_MODES else THEME_SYSTEM


def system_scheme() -> str:
    """Return the OS color scheme as "light"/"dark" (Unknown counts as light)."""
    hints = QGuiApplication.styleHints()
    return THEME_DARK if hints.colorScheme() == Qt.ColorScheme.Dark else THEME_LIGHT


def resolved_scheme(mode: str) -> str:
    """Resolve a theme mode to the concrete "light"/"dark" scheme."""
    mode = normalize_mode(mode)
    return system_scheme() if mode == THEME_SYSTEM else mode


def current_scheme() -> str:
    """Scheme used by paint-time readers (canvas renderer)."""
    return _applied_scheme if _applied_scheme is not None else system_scheme()


def apply_theme(mode: str) -> str:
    """Apply the Fusion style and palette for a theme mode.

    Returns the resolved scheme ("light" or "dark"). Safe to call again
    whenever the mode or the system scheme changes.
    """
    global _applied_scheme
    scheme = resolved_scheme(mode)
    app = QApplication.instance()
    if app is None:
        return scheme
    app.setStyle("Fusion")
    app.setPalette(_palette(scheme))
    app.setStyleSheet(_MUTED_QSS[scheme])
    _applied_scheme = scheme
    return scheme


def canvas_chrome(scheme: str | None = None) -> dict[str, QColor]:
    """Chrome colors for the map canvas in the given (or current) scheme."""
    return _CANVAS_CHROME[current_scheme() if scheme is None else normalize_scheme(scheme)]


def normalize_scheme(scheme: str) -> str:
    return scheme if scheme in (THEME_LIGHT, THEME_DARK) else THEME_LIGHT


def muted(label: QLabel) -> None:
    """Mark a label as secondary/hint text themed by ``apply_theme``."""
    label.setProperty("muted", True)


def _palette(scheme: str) -> QPalette:
    palette = QPalette()
    if scheme == THEME_DARK:
        window = QColor(37, 42, 49)
        base = QColor(27, 32, 38)
        text = QColor(230, 233, 237)
        palette.setColor(QPalette.ColorRole.Window, window)
        palette.setColor(QPalette.ColorRole.WindowText, text)
        palette.setColor(QPalette.ColorRole.Base, base)
        palette.setColor(QPalette.ColorRole.AlternateBase, window)
        palette.setColor(QPalette.ColorRole.ToolTipBase, window)
        palette.setColor(QPalette.ColorRole.ToolTipText, text)
        palette.setColor(QPalette.ColorRole.Text, text)
        palette.setColor(QPalette.ColorRole.PlaceholderText, QColor(139, 148, 158))
        palette.setColor(QPalette.ColorRole.Button, window)
        palette.setColor(QPalette.ColorRole.ButtonText, text)
        palette.setColor(QPalette.ColorRole.BrightText, QColor(255, 107, 107))
        palette.setColor(QPalette.ColorRole.Link, QColor(106, 176, 243))
        palette.setColor(QPalette.ColorRole.Highlight, QColor(63, 124, 191))
        palette.setColor(QPalette.ColorRole.HighlightedText, QColor(255, 255, 255))
        palette.setColor(QPalette.ColorRole.Light, QColor(58, 66, 76))
        palette.setColor(QPalette.ColorRole.Midlight, QColor(47, 54, 62))
        palette.setColor(QPalette.ColorRole.Mid, QColor(74, 82, 92))
        palette.setColor(QPalette.ColorRole.Dark, QColor(23, 27, 32))
        palette.setColor(QPalette.ColorRole.Shadow, QColor(12, 14, 17))
        for role in (QPalette.ColorRole.WindowText, QPalette.ColorRole.Text,
                     QPalette.ColorRole.ButtonText):
            palette.setColor(QPalette.ColorGroup.Disabled, role, QColor(111, 119, 128))
        palette.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.Base, base)
        palette.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.Window, window)
    else:
        window = QColor(242, 243, 245)
        text = QColor(31, 36, 42)
        palette.setColor(QPalette.ColorRole.Window, window)
        palette.setColor(QPalette.ColorRole.WindowText, text)
        palette.setColor(QPalette.ColorRole.Base, QColor(255, 255, 255))
        palette.setColor(QPalette.ColorRole.AlternateBase, window)
        palette.setColor(QPalette.ColorRole.ToolTipBase, QColor(255, 255, 255))
        palette.setColor(QPalette.ColorRole.ToolTipText, text)
        palette.setColor(QPalette.ColorRole.Text, text)
        palette.setColor(QPalette.ColorRole.PlaceholderText, QColor(122, 130, 139))
        palette.setColor(QPalette.ColorRole.Button, window)
        palette.setColor(QPalette.ColorRole.ButtonText, text)
        palette.setColor(QPalette.ColorRole.BrightText, QColor(201, 42, 42))
        palette.setColor(QPalette.ColorRole.Link, QColor(28, 111, 208))
        palette.setColor(QPalette.ColorRole.Highlight, QColor(63, 124, 191))
        palette.setColor(QPalette.ColorRole.HighlightedText, QColor(255, 255, 255))
        palette.setColor(QPalette.ColorRole.Light, QColor(255, 255, 255))
        palette.setColor(QPalette.ColorRole.Midlight, QColor(222, 225, 229))
        palette.setColor(QPalette.ColorRole.Mid, QColor(166, 173, 181))
        palette.setColor(QPalette.ColorRole.Dark, QColor(160, 166, 173))
        palette.setColor(QPalette.ColorRole.Shadow, QColor(97, 103, 110))
        for role in (QPalette.ColorRole.WindowText, QPalette.ColorRole.Text,
                     QPalette.ColorRole.ButtonText):
            palette.setColor(QPalette.ColorGroup.Disabled, role, QColor(154, 161, 169))
        palette.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.Base, QColor(236, 238, 240))
        palette.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.Window, window)
    return palette
