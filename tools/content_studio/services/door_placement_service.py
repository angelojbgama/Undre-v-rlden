from __future__ import annotations

from dataclasses import dataclass

from ..model.content_workspace import ContentWorkspace
from ..model.fixture_cutout import (
    FixtureCutout,
    FixtureCutoutCell,
)
from ..model.map_document import MapDocument
from .door_authoring_service import DoorAuthoringService
from .fixture_reservation_service import (
    FixtureTerrainReservationService,
)
from .tile_semantic_catalog import TileSemanticCatalog


@dataclass(
    frozen=True,
    slots=True,
)
class DoorPlacementPlan:
    definition_id: str
    orientation: str
    layer_index: int
    cells: tuple[
        tuple[int, int],
        ...,
    ]
    position: tuple[int, int]


@dataclass(
    frozen=True,
    slots=True,
)
class DoorPlacementPreview:
    definition_id: str
    orientation: str
    cells: tuple[
        tuple[int, int],
        ...,
    ]
    position: tuple[int, int]
    valid: bool
    layer_index: int | None = None
    error: str = ""


@dataclass(
    frozen=True,
    slots=True,
)
class DoorPlacementResult:
    object_id: int
    plan: DoorPlacementPlan


class DoorPlacementService:
    """Wall-aware placement for authored WorldObject doors.

    The door remains an ordinary ObjectPlacement.  This service only owns
    Studio authoring behavior: semantic wall detection, placement anchor
    calculation and the atomic wall-opening operation.
    """

    def __init__(
        self,
        document: MapDocument,
        workspace: ContentWorkspace,
    ) -> None:
        self.document = document
        self.workspace = workspace
        self.doors = DoorAuthoringService(
            workspace
        )
        self.semantics = TileSemanticCatalog(
            workspace
        )

    def preview(
        self,
        definition_id: str,
        tile: tuple[int, int],
        preferred_layer_index: int | None = None,
        orientation: str | None = None,
    ) -> DoorPlacementPreview:
        entry = self.doors.entry(
            definition_id,
            self.document.tile_size,
        )

        if entry is None:
            raise ValueError(
                f"not a door definition: {definition_id}"
            )

        if (
            entry.placement.mode
            != "wall"
        ):
            raise ValueError(
                "door does not use wall placement"
            )

        selected_orientation = (
            orientation
            or entry.placement.orientations[0]
        )

        if (
            selected_orientation
            not in entry.placement.orientations
        ):
            raise ValueError(
                f"unsupported door orientation: "
                f"{selected_orientation}"
            )

        if selected_orientation != "horizontal":
            raise ValueError(
                "vertical door placement requires "
                "directional door visuals"
            )

        span = (
            entry.placement.span_tiles
        )

        thickness = (
            entry.placement.thickness_tiles
        )

        tile_x = int(
            tile[0]
        )

        tile_y = int(
            tile[1]
        )

        left = (
            tile_x
            - span // 2
        )

        bottom = (
            tile_y
            + 1
        )

        top = (
            bottom
            - thickness
        )

        right = (
            left
            + span
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
        )

        tile_size = (
            self.document.tile_size
        )

        position = (
            left * tile_size
            + (
                span * tile_size
            ) // 2,
            bottom * tile_size,
        )

        if (
            left < 0
            or top < 0
            or right > self.document.width
            or bottom > self.document.height
        ):
            return DoorPlacementPreview(
                definition_id=definition_id,
                orientation=selected_orientation,
                cells=cells,
                position=position,
                valid=False,
                error="door footprint is outside the map",
            )

        try:
            layer_index = (
                self._find_wall_layer(
                    cells,
                    preferred_layer_index,
                )
            )
        except (
            IndexError,
            ValueError,
        ) as error:
            return DoorPlacementPreview(
                definition_id=definition_id,
                orientation=selected_orientation,
                cells=cells,
                position=position,
                valid=False,
                error=str(error),
            )

        return DoorPlacementPreview(
            definition_id=definition_id,
            orientation=selected_orientation,
            cells=cells,
            position=position,
            valid=True,
            layer_index=layer_index,
        )

    def plan(
        self,
        definition_id: str,
        tile: tuple[int, int],
        preferred_layer_index: int | None = None,
        orientation: str | None = None,
    ) -> DoorPlacementPlan:
        preview = self.preview(
            definition_id,
            tile,
            preferred_layer_index,
            orientation,
        )

        if not preview.valid:
            raise ValueError(
                preview.error
            )

        if preview.layer_index is None:
            raise ValueError(
                "door preview has no wall layer"
            )

        return DoorPlacementPlan(
            definition_id=preview.definition_id,
            orientation=preview.orientation,
            layer_index=preview.layer_index,
            cells=preview.cells,
            position=preview.position,
        )

    def place(
        self,
        definition_id: str,
        tile: tuple[int, int],
        preferred_layer_index: int | None = None,
        orientation: str | None = None,
    ) -> DoorPlacementResult:
        plan = self.plan(
            definition_id,
            tile,
            preferred_layer_index,
            orientation,
        )

        object_id = (
            self.document
            .place_object_with_tile_opening(
                plan.definition_id,
                plan.position[0],
                plan.position[1],
                plan.layer_index,
                plan.cells,
                label="Place Wall Door",
            )
        )

        return DoorPlacementResult(
            object_id=object_id,
            plan=plan,
        )

    def is_door_instance(
        self,
        persistent_id: int,
    ) -> bool:
        placement = self.document.entity(
            "objects",
            persistent_id,
        )

        if placement is None:
            return False

        definition_id = placement.get(
            "definitionId"
        )

        if not isinstance(
            definition_id,
            str,
        ):
            return False

        definition = self.workspace.find(
            "objects",
            definition_id,
        )

        return bool(
            definition is not None
            and isinstance(
                definition.data.get(
                    "door"
                ),
                dict,
            )
        )

    def delete(
        self,
        persistent_id: int,
    ) -> None:
        if not self.is_door_instance(
            persistent_id
        ):
            raise ValueError(
                "object is not a door fixture"
            )

        fallback = (
            self._legacy_cutout(
                persistent_id
            )
        )

        self.document.delete_object_with_tile_restoration(
            persistent_id,
            fallback_cutout=fallback,
            label="Delete Wall Door",
        )

    def move(
        self,
        persistent_id: int,
        tile: tuple[int, int],
        preferred_layer_index: int | None = None,
        orientation: str | None = None,
    ) -> DoorPlacementResult:

        placement = self.document.entity(
            "objects",
            persistent_id,
        )

        if (
            placement is None
            or not self.is_door_instance(
                persistent_id
            )
        ):
            raise ValueError(
                "object is not a door fixture"
            )

        definition_id = placement.get(
            "definitionId"
        )

        if not isinstance(
            definition_id,
            str,
        ):
            raise ValueError(
                "door placement has no definition"
            )

        fallback = (
            self._legacy_cutout(
                persistent_id
            )
        )

        # Validate against a temporary map where the old fixture has
        # already restored its cutout. This makes overlapping moves valid.
        working = MapDocument(
            self.document.snapshot()
        )

        working.delete_object_with_tile_restoration(
            persistent_id,
            fallback_cutout=fallback,
            label="Preview Door Move",
        )

        plan = DoorPlacementService(
            working,
            self.workspace,
        ).plan(
            definition_id,
            tile,
            preferred_layer_index,
            orientation,
        )

        self.document.move_object_with_tile_opening(
            persistent_id,
            plan.position[0],
            plan.position[1],
            plan.layer_index,
            plan.cells,
            fallback_cutout=fallback,
            terrain_role="wall",
            label="Move Wall Door",
        )

        return DoorPlacementResult(
            object_id=persistent_id,
            plan=plan,
        )

    def _legacy_cutout(
        self,
        persistent_id: int,
    ) -> FixtureCutout | None:
        if (
            self.document.fixture_cutout(
                persistent_id
            )
            is not None
        ):
            return None

        reservations = (
            FixtureTerrainReservationService(
                self.document,
                self.workspace,
            ).reservations(
                "wall"
            )
        )

        reservation = next(
            (
                value
                for value
                in reservations
                if value.owner_id
                == persistent_id
            ),
            None,
        )

        if reservation is None:
            return None

        reserved = set(
            reservation.cells
        )

        neighbors: set[
            tuple[int, int]
        ] = set()

        for x, y in reserved:
            for nx, ny in (
                (x - 1, y),
                (x + 1, y),
                (x, y - 1),
                (x, y + 1),
            ):
                if (
                    (nx, ny)
                    not in reserved
                    and 0 <= nx
                    < self.document.width
                    and 0 <= ny
                    < self.document.height
                ):
                    neighbors.add(
                        (nx, ny)
                    )

        references = self.document.data.get(
            "tileReferences",
            [],
        )

        if not isinstance(
            references,
            list,
        ):
            return None

        candidates: dict[
            tuple[int, int],
            int,
        ] = {}

        for layer_index, layer in enumerate(
            self.document.layers
        ):
            cells = layer.get(
                "cells",
                [],
            )

            if not isinstance(
                cells,
                list,
            ):
                continue

            for nx, ny in neighbors:
                index = (
                    ny
                    * self.document.width
                    + nx
                )

                if (
                    index >= len(cells)
                    or not isinstance(
                        cells[index],
                        int,
                    )
                ):
                    continue

                reference_index = int(
                    cells[index]
                )

                if (
                    reference_index < 0
                    or reference_index
                    >= len(references)
                    or not isinstance(
                        references[
                            reference_index
                        ],
                        dict,
                    )
                ):
                    continue

                reference = references[
                    reference_index
                ]

                semantics = (
                    self.semantics.by_reference(
                        str(
                            reference.get(
                                "tilesetId",
                                "",
                            )
                        ),
                        int(
                            reference.get(
                                "sourceIndex",
                                0,
                            )
                        ),
                    )
                )

                if not any(
                    semantic.role
                    in {
                        "wall",
                        "corner",
                    }
                    for semantic
                    in semantics
                ):
                    continue

                key = (
                    layer_index,
                    reference_index,
                )

                candidates[
                    key
                ] = (
                    candidates.get(
                        key,
                        0,
                    )
                    + 1
                )

        if not candidates:
            return None

        layer_index, reference_index = min(
            candidates,
            key=lambda key: (
                -candidates[key],
                key[0],
                key[1],
            ),
        )

        bindings = self.document.data.get(
            "collisionBindings",
            [],
        )

        solid = False

        if isinstance(
            bindings,
            list,
        ):
            solid = any(
                isinstance(
                    binding,
                    dict,
                )
                and int(
                    binding.get(
                        "layer",
                        -1,
                    )
                )
                == layer_index
                and (
                    int(
                        binding.get(
                            "x",
                            -1,
                        )
                    ),
                    int(
                        binding.get(
                            "y",
                            -1,
                        )
                    ),
                )
                in neighbors
                for binding
                in bindings
            )

        return FixtureCutout(
            owner_id=persistent_id,
            layer_index=layer_index,
            terrain_role="wall",
            cells=tuple(
                FixtureCutoutCell(
                    x=x,
                    y=y,
                    tile_reference=reference_index,
                    solid=solid,
                )
                for x, y
                in reservation.cells
            ),
        )

    def _find_wall_layer(
        self,
        cells: tuple[
            tuple[int, int],
            ...,
        ],
        preferred_layer_index: int | None,
    ) -> int:
        if (
            preferred_layer_index
            is not None
            and (
                preferred_layer_index < 0
                or preferred_layer_index
                >= len(
                    self.document.layers
                )
            )
        ):
            raise IndexError(
                "preferred layer index out of range"
            )

        candidates: list[int] = []

        if preferred_layer_index is not None:
            candidates.append(
                preferred_layer_index
            )

        candidates.extend(
            index
            for index in range(
                len(
                    self.document.layers
                )
            )
            if index
            not in candidates
        )

        for layer_index in candidates:
            if all(
                self._is_semantic_wall(
                    layer_index,
                    cell_x,
                    cell_y,
                )
                for cell_x, cell_y
                in cells
            ):
                return layer_index

        raise ValueError(
            "door footprint must cover semantic wall tiles"
        )

    def _is_semantic_wall(
        self,
        layer_index: int,
        x: int,
        y: int,
    ) -> bool:
        layer = self.document.layers[
            layer_index
        ]

        cells = layer.get(
            "cells",
            [],
        )

        if not isinstance(
            cells,
            list,
        ):
            return False

        index = (
            y
            * self.document.width
            + x
        )

        if (
            index < 0
            or index >= len(cells)
        ):
            return False

        reference_index = (
            cells[index]
        )

        if (
            isinstance(
                reference_index,
                bool,
            )
            or not isinstance(
                reference_index,
                int,
            )
        ):
            return False

        references = self.document.data.get(
            "tileReferences",
            [],
        )

        if (
            not isinstance(
                references,
                list,
            )
            or reference_index < 0
            or reference_index
            >= len(references)
        ):
            return False

        reference = references[
            reference_index
        ]

        if not isinstance(
            reference,
            dict,
        ):
            return False

        tileset_id = reference.get(
            "tilesetId"
        )

        source_index = reference.get(
            "sourceIndex"
        )

        if (
            not isinstance(
                tileset_id,
                str,
            )
            or not isinstance(
                source_index,
                int,
            )
            or isinstance(
                source_index,
                bool,
            )
        ):
            return False

        semantics = (
            self.semantics.by_reference(
                tileset_id,
                source_index,
            )
        )

        return any(
            semantic.role == "wall"
            for semantic in semantics
        )
