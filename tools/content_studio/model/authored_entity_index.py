from __future__ import annotations

from dataclasses import dataclass

from .content_workspace import ContentWorkspace
from .types import ContentDefinition, Diagnostic


ENTITY_CATEGORIES: tuple[str, ...] = ("enemies", "npcs", "objects", "pickups")


@dataclass(frozen=True, slots=True)
class AuthoredEntityCandidate:
    """A placement candidate without copying authored definition data."""

    definition: ContentDefinition
    diagnostics: tuple[Diagnostic, ...]

    @property
    def placeable(self) -> bool:
        return not any(issue.is_error for issue in self.diagnostics)


class AuthoredEntityIndex:
    """Read-only placement view over the current authored workspace index.

    Discovery deliberately depends on the workspace's authored JSON index and
    never on its compiled runtime registry.  The candidate retains the
    original ContentDefinition object so edits are reflected without a second
    copy of project content.
    """

    def __init__(self, workspace: ContentWorkspace | None = None) -> None:
        self.workspace = workspace

    def set_workspace(self, workspace: ContentWorkspace | None) -> None:
        self.workspace = workspace

    def candidates(self, category: str | None = None, query: str = "") -> list[AuthoredEntityCandidate]:
        if not self.workspace:
            return []
        categories = (category,) if category else ENTITY_CATEGORIES
        return [AuthoredEntityCandidate(definition, tuple(self.workspace.validate_local(definition)))
                for name in categories
                for definition in self.workspace.definitions(name, query)]

    def find(self, category: str, definition_id: str) -> AuthoredEntityCandidate | None:
        if category not in ENTITY_CATEGORIES or not self.workspace:
            return None
        definition = self.workspace.find(category, definition_id)
        if not definition:
            return None
        return AuthoredEntityCandidate(definition, tuple(self.workspace.validate_local(definition)))
