"""First-class Crafting Recipe authoring on top of the ContentWorkspace."""

from __future__ import annotations

import copy

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition, JsonValue

RECIPE_ID_PREFIX = "recipe."
MIN_RECIPE_INPUTS = 2
MAX_RECIPE_INPUTS = 4
MIN_RECIPE_OUTPUTS = 1
MAX_RECIPE_OUTPUTS = 4
MAX_QUANTITY = (1 << 32) - 1

# Sentinel for update_recipe: explicitly clears the quest gate instead of
# keeping the authored value (None keeps it).
_UNSET = object()


class CraftingAuthoringService:
    """CRUD facade that keeps recipes and their authoring descriptors consistent.

    Recipes are authored content like any other category: they reference Items
    by ``itemId`` so the workspace usage tracking blocks deleting Items that
    recipes still need.
    """

    def __init__(self, workspace: ContentWorkspace | None = None) -> None:
        self.workspace = workspace

    def set_context(self, workspace: ContentWorkspace | None) -> None:
        self.workspace = workspace

    def recipes(self, query: str = "") -> tuple[ContentDefinition, ...]:
        workspace = self._require_workspace()
        return tuple(workspace.definitions("craftingRecipes", query))

    def find(self, recipe_id: str) -> ContentDefinition | None:
        workspace = self._require_workspace()
        return workspace.find("craftingRecipes", recipe_id)

    def create_recipe(
        self,
        display_name: str,
        recipe_id: str,
        inputs: list[dict[str, JsonValue]] | None = None,
        outputs: list[dict[str, JsonValue]] | None = None,
        unlock_quest_id: str | None = None,
    ) -> ContentDefinition:
        workspace = self._require_workspace()
        normalized_id = self._normalize_recipe_id(recipe_id)
        normalized_name = self._normalize_display_name(display_name)
        normalized_inputs = self.normalize_ingredients(
            MIN_RECIPE_INPUTS, MAX_RECIPE_INPUTS, "input", inputs)
        normalized_outputs = self.normalize_ingredients(
            MIN_RECIPE_OUTPUTS, MAX_RECIPE_OUTPUTS, "output", outputs)
        normalized_quest = self._normalize_quest(unlock_quest_id)

        if workspace.find("craftingRecipes", normalized_id):
            raise ValueError(f"recipe already exists: {normalized_id}")
        if workspace.find("authoringDescriptors", normalized_id):
            raise ValueError(
                f"authoring descriptor already exists: {normalized_id}")

        workspace.create_definition_bundle(
            "Create Crafting Recipe",
            [
                (
                    "craftingRecipes",
                    normalized_id,
                    self._recipe_data(
                        normalized_id, normalized_inputs, normalized_outputs,
                        normalized_quest),
                ),
                (
                    "authoringDescriptors",
                    normalized_id,
                    self._descriptor_data(normalized_id, normalized_name),
                ),
            ],
        )

        result = workspace.find("craftingRecipes", normalized_id)
        if result is None:
            raise RuntimeError("created recipe could not be indexed")
        return result

    def update_recipe(
        self,
        recipe_id: str,
        *,
        display_name: str | None = None,
        inputs: list[dict[str, JsonValue]] | None = None,
        outputs: list[dict[str, JsonValue]] | None = None,
        unlock_quest_id: str | None | object = None,
    ) -> ContentDefinition:
        workspace = self._require_workspace()
        recipe = self._require_recipe(recipe_id)

        if inputs is None:
            normalized_inputs = self.normalize_ingredients(
                MIN_RECIPE_INPUTS, MAX_RECIPE_INPUTS, "input",
                recipe.data.get("inputs"))
        else:
            normalized_inputs = self.normalize_ingredients(
                MIN_RECIPE_INPUTS, MAX_RECIPE_INPUTS, "input", inputs)

        if outputs is None:
            normalized_outputs = self.normalize_ingredients(
                MIN_RECIPE_OUTPUTS, MAX_RECIPE_OUTPUTS, "output",
                recipe.data.get("outputs"))
        else:
            normalized_outputs = self.normalize_ingredients(
                MIN_RECIPE_OUTPUTS, MAX_RECIPE_OUTPUTS, "output", outputs)

        # None means "keep the authored value"; the explicit _UNSET sentinel
        # means "clear the quest gate"; a string sets/validates one.
        if unlock_quest_id is None:
            normalized_quest = self._normalize_quest(
                recipe.data.get("unlockQuestId"), required=False)
        elif unlock_quest_id is _UNSET:
            normalized_quest = None
        else:
            normalized_quest = self._normalize_quest(unlock_quest_id)

        descriptor = workspace.find("authoringDescriptors", recipe_id)
        if display_name is None:
            current_name = (
                descriptor.data.get("displayName")
                if descriptor is not None
                else recipe.display_name
            )
            normalized_name = self._normalize_display_name(str(current_name))
        else:
            normalized_name = self._normalize_display_name(display_name)

        descriptor_tags: JsonValue = []
        if descriptor is not None:
            existing_tags = descriptor.data.get("tags", [])
            if isinstance(existing_tags, list):
                descriptor_tags = copy.deepcopy(existing_tags)

        workspace.upsert_definition_bundle(
            "Update Crafting Recipe",
            [
                (
                    "craftingRecipes",
                    recipe_id,
                    self._recipe_data(
                        recipe_id, normalized_inputs, normalized_outputs, normalized_quest),
                ),
                (
                    "authoringDescriptors",
                    recipe_id,
                    {
                        "definitionId": recipe_id,
                        "displayName": normalized_name,
                        "category": "craftingRecipe",
                        "tags": descriptor_tags,
                    },
                ),
            ],
        )

        result = workspace.find("craftingRecipes", recipe_id)
        if result is None:
            raise RuntimeError("updated recipe could not be indexed")
        return result

    def rename_recipe(self, recipe_id: str, new_recipe_id: str) -> ContentDefinition:
        workspace = self._require_workspace()
        recipe = self._require_recipe(recipe_id)
        normalized_new = self._normalize_recipe_id(new_recipe_id)
        if normalized_new == recipe_id:
            return recipe
        if workspace.find("craftingRecipes", normalized_new):
            raise ValueError(f"recipe already exists: {normalized_new}")
        if workspace.find("authoringDescriptors", normalized_new):
            raise ValueError(
                f"authoring descriptor already exists: {normalized_new}")
        # rename_definition replaces the exact id string across the project,
        # which also keeps the paired authoring descriptor consistent.
        return workspace.rename_definition(recipe, normalized_new)

    def delete_recipe(self, recipe_id: str) -> bool:
        workspace = self._require_workspace()
        recipe = self._require_recipe(recipe_id)

        def operation() -> None:
            for content_file in workspace.files:
                if content_file.origin != "project":
                    continue
                changed = self._remove_from_category(
                    content_file.data, "craftingRecipes", "id", recipe.definition_id)
                changed = self._remove_from_category(
                    content_file.data, "authoringDescriptors", "definitionId",
                    recipe.definition_id) or changed
                if changed:
                    content_file.dirty = True

        workspace.mutate("Delete Crafting Recipe", operation)
        return True

    def normalize_ingredients(
        self,
        minimum: int,
        maximum: int,
        label: str,
        value: object,
    ) -> list[dict[str, JsonValue]]:
        """Validate and normalize one ingredient list; items must exist."""
        workspace = self._require_workspace()
        if not isinstance(value, list):
            raise ValueError(f"recipe {label}s must be a list")
        if not minimum <= len(value) <= maximum:
            raise ValueError(
                f"recipe requires between {minimum} and {maximum} distinct {label}s")
        normalized: list[dict[str, JsonValue]] = []
        seen: set[str] = set()
        for entry in value:
            if not isinstance(entry, dict):
                raise ValueError(f"recipe {label} must be an object")
            item_id = entry.get("itemId")
            quantity = entry.get("quantity")
            if not isinstance(item_id, str) or not item_id.strip():
                raise ValueError(f"recipe {label} itemId must reference an Item")
            item_id = item_id.strip()
            if (
                not isinstance(quantity, int)
                or isinstance(quantity, bool)
                or quantity <= 0
                or quantity > MAX_QUANTITY
            ):
                raise ValueError(
                    f"recipe {label} quantity must be a positive count")
            if item_id in seen:
                raise ValueError(f"recipe repeats {label} item: {item_id}")
            if workspace.find("items", item_id) is None:
                raise ValueError(f"recipe {label} item does not exist: {item_id}")
            seen.add(item_id)
            normalized.append({"itemId": item_id, "quantity": quantity})
        return normalized

    def _require_workspace(self) -> ContentWorkspace:
        if self.workspace is None:
            raise ValueError("content workspace is unavailable")
        return self.workspace

    def _require_recipe(self, recipe_id: str) -> ContentDefinition:
        definition = self._require_workspace().find("craftingRecipes", recipe_id)
        if definition is None:
            raise ValueError(f"recipe not found: {recipe_id}")
        return definition

    @staticmethod
    def _normalize_recipe_id(recipe_id: str) -> str:
        normalized = recipe_id.strip()
        if (
            not normalized.startswith(RECIPE_ID_PREFIX)
            or len(normalized) <= len(RECIPE_ID_PREFIX)
        ):
            raise ValueError("recipe id must use the recipe.* namespace")
        return normalized

    @staticmethod
    def _normalize_display_name(display_name: str) -> str:
        normalized = display_name.strip()
        if not normalized:
            raise ValueError("recipe display name is required")
        return normalized

    def _normalize_quest(self, quest_id: object, required: bool = True) -> str | None:
        """Validate the optional unlock quest; empty/None clears the gate."""
        workspace = self._require_workspace()
        if quest_id is None or (isinstance(quest_id, str) and not quest_id.strip()):
            if required:
                return None
            return None
        if not isinstance(quest_id, str):
            raise ValueError("recipe unlock quest must be a quest ID")
        normalized = quest_id.strip()
        if workspace.find("quests", normalized) is None:
            raise ValueError(f"recipe unlock quest does not exist: {normalized}")
        return normalized

    @staticmethod
    def _recipe_data(
        recipe_id: str,
        inputs: list[dict[str, JsonValue]],
        outputs: list[dict[str, JsonValue]],
        unlock_quest_id: str | None = None,
    ) -> dict[str, JsonValue]:
        return {
            "id": recipe_id,
            "inputs": copy.deepcopy(inputs),
            "outputs": copy.deepcopy(outputs),
            "unlockQuestId": unlock_quest_id,
        }

    @staticmethod
    def _descriptor_data(
        recipe_id: str,
        display_name: str,
    ) -> dict[str, JsonValue]:
        return {
            "definitionId": recipe_id,
            "displayName": display_name,
            "category": "craftingRecipe",
            "tags": [],
        }

    @staticmethod
    def _remove_from_category(
        data: dict[str, JsonValue],
        category: str,
        id_field: str,
        definition_id: str,
    ) -> bool:
        values = data.get(category)
        if not isinstance(values, list):
            return False
        before = len(values)
        values[:] = [
            value for value in values
            if not (isinstance(value, dict) and value.get(id_field) == definition_id)
        ]
        return len(values) != before
