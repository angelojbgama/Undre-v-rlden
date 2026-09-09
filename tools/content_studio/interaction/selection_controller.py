from __future__ import annotations

from dataclasses import dataclass
from typing import Callable


@dataclass(frozen=True, slots=True)
class Selection:
    category: str
    identifier: str | int

    def as_tuple(self) -> tuple[str, str | int]:
        return self.category, self.identifier


class SelectionController:
    """Single selection authority for map entities and map elements."""

    def __init__(self, changed: Callable[[Selection | None], None] | None = None) -> None:
        self._selection: Selection | None = None
        self._changed = changed

    @property
    def current(self) -> Selection | None:
        return self._selection

    def select(self, category: str, identifier: str | int) -> Selection:
        self._selection = Selection(category, identifier)
        if self._changed:
            self._changed(self._selection)
        return self._selection

    def select_value(self, selection: Selection | tuple[str, str | int] | None) -> None:
        if selection is None:
            self.clear()
        elif isinstance(selection, Selection):
            self.select(selection.category, selection.identifier)
        else:
            self.select(selection[0], selection[1])

    def clear(self) -> None:
        self._selection = None
        if self._changed:
            self._changed(None)

    def matches(self, category: str, identifier: str | int) -> bool:
        return self._selection == Selection(category, identifier)
