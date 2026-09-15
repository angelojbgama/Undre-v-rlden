"""Reusable visual selection for authored Items.

Items always reference StaticSprite definitions. Existing StaticSprites are
reused directly. Animation frames can be materialized into a deterministic
StaticSprite without copying image files.
"""

from __future__ import annotations

import copy
from dataclasses import dataclass

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition, JsonValue


@dataclass(frozen=True, slots=True)
class AnimationFrameVisual:
    animation_id: str
    frame_index: int
    image_id: str
    source: tuple[int, int, int, int]
    anchor: tuple[int, int]


class ItemVisualService:
    """Resolve or create the StaticSprite referenced by an Item."""

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

    def static_sprites(
        self,
        query: str = "",
    ) -> tuple[ContentDefinition, ...]:
        workspace = self._require_workspace()

        return tuple(
            workspace.definitions(
                "staticSprites",
                query,
            )
        )

    def select_static_sprite(
        self,
        visual_id: str,
    ) -> ContentDefinition:
        workspace = self._require_workspace()

        visual_id = visual_id.strip()

        definition = workspace.find(
            "staticSprites",
            visual_id,
        )

        if definition is None:
            raise ValueError(
                f"static sprite does not exist: {visual_id}"
            )

        return definition

    def animation_frames(
        self,
        query: str = "",
    ) -> tuple[AnimationFrameVisual, ...]:
        workspace = self._require_workspace()

        result: list[AnimationFrameVisual] = []

        for animation in workspace.definitions(
            "animations",
            query,
        ):
            image_id = animation.data.get(
                "imageId"
            )

            frames = animation.data.get(
                "frames"
            )

            if (
                not isinstance(image_id, str)
                or not image_id
                or not isinstance(frames, list)
            ):
                continue

            for frame_index, raw_frame in enumerate(
                frames
            ):
                decoded = self._try_decode_frame(
                    animation.definition_id,
                    frame_index,
                    image_id,
                    raw_frame,
                )

                if decoded is not None:
                    result.append(
                        decoded
                    )

        return tuple(
            result
        )

    def materialize_animation_frame(
        self,
        item_id: str,
        animation_id: str,
        frame_index: int,
    ) -> ContentDefinition:
        workspace = self._require_workspace()

        visual_id = self.generated_visual_id(
            item_id
        )

        animation = workspace.find(
            "animations",
            animation_id,
        )

        if animation is None:
            raise ValueError(
                f"animation does not exist: {animation_id}"
            )

        image_id = animation.data.get(
            "imageId"
        )

        if (
            not isinstance(image_id, str)
            or not image_id
        ):
            raise ValueError(
                f"animation has no imageId: {animation_id}"
            )

        if workspace.find(
            "visualImages",
            image_id,
        ) is None:
            raise ValueError(
                f"animation image does not exist: {image_id}"
            )

        frames = animation.data.get(
            "frames"
        )

        if not isinstance(
            frames,
            list,
        ):
            raise ValueError(
                f"animation frames are invalid: {animation_id}"
            )

        if (
            not isinstance(frame_index, int)
            or isinstance(frame_index, bool)
            or frame_index < 0
            or frame_index >= len(frames)
        ):
            raise ValueError(
                f"animation frame is outside range: {frame_index}"
            )

        frame = self._decode_frame(
            animation_id,
            frame_index,
            image_id,
            frames[frame_index],
        )

        expected: dict[str, JsonValue] = {
            "id": visual_id,
            "imageId": frame.image_id,
            "source": {
                "x": frame.source[0],
                "y": frame.source[1],
                "width": frame.source[2],
                "height": frame.source[3],
            },
            "anchor": {
                "x": frame.anchor[0],
                "y": frame.anchor[1],
            },
        }

        existing = workspace.find(
            "staticSprites",
            visual_id,
        )

        if existing is not None:
            if not self._same_visual(
                existing.data,
                expected,
            ):
                raise ValueError(
                    f"generated visual conflict: {visual_id}"
                )

            return existing

        workspace.create_definition_bundle(
            "Create Item Static Sprite",
            [(
                "staticSprites",
                visual_id,
                expected,
            )],
        )

        created = workspace.find(
            "staticSprites",
            visual_id,
        )

        if created is None:
            raise RuntimeError(
                "item StaticSprite could not be indexed"
            )

        return created

    @staticmethod
    def generated_visual_id(
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

        return (
            "visual.item."
            + normalized[len("item."):]
        )

    def _require_workspace(
        self,
    ) -> ContentWorkspace:
        if self.workspace is None:
            raise ValueError(
                "content workspace is unavailable"
            )

        return self.workspace

    @classmethod
    def _decode_frame(
        cls,
        animation_id: str,
        frame_index: int,
        image_id: str,
        raw_frame: object,
    ) -> AnimationFrameVisual:
        result = cls._try_decode_frame(
            animation_id,
            frame_index,
            image_id,
            raw_frame,
        )

        if result is None:
            raise ValueError(
                f"animation frame is invalid: "
                f"{animation_id} #{frame_index}"
            )

        return result

    @staticmethod
    def _try_decode_frame(
        animation_id: str,
        frame_index: int,
        image_id: str,
        raw_frame: object,
    ) -> AnimationFrameVisual | None:
        if not isinstance(
            raw_frame,
            dict,
        ):
            return None

        source = raw_frame.get(
            "source"
        )

        anchor = raw_frame.get(
            "anchor"
        )

        if (
            not isinstance(source, dict)
            or not isinstance(anchor, dict)
        ):
            return None

        source_values = (
            source.get("x"),
            source.get("y"),
            source.get("width"),
            source.get("height"),
        )

        anchor_values = (
            anchor.get("x"),
            anchor.get("y"),
        )

        if not all(
            isinstance(value, int)
            and not isinstance(value, bool)
            for value in (
                *source_values,
                *anchor_values,
            )
        ):
            return None

        x, y, width, height = source_values

        if width <= 0 or height <= 0:
            return None

        return AnimationFrameVisual(
            animation_id=animation_id,
            frame_index=frame_index,
            image_id=image_id,
            source=(
                x,
                y,
                width,
                height,
            ),
            anchor=(
                anchor_values[0],
                anchor_values[1],
            ),
        )

    @staticmethod
    def _same_visual(
        actual: dict[str, JsonValue],
        expected: dict[str, JsonValue],
    ) -> bool:
        return (
            actual.get("id")
            == expected.get("id")
            and actual.get("imageId")
            == expected.get("imageId")
            and actual.get("source")
            == expected.get("source")
            and actual.get("anchor")
            == expected.get("anchor")
        )