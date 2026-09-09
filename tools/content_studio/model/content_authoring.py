from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable

from .content_workspace import ContentWorkspace
from .types import ContentDefinition, ContentReference


@dataclass(frozen=True, slots=True)
class DefinitionSchema:
    """Small category descriptor for generic authoring UI adapters."""

    category: str
    id_field: str = "id"
    display_name_field: str = "displayName"

    @classmethod
    def for_category(cls, category: str) -> "DefinitionSchema":
        return cls(category, "definitionId" if category == "authoringDescriptors" else "id")


class DefinitionRepository:
    """CRUD facade over ``ContentWorkspace``; it owns no parallel data."""

    def __init__(self, workspace: ContentWorkspace) -> None:
        self.workspace = workspace

    def all(self, category: str | None = None, query: str = "") -> list[ContentDefinition]:
        return self.workspace.definitions(category, query)

    def find(self, reference: ContentReference) -> ContentDefinition | None:
        return self.workspace.find(reference.category, reference.definition_id)

    def create(self, category: str, definition_id: str) -> ContentDefinition:
        return self.workspace.create_definition(category, definition_id)

    def update(self, definition: ContentDefinition, path: str, value: object) -> None:
        self.workspace.update(definition, path, value)  # type: ignore[arg-type]

    def delete(self, definition: ContentDefinition) -> None:
        self.workspace.delete_definition(definition)

    def duplicate(self, definition: ContentDefinition, new_definition_id: str) -> ContentDefinition:
        created = self.create(definition.category, new_definition_id)
        data = dict(definition.data)
        data[DefinitionSchema.for_category(definition.category).id_field] = new_definition_id
        self.workspace.replace_definition(created, data)
        return self.workspace.find(definition.category, new_definition_id) or created

    def rename(self, definition: ContentDefinition, new_definition_id: str) -> ContentDefinition:
        return self.workspace.rename_definition(definition, new_definition_id)


class DefinitionEditor:
    """UI-neutral command adapter used by future category-specific inspectors."""

    def __init__(self, repository: DefinitionRepository) -> None:
        self.repository = repository

    def set_field(self, definition: ContentDefinition, path: str, value: object) -> None:
        self.repository.update(definition, path, value)

    def duplicate(self, definition: ContentDefinition, new_definition_id: str) -> ContentDefinition:
        return self.repository.duplicate(definition, new_definition_id)

    def rename(self, definition: ContentDefinition, new_definition_id: str) -> ContentDefinition:
        return self.repository.rename(definition, new_definition_id)


class ReferenceIndex:
    """Typed usage view that supplements, but does not replace, find_usages()."""

    def __init__(self, workspace: ContentWorkspace) -> None:
        self.workspace = workspace
        self._usages: dict[ContentReference, list[ContentReference]] = {}
        self.rebuild()

    def rebuild(self) -> None:
        self._usages.clear()
        for owner in self.workspace.definitions():
            for category, definition_id in _typed_references(owner.data):
                target = ContentReference(category, definition_id)
                self._usages.setdefault(target, []).append(ContentReference(owner.category, owner.definition_id))

    def usages(self, reference: ContentReference) -> list[ContentReference]:
        return list(self._usages.get(reference, ()))


def _typed_references(value: object, field_name: str = "") -> Iterable[tuple[str, str]]:
    if isinstance(value, dict):
        for key, child in value.items():
            yield from _typed_references(child, key)
    elif isinstance(value, list):
        for child in value:
            yield from _typed_references(child, field_name)
    elif isinstance(value, str) and value:
        category = {
            "visualSetId": "enemyVisuals",
            "visualId": "staticSprites",
            "imageId": "visualImages",
            "behaviorProfileId": "behaviors",
            "projectileDefinitionId": "projectiles",
            "rewardProfileId": "rewardProfiles",
            "rewardGrantId": "rewardGrants",
            "defaultDialogueId": "dialogues",
            "itemId": "items",
            "pickupDefinitionId": "pickups",
        }.get(field_name)
        if category:
            yield category, value
        elif field_name.endswith("AnimationId"):
            yield "animations", value
