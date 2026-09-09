from __future__ import annotations

from typing import Literal

from ..model.content_workspace import ContentWorkspace
from ..model.map_document import MapDocument


EditContext = Literal["map", "content"]


class CommandCoordinator:
    """Coordinates document-scoped histories without replacing either one."""

    def __init__(self) -> None:
        self.active_context: EditContext = "map"

    def mark(self, context: EditContext) -> None:
        self.active_context = context

    def undo(self, document: MapDocument | None, workspace: ContentWorkspace | None) -> bool:
        target = workspace if self.active_context == "content" else document
        if target is None or not target.undo():
            return False
        return True

    def redo(self, document: MapDocument | None, workspace: ContentWorkspace | None) -> bool:
        target = workspace if self.active_context == "content" else document
        if target is None or not target.redo():
            return False
        return True
