"""First-class Item authoring and generated Pickup ownership."""

from __future__ import annotations

import copy

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition, JsonValue


VALID_ITEM_CATEGORIES = frozenset({
    "consumable",
    "equipment",
    "key",
    "misc",
})

DEFAULT_STACK_LIMIT = 66
DEFAULT_KEY_STACK_LIMIT = 1
MAX_STACK_LIMIT = (1 << 32) - 1

DEFAULT_PICKUP_BOUNDS: dict[str, JsonValue] = {
    "x": -5,
    "y": -5,
    "width": 10,
    "height": 10,
}


class ItemAuthoringService:
    """CRUD facade that keeps Item and owned item-Pickup definitions synchronized."""

    def __init__(
        self,
        workspace: ContentWorkspace | None = None,
    ) -> None:
        self.workspace = workspace

    def set_context(
        self,
        workspace: ContentWorkspace | None,
    ) -> None:
        self.workspace = workspace

    def items(
        self,
        query: str = "",
    ) -> tuple[ContentDefinition, ...]:
        workspace = self._require_workspace()
        return tuple(
            workspace.definitions(
                "items",
                query,
            )
        )

    def find(
        self,
        item_id: str,
    ) -> ContentDefinition | None:
        workspace = self._require_workspace()
        return workspace.find(
            "items",
            item_id,
        )

    @staticmethod
    def generated_pickup_id(
        item_id: str,
    ) -> str:
        normalized = ItemAuthoringService._normalize_item_id(
            item_id
        )
        return "pickup." + normalized[len("item."):]

    def stack_limit(
        self,
        item_id: str,
    ) -> int:
        """Return the authored stack limit for one Item."""

        normalized = item_id.strip()

        if not normalized:
            raise ValueError(
                "item ID cannot be empty"
            )

        item = self._require_item(
            normalized
        )

        value = item.data.get(
            "stackLimit"
        )

        if (
            not isinstance(
                value,
                int,
            )
            or isinstance(
                value,
                bool,
            )
            or value <= 0
            or value > MAX_STACK_LIMIT
        ):
            raise ValueError(
                f"item stackLimit is invalid: "
                f"{normalized}"
            )

        return value

    def validate_stack(
        self,
        item_id: str,
        quantity: int,
    ) -> dict[str, JsonValue]:
        """Validate and normalize one authored ItemStack."""

        normalized = item_id.strip()

        if not normalized:
            raise ValueError(
                "itemId must reference an Item"
            )

        if (
            not isinstance(
                quantity,
                int,
            )
            or isinstance(
                quantity,
                bool,
            )
            or quantity <= 0
        ):
            raise ValueError(
                "item quantity must be positive"
            )

        limit = self.stack_limit(
            normalized
        )

        if quantity > limit:
            raise ValueError(
                f"item quantity exceeds stackLimit "
                f"{limit}: {normalized}"
            )

        return {
            "itemId": normalized,
            "quantity": quantity,
        }

    def pickup_for_item(
        self,
        item_id: str,
    ) -> ContentDefinition | None:
        workspace = self._require_workspace()
        matches: list[ContentDefinition] = []

        for pickup in workspace.definitions(
            "pickups"
        ):
            payload = pickup.data.get(
                "payload"
            )

            if not isinstance(
                payload,
                dict,
            ):
                continue

            if (
                payload.get("kind") == "item"
                and payload.get("itemId") == item_id
            ):
                matches.append(
                    pickup
                )

        if len(matches) > 1:
            raise ValueError(
                f"multiple pickups reference item: {item_id}"
            )

        return (
            matches[0]
            if matches
            else None
        )

    def create_item(
        self,
        display_name: str,
        item_id: str,
        visual_id: str,
        category: str = "misc",
        stack_limit: int | None = None,
        generated_visual: dict[str, JsonValue] | None = None,
    ) -> ContentDefinition:
        workspace = self._require_workspace()

        normalized_id = self._normalize_item_id(
            item_id
        )
        normalized_name = self._normalize_display_name(
            display_name
        )
        normalized_category = self._normalize_category(
            category
        )
        normalized_visual, visual_entry = (
            self._resolve_visual_for_bundle(
                visual_id,
                generated_visual,
            )
        )
        normalized_stack = self._normalize_stack_limit(
            normalized_category,
            stack_limit,
        )

        if workspace.find(
            "items",
            normalized_id,
        ):
            raise ValueError(
                f"item already exists: {normalized_id}"
            )

        pickup_id = self.generated_pickup_id(
            normalized_id
        )

        if workspace.find(
            "pickups",
            pickup_id,
        ):
            raise ValueError(
                f"pickup already exists: {pickup_id}"
            )

        if workspace.find(
            "authoringDescriptors",
            normalized_id,
        ):
            raise ValueError(
                f"authoring descriptor already exists: {normalized_id}"
            )

        if self.pickup_for_item(
            normalized_id
        ):
            raise ValueError(
                f"pickup already references item: {normalized_id}"
            )

        entries: list[
            tuple[str, str, dict[str, JsonValue]]
        ] = []

        if visual_entry is not None:
            entries.append((
                "staticSprites",
                normalized_visual,
                visual_entry,
            ))

        entries.extend([
            (
                "items",
                normalized_id,
                self._item_data(
                    normalized_id,
                    normalized_visual,
                    normalized_category,
                    normalized_stack,
                ),
            ),
            (
                "pickups",
                pickup_id,
                self._pickup_data(
                    pickup_id,
                    normalized_visual,
                    normalized_id,
                ),
            ),
            (
                "authoringDescriptors",
                normalized_id,
                self._descriptor_data(
                    normalized_id,
                    normalized_name,
                ),
            ),
        ])

        workspace.create_definition_bundle(
            "Create Item",
            entries,
        )

        result = workspace.find(
            "items",
            normalized_id,
        )

        if result is None:
            raise RuntimeError(
                "created item could not be indexed"
            )

        return result

    def update_item(
        self,
        item_id: str,
        *,
        display_name: str | None = None,
        visual_id: str | None = None,
        category: str | None = None,
        stack_limit: int | None = None,
        generated_visual: dict[str, JsonValue] | None = None,
    ) -> ContentDefinition:
        workspace = self._require_workspace()
        item = self._require_item(
            item_id
        )

        old_category = self._normalize_category(
            str(
                item.data.get(
                    "category",
                    "misc",
                )
            )
        )

        new_category = (
            self._normalize_category(
                category
            )
            if category is not None
            else old_category
        )

        old_visual = item.data.get(
            "visualId"
        )

        new_visual, visual_entry = (
            self._resolve_visual_for_bundle(
                visual_id
                if visual_id is not None
                else (
                    old_visual
                    if isinstance(
                        old_visual,
                        str,
                    )
                    else ""
                ),
                generated_visual,
            )
        )

        if stack_limit is not None:
            new_stack = self._normalize_stack_limit(
                new_category,
                stack_limit,
            )
        elif new_category != old_category:
            new_stack = self._normalize_stack_limit(
                new_category,
                None,
            )
        else:
            current_stack = item.data.get(
                "stackLimit"
            )
            new_stack = self._normalize_stack_limit(
                new_category,
                (
                    current_stack
                    if isinstance(
                        current_stack,
                        int,
                    )
                    and not isinstance(
                        current_stack,
                        bool,
                    )
                    else None
                ),
            )

        descriptor = workspace.find(
            "authoringDescriptors",
            item_id,
        )

        if display_name is None:
            current_name = (
                descriptor.data.get(
                    "displayName"
                )
                if descriptor is not None
                else item.display_name
            )
            new_name = self._normalize_display_name(
                str(current_name)
            )
        else:
            new_name = self._normalize_display_name(
                display_name
            )

        pickup = self.pickup_for_item(
            item_id
        )

        if pickup is None:
            pickup_id = self.generated_pickup_id(
                item_id
            )

            existing = workspace.find(
                "pickups",
                pickup_id,
            )

            if existing is not None:
                raise ValueError(
                    f"pickup id is already in use: {pickup_id}"
                )
        else:
            pickup_id = pickup.definition_id

        updated_item = copy.deepcopy(
            item.data
        )

        updated_item.update({
            "id": item_id,
            "visualId": new_visual,
            "category": new_category,
            "stackLimit": new_stack,
        })

        if new_category == "equipment":
            existing_equipment = item.data.get(
                "equipment"
            )

            if (
                old_category == "equipment"
                and isinstance(
                    existing_equipment,
                    dict,
                )
            ):
                updated_item["equipment"] = copy.deepcopy(
                    existing_equipment
                )
            else:
                updated_item["equipment"] = self._default_equipment()

            updated_item["use"] = None
        else:
            updated_item["equipment"] = None

        if pickup is not None:
            updated_pickup = copy.deepcopy(
                pickup.data
            )
            updated_pickup.update({
                "id": pickup_id,
                "visualId": new_visual,
                "payload": {
                    "kind": "item",
                    "itemId": item_id,
                    "quantity": 1,
                },
            })
        else:
            updated_pickup = self._pickup_data(
                pickup_id,
                new_visual,
                item_id,
            )

        descriptor_tags: JsonValue = []

        if descriptor is not None:
            existing_tags = descriptor.data.get(
                "tags",
                [],
            )

            if isinstance(
                existing_tags,
                list,
            ):
                descriptor_tags = copy.deepcopy(
                    existing_tags
                )

        updated_descriptor: dict[str, JsonValue] = {
            "definitionId": item_id,
            "displayName": new_name,
            "category": "item",
            "tags": descriptor_tags,
        }

        entries: list[
            tuple[str, str, dict[str, JsonValue]]
        ] = []

        if visual_entry is not None:
            entries.append((
                "staticSprites",
                new_visual,
                visual_entry,
            ))

        entries.extend([
            (
                "items",
                item_id,
                updated_item,
            ),
            (
                "pickups",
                pickup_id,
                updated_pickup,
            ),
            (
                "authoringDescriptors",
                item_id,
                updated_descriptor,
            ),
        ])

        workspace.upsert_definition_bundle(
            "Update Item",
            entries,
        )

        result = workspace.find(
            "items",
            item_id,
        )

        if result is None:
            raise RuntimeError(
                "updated item could not be indexed"
            )

        return result

    def rename_item(
        self,
        item_id: str,
        new_item_id: str,
    ) -> ContentDefinition:
        workspace = self._require_workspace()
        self._require_item(
            item_id
        )

        normalized_new = self._normalize_item_id(
            new_item_id
        )

        if normalized_new == item_id:
            return self._require_item(
                item_id
            )

        if workspace.find(
            "items",
            normalized_new,
        ):
            raise ValueError(
                f"item already exists: {normalized_new}"
            )

        pickup = self.pickup_for_item(
            item_id
        )

        if pickup is None:
            raise ValueError(
                f"item has no owned pickup: {item_id}"
            )

        old_pickup_id = pickup.definition_id
        new_pickup_id = self.generated_pickup_id(
            normalized_new
        )

        conflicting_pickup = workspace.find(
            "pickups",
            new_pickup_id,
        )

        if (
            conflicting_pickup is not None
            and conflicting_pickup.definition_id != old_pickup_id
        ):
            raise ValueError(
                f"pickup already exists: {new_pickup_id}"
            )

        if workspace.find(
            "authoringDescriptors",
            normalized_new,
        ):
            raise ValueError(
                f"authoring descriptor already exists: {normalized_new}"
            )

        replacements = {
            item_id: normalized_new,
            old_pickup_id: new_pickup_id,
        }

        def operation() -> None:
            for content_file in workspace.files:
                if content_file.origin != "project":
                    continue

                before = copy.deepcopy(
                    content_file.data
                )

                self._replace_exact(
                    content_file.data,
                    replacements,
                )

                if content_file.data != before:
                    content_file.dirty = True

        workspace.mutate(
            "Rename Item",
            operation,
        )

        result = workspace.find(
            "items",
            normalized_new,
        )

        if result is None:
            raise RuntimeError(
                "renamed item could not be indexed"
            )

        owned = self.pickup_for_item(
            normalized_new
        )

        if (
            owned is None
            or owned.definition_id != new_pickup_id
        ):
            raise RuntimeError(
                "renamed item pickup is inconsistent"
            )

        return result

    def delete_item(
        self,
        item_id: str,
    ) -> bool:
        workspace = self._require_workspace()
        item = self._require_item(
            item_id
        )
        pickup = self.pickup_for_item(
            item_id
        )

        external: list[str] = []

        for usage in workspace.find_usages(
            item_id
        ):
            if (
                pickup is not None
                and usage.category == "pickups"
                and usage.definition_id == pickup.definition_id
            ):
                continue

            external.append(
                f"{usage.category}/{usage.definition_id}"
            )

        if pickup is not None:
            for usage in workspace.find_usages(
                pickup.definition_id
            ):
                external.append(
                    f"{usage.category}/{usage.definition_id}"
                )

        if external:
            raise ValueError(
                "item is in use: "
                + ", ".join(
                    sorted(
                        set(external)
                    )
                )
            )

        pickup_id = (
            pickup.definition_id
            if pickup is not None
            else None
        )

        def operation() -> None:
            for content_file in workspace.files:
                if content_file.origin != "project":
                    continue

                changed = False

                changed = self._remove_from_category(
                    content_file.data,
                    "items",
                    "id",
                    item.definition_id,
                ) or changed

                if pickup_id is not None:
                    changed = self._remove_from_category(
                        content_file.data,
                        "pickups",
                        "id",
                        pickup_id,
                    ) or changed

                changed = self._remove_from_category(
                    content_file.data,
                    "authoringDescriptors",
                    "definitionId",
                    item.definition_id,
                ) or changed

                if changed:
                    content_file.dirty = True

        workspace.mutate(
            "Delete Item",
            operation,
        )

        return True

    def _require_workspace(
        self,
    ) -> ContentWorkspace:
        if self.workspace is None:
            raise ValueError(
                "content workspace is unavailable"
            )

        return self.workspace

    def _require_item(
        self,
        item_id: str,
    ) -> ContentDefinition:
        workspace = self._require_workspace()
        definition = workspace.find(
            "items",
            item_id,
        )

        if definition is None:
            raise ValueError(
                f"item not found: {item_id}"
            )

        return definition

    def _resolve_visual_for_bundle(
        self,
        visual_id: str,
        generated_visual: dict[str, JsonValue] | None,
    ) -> tuple[
        str,
        dict[str, JsonValue] | None,
    ]:
        """Resolve an existing visual or validate one pending creation."""

        workspace = self._require_workspace()
        normalized = visual_id.strip()

        if not normalized:
            raise ValueError(
                "item visual is required"
            )

        existing = workspace.find(
            "staticSprites",
            normalized,
        )

        if generated_visual is None:
            if existing is None:
                raise ValueError(
                    f"item visual does not exist: "
                    f"{normalized}"
                )

            return (
                normalized,
                None,
            )

        candidate = copy.deepcopy(
            generated_visual
        )

        if candidate.get(
            "id"
        ) != normalized:
            raise ValueError(
                "generated item visual ID does not "
                "match the selected visual"
            )

        if existing is not None:
            if existing.data != candidate:
                raise ValueError(
                    f"generated visual conflict: "
                    f"{normalized}"
                )

            return (
                normalized,
                None,
            )

        image_id = candidate.get(
            "imageId"
        )

        if (
            not isinstance(
                image_id,
                str,
            )
            or not image_id
            or workspace.find(
                "visualImages",
                image_id,
            ) is None
        ):
            raise ValueError(
                "generated item visual must reference "
                "an existing visual image"
            )

        for field in (
            "source",
            "anchor",
        ):
            if not isinstance(
                candidate.get(
                    field
                ),
                dict,
            ):
                raise ValueError(
                    f"generated item visual has invalid "
                    f"{field}"
                )

        return (
            normalized,
            candidate,
        )

    def _require_visual(
        self,
        visual_id: str,
    ) -> str:
        workspace = self._require_workspace()
        normalized = visual_id.strip()

        if not normalized:
            raise ValueError(
                "item visual is required"
            )

        if workspace.find(
            "staticSprites",
            normalized,
        ) is None:
            raise ValueError(
                f"item visual does not exist: {normalized}"
            )

        return normalized

    @staticmethod
    def _normalize_item_id(
        item_id: str,
    ) -> str:
        normalized = item_id.strip()

        if (
            not normalized.startswith("item.")
            or len(normalized) <= len("item.")
        ):
            raise ValueError(
                "item id must use the item.* namespace"
            )

        return normalized

    @staticmethod
    def _normalize_display_name(
        display_name: str,
    ) -> str:
        normalized = display_name.strip()

        if not normalized:
            raise ValueError(
                "item display name is required"
            )

        return normalized

    @staticmethod
    def _normalize_category(
        category: str,
    ) -> str:
        normalized = category.strip()

        if normalized not in VALID_ITEM_CATEGORIES:
            raise ValueError(
                f"unknown item category: {normalized}"
            )

        return normalized

    @staticmethod
    def _normalize_stack_limit(
        category: str,
        stack_limit: int | None,
    ) -> int:
        if stack_limit is not None:
            if (
                not isinstance(
                    stack_limit,
                    int,
                )
                or isinstance(
                    stack_limit,
                    bool,
                )
                or stack_limit <= 0
                or stack_limit > MAX_STACK_LIMIT
            ):
                raise ValueError(
                    "item stack limit must be between 1 and 4294967295"
                )

        if category == "equipment":
            return 1

        if stack_limit is not None:
            return stack_limit

        if category == "key":
            return DEFAULT_KEY_STACK_LIMIT

        return DEFAULT_STACK_LIMIT

    @staticmethod
    def _default_equipment() -> dict[str, JsonValue]:
        return {
            "slot": "armor",
            "modifiers": {
                "maximumHealthBonus": 0,
                "playerAttackDamageBonus": 0,
            },
        }

    @classmethod
    def _item_data(
        cls,
        item_id: str,
        visual_id: str,
        category: str,
        stack_limit: int,
    ) -> dict[str, JsonValue]:
        return {
            "id": item_id,
            "visualId": visual_id,
            "category": category,
            "stackLimit": stack_limit,
            "use": None,
            "equipment": (
                cls._default_equipment()
                if category == "equipment"
                else None
            ),
        }

    @staticmethod
    def _pickup_data(
        pickup_id: str,
        visual_id: str,
        item_id: str,
    ) -> dict[str, JsonValue]:
        return {
            "id": pickup_id,
            "visualId": visual_id,
            "collectionBounds": copy.deepcopy(
                DEFAULT_PICKUP_BOUNDS
            ),
            "payload": {
                "kind": "item",
                "itemId": item_id,
                "quantity": 1,
            },
        }

    @staticmethod
    def _descriptor_data(
        item_id: str,
        display_name: str,
    ) -> dict[str, JsonValue]:
        return {
            "definitionId": item_id,
            "displayName": display_name,
            "category": "item",
            "tags": [],
        }

    @classmethod
    def _replace_exact(
        cls,
        value: JsonValue,
        replacements: dict[str, str],
    ) -> JsonValue:
        if isinstance(
            value,
            str,
        ):
            return replacements.get(
                value,
                value,
            )

        if isinstance(
            value,
            list,
        ):
            for index, child in enumerate(
                value
            ):
                value[index] = cls._replace_exact(
                    child,
                    replacements,
                )

            return value

        if isinstance(
            value,
            dict,
        ):
            for key, child in list(
                value.items()
            ):
                value[key] = cls._replace_exact(
                    child,
                    replacements,
                )

            return value

        return value

    @staticmethod
    def _remove_from_category(
        data: dict[str, JsonValue],
        category: str,
        id_field: str,
        definition_id: str,
    ) -> bool:
        values = data.get(
            category
        )

        if not isinstance(
            values,
            list,
        ):
            return False

        before = len(
            values
        )

        values[:] = [
            value
            for value in values
            if not (
                isinstance(
                    value,
                    dict,
                )
                and value.get(
                    id_field
                ) == definition_id
            )
        ]

        return len(values) != before