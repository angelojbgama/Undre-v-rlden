"""Tests for the app icon service (game.exe icon management).

The ICO container is validated by parsing its on-disk struct and decoding
payloads back through QImage; only synthetic art is used.
"""

from __future__ import annotations

import struct
import tempfile
import unittest
from pathlib import Path

from PySide6.QtCore import QBuffer, QIODevice, Qt
from PySide6.QtGui import QGuiApplication, QImage

from tools.content_studio.services.app_icon_service import (
    ICON_SIZES,
    _PNG_SIGNATURE,
    build_icon,
    ensure_resource_file,
    generate_app_icon_files,
    load_source_image,
    square_canvas,
)


def _synthetic_png(path: Path, width: int = 22, height: int = 24) -> None:
    """Non-square synthetic art; no licensed asset is involved."""
    image = QImage(width, height, QImage.Format.Format_RGBA8888)
    image.fill(0)
    for y in range(0, height, 4):
        for x in range(0, width, 4):
            image.setPixel(x, y, 0xFFFFFFFF)
    image.save(str(path), "PNG")


def _parse_ico(data: bytes) -> tuple[int, list[tuple[int, int, int, bytes]]]:
    """Returns (type, [(width, height, payloadSize, payload), ...])."""
    reserved, icon_type, count = struct.unpack_from("<HHH", data, 0)
    entries = []
    for index in range(count):
        base = 6 + 16 * index
        width, height, _colors, _reserved, _planes, _bpp, size, offset = (
            struct.unpack_from("<BBBBHHII", data, base))
        entries.append((width, height, size, data[offset:offset + size]))
    return icon_type, entries


class AppIconServiceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if QGuiApplication.instance() is None:
            cls._app = QGuiApplication([])

    def test_load_rejects_missing_source(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError):
                load_source_image(Path(tmp) / "missing.png")

    def test_build_icon_writes_all_sizes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "source.png"
            output = Path(tmp) / "game_icon.ico"
            _synthetic_png(source)
            sizes = build_icon(source, output)
            self.assertEqual(sizes, ICON_SIZES)
            icon_type, entries = _parse_ico(output.read_bytes())
            self.assertEqual(icon_type, 1)
            # A stored byte of 0 encodes the 256px entry per the ICO spec.
            decoded = [(w or 256, h or 256) for w, h, _, _ in entries]
            self.assertEqual(sorted(decoded),
                             sorted((size, size) for size in ICON_SIZES))
            for _w, _h, size, payload in entries:
                self.assertEqual(size, len(payload))
                self.assertTrue(payload.startswith(_PNG_SIGNATURE))

    def test_icon_is_square_by_padding_not_stretching(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "source.png"
            output = Path(tmp) / "game_icon.ico"
            _synthetic_png(source, width=8, height=16)
            build_icon(source, output, sizes=(16, 32))
            _icon_type, entries = _parse_ico(output.read_bytes())
            largest = next(payload for w, h, _size, payload in entries if w == 32)
            image = QImage.fromData(largest, "PNG")
            self.assertEqual((image.width(), image.height()), (32, 32))
            self.assertEqual(image.pixelColor(0, 16).alpha(), 0,
                             "the padded column stays transparent")

    def test_square_canvas_centers_the_source(self) -> None:
        image = QImage(8, 4, QImage.Format.Format_RGBA8888)
        image.fill(0xFFFF00FF)
        canvas = square_canvas(image)
        self.assertEqual((canvas.width(), canvas.height()), (8, 8))
        self.assertEqual(canvas.pixelColor(4, 0).alpha(), 0)
        self.assertEqual(canvas.pixelColor(4, 2).red(), 255)

    def test_resource_file_written_once(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            rc = Path(tmp) / "src" / "win32" / "game.rc"
            ico = Path(tmp) / "build" / "game_icon.ico"
            self.assertTrue(ensure_resource_file(ico, rc))
            content = rc.read_text(encoding="utf-8")
            self.assertIn('1 ICON "', content)
            self.assertIn("game_icon.ico", content)
            self.assertFalse(ensure_resource_file(ico, rc),
                             "an existing .rc is never overwritten")

    def test_generate_entry_point(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "assets" / "icon.png"
            source.parent.mkdir()
            _synthetic_png(source)
            result = generate_app_icon_files(
                source, Path(tmp) / "build" / "game_icon.ico",
                Path(tmp) / "src" / "win32" / "game.rc")
            self.assertTrue(result.ico_path.exists())
            self.assertIsNotNone(result.rc_path)
            self.assertTrue(result.rc_path.exists())


if __name__ == "__main__":
    unittest.main()
