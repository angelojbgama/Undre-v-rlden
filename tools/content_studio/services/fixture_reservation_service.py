from __future__ import annotations

from dataclasses import dataclass

from ..model.content_workspace import (
    ContentWorkspace,
)
from ..model.map_document import (
    MapDocument,
)
from .door_authoring_service import (
    DoorAuthoringService,
)


@dataclass(
    frozen=True,
    slots=True,
)
class FixtureTerrainReservation:
    """Terrain cells owned by an authored fixture."""

    owner_id: int
    definition_id: str
    terrain_role: str
    cells: tuple[
        tuple[int, int],
        ...,
    ]


class FixtureTerrainReservationService:
    """Derive terrain reservations from existing map fixtures.

    Reservations are intentionally not serialized into UMAP.
    A fixture remains the single source of truth and the reservation
    is reconstructed from its placement plus authored definition.

    Doors are the first fixture capability using this service, but
    terrain painting only depends on the generic reservation result.
    """

    def __init__(
        self,
        document: MapDocument | None = None,
        workspace: ContentWorkspace | None = None,
    ) -> None:
        self.document = document
        self.workspace = workspace

    def set_context(
        self,
        document: MapDocument | None,
        workspace: ContentWorkspace | None,
    ) -> None:
        self.document = document
        self.workspace = workspace

    def reservations(
        self,
        terrain_role: str | None = None,
    ) -> tuple[
        FixtureTerrainReservation,
        ...,
    ]:
        document = self.document
        workspace = self.workspace

        if (
            document is None
            or workspace is None
        ):
            return ()

        requested_role = (
            self._normalize_role(
                terrain_role
            )
            if terrain_role is not None
            else None
        )

        objects = document.data.get(
            "objects",
            [],
        )

        if not isinstance(
            objects,
            list,
        ):
            return ()

        doors = DoorAuthoringService(
            workspace
        )

        result: list[
            FixtureTerrainReservation
        ] = []

        for placement in objects:
            if not isinstance(
                placement,
                dict,
            ):
                continue

            definition_id = placement.get(
                "definitionId"
            )

            owner_id = placement.get(
                "id"
            )

            position = placement.get(
                "position"
            )

            if (
                not isinstance(
                    definition_id,
                    str,
                )
                or not isinstance(
                    owner_id,
                    int,
                )
                or isinstance(
                    owner_id,
                    bool,
                )
                or not isinstance(
                    position,
                    dict,
                )
            ):
                continue

            definition = workspace.find(
                "objects",
                definition_id,
            )

            if (
                definition is None
                or not isinstance(
                    definition.data.get(
                        "door"
                    ),
                    dict,
                )
            ):
                continue

            entry = doors.entry(
                definition_id,
                document.tile_size,
            )

            if entry is None:
                continue

            if (
                entry.placement.mode
                != "wall"
            ):
                continue

            role = "wall"

            if (
                requested_role is not None
                and requested_role != role
            ):
                continue

            cells = self._door_cells(
                position,
                entry.placement.span_tiles,
                entry.placement.thickness_tiles,
            )

            if not cells:
                continue

            result.append(
                FixtureTerrainReservation(
                    owner_id=owner_id,
                    definition_id=definition_id,
                    terrain_role=role,
                    cells=cells,
                )
            )

        result.sort(
            key=lambda value: (
                value.owner_id,
                value.definition_id,
            )
        )

        return tuple(
            result
        )

    def reserved_cells(
        self,
        terrain_role: str,
    ) -> set[
        tuple[int, int]
    ]:
        result: set[
            tuple[int, int]
        ] = set()

        for reservation in self.reservations(
            terrain_role
        ):
            result.update(
                reservation.cells
            )

        return result

    def _door_cells(
        self,
        position: dict[str, object],
        span_tiles: int,
        thickness_tiles: int,
    ) -> tuple[
        tuple[int, int],
        ...,
    ]:
        document = self.document

        if document is None:
            return ()

        raw_x = position.get(
            "x"
        )

        raw_y = position.get(
            "y"
        )

        if (
            not isinstance(
                raw_x,
                int,
            )
            or isinstance(
                raw_x,
                bool,
            )
            or not isinstance(
                raw_y,
                int,
            )
            or isinstance(
                raw_y,
                bool,
            )
        ):
            return ()

        tile_size = (
            document.tile_size
        )

        span_pixels = (
            span_tiles
            * tile_size
        )

        # DoorPlacementService authors horizontal wall fixtures
        # at their bottom-center anchor. Reconstruct that footprint
        # from the placement itself so save/reload needs no extra
        # reservation metadata.
        left_pixel = (
            raw_x
            - span_pixels // 2
        )

        left = (
            left_pixel
            + tile_size // 2
        ) // tile_size

        bottom = (
            raw_y
            + tile_size // 2
        ) // tile_size

        top = (
            bottom
            - thickness_tiles
        )

        right = (
            left
            + span_tiles
        )

        cells = tuple(
            (
                x,
                y,
            )
            for y in range(
                top,
                bottom,
            )
            for x in range(
                left,
                right,
            )
            if (
                0 <= x < document.width
                and 0 <= y < document.height
            )
        )

        return cells

    @staticmethod
    def _normalize_role(
        role: str,
    ) -> str:
        normalized = role.strip()

        if normalized == "corner":
            return "wall"

        return normalized
