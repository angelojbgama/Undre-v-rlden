from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, TypeAlias

JsonScalar: TypeAlias = None | bool | int | float | str
JsonValue: TypeAlias = JsonScalar | list["JsonValue"] | dict[str, "JsonValue"]


@dataclass(frozen=True, slots=True)
class Diagnostic:
    severity: str
    message: str
    path: str = ""
    code: str = ""
    definition_id: str = ""
    source_path: Path | None = None
    map_id: str = ""

    @property
    def is_error(self) -> bool:
        return self.severity.lower() == "error"


@dataclass(frozen=True, slots=True)
class DefinitionKey:
    category: str
    definition_id: str


@dataclass(slots=True)
class ContentDefinition:
    category: str
    definition_id: str
    data: dict[str, JsonValue]
    source_path: Path | None = None
    origin: str = "project"
    display_name: str = ""

    def key(self) -> DefinitionKey:
        return DefinitionKey(self.category, self.definition_id)

    def matches(self, query: str) -> bool:
        needle = query.casefold().strip()
        return not needle or needle in self.definition_id.casefold() or needle in self.display_name.casefold()


@dataclass(slots=True)
class ContentFile:
    path: Path
    data: dict[str, JsonValue]
    dirty: bool = False
    valid_json: bool = True
    origin: str = "project"


@dataclass(slots=True)
class ProjectPreferences:
    language: str = "pt-BR"
    asset_root: str = ""
    last_project: str = ""
    left_panel_width: int = 260
    right_panel_width: int = 340


@dataclass(slots=True)
class ToolResult:
    returncode: int
    stdout: str
    stderr: str
    command: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return self.returncode == 0
