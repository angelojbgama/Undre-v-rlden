from __future__ import annotations

from dataclasses import dataclass
from math import ceil
from typing import Iterable

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition


DEFAULT_MODE = "wall"
DEFAULT_ANCHOR = "bottom-center"
DEFAULT_ORIENTATIONS = ("horizontal",)

DEFAULT_FAMILY_SORT_ORDER = 1000

SUPPORTED_MODES = {
    "wall",
}

SUPPORTED_ANCHORS = {
    "bottom-center",
}

SUPPORTED_ORIENTATIONS = {
    "horizontal",
    "vertical",
}


@dataclass(
    frozen=True,
    slots=True,
)
class DoorPlacementProfile:
    """Authoring-only rules for snapping a door into map geometry."""

    mode: str
    span_tiles: int
    thickness_tiles: int
    anchor: str
    orientations: tuple[str, ...]


@dataclass(
    frozen=True,
    slots=True,
)
class DoorFamilyMetadata:
    """Authoring-only grouping for doors derived from descriptor tags.

    Families never reach the runtime: they exist to organize, filter and
    search the specialized Studio door library.
    """

    family_id: str
    display_name: str
    description: str
    thumbnail: str | None
    sort_order: int


@dataclass(
    frozen=True,
    slots=True,
)
class _RawDoorFamily:
    """Family fields explicitly provided by one door's tags."""

    definition_id: str
    family_id: str
    display_name: str | None
    description: str | None
    thumbnail: str | None
    sort_order: int | None


@dataclass(
    frozen=True,
    slots=True,
)
class DoorCatalogEntry:
    """Door definition prepared for the specialized Studio library."""

    definition_id: str
    display_name: str
    visual_set_id: str
    source_animation_id: str
    frame_width: int
    frame_height: int
    placement: DoorPlacementProfile
    family: DoorFamilyMetadata | None = None


class DoorAuthoringService:
    """Resolve reusable WorldObject doors into Studio placement metadata.

    Doors remain ordinary authored WorldObjects with the `door`
    capability.  This service adds authoring semantics only; it does
    not create a parallel runtime DoorDefinition.
    """

    def __init__(
        self,
        workspace: ContentWorkspace,
    ) -> None:
        self.workspace = workspace

    def entries(
        self,
        tile_size: int,
    ) -> list[DoorCatalogEntry]:
        self._require_tile_size(
            tile_size
        )

        prepared: list[
            tuple[
                ContentDefinition,
                _RawDoorFamily | None,
            ]
        ] = []

        for definition in self.workspace.definitions(
            "objects"
        ):
            if not isinstance(
                definition.data.get("door"),
                dict,
            ):
                continue

            descriptor = self.workspace.find(
                "authoringDescriptors",
                definition.definition_id,
            )

            prepared.append(
                (
                    definition,
                    self._parse_family(
                        definition.definition_id,
                        self._tags(descriptor),
                    ),
                )
            )

        families = self._resolve_families(
            raw
            for _, raw in prepared
            if raw is not None
        )

        result = [
            self._build_entry(
                definition,
                tile_size,
                family=(
                    families[raw.family_id]
                    if raw is not None
                    else None
                ),
            )
            for definition, raw in prepared
        ]

        result.sort(
            key=self._entry_sort_key
        )

        return result

    def families(
        self,
        tile_size: int,
    ) -> list[DoorFamilyMetadata]:
        resolved: dict[
            str,
            DoorFamilyMetadata,
        ] = {}

        for entry in self.entries(
            tile_size
        ):
            if entry.family is not None:
                resolved.setdefault(
                    entry.family.family_id,
                    entry.family,
                )

        return sorted(
            resolved.values(),
            key=lambda family: (
                family.sort_order,
                family.display_name.casefold(),
                family.family_id,
            ),
        )

    def entry(
        self,
        definition_id: str,
        tile_size: int,
    ) -> DoorCatalogEntry | None:
        self._require_tile_size(
            tile_size
        )

        definition = self.workspace.find(
            "objects",
            definition_id,
        )

        if definition is None:
            return None

        if not isinstance(
            definition.data.get("door"),
            dict,
        ):
            return None

        descriptor = self.workspace.find(
            "authoringDescriptors",
            definition.definition_id,
        )

        raw_family = self._parse_family(
            definition.definition_id,
            self._tags(descriptor),
        )

        return self._build_entry(
            definition,
            tile_size,
            family=(
                self._resolve_families(
                    [raw_family]
                )[raw_family.family_id]
                if raw_family is not None
                else None
            ),
        )

    @staticmethod
    def _require_tile_size(
        tile_size: int,
    ) -> None:
        if (
            isinstance(tile_size, bool)
            or not isinstance(tile_size, int)
            or tile_size <= 0
        ):
            raise ValueError(
                "tile_size must be a positive integer"
            )

    def _build_entry(
        self,
        definition: ContentDefinition,
        tile_size: int,
        family: DoorFamilyMetadata | None = None,
    ) -> DoorCatalogEntry:
        descriptor = self.workspace.find(
            "authoringDescriptors",
            definition.definition_id,
        )

        tags = self._tags(
            descriptor
        )

        visual_set_id = definition.data.get(
            "visualSetId"
        )

        if (
            not isinstance(visual_set_id, str)
            or not visual_set_id
        ):
            raise ValueError(
                f"{definition.definition_id}: "
                "door visualSetId is required"
            )

        source_animation_id = (
            self._tag(
                tags,
                "object-source-animation:",
            )
            or self._visual_animation(
                visual_set_id
            )
        )

        animation = self.workspace.find(
            "animations",
            source_animation_id,
        )

        if animation is None:
            raise ValueError(
                f"{definition.definition_id}: "
                f"unknown door animation: "
                f"{source_animation_id!r}"
            )

        frame_width, frame_height = (
            self._frame_size(
                definition.definition_id,
                animation,
            )
        )

        inferred_span = max(
            1,
            ceil(
                frame_width
                / tile_size
            ),
        )

        span_tiles = self._positive_int_tag(
            tags,
            "door-span-tiles:",
            inferred_span,
        )

        thickness_tiles = self._positive_int_tag(
            tags,
            "door-thickness-tiles:",
            1,
        )

        mode = (
            self._tag(
                tags,
                "door-placement:",
            )
            or DEFAULT_MODE
        )

        if mode not in SUPPORTED_MODES:
            raise ValueError(
                f"{definition.definition_id}: "
                f"unsupported door-placement: {mode}"
            )

        anchor = (
            self._tag(
                tags,
                "door-anchor:",
            )
            or DEFAULT_ANCHOR
        )

        if anchor not in SUPPORTED_ANCHORS:
            raise ValueError(
                f"{definition.definition_id}: "
                f"unsupported door-anchor: {anchor}"
            )

        orientations = self._orientations(
            definition.definition_id,
            tags,
        )

        return DoorCatalogEntry(
            definition_id=definition.definition_id,
            display_name=definition.display_name,
            visual_set_id=visual_set_id,
            source_animation_id=source_animation_id,
            frame_width=frame_width,
            frame_height=frame_height,
            placement=DoorPlacementProfile(
                mode=mode,
                span_tiles=span_tiles,
                thickness_tiles=thickness_tiles,
                anchor=anchor,
                orientations=orientations,
            ),
            family=family,
        )

    def _visual_animation(
        self,
        visual_set_id: str,
    ) -> str:
        visual = self.workspace.find(
            "objectVisuals",
            visual_set_id,
        )

        if visual is None:
            raise ValueError(
                f"unknown door objectVisual: "
                f"{visual_set_id!r}"
            )

        for field in (
            "doorClosedAnimationId",
            "idleAnimationId",
        ):
            animation_id = visual.data.get(
                field
            )

            if (
                isinstance(animation_id, str)
                and animation_id
            ):
                return animation_id

        raise ValueError(
            f"{visual_set_id}: "
            "door visual has no closed/idle animation"
        )

    @staticmethod
    def _frame_size(
        definition_id: str,
        animation: ContentDefinition,
    ) -> tuple[int, int]:
        frames = animation.data.get(
            "frames"
        )

        if (
            not isinstance(frames, list)
            or not frames
            or not isinstance(frames[0], dict)
        ):
            raise ValueError(
                f"{definition_id}: "
                "door animation has no frame"
            )

        source = frames[0].get(
            "source"
        )

        if not isinstance(
            source,
            dict,
        ):
            raise ValueError(
                f"{definition_id}: "
                "door animation frame has no source"
            )

        width = source.get(
            "width"
        )

        height = source.get(
            "height"
        )

        if (
            isinstance(width, bool)
            or isinstance(height, bool)
            or not isinstance(width, int)
            or not isinstance(height, int)
            or width <= 0
            or height <= 0
        ):
            raise ValueError(
                f"{definition_id}: "
                "door animation frame size must be positive"
            )

        return (
            width,
            height,
        )

    @staticmethod
    def _tags(
        descriptor: ContentDefinition | None,
    ) -> tuple[str, ...]:
        if descriptor is None:
            return ()

        value = descriptor.data.get(
            "tags",
            [],
        )

        if not isinstance(
            value,
            list,
        ):
            return ()

        return tuple(
            tag
            for tag in value
            if isinstance(tag, str)
        )

    @staticmethod
    def _tag(
        tags: tuple[str, ...],
        prefix: str,
    ) -> str | None:
        matches = [
            tag[len(prefix):].strip()
            for tag in tags
            if tag.startswith(prefix)
        ]

        if not matches:
            return None

        if len(matches) > 1:
            raise ValueError(
                f"duplicate authoring tag: {prefix}"
            )

        return matches[0]

    def _positive_int_tag(
        self,
        tags: tuple[str, ...],
        prefix: str,
        default: int,
    ) -> int:
        raw = self._tag(
            tags,
            prefix,
        )

        if raw is None:
            return default

        try:
            value = int(
                raw
            )
        except ValueError as error:
            raise ValueError(
                f"{prefix[:-1]} must be a positive integer"
            ) from error

        if value <= 0:
            raise ValueError(
                f"{prefix[:-1]} must be a positive integer"
            )

        return value

    def _orientations(
        self,
        definition_id: str,
        tags: tuple[str, ...],
    ) -> tuple[str, ...]:
        raw = self._tag(
            tags,
            "door-orientations:",
        )

        if raw is None:
            return DEFAULT_ORIENTATIONS

        orientations = tuple(
            value.strip()
            for value in raw.split(",")
            if value.strip()
        )

        if not orientations:
            raise ValueError(
                f"{definition_id}: "
                "door-orientations cannot be empty"
            )

        invalid = [
            value
            for value in orientations
            if value not in SUPPORTED_ORIENTATIONS
        ]

        if invalid:
            raise ValueError(
                f"{definition_id}: "
                f"unsupported door orientation: "
                f"{invalid[0]}"
            )

        return tuple(
            dict.fromkeys(
                orientations
            )
        )

    def _parse_family(
        self,
        definition_id: str,
        tags: tuple[str, ...],
    ) -> _RawDoorFamily | None:
        family_id = self._tag(
            tags,
            "door-family:",
        )

        if family_id is None:
            return None

        if not family_id:
            raise ValueError(
                f"{definition_id}: "
                "door-family tag requires a value"
            )

        return _RawDoorFamily(
            definition_id=definition_id,
            family_id=family_id,
            display_name=self._tag(
                tags,
                "door-family-name:",
            ),
            description=self._tag(
                tags,
                "door-family-description:",
            ),
            thumbnail=self._tag(
                tags,
                "door-family-thumbnail:",
            ),
            sort_order=self._family_sort_order(
                definition_id,
                tags,
            ),
        )

    def _family_sort_order(
        self,
        definition_id: str,
        tags: tuple[str, ...],
    ) -> int | None:
        raw = self._tag(
            tags,
            "door-family-sort-order:",
        )

        if raw is None:
            return None

        try:
            value = int(
                raw
            )
        except ValueError as error:
            raise ValueError(
                f"{definition_id}: "
                "door-family-sort-order must be "
                "a non-negative integer"
            ) from error

        if value < 0:
            raise ValueError(
                f"{definition_id}: "
                "door-family-sort-order must be "
                "a non-negative integer"
            )

        return value

    @staticmethod
    def _resolve_families(
        raws: Iterable[_RawDoorFamily],
    ) -> dict[str, DoorFamilyMetadata]:
        # Merge per-door family metadata deterministically: explicitly
        # provided fields must agree across doors of the same family.
        provided: dict[
            str,
            dict[str, object],
        ] = {}

        for raw in raws:
            current = provided.setdefault(
                raw.family_id,
                {},
            )

            for field, value in (
                ("display_name", raw.display_name),
                ("description", raw.description),
                ("thumbnail", raw.thumbnail),
                ("sort_order", raw.sort_order),
            ):
                if value is None:
                    continue

                if (
                    field in current
                    and current[field] != value
                ):
                    raise ValueError(
                        "conflicting door family metadata: "
                        f"{raw.family_id}"
                    )

                current[field] = value

        resolved: dict[
            str,
            DoorFamilyMetadata,
        ] = {}

        for family_id, current in provided.items():
            fallback = family_id.rsplit(
                ".",
                1,
            )[-1]

            display_name = (
                current.get("display_name")
                or (
                    fallback[:1].upper()
                    + fallback[1:]
                )
            )

            resolved[family_id] = DoorFamilyMetadata(
                family_id=family_id,
                display_name=str(display_name),
                description=str(
                    current.get(
                        "description",
                        "",
                    )
                ),
                thumbnail=(
                    current.get("thumbnail")
                ),
                sort_order=int(
                    current.get(
                        "sort_order",
                        DEFAULT_FAMILY_SORT_ORDER,
                    )
                ),
            )

        return resolved

    @staticmethod
    def _entry_sort_key(
        entry: DoorCatalogEntry,
    ) -> tuple[object, ...]:
        family = entry.family

        family_key = (
            (
                0,
                family.sort_order,
                family.display_name.casefold(),
                family.family_id,
            )
            if family is not None
            else (1, 0, "", "")
        )

        return (
            family_key,
            entry.display_name.casefold(),
            entry.definition_id,
        )
