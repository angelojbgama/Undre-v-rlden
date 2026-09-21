from __future__ import annotations

import copy
from dataclasses import dataclass

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition

ATTACK_KINDS = (
    "meleeHitbox",
    "projectile",
)

TIMELINE_KINDS = (
    "activateHitbox",
    "deactivateHitbox",
    "spawnProjectile",
    "playEffect",
)

DIRECTIONS = (
    "down",
    "up",
    "left",
    "right",
)

EDITABLE_FIELDS = (
    "kind",
    "damage",
    "totalTicks",
    "cooldownTicks",
    "minimumRangePixels",
    "maximumRangePixels",
    "visualActionId",
    "meleeHitboxes",
    "projectileDefinitionId",
    "timeline",
    "ammo",
    "presentationEffectId",
)

@dataclass(
    frozen=True,
    slots=True,
)
class AttackCatalogEntry:
    definition_id: str
    kind: str
    damage_amount: int
    # builtin: runtime-only; override: authored over a builtin id;
    # authored: ordinary project definition.
    status: str
    referenced_by: tuple[str, ...]


class AttackAuthoringService:
    """Author attack definitions."""

    def __init__(
        self,
        workspace: ContentWorkspace,
    ) -> None:
        self.workspace = workspace

    def entries(
        self,
    ) -> list[AttackCatalogEntry]:
        referenced_by = self._references()

        result = [
            self._entry(
                definition.definition_id,
                definition.data,
                "authored",
                referenced_by,
            )
            for definition in self.workspace.definitions("attacks")
        ]

        result.sort(key=lambda entry: entry.definition_id)

        return result

    def configuration(
        self,
        definition_id: str,
    ) -> dict[str, object]:
        definition = self.workspace.find(
            "attacks",
            definition_id,
        )

        if definition is None:
            raise ValueError(
                f"unknown attack definition: "
                f"{definition_id}"
            )

        data = copy.deepcopy(definition.data)

        data.setdefault(
            "id",
            definition_id,
        )

        return data

    def configure(
        self,
        definition_id: str,
        data: dict[str, object],
    ) -> None:
        if not isinstance(
            data.get("id"),
            str,
        ) or data["id"] != definition_id:
            raise ValueError(
                "attack id does not match the edited definition"
            )

        normalized = self._validate(
            data
        )

        self.workspace.upsert_definition_bundle(
            "Edit Attack",
            [
                (
                    "attacks",
                    definition_id,
                    normalized,
                )
            ],
        )

    def delete(
        self,
        definition_id: str,
    ) -> None:
        definition = self.workspace.find(
            "attacks",
            definition_id,
        )

        if definition is None:
            raise ValueError(
                f"attack definition is not authored: "
                f"{definition_id}"
            )

        referencing = self._references().get(
            definition_id,
            (),
        )

        if referencing:
            raise ValueError(
                "attack is referenced by enemies: "
                + ", ".join(referencing)
            )

        self.workspace.delete_definition(
            definition
        )

    def _entry(
        self,
        definition_id: str,
        data: dict[str, object],
        status: str,
        referenced_by: dict[str, tuple[str, ...]],
    ) -> AttackCatalogEntry:
        kind = data.get(
            "kind"
        )

        damage = data.get(
            "damage"
        )

        amount = (
            damage.get("amount")
            if isinstance(damage, dict)
            else None
        )

        return AttackCatalogEntry(
            definition_id=definition_id,
            kind=kind
            if isinstance(kind, str)
            else "",
            damage_amount=amount
            if isinstance(amount, int)
            and not isinstance(amount, bool)
            else 0,
            status=status,
            referenced_by=referenced_by.get(
                definition_id,
                (),
            ),
        )

    def _references(
        self,
    ) -> dict[str, tuple[str, ...]]:
        references: dict[
            str,
            list[str],
        ] = {}

        for enemy in self.workspace.definitions(
            "enemies"
        ):
            attack_ids = enemy.data.get(
                "attackIds"
            )

            if not isinstance(
                attack_ids,
                list,
            ):
                continue

            for attack_id in attack_ids:
                if not isinstance(
                    attack_id,
                    str,
                ):
                    continue

                references.setdefault(
                    attack_id,
                    [],
                ).append(
                    enemy.definition_id
                )

        return {
            attack_id: tuple(enemies)
            for attack_id, enemies in references.items()
        }

    def _validate(
        self,
        data: dict[str, object],
    ) -> dict[str, object]:
        kind = data.get(
            "kind"
        )

        if kind not in ATTACK_KINDS:
            raise ValueError(
                f"unknown attack kind: {kind!r}"
            )

        damage = data.get(
            "damage"
        )

        if not isinstance(
            damage,
            dict,
        ):
            raise ValueError(
                "attack damage must be an object"
            )

        for field in (
            "amount",
            "knockbackPixels",
        ):
            if not self._is_int(
                damage.get(field)
            ):
                raise ValueError(
                    f"attack damage {field} must be an integer"
                )

        for field in (
            "totalTicks",
            "cooldownTicks",
        ):
            if not self._is_int(
                data.get(field)
            ):
                raise ValueError(
                    f"attack {field} must be an integer"
                )

        total_ticks = data["totalTicks"]

        if total_ticks <= 0:
            raise ValueError(
                "attack totalTicks must be positive"
            )

        if data["cooldownTicks"] < 0:
            raise ValueError(
                "attack cooldownTicks must not be negative"
            )

        minimum_range = data.get(
            "minimumRangePixels"
        )

        maximum_range = data.get(
            "maximumRangePixels"
        )

        if (
            not self._is_int(minimum_range)
            or not self._is_int(maximum_range)
            or minimum_range > maximum_range
        ):
            raise ValueError(
                "attack range values are invalid"
            )

        visual_action = data.get(
            "visualActionId"
        )

        if (
            not isinstance(visual_action, str)
            or not visual_action
        ):
            raise ValueError(
                "attack visualActionId is required"
            )

        timeline = self._validate_timeline(
            data.get("timeline"),
            total_ticks,
            kind,
        )

        ammo = self._validate_ammo(
            data.get("ammo"),
            kind,
        )

        presentation_effect_id = (
            self._validate_presentation_effect(
                data.get("presentationEffectId"),
            )
        )

        melee_hitboxes = data.get(
            "meleeHitboxes"
        )

        projectile_id = data.get(
            "projectileDefinitionId"
        )

        if kind == "meleeHitbox":
            normalized_hitboxes = (
                self._validate_melee_hitboxes(
                    melee_hitboxes
                )
            )

            return self._normalized(
                data,
                melee_hitboxes=normalized_hitboxes,
                projectile_definition_id=None,
                timeline=timeline,
                ammo=None,
                presentation_effect_id=(
                    presentation_effect_id
                ),
            )

        if (
            not isinstance(projectile_id, str)
            or not projectile_id
        ):
            raise ValueError(
                "projectile attack requires projectileDefinitionId"
            )

        if self.workspace.find(
            "projectiles",
            projectile_id,
        ) is None:
            raise ValueError(
                f"unknown projectile: {projectile_id}"
            )

        return self._normalized(
            data,
            melee_hitboxes=None,
            projectile_definition_id=projectile_id,
            timeline=timeline,
            ammo=ammo,
            presentation_effect_id=(
                presentation_effect_id
            ),
        )

    def _validate_ammo(
        self,
        ammo: object,
        kind: str,
    ) -> dict[str, object] | None:
        if ammo is None:
            return None

        if not isinstance(ammo, dict):
            raise ValueError(
                "attack ammo must be an object"
            )

        if kind != "projectile":
            raise ValueError(
                "only projectile attacks can consume ammo"
            )

        item_id = ammo.get("itemId")

        if (
            not isinstance(item_id, str)
            or not item_id
        ):
            raise ValueError(
                "attack ammo requires itemId"
            )

        if self.workspace.find(
            "items",
            item_id,
        ) is None:
            raise ValueError(
                f"unknown item: {item_id}"
            )

        amount = ammo.get("amount", 1)

        if not self._is_int(amount) or amount < 1:
            raise ValueError(
                "attack ammo amount must be positive"
            )

        return {
            "itemId": item_id,
            "amount": int(amount),
        }

    def _validate_presentation_effect(
        self,
        presentation_effect_id: object,
    ) -> str | None:
        if presentation_effect_id is None:
            return None

        if (
            not isinstance(presentation_effect_id, str)
            or not presentation_effect_id
        ):
            raise ValueError(
                "attack presentationEffectId must be a "
                "non-empty string or None"
            )

        if self.workspace.find(
            "presentationEffects",
            presentation_effect_id,
        ) is None:
            raise ValueError(
                f"unknown presentation effect: "
                f"{presentation_effect_id}"
            )

        return presentation_effect_id

    def _validate_timeline(
        self,
        timeline: object,
        total_ticks: int,
        kind: str,
    ) -> list[dict[str, object]]:
        if not isinstance(
            timeline,
            list,
        ):
            raise ValueError(
                "attack timeline must be an array"
            )

        previous_tick = -1

        normalized: list[
            dict[str, object]
        ] = []

        for event in timeline:
            if not isinstance(
                event,
                dict,
            ):
                raise ValueError(
                    "attack timeline events must be objects"
                )

            tick = event.get(
                "tick"
            )

            event_kind = event.get(
                "kind"
            )

            if (
                not self._is_int(tick)
                or tick < 0
                or tick >= total_ticks
            ):
                raise ValueError(
                    "attack timeline tick must be within totalTicks"
                )

            if tick < previous_tick:
                raise ValueError(
                    "attack timeline must be ordered by tick"
                )

            if event_kind not in TIMELINE_KINDS:
                raise ValueError(
                    f"unknown attack timeline kind: {event_kind!r}"
                )

            if event_kind == "playEffect":
                animation_id = event.get("animationId")

                if (
                    not isinstance(animation_id, str)
                    or not animation_id
                    or self.workspace.find(
                        "animations",
                        animation_id,
                    )
                    is None
                ):
                    raise ValueError(
                        "playEffect timeline event requires an existing animation"
                    )

                offset_x = event.get("offsetX", 0)
                offset_y = event.get("offsetY", 0)

                if not self._is_int(offset_x) or not self._is_int(offset_y):
                    raise ValueError(
                        "playEffect timeline offsets must be integers"
                    )

                normalized.append(
                    {
                        "tick": tick,
                        "kind": event_kind,
                        "animationId": animation_id,
                        "offsetX": int(offset_x),
                        "offsetY": int(offset_y),
                    }
                )

                previous_tick = tick

                continue

            previous_tick = tick

            normalized.append(
                {
                    "tick": tick,
                    "kind": event_kind,
                }
            )

        if (
            kind == "projectile"
            and not any(
                event["kind"] == "spawnProjectile"
                for event in normalized
            )
        ):
            raise ValueError(
                "projectile attack requires a spawnProjectile timeline event"
            )

        return normalized

    @staticmethod
    def _validate_melee_hitboxes(
        melee_hitboxes: object,
    ) -> dict[str, dict[str, int]]:
        if not isinstance(
            melee_hitboxes,
            dict,
        ):
            raise ValueError(
                "melee attack requires meleeHitboxes"
            )

        normalized: dict[
            str,
            dict[str, int],
        ] = {}

        for direction in DIRECTIONS:
            box = melee_hitboxes.get(
                direction
            )

            if not isinstance(
                box,
                dict,
            ):
                raise ValueError(
                    f"melee hitbox {direction} is required"
                )

            for field in (
                "offsetX",
                "offsetY",
                "width",
                "height",
            ):
                if not AttackAuthoringService._is_int(
                    box.get(field)
                ):
                    raise ValueError(
                        f"melee hitbox {direction}.{field} "
                        "must be an integer"
                    )

            if box["width"] <= 0 or box["height"] <= 0:
                raise ValueError(
                    f"melee hitbox {direction} must have "
                    "positive size"
                )

            normalized[direction] = {
                field: box[field]
                for field in (
                    "offsetX",
                    "offsetY",
                    "width",
                    "height",
                )
            }

        return normalized

    @staticmethod
    def _normalized(
        data: dict[str, object],
        *,
        melee_hitboxes: object,
        projectile_definition_id: object,
        timeline: list[dict[str, object]],
        ammo: dict[str, object] | None,
        presentation_effect_id: str | None,
    ) -> dict[str, object]:
        # Preserve shapes and any field this editor does not own so a
        # roundtrip through the dialog never erases authored masks.
        preserved = {
            key: copy.deepcopy(value)
            for key, value in data.items()
            if key not in EDITABLE_FIELDS
            and key != "id"
        }

        normalized: dict[str, object] = {
            "id": data["id"],
            "kind": data["kind"],
            "damage": {
                "amount": data["damage"]["amount"],
                "knockbackPixels": data["damage"][
                    "knockbackPixels"
                ],
            },
            "totalTicks": data["totalTicks"],
            "cooldownTicks": data["cooldownTicks"],
            "minimumRangePixels": data[
                "minimumRangePixels"
            ],
            "maximumRangePixels": data[
                "maximumRangePixels"
            ],
            "visualActionId": data["visualActionId"],
            "meleeHitboxes": melee_hitboxes,
            "projectileDefinitionId": (
                projectile_definition_id
            ),
            "timeline": timeline,
        }

        if ammo is not None:
            normalized["ammo"] = ammo

        if presentation_effect_id is not None:
            normalized["presentationEffectId"] = (
                presentation_effect_id
            )

        normalized.update(
            preserved
        )

        return normalized

    @staticmethod
    def _is_int(
        value: object,
    ) -> bool:
        return isinstance(value, int) and not isinstance(
            value, bool
        )
