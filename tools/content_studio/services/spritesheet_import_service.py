from __future__ import annotations

import copy
import re
import shutil
from dataclasses import dataclass
from pathlib import Path

from ..model.content_authoring import DefinitionRepository
from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition, Diagnostic
from .assets import is_safe_relative_path
from .import_service import ImageDimensions, SUPPORTED_IMAGE_SUFFIXES, TilesetImporter, calculate_grid


@dataclass(frozen=True, slots=True)
class SpritesheetImportRequest:
    source_image: Path
    image_id: str
    animation_id: str
    frame_width: int
    frame_height: int
    duration_ticks: int = 8
    spacing: int = 0
    margin: int = 0
    asset_root: Path | None = None


@dataclass(slots=True)
class SpritesheetImportResult:
    image_id: str = ""
    animation_id: str = ""
    dimensions: ImageDimensions | None = None
    columns: int = 0
    rows: int = 0
    diagnostics: list[Diagnostic] | None = None

    @property
    def ok(self) -> bool:
        return bool(self.animation_id) and not any(issue.is_error for issue in self.diagnostics or [])


class SpritesheetImportService:
    """Compile one rectangular spritesheet into authored image + animation definitions."""

    def __init__(self, image_inspector: TilesetImporter | None = None) -> None:
        self.image_inspector = image_inspector or TilesetImporter()

    def inspect(self, source_image: Path) -> ImageDimensions:
        return self.image_inspector.inspect(source_image)

    def import_spritesheet(self, workspace: ContentWorkspace,
                           request: SpritesheetImportRequest,
                           existing_animation: ContentDefinition | None = None
                           ) -> SpritesheetImportResult:
        try:
            dimensions, columns, rows = self._validate(
                workspace, request, existing_animation)
            relative_path = self._import_asset(request)
            repository = DefinitionRepository(workspace)
            image = workspace.find("visualImages", request.image_id)
            if image is None:
                image = repository.create("visualImages", request.image_id)
                workspace.replace_definition(image, {
                    "id": request.image_id,
                    "root": "gameAssets",
                    "relativePath": relative_path,
                })
            elif (image.data.get("root") != "gameAssets" or
                  image.data.get("relativePath") != relative_path):
                raise ValueError(
                    f"visual image ID already refers to another asset: {request.image_id}")
            previous_frames = (
                existing_animation.data.get("frames", [])
                if existing_animation is not None else []
            )
            frames = []
            for row in range(rows):
                for column in range(columns):
                    frame_index = row * columns + column
                    previous = (
                        previous_frames[frame_index]
                        if isinstance(previous_frames, list)
                        and frame_index < len(previous_frames)
                        and isinstance(previous_frames[frame_index], dict)
                        else {}
                    )
                    previous_offset = previous.get("drawOffset", {})
                    # Older Studio builds could persist an empty drawOffset.
                    # Always emit the complete point contract expected by the
                    # authoritative C++ content decoder.
                    draw_offset = {
                        "x": int(previous_offset.get("x", 0))
                        if isinstance(previous_offset, dict) else 0,
                        "y": int(previous_offset.get("y", 0))
                        if isinstance(previous_offset, dict) else 0,
                    }
                    previous_markers = previous.get("markers", [])
                    frames.append({
                        "source": {
                            "x": request.margin + column * (request.frame_width + request.spacing),
                            "y": request.margin + row * (request.frame_height + request.spacing),
                            "width": request.frame_width,
                            "height": request.frame_height,
                        },
                        "anchor": {"x": request.frame_width // 2, "y": request.frame_height - 1},
                        "drawOffset": draw_offset,
                        "durationTicks": request.duration_ticks,
                        "markers": (
                            copy.deepcopy(previous_markers)
                            if isinstance(previous_markers, list) else []
                        ),
                    })
            animation = existing_animation or repository.create(
                "animations", request.animation_id)
            workspace.replace_definition(animation, {
                "id": request.animation_id,
                "imageId": request.image_id,
                # Loop is an animation-authoring decision. The import screen only
                # describes how the source image is divided into frames.
                "loop": (bool(existing_animation.data.get("loop", False))
                         if existing_animation is not None else False),
                "frames": frames,
            })
            return SpritesheetImportResult(
                request.image_id, request.animation_id, dimensions, columns, rows, [],
            )
        except (OSError, ValueError) as error:
            return SpritesheetImportResult(diagnostics=[Diagnostic(
                "error", str(error), code="spritesheet_import")])

    def _validate(self, workspace: ContentWorkspace,
                  request: SpritesheetImportRequest,
                  existing_animation: ContentDefinition | None = None
                  ) -> tuple[ImageDimensions, int, int]:
        if not request.image_id.strip() or not request.animation_id.strip():
            raise ValueError("image ID and animation ID are required")
        if request.frame_width <= 0 or request.frame_height <= 0:
            raise ValueError("frame dimensions must be positive")
        if request.duration_ticks <= 0:
            raise ValueError("frame duration must be positive")
        if request.spacing < 0 or request.margin < 0:
            raise ValueError("spacing and margin cannot be negative")
        found_animation = workspace.find("animations", request.animation_id)
        if existing_animation is not None and (
                existing_animation.category != "animations"
                or existing_animation.definition_id != request.animation_id
                or found_animation is not existing_animation):
            raise ValueError("the animation selected for reimport is no longer available")
        if found_animation is not None and existing_animation is None:
            raise ValueError(f"animation ID already exists: {request.animation_id}")
        dimensions = self.inspect(request.source_image)
        columns, rows = calculate_grid(
            dimensions, request.frame_width, request.frame_height,
            request.spacing, request.margin,
        )
        # A spritesheet may contain transparent padding after its last complete
        # frame. Only complete frames are authored; the preview keeps any unused
        # edge visible so the artist can adjust the grid if necessary.
        if columns * rows > 4096:
            raise ValueError("animation cannot contain more than 4096 frames")
        return dimensions, columns, rows

    def _import_asset(self, request: SpritesheetImportRequest) -> str:
        source = request.source_image.expanduser().resolve()
        if source.suffix.casefold() not in SUPPORTED_IMAGE_SUFFIXES:
            raise ValueError(f"unsupported image format: {source.suffix or '<none>'}")
        root = request.asset_root.expanduser().resolve() if request.asset_root else None
        if root is None:
            raise ValueError("the repository assets directory is unavailable")
        try:
            relative = source.relative_to(root)
        except ValueError:
            safe_id = re.sub(r"[^A-Za-z0-9_.-]+", "_", request.animation_id).strip("._") or "animation"
            relative = Path("spritesheets") / f"{safe_id}{source.suffix.casefold()}"
            destination = root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            if source != destination.resolve():
                shutil.copy2(source, destination)
        relative_text = relative.as_posix()
        if not is_safe_relative_path(relative_text):
            raise ValueError("asset path is not safe")
        return relative_text
