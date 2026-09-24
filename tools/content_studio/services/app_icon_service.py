"""Windows application icon management for the Content Studio.

The licensed art never enters Git, so the icon of ``game.exe`` is derived at
tooling time: this service reads a source PNG (by default the local
``assets/icon.png``), writes a multi-size ``.ico`` into the build directory
and makes sure the resource script that ``build.bat`` compiles exists. The
C++ runtime itself never embeds the icon; ``rc.exe`` produces a ``.res`` that
is linked into ``game.exe``.

Only PySide6 (the Studio's declared dependency) is used: frames are scaled
with ``QImage`` and the ICO container itself is assembled from its on-disk
struct, so the tooling stays free of extra image libraries.
"""

from __future__ import annotations

import os
import struct
from dataclasses import dataclass
from pathlib import Path

from PySide6.QtCore import QBuffer, QIODevice, Qt
from PySide6.QtGui import QImage, QPainter

#: Standard Windows icon sizes; small sizes keep the sprite readable because
#: the source is pixel art and frames use nearest-neighbor resampling.
ICON_SIZES = (16, 24, 32, 48, 64, 256)

RC_TEMPLATE = """\
// Generated once by the Content Studio app icon service (tools menu). The
// .ico stays inside the build directory because it is derived from licensed
// art; build.bat skips this resource while the .ico is absent.
1 ICON "{ico_relative}"
"""

_PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


@dataclass(frozen=True)
class AppIconResult:
    """Paths written by :func:`generate_app_icon_files`."""

    source_png: Path
    ico_path: Path
    rc_path: Path | None
    sizes: tuple[int, ...]


def load_source_image(source_png: Path) -> QImage:
    """Loads the source PNG; raises when the file is missing or undecodable."""
    image = QImage(str(source_png))
    if image.isNull():
        raise ValueError(f"could not decode source PNG: {source_png}")
    return image.convertToFormat(QImage.Format.Format_RGBA8888)


def square_canvas(image: QImage) -> QImage:
    """Pads the source onto a transparent square so icons stay centered."""
    side = max(image.width(), image.height())
    canvas = QImage(side, side, QImage.Format.Format_RGBA8888)
    canvas.fill(Qt.GlobalColor.transparent)
    painter = QPainter(canvas)
    painter.drawImage((side - image.width()) // 2, (side - image.height()) // 2, image)
    painter.end()
    return canvas


def render_frames(canvas: QImage, sizes: tuple[int, ...]) -> list[QImage]:
    """Nearest-neighbor frames, largest first.

    The largest frame leads because consumers (and the ICO container) key
    off the first image; nearest-neighbor (``FastTransformation``) keeps the
    pixel art edges crisp.
    """
    frames = [canvas.scaled(size, size, Qt.AspectRatioMode.IgnoreAspectRatio,
                            Qt.TransformationMode.FastTransformation)
              for size in sorted(sizes, reverse=True)]
    return frames


def _png_bytes(image: QImage) -> bytes:
    buffer = QBuffer()
    buffer.open(QIODevice.OpenModeFlag.WriteOnly)
    image.save(buffer, "PNG")
    return bytes(buffer.data())


def build_icon(source_png: Path, output_ico: Path,
               sizes: tuple[int, ...] = ICON_SIZES) -> tuple[int, ...]:
    """Renders ``source_png`` as a multi-size ``.ico`` and returns the sizes.

    Non-square sources are padded, not stretched. Frames are stored as PNG
    entries (standard since Windows Vista, the supported target).
    """
    canvas = square_canvas(load_source_image(source_png))
    frames = render_frames(canvas, sizes)
    payloads = [_png_bytes(frame) for frame in frames]
    output_ico.parent.mkdir(parents=True, exist_ok=True)
    header = struct.pack("<HHH", 0, 1, len(frames))
    directory = b""
    offset = 6 + 16 * len(frames)
    for frame, payload in zip(frames, payloads):
        side = frame.width()
        directory += struct.pack("<BBBBHHII", side % 256, side % 256, 0, 0,
                                 1, 32, len(payload), offset)
        offset += len(payload)
    output_ico.write_bytes(header + directory + b"".join(payloads))
    return tuple(sorted(sizes))


def ensure_resource_file(output_ico: Path, rc_path: Path) -> bool:
    """Writes the resource script when missing; returns True when written.

    The ``.rc`` references the ``.ico`` relative to its own directory so
    ``rc.exe`` resolves it regardless of the caller's working directory.
    """
    if rc_path.exists():
        return False
    rc_path.parent.mkdir(parents=True, exist_ok=True)
    ico_relative = Path(os.path.relpath(output_ico.resolve(), rc_path.resolve().parent))
    rc_path.write_text(RC_TEMPLATE.format(ico_relative=ico_relative.as_posix()),
                       encoding="utf-8")
    return True


def generate_app_icon_files(source_png: Path, output_ico: Path,
                            rc_path: Path | None = None) -> AppIconResult:
    """Full Studio entry point: icon + resource script in one call."""
    sizes = build_icon(source_png, output_ico)
    wrote_rc = ensure_resource_file(output_ico, rc_path) if rc_path else False
    return AppIconResult(source_png=source_png, ico_path=output_ico,
                         rc_path=rc_path if wrote_rc else None, sizes=sizes)


def main(argv: list[str] | None = None) -> int:
    import argparse

    repo_root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(
        description="Generate the game.exe application icon from a source PNG.")
    parser.add_argument("--png", type=Path, default=repo_root / "assets" / "icon.png",
                        help="source PNG (default: assets/icon.png)")
    parser.add_argument("--output", type=Path,
                        default=repo_root / "build" / "game_icon.ico",
                        help="output .ico (default: build/game_icon.ico)")
    parser.add_argument("--rc", type=Path, dest="rc",
                        default=repo_root / "src" / "win32" / "game.rc",
                        help="resource script path (empty to skip)")
    args = parser.parse_args(argv)
    result = generate_app_icon_files(args.png, args.output, args.rc)
    print(f"icon: {result.ico_path} ({', '.join(str(s) for s in result.sizes)})")
    if result.rc_path:
        print(f"resource script: {result.rc_path}")
    print("rebuild with build.bat to embed the icon into game.exe")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
