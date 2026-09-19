"""Shared tile thumbnail loader for palettes (audit SS1/ST1)."""

from __future__ import annotations

from pathlib import Path

from PySide6.QtGui import QImage, QPixmap

from ..model.content_workspace import ContentWorkspace


def tile_pixmap(workspace: ContentWorkspace | None, asset_root: Path | None,
                tileset_id: str, source_index: int,
                cache: dict[str, QImage] | None = None) -> QPixmap | None:
    """Crop one tile out of a tileset atlas; None when unavailable."""
    if workspace is None or asset_root is None or not tileset_id:
        return None
    definition = workspace.find("tilesets", tileset_id)
    if definition is None:
        return None
    relative = definition.data.get("relativeAssetPath")
    if not isinstance(relative, str):
        return None
    image = cache.get(tileset_id) if cache is not None else None
    if image is None:
        image = QImage(str(Path(asset_root) / relative))
        if cache is not None:
            cache[tileset_id] = image
    if image.isNull():
        return None
    columns = max(1, int(definition.data.get("columns", 1)))
    tile_size = max(1, int(definition.data.get("tileSize", 16)))
    tile = image.copy(
        int(source_index) % columns * tile_size,
        int(source_index) // columns * tile_size,
        tile_size, tile_size)
    return QPixmap.fromImage(tile) if not tile.isNull() else None
