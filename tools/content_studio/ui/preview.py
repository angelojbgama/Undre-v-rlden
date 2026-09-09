from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtGui import QImage, QPixmap
from PySide6.QtWidgets import QLabel, QVBoxLayout, QWidget

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition
from ..services.assets import AssetEntry


class PreviewWidget(QWidget):
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.image = QLabel("No visual selected")
        self.image.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.image.setMinimumSize(180, 180)
        self.image.setStyleSheet("background: #222831; color: #aeb8c4;")
        self.info = QLabel()
        self.info.setWordWrap(True)
        layout = QVBoxLayout(self)
        layout.addWidget(self.image, 1)
        layout.addWidget(self.info)

    def show_definition(self, definition: ContentDefinition | None, workspace: ContentWorkspace | None, asset_root: Path | None) -> None:
        if not definition or not workspace:
            self.image.setText("No visual selected")
            self.image.setPixmap(QPixmap())
            self.info.clear()
            return
        image_id = _visual_image_id(definition, workspace)
        image_definition = workspace.find("visualImages", image_id) if image_id else None
        image = load_definition_image(image_definition, workspace, asset_root)
        if image is None:
            self.image.setText("Visual unavailable\n" + (image_id or "no visual reference"))
            self.image.setPixmap(QPixmap())
        else:
            pixmap = QPixmap.fromImage(image)
            self.image.setPixmap(pixmap.scaled(256, 256, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.FastTransformation))
        self.info.setText(f"{definition.display_name}\n{definition.category}: {definition.definition_id}")

    def show_asset(self, entry: AssetEntry | None) -> None:
        if entry is None:
            self.image.setText("No asset selected"); self.image.setPixmap(QPixmap()); self.info.clear(); return
        source = QImage(str(entry.absolute_path))
        if source.isNull():
            self.image.setText("Asset unavailable"); self.image.setPixmap(QPixmap())
        else:
            self.image.setPixmap(QPixmap.fromImage(source).scaled(320, 320, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.FastTransformation))
        self.info.setText(f"{entry.root}/{entry.relative_path.as_posix()}\n{source.width()} × {source.height()} px")


def _visual_image_id(definition: ContentDefinition, workspace: ContentWorkspace) -> str:
    data = definition.data
    category = definition.category
    if category == "visualImages":
        return definition.definition_id
    if category in {"staticSprites", "animations"}:
        return str(data.get("imageId", ""))
    if category == "enemies":
        owner = workspace.find("enemyVisuals", str(data.get("visualSetId", "")))
        return _visual_image_id(owner, workspace) if owner else ""
    if category == "objects":
        owner = workspace.find("objectVisuals", str(data.get("visualSetId", "")))
        return _visual_image_id(owner, workspace) if owner else ""
    if category == "npcs":
        owner = workspace.find("npcVisuals", str(data.get("visualSetId", "")))
        return _visual_image_id(owner, workspace) if owner else ""
    if category in {"enemyVisuals", "npcVisuals"}:
        for value in _walk(data):
            if isinstance(value, str):
                animation = workspace.find("animations", value)
                if animation:
                    return str(animation.data.get("imageId", ""))
    if category == "objectVisuals":
        animation = workspace.find("animations", str(data.get("idleAnimationId", "")))
        return str(animation.data.get("imageId", "")) if animation else ""
    if category in {"items", "pickups", "projectiles"}:
        visual_id = str(data.get("visualId", ""))
        sprite = workspace.find("staticSprites", visual_id)
        return _visual_image_id(sprite, workspace) if sprite else visual_id
    if category in {"visualImages", "staticSprites"}:
        return definition.definition_id if category == "visualImages" else str(data.get("imageId", ""))
    return ""


def load_definition_image(image_definition: ContentDefinition | None,
                          workspace: ContentWorkspace,
                          asset_root: Path | None) -> QImage | None:
    if image_definition is None:
        return None
    if image_definition.category != "visualImages":
        image_id = _visual_image_id(image_definition, workspace)
        image_definition = workspace.find("visualImages", image_id) if image_id else None
    if image_definition is None:
        return None
    relative = image_definition.data.get("relativePath")
    root = asset_root if image_definition.data.get("root", "gameAssets") == "gameAssets" else workspace.root
    if not isinstance(relative, str) or root is None:
        return None
    candidate = QImage(str(root / relative))
    return candidate if not candidate.isNull() else None


def _walk(value: object):
    if isinstance(value, dict):
        for child in value.values():
            yield from _walk(child)
    elif isinstance(value, list):
        for child in value:
            yield from _walk(child)
    else:
        yield value
