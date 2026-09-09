from __future__ import annotations

import json
import os
from pathlib import Path

from ..model.types import ProjectPreferences
from ..formats.json_io import write_atomic


def preferences_path() -> Path:
    if os.name == "nt" and os.environ.get("APPDATA"):
        return Path(os.environ["APPDATA"]) / "DungeonUnderworld" / "ContentStudio" / "settings.json"
    if os.environ.get("XDG_CONFIG_HOME"):
        return Path(os.environ["XDG_CONFIG_HOME"]) / "DungeonUnderworld" / "ContentStudio" / "settings.json"
    if os.environ.get("HOME"):
        return Path(os.environ["HOME"]) / ".config" / "DungeonUnderworld" / "ContentStudio" / "settings.json"
    return Path.home() / ".config" / "DungeonUnderworld" / "ContentStudio" / "settings.json"


def load_preferences(path: Path | None = None) -> ProjectPreferences:
    target = path or preferences_path()
    try:
        data = json.loads(target.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return ProjectPreferences()
    if not isinstance(data, dict):
        return ProjectPreferences()
    return ProjectPreferences(
        language=data.get("language", "pt-BR") if data.get("language") in {"pt-BR", "en-US"} else "pt-BR",
        asset_root=str(data.get("assetRoot", "")),
        last_project=str(data.get("lastProject", "")),
        left_panel_width=max(180, int(data.get("leftPanelWidth", 260))),
        right_panel_width=max(240, int(data.get("rightPanelWidth", 340))),
    )


def save_preferences(preferences: ProjectPreferences, path: Path | None = None) -> None:
    write_atomic(path or preferences_path(), {
        "language": preferences.language,
        "assetRoot": preferences.asset_root,
        "lastProject": preferences.last_project,
        "leftPanelWidth": preferences.left_panel_width,
        "rightPanelWidth": preferences.right_panel_width,
    })

