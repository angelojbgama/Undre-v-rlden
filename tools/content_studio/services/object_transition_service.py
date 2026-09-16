from __future__ import annotations

from dataclasses import dataclass

from ..model.map_document import MapDocument
from ..model.world_project import WorldProject


@dataclass(
    frozen=True,
    slots=True,
)
class ObjectTransitionConfiguration:
    object_id: int
    enabled: bool
    target_map_id: str | None
    target_spawn_id: str | None


class ObjectTransitionService:
    # Author generic per-object map transitions without Door coupling.

    def __init__(
        self,
        project: WorldProject,
        document: MapDocument,
    ) -> None:
        self.project = project
        self.document = document

    def configuration(
        self,
        object_id: int,
    ) -> ObjectTransitionConfiguration:
        placement = self._placement(
            object_id
        )

        raw = placement.get(
            "transition"
        )

        if raw is None:
            return ObjectTransitionConfiguration(
                object_id=int(object_id),
                enabled=False,
                target_map_id=None,
                target_spawn_id=None,
            )

        if not isinstance(raw, dict):
            raise ValueError(
                "object transition configuration is invalid"
            )

        target_map_id = raw.get(
            "targetMapId"
        )
        target_spawn_id = raw.get(
            "targetSpawnId"
        )

        if (
            not isinstance(target_map_id, str)
            or not target_map_id
            or not isinstance(target_spawn_id, str)
            or not target_spawn_id
        ):
            raise ValueError(
                "object transition target is invalid"
            )

        return ObjectTransitionConfiguration(
            object_id=int(object_id),
            enabled=True,
            target_map_id=target_map_id,
            target_spawn_id=target_spawn_id,
        )

    def available_maps(
        self,
    ) -> tuple[str, ...]:
        return tuple(
            document.map_id
            for document in self.project.maps
        )

    def available_spawns(
        self,
        map_id: str,
    ) -> tuple[str, ...]:
        document = self.project.map_by_id(
            map_id
        )

        if document is None:
            raise ValueError(
                f"unknown transition target map: {map_id}"
            )

        values = document.data.get(
            "playerSpawns",
            [],
        )

        if not isinstance(values, list):
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
        enabled: bool,
        target_map_id: str | None = None,
        target_spawn_id: str | None = None,
    ) -> ObjectTransitionConfiguration:
        self._placement(
            object_id
        )

        if not enabled:
            self.document.set_object_transition_configuration(
                object_id,
                None,
            )

            return self.configuration(
                object_id
            )

        normalized_map = (
            target_map_id.strip()
            if isinstance(target_map_id, str)
            else ""
        )

        normalized_spawn = (
            target_spawn_id.strip()
            if isinstance(target_spawn_id, str)
            else ""
        )

        target = self.project.map_by_id(
            normalized_map
        )

        if target is None:
            raise ValueError(
                f"unknown transition target map: {normalized_map}"
            )

        if normalized_spawn not in self.available_spawns(
            normalized_map
        ):
            raise ValueError(
                f"unknown transition target spawn: {normalized_spawn}"
            )

        self.document.set_object_transition_configuration(
            object_id,
            {
                "targetMapId": normalized_map,
                "targetSpawnId": normalized_spawn,
            },
        )

        return self.configuration(
            object_id
        )

    def _placement(
        self,
        object_id: int,
    ) -> dict[str, object]:
        placement = self.document.entity(
            "objects",
            int(object_id),
        )

        if placement is None:
            raise ValueError(
                "object placement was not found"
            )

        return placement
