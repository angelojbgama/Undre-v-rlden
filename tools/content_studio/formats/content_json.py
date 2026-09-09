from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from ..model.types import ContentDefinition, ContentFile, Diagnostic, JsonValue
from .json_io import DuplicateKeyError, load_json, write_atomic

CONTENT_FORMAT = "dungeon-underworld-content"
CONTENT_VERSION = 5
CONTENT_CATEGORIES: tuple[str, ...] = (
    "tilesets", "projectiles", "attacks", "behaviors", "enemies", "items", "objects", "pickups",
    "npcVisuals", "npcs", "dialogues", "quests", "playerProgressions", "rewardProfiles",
    "rewardGrants", "shops", "authoringDescriptors", "tileSemantics", "stamps", "presentationEffects",
    "visualImages", "staticSprites", "animations", "enemyVisuals", "objectVisuals",
)
ID_FIELDS = {category: ("definitionId" if category == "authoringDescriptors" else "id") for category in CONTENT_CATEGORIES}


@dataclass(slots=True)
class ContentDecode:
    data: dict[str, JsonValue] | None
    diagnostics: list[Diagnostic]


def decode_content(path: Path) -> ContentDecode:
    try:
        value = load_json(path)
    except (OSError, UnicodeError, ValueError, DuplicateKeyError) as error:
        return ContentDecode(None, [Diagnostic("error", str(error), source_path=path, code="json_decode")])
    diagnostics: list[Diagnostic] = []
    if not isinstance(value, dict):
        return ContentDecode(None, [Diagnostic("error", "top-level JSON value must be an object", source_path=path, code="wrong_type")])
    if value.get("format") != CONTENT_FORMAT:
        diagnostics.append(Diagnostic("error", "invalid content format identifier", "format", "invalid_format", source_path=path))
    version = value.get("version")
    if not isinstance(version, int) or isinstance(version, bool) or version < 1 or version > CONTENT_VERSION:
        diagnostics.append(Diagnostic("error", f"unsupported content schema version: {version!r}", "version", "invalid_version", source_path=path))
    # The canonical C++ decoder treats categories as optional arrays.  Older
    # authored packs commonly contain only the categories they use, so an
    # absent category is an empty collection rather than a schema failure.
    for category in CONTENT_CATEGORIES:
        entries = value.get(category)
        if entries is not None and not isinstance(entries, list):
            diagnostics.append(Diagnostic("error", "content category must be an array", category, "wrong_type", source_path=path))
    # Unknown fields are intentionally reported but the raw object is retained so
    # the browser can still show recoverable authored data.
    known = {"format", "version", *CONTENT_CATEGORIES}
    for key in value:
        if key not in known:
            diagnostics.append(Diagnostic("error", f"unknown content field: {key}", key, "unknown_field", source_path=path))
    return ContentDecode(value, diagnostics)


def write_content(path: Path, data: dict[str, JsonValue]) -> None:
    write_atomic(path, data)


def iter_definitions(content_file: ContentFile) -> Iterable[ContentDefinition]:
    for category in CONTENT_CATEGORIES:
        values = content_file.data.get(category, [])
        if not isinstance(values, list):
            continue
        id_field = ID_FIELDS[category]
        for index, value in enumerate(values):
            if not isinstance(value, dict):
                continue
            definition_id = value.get(id_field)
            if not isinstance(definition_id, str) or not definition_id:
                continue
            yield ContentDefinition(category, definition_id, value, content_file.path, content_file.origin, "")
