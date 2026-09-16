from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtGui import QImage

from ..model.content_workspace import ContentWorkspace


@dataclass(
    frozen=True,
    slots=True,
)
class ResolvedStudioVisual:
    image: QImage
    anchor_x: int
    anchor_y: int
    draw_offset_x: int
    draw_offset_y: int
    animation_id: str
    frame_index: int


class StudioVisualResolver:
    """Reusable cached visual resolver for Content Studio canvases."""

    def __init__(
        self,
        image_loader: Callable[[str], QImage] | None = None,
    ) -> None:
        self._image_loader = (
            image_loader
            if image_loader is not None
            else QImage
        )

        self.workspace: ContentWorkspace | None = None
        self.asset_root: Path | None = None

        self._source_images: dict[
            str,
            QImage | None,
        ] = {}

        self._animation_frames: dict[
            tuple[object, ...],
            ResolvedStudioVisual | None,
        ] = {}

        self._tile_frames: dict[
            tuple[object, ...],
            QImage | None,
        ] = {}

        self._scaled_tile_frames: dict[
            tuple[int, int],
            QImage,
        ] = {}

    def set_context(
        self,
        workspace: ContentWorkspace | None,
        asset_root: Path | None,
    ) -> None:
        normalized_root = (
            Path(asset_root)
            if asset_root is not None
            else None
        )

        if (
            workspace is self.workspace
            and normalized_root == self.asset_root
        ):
            return

        self.workspace = workspace
        self.asset_root = normalized_root
        self.invalidate()

    def invalidate(self) -> None:
        self._source_images.clear()
        self._animation_frames.clear()
        self._tile_frames.clear()
        self._scaled_tile_frames.clear()

    def resolve_object(
        self,
        definition_id: str,
        frame_index: int = 0,
    ) -> ResolvedStudioVisual | None:
        workspace = self.workspace

        if workspace is None:
            return None

        definition = workspace.find(
            "objects",
            definition_id,
        )

        if definition is None:
            return None

        visual_set_id = definition.data.get(
            "visualSetId"
        )

        if not isinstance(
            visual_set_id,
            str,
        ):
            return None

        visual_set = workspace.find(
            "objectVisuals",
            visual_set_id,
        )

        if visual_set is None:
            return None

        animation_ids: list[str] = []

        if isinstance(
            definition.data.get("door"),
            dict,
        ):
            closed = visual_set.data.get(
                "doorClosedAnimationId"
            )

            if isinstance(
                closed,
                str,
            ) and closed:
                animation_ids.append(
                    closed
                )

        idle = visual_set.data.get(
            "idleAnimationId"
        )

        if (
            isinstance(
                idle,
                str,
            )
            and idle
            and idle not in animation_ids
        ):
            animation_ids.append(
                idle
            )

        for animation_id in animation_ids:
            visual = self.resolve_animation_frame(
                animation_id,
                frame_index,
            )

            if visual is not None:
                return visual

        return None

    def resolve_animation_frame(
        self,
        animation_id: str,
        frame_index: int = 0,
    ) -> ResolvedStudioVisual | None:
        workspace = self.workspace

        if workspace is None:
            return None

        animation = workspace.find(
            "animations",
            animation_id,
        )

        if animation is None:
            return None

        frames = animation.data.get(
            "frames"
        )

        if (
            not isinstance(
                frames,
                list,
            )
            or frame_index < 0
            or frame_index >= len(frames)
        ):
            return None

        frame = frames[
            frame_index
        ]

        if not isinstance(
            frame,
            dict,
        ):
            return None

        source = frame.get(
            "source"
        )

        if not isinstance(
            source,
            dict,
        ):
            return None

        image_id = animation.data.get(
            "imageId"
        )

        if not isinstance(
            image_id,
            str,
        ):
            return None

        path = self._visual_image_path(
            image_id
        )

        if path is None:
            return None

        source_rect = (
            int(source.get("x", 0)),
            int(source.get("y", 0)),
            int(source.get("width", 0)),
            int(source.get("height", 0)),
        )

        anchor = frame.get(
            "anchor"
        )

        if not isinstance(
            anchor,
            dict,
        ):
            anchor = {}

        draw_offset = frame.get(
            "drawOffset"
        )

        if not isinstance(
            draw_offset,
            dict,
        ):
            draw_offset = {}

        anchor_value = (
            int(anchor.get("x", 0)),
            int(anchor.get("y", 0)),
        )

        offset_value = (
            int(draw_offset.get("x", 0)),
            int(draw_offset.get("y", 0)),
        )

        cache_key = (
            animation_id,
            frame_index,
            image_id,
            str(path),
            source_rect,
            anchor_value,
            offset_value,
        )

        if cache_key in self._animation_frames:
            return self._animation_frames[
                cache_key
            ]

        source_image = self._load_path(
            path
        )

        if source_image is None:
            self._animation_frames[
                cache_key
            ] = None
            return None

        x, y, width, height = (
            source_rect
        )

        if width <= 0 or height <= 0:
            self._animation_frames[
                cache_key
            ] = None
            return None

        image = source_image.copy(
            x,
            y,
            width,
            height,
        )

        if image.isNull():
            self._animation_frames[
                cache_key
            ] = None
            return None

        result = ResolvedStudioVisual(
            image=image,
            anchor_x=anchor_value[0],
            anchor_y=anchor_value[1],
            draw_offset_x=offset_value[0],
            draw_offset_y=offset_value[1],
            animation_id=animation_id,
            frame_index=frame_index,
        )

        self._animation_frames[
            cache_key
        ] = result

        return result

    def resolve_tile(
        self,
        reference: dict[str, object],
        fallback_index: int,
        default_tile_size: int,
    ) -> QImage | None:
        workspace = self.workspace

        if workspace is None:
            return None

        tileset_id = reference.get(
            "tilesetId"
        )

        if not isinstance(
            tileset_id,
            str,
        ):
            return None

        tileset = workspace.find(
            "tilesets",
            tileset_id,
        )

        if tileset is None:
            return None

        relative = tileset.data.get(
            "relativeAssetPath"
        )

        if (
            not isinstance(
                relative,
                str,
            )
            or self.asset_root is None
        ):
            return None

        path = (
            self.asset_root
            / relative
        )

        source_index = int(
            reference.get(
                "sourceIndex",
                fallback_index,
            )
        )

        flags = int(
            reference.get(
                "flags",
                0,
            )
        )

        columns = max(
            1,
            int(
                tileset.data.get(
                    "columns",
                    1,
                )
            ),
        )

        tile_size = max(
            1,
            int(
                tileset.data.get(
                    "tileSize",
                    default_tile_size,
                )
            ),
        )

        cache_key = (
            tileset_id,
            str(path),
            source_index,
            flags,
            columns,
            tile_size,
        )

        if cache_key in self._tile_frames:
            return self._tile_frames[
                cache_key
            ]

        atlas = self._load_path(
            path
        )

        if atlas is None:
            self._tile_frames[
                cache_key
            ] = None
            return None

        result = atlas.copy(
            (
                source_index
                % columns
            )
            * tile_size,
            (
                source_index
                // columns
            )
            * tile_size,
            tile_size,
            tile_size,
        )

        if result.isNull():
            self._tile_frames[
                cache_key
            ] = None
            return None

        if flags & 1:
            result = result.mirrored(
                True,
                False,
            )

        self._tile_frames[
            cache_key
        ] = result

        return result

    def resolve_scaled_tile(
        self,
        reference: dict[str, object],
        fallback_index: int,
        default_tile_size: int,
        display_size: int,
    ) -> QImage | None:
        if display_size <= 0:
            return None

        image = self.resolve_tile(
            reference,
            fallback_index,
            default_tile_size,
        )

        if image is None:
            return None

        if (
            image.width() == display_size
            and image.height() == display_size
        ):
            return image

        cache_key = (
            id(image),
            int(display_size),
        )

        cached = self._scaled_tile_frames.get(
            cache_key
        )

        if cached is not None:
            return cached

        scaled = image.scaled(
            display_size,
            display_size,
            Qt.AspectRatioMode.IgnoreAspectRatio,
            Qt.TransformationMode.FastTransformation,
        )

        self._scaled_tile_frames[
            cache_key
        ] = scaled

        return scaled

    def _visual_image_path(
        self,
        image_id: str,
    ) -> Path | None:
        workspace = self.workspace

        if workspace is None:
            return None

        image_definition = workspace.find(
            "visualImages",
            image_id,
        )

        if image_definition is None:
            return None

        relative = image_definition.data.get(
            "relativePath"
        )

        if not isinstance(
            relative,
            str,
        ):
            return None

        root_name = image_definition.data.get(
            "root",
            "gameAssets",
        )

        if root_name == "contentWorkspace":
            root = workspace.root
        else:
            root = self.asset_root

        if root is None:
            return None

        return Path(root) / relative

    def _load_path(
        self,
        path: Path,
    ) -> QImage | None:
        key = str(
            path.resolve()
        )

        if key in self._source_images:
            return self._source_images[
                key
            ]

        image = self._image_loader(
            str(path)
        )

        if image.isNull():
            self._source_images[
                key
            ] = None
            return None

        self._source_images[
            key
        ] = image

        return image
