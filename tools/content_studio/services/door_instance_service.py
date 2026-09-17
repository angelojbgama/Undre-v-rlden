from __future__ import annotations

from dataclasses import dataclass

from ..model.content_workspace import ContentWorkspace
from ..model.map_document import MapDocument
from ..model.types import ContentDefinition


DOOR_STATES = (
    "closed",
    "locked",
    "open",
)

PERSISTENCE_POLICIES = (
    "persistent",
    "resetOnMapEnter",
)

# Player attacks are builtin runtime content (not authored through the
# Studio workspace), so they must be offered explicitly. Ids are stable
# and must match src/game/content/builtin_content.cpp.
BUILTIN_ATTACKS = (
    ("attack.player.sword", "door_open_attack_sword"),
    ("attack.player.bow", "door_open_attack_bow"),
)

BUILTIN_ATTACK_IDS = frozenset(
    attack_id
    for attack_id, _ in BUILTIN_ATTACKS
)


@dataclass(
    frozen=True,
    slots=True,
)
class AttackOption:
    definition_id: str
    display_name: str | None = None
    label_key: str | None = None


@dataclass(
    frozen=True,
    slots=True,
)
class DoorInstanceConfiguration:
    object_id: int
    definition_id: str
    uses_definition_defaults: bool
    initial_state: str
    required_item_id: str | None
    consume_item: bool
    persistence: str
    open_attack_id: str | None = None
    encounter_id: str | None = None


class DoorInstanceService:
    """Edit per-placement door behavior without creating a second door type."""

    def __init__(
        self,
        document: MapDocument,
        workspace: ContentWorkspace,
    ) -> None:
        self.document = document
        self.workspace = workspace

    def available_keys(
        self,
    ) -> tuple[ContentDefinition, ...]:
        keys = [
            definition
            for definition in self.workspace.definitions(
                "items"
            )
            if definition.data.get(
                "category"
            ) == "key"
        ]

        keys.sort(
            key=lambda value: (
                value.display_name.casefold(),
                value.definition_id,
            )
        )

        return tuple(
            keys
        )

    def configuration(
        self,
        object_id: int,
    ) -> DoorInstanceConfiguration:
        placement, definition = (
            self._door_placement(
                object_id
            )
        )

        definition_door = definition.data.get(
            "door"
        )

        assert isinstance(
            definition_door,
            dict,
        )

        authored = placement.get(
            "door"
        )

        uses_defaults = not isinstance(
            authored,
            dict,
        )

        if uses_defaults:
            initial_state = str(
                definition_door.get(
                    "initialState",
                    "closed",
                )
            )

            required_item_id = None
            consume_item = False
            open_attack_id = None
            encounter_id = None

        else:
            assert isinstance(
                authored,
                dict,
            )

            initial_state = str(
                authored.get(
                    "initialState",
                    definition_door.get(
                        "initialState",
                        "closed",
                    ),
                )
            )

            raw_required = authored.get(
                "requiredItemId"
            )

            required_item_id = (
                raw_required
                if isinstance(
                    raw_required,
                    str,
                )
                and raw_required
                else None
            )

            consume_item = bool(
                authored.get(
                    "consumeItem",
                    False,
                )
            )

            open_attack_id = (
                self._optional_id(
                    authored.get(
                        "openOnAttackId"
                    )
                )
            )

            encounter_id = (
                self._optional_id(
                    authored.get(
                        "encounterId"
                    )
                )
            )

        persistence = str(
            placement.get(
                "persistence",
                "persistent",
            )
        )

        return DoorInstanceConfiguration(
            object_id=int(
                object_id
            ),
            definition_id=definition.definition_id,
            uses_definition_defaults=uses_defaults,
            initial_state=initial_state,
            required_item_id=required_item_id,
            consume_item=consume_item,
            persistence=persistence,
            open_attack_id=open_attack_id,
            encounter_id=encounter_id,
        )

    @staticmethod
    def _optional_id(
        value: object,
    ) -> str | None:
        return (
            value
            if isinstance(
                value,
                str,
            )
            and value
            else None
        )

    def _known_attack(
        self,
        attack_id: str,
    ) -> bool:
        if attack_id in BUILTIN_ATTACK_IDS:
            return True

        return self.workspace.find(
            "attacks",
            attack_id,
        ) is not None

    def available_attacks(
        self,
    ) -> tuple[AttackOption, ...]:
        entries: list[AttackOption] = []

        offered: set[str] = set()

        for attack_id, label_key in BUILTIN_ATTACKS:
            entries.append(
                AttackOption(
                    definition_id=attack_id,
                    label_key=label_key,
                )
            )

            offered.add(
                attack_id
            )

        authored = [
            definition
            for definition in self.workspace.definitions(
                "attacks"
            )
            if definition.definition_id
            not in offered
        ]

        authored.sort(
            key=lambda value: (
                value.display_name.casefold(),
                value.definition_id,
            )
        )

        entries.extend(
            AttackOption(
                definition_id=definition.definition_id,
                display_name=definition.display_name,
            )
            for definition in authored
        )

        return tuple(
            entries
        )

    def available_encounters(
        self,
    ) -> tuple[str, ...]:
        values = self.document.data.get(
            "encounters",
            [],
        )

        if not isinstance(
            values,
            list,
        ):
            return ()

        return tuple(
            str(value["id"])
            for value in values
            if (
                isinstance(value, dict)
                and isinstance(value.get("id"), str)
                and value["id"]
            )
        )

    def configure(
        self,
        object_id: int,
        *,
        uses_definition_defaults: bool,
        initial_state: str,
        required_item_id: str | None,
        consume_item: bool,
        persistence: str,
        open_attack_id: str | None = None,
        encounter_id: str | None = None,
    ) -> DoorInstanceConfiguration:
        placement, unused_definition = (
            self._door_placement(
                object_id
            )
        )

        del placement
        del unused_definition

        if persistence not in PERSISTENCE_POLICIES:
            raise ValueError(
                "invalid object persistence policy"
            )

        if uses_definition_defaults:
            self.document.set_object_door_configuration(
                object_id,
                None,
                persistence,
            )

            return self.configuration(
                object_id
            )

        if initial_state not in DOOR_STATES:
            raise ValueError(
                f"invalid door initial state: "
                f"{initial_state}"
            )

        normalized_required = (
            required_item_id.strip()
            if isinstance(
                required_item_id,
                str,
            )
            else ""
        )

        if (
            normalized_required
            and initial_state != "locked"
        ):
            raise ValueError(
                "required key is only meaningful "
                "for a locked door"
            )

        if consume_item and not normalized_required:
            raise ValueError(
                "consumeItem requires a required key"
            )

        if normalized_required:
            item = self.workspace.find(
                "items",
                normalized_required,
            )

            if item is None:
                raise ValueError(
                    f"unknown required key item: "
                    f"{normalized_required}"
                )

            if item.data.get(
                "category"
            ) != "key":
                raise ValueError(
                    "required door item must have "
                    "category key"
                )

        normalized_attack = (
            open_attack_id.strip()
            if isinstance(
                open_attack_id,
                str,
            )
            else ""
        )

        if normalized_attack and not self._known_attack(
            normalized_attack,
        ):
            raise ValueError(
                f"unknown door open attack: "
                f"{normalized_attack}"
            )

        normalized_encounter = (
            encounter_id.strip()
            if isinstance(
                encounter_id,
                str,
            )
            else ""
        )

        if (
            normalized_encounter
            and normalized_encounter
            not in self.available_encounters()
        ):
            raise ValueError(
                f"unknown door open encounter: "
                f"{normalized_encounter}"
            )

        door: dict[str, object] = {
            "initialState": initial_state,
            "consumeItem": bool(
                consume_item
            ),
        }

        if normalized_required:
            door["requiredItemId"] = (
                normalized_required
            )

        if normalized_attack:
            door["openOnAttackId"] = (
                normalized_attack
            )

        if normalized_encounter:
            door["encounterId"] = (
                normalized_encounter
            )

        self.document.set_object_door_configuration(
            object_id,
            door,
            persistence,
        )

        return self.configuration(
            object_id
        )

    def _door_placement(
        self,
        object_id: int,
    ) -> tuple[
        dict[str, object],
        ContentDefinition,
    ]:
        placement = self.document.entity(
            "objects",
            int(
                object_id
            ),
        )

        if placement is None:
            raise ValueError(
                "object placement was not found"
            )

        definition_id = placement.get(
            "definitionId"
        )

        if not isinstance(
            definition_id,
            str,
        ):
            raise ValueError(
                "object placement has no definition"
            )

        definition = self.workspace.find(
            "objects",
            definition_id,
        )

        if definition is None:
            raise ValueError(
                f"object definition not found: "
                f"{definition_id}"
            )

        if not isinstance(
            definition.data.get(
                "door"
            ),
            dict,
        ):
            raise ValueError(
                "selected object does not have "
                "door capability"
            )

        return (
            placement,
            definition,
        )
