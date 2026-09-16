from __future__ import annotations

from copy import deepcopy

from ..model.content_workspace import ContentWorkspace


OBJECT_COLLISION_MASK_CHANNEL = "objectCollision"


class AnimationFrameMaskService:
    # Channel-agnostic authoring service for animation frame masks.

    def __init__(
        self,
        workspace: ContentWorkspace,
    ) -> None:
        self.workspace = workspace

    def channel_masks(
        self,
        animation_id: str,
        channel: str,
    ) -> list[dict[str, object] | None]:
        animation = self._animation(
            animation_id
        )
        frames = self._frames(
            animation.data
        )

        result: list[
            dict[str, object] | None
        ] = []

        for frame in frames:
            masks = frame.get(
                "masks",
                [],
            )
            masks = (
                masks
                if isinstance(masks, list)
                else []
            )

            selected = [
                value
                for value in masks
                if (
                    isinstance(value, dict)
                    and value.get("channel") == channel
                )
            ]

            if len(selected) > 1:
                raise ValueError(
                    f"{animation_id}: duplicate frame mask channel "
                    f"{channel!r}"
                )

            if not selected:
                result.append(
                    None
                )
                continue

            result.append(
                self._normalized_mask(
                    selected[0],
                    frame,
                )
            )

        return result

    def replace_channel(
        self,
        animation_id: str,
        channel: str,
        masks: list[
            dict[str, object] | None
        ],
    ) -> None:
        animation = self._animation(
            animation_id
        )
        frames = self._frames(
            animation.data
        )

        if len(masks) != len(frames):
            raise ValueError(
                "frame mask list must match animation frame count"
            )

        data = deepcopy(
            animation.data
        )
        output_frames = self._frames(
            data
        )

        for frame, authored in zip(
            output_frames,
            masks,
        ):
            raw_masks = frame.get(
                "masks",
                [],
            )
            raw_masks = (
                raw_masks
                if isinstance(raw_masks, list)
                else []
            )

            preserved = [
                deepcopy(value)
                for value in raw_masks
                if (
                    not isinstance(value, dict)
                    or value.get("channel") != channel
                )
            ]

            if authored is not None:
                mask = self._normalized_mask(
                    authored,
                    frame,
                )
                mask["channel"] = channel
                preserved.append(
                    mask
                )

            if preserved:
                frame["masks"] = preserved
            else:
                frame.pop(
                    "masks",
                    None,
                )

        self.workspace.replace_definition(
            animation,
            data,
        )

    def _animation(
        self,
        animation_id: str,
    ):
        animation = self.workspace.find(
            "animations",
            animation_id,
        )

        if animation is None:
            raise ValueError(
                f"animation not found: {animation_id}"
            )

        return animation

    @staticmethod
    def _frames(
        animation_data: dict[str, object],
    ) -> list[dict[str, object]]:
        frames = animation_data.get(
            "frames",
            [],
        )

        if not isinstance(frames, list):
            raise ValueError(
                "animation frames must be a list"
            )

        result = [
            frame
            for frame in frames
            if isinstance(frame, dict)
        ]

        if len(result) != len(frames):
            raise ValueError(
                "animation contains an invalid frame"
            )

        if not result:
            raise ValueError(
                "animation has no frames"
            )

        return result

    @staticmethod
    def _normalized_mask(
        value: dict[str, object],
        frame: dict[str, object],
    ) -> dict[str, object]:
        source = frame.get(
            "source",
            {},
        )

        if not isinstance(source, dict):
            raise ValueError(
                "animation frame source is invalid"
            )

        width = int(
            value.get(
                "width",
                0,
            )
        )
        height = int(
            value.get(
                "height",
                0,
            )
        )

        frame_width = max(
            1,
            int(
                source.get(
                    "width",
                    1,
                )
            ),
        )
        frame_height = max(
            1,
            int(
                source.get(
                    "height",
                    1,
                )
            ),
        )

        if (
            width != frame_width
            or height != frame_height
        ):
            raise ValueError(
                "frame mask dimensions must match the animation frame"
            )

        origin = value.get(
            "origin",
            {},
        )
        cells = value.get(
            "cells",
            [],
        )

        if not isinstance(origin, dict):
            raise ValueError(
                "frame mask origin is invalid"
            )

        if not isinstance(cells, list):
            raise ValueError(
                "frame mask cells are invalid"
            )

        normalized_cells = [
            int(cell)
            for cell in cells
        ]

        expected = width * height

        if (
            expected <= 0
            or len(normalized_cells) != expected
        ):
            raise ValueError(
                "frame mask cell count does not match its dimensions"
            )

        if any(
            cell not in (0, 1)
            for cell in normalized_cells
        ):
            raise ValueError(
                "frame mask cells must be 0 or 1"
            )

        if not any(
            normalized_cells
        ):
            raise ValueError(
                "empty frame masks must be removed instead of persisted"
            )

        return {
            "width": width,
            "height": height,
            "origin": {
                "x": int(
                    origin.get(
                        "x",
                        0,
                    )
                ),
                "y": int(
                    origin.get(
                        "y",
                        0,
                    )
                ),
            },
            "cells": normalized_cells,
        }
