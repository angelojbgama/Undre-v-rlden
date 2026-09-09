from __future__ import annotations

import shutil
import struct
from dataclasses import dataclass
from pathlib import Path

from ..model.content_authoring import DefinitionRepository
from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition, Diagnostic
from .assets import is_safe_relative_path


SUPPORTED_IMAGE_SUFFIXES = frozenset({".png", ".jpg", ".jpeg", ".bmp", ".gif"})


@dataclass(frozen=True, slots=True)
class ImageDimensions:
    width: int
    height: int


@dataclass(frozen=True, slots=True)
class TilesetImportRequest:
    source_image: Path
    tileset_id: str
    tile_width: int = 16
    tile_height: int = 16
    spacing: int = 0
    margin: int = 0
    asset_root: Path | None = None
    workspace_root: Path | None = None
    copy_to_workspace: bool = False
    display_name: str = ""


@dataclass(slots=True)
class TilesetImportResult:
    definition: ContentDefinition | None
    dimensions: ImageDimensions | None
    columns: int = 0
    rows: int = 0
    asset_root: str = ""
    diagnostics: list[Diagnostic] | None = None

    @property
    def ok(self) -> bool:
        return self.definition is not None and not any(issue.is_error for issue in self.diagnostics or [])


class ImportRecipeRegistry:
    """Minimal named recipe registry for future sprite/item importers."""

    def __init__(self) -> None:
        self._recipes: dict[str, object] = {}

    def register(self, name: str, recipe: object) -> None:
        if not name.strip():
            raise ValueError("import recipe name cannot be empty")
        self._recipes[name] = recipe

    def get(self, name: str) -> object | None:
        return self._recipes.get(name)

    def names(self) -> tuple[str, ...]:
        return tuple(sorted(self._recipes))


class TilesetImporter:
    """Imports only the authored tileset metadata; source assets stay external."""

    def inspect(self, source_image: Path) -> ImageDimensions:
        source_image = source_image.expanduser()
        if source_image.suffix.casefold() not in SUPPORTED_IMAGE_SUFFIXES:
            raise ValueError(f"unsupported image format: {source_image.suffix or '<none>'}")
        if not source_image.is_file():
            raise ValueError("source image does not exist")
        return read_image_dimensions(source_image)

    def import_tileset(self, workspace: ContentWorkspace, request: TilesetImportRequest) -> TilesetImportResult:
        diagnostics: list[Diagnostic] = []
        try:
            self._validate_request(request)
            dimensions = self.inspect(request.source_image)
            columns, rows = calculate_grid(dimensions, request.tile_width, request.tile_height, request.spacing, request.margin)
            root_name, relative_path = self._resolve_asset(request)
            repository = DefinitionRepository(workspace)
            existing = workspace.find("tilesets", request.tileset_id)
            if existing is None:
                definition = repository.create("tilesets", request.tileset_id)
            else:
                definition = existing
            data = dict(definition.data)
            data.update({
                "id": request.tileset_id,
                "displayName": request.display_name.strip() or request.tileset_id,
                "relativeAssetPath": relative_path,
                "tileSize": request.tile_width,
                "columns": columns,
                "rows": rows,
            })
            workspace.replace_definition(definition, data)
            definition = workspace.find("tilesets", request.tileset_id)
            return TilesetImportResult(definition, dimensions, columns, rows, root_name, diagnostics)
        except (OSError, ValueError) as error:
            diagnostics.append(Diagnostic("error", str(error), code="tileset_import"))
            return TilesetImportResult(None, None, diagnostics=diagnostics)

    def _validate_request(self, request: TilesetImportRequest) -> None:
        if not request.tileset_id.strip():
            raise ValueError("tileset ID cannot be empty")
        if request.tile_width <= 0 or request.tile_height <= 0:
            raise ValueError("tile dimensions must be positive")
        if request.tile_width != request.tile_height:
            raise ValueError("the current authored tileset contract requires square tiles")
        if request.spacing < 0 or request.margin < 0:
            raise ValueError("spacing and margin cannot be negative")
        if request.spacing or request.margin:
            raise ValueError("spacing and margin are not representable by the current authored tileset contract")

    def _resolve_asset(self, request: TilesetImportRequest) -> tuple[str, str]:
        source = request.source_image.expanduser().resolve()
        candidates = (("gameAssets", request.asset_root), ("contentWorkspace", request.workspace_root))
        for root_name, root in candidates:
            if root is None:
                continue
            root = root.expanduser().resolve()
            try:
                relative = source.relative_to(root)
            except ValueError:
                continue
            relative_text = relative.as_posix()
            if not is_safe_relative_path(relative_text):
                raise ValueError("asset path is not safe")
            return root_name, relative_text
        if request.copy_to_workspace and request.workspace_root is not None:
            root = request.workspace_root.expanduser().resolve()
            destination = root / "assets" / source.name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
            return "contentWorkspace", destination.relative_to(root).as_posix()
        raise ValueError("source image must be under the licensed asset root or content workspace")


class ImportService:
    def __init__(self, recipes: ImportRecipeRegistry | None = None) -> None:
        self.recipes = recipes or ImportRecipeRegistry()
        self.tilesets = TilesetImporter()

    def inspect_image(self, source_image: Path) -> ImageDimensions:
        return self.tilesets.inspect(source_image)

    def import_tileset(self, workspace: ContentWorkspace, request: TilesetImportRequest) -> TilesetImportResult:
        return self.tilesets.import_tileset(workspace, request)


def calculate_grid(dimensions: ImageDimensions, tile_width: int, tile_height: int,
                   spacing: int = 0, margin: int = 0) -> tuple[int, int]:
    if tile_width <= 0 or tile_height <= 0 or spacing < 0 or margin < 0:
        raise ValueError("invalid tileset grid values")
    available_width = dimensions.width - margin * 2 + spacing
    available_height = dimensions.height - margin * 2 + spacing
    columns = available_width // (tile_width + spacing)
    rows = available_height // (tile_height + spacing)
    if columns <= 0 or rows <= 0:
        raise ValueError("tile grid does not fit inside source image")
    return columns, rows


def read_image_dimensions(path: Path) -> ImageDimensions:
    """Read dimensions without adding Pillow; Qt remains the preview decoder."""
    data = path.read_bytes()
    if data.startswith(b"\x89PNG\r\n\x1a\n") and len(data) >= 24:
        width, height = struct.unpack(">II", data[16:24])
        return ImageDimensions(width, height)
    if data[:6] in {b"GIF87a", b"GIF89a"} and len(data) >= 10:
        width, height = struct.unpack("<HH", data[6:10])
        return ImageDimensions(width, height)
    if data.startswith(b"BM") and len(data) >= 26:
        width, height = struct.unpack("<ii", data[18:26])
        return ImageDimensions(abs(width), abs(height))
    if data.startswith(b"\xff\xd8"):
        return _jpeg_dimensions(data)
    raise ValueError("could not read image dimensions; use a valid PNG, JPEG, BMP or GIF")


def _jpeg_dimensions(data: bytes) -> ImageDimensions:
    index = 2
    while index + 9 < len(data):
        if data[index] != 0xFF:
            index += 1
            continue
        marker = data[index + 1]
        index += 2
        if marker in {0xD8, 0xD9}:
            continue
        if index + 2 > len(data):
            break
        length = struct.unpack(">H", data[index:index + 2])[0]
        if marker in {0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF} and index + 7 <= len(data):
            height, width = struct.unpack(">HH", data[index + 3:index + 7])
            return ImageDimensions(width, height)
        index += max(2, length)
    raise ValueError("could not read JPEG dimensions")
