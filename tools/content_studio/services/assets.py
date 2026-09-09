from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True, slots=True)
class AssetEntry:
    root: str
    relative_path: Path
    absolute_path: Path


def is_safe_relative_path(path: str | Path) -> bool:
    raw = str(path).replace("\\", "/")
    if not raw or raw.startswith("/") or (len(raw) >= 2 and raw[1] == ":"):
        return False
    parts = raw.split("/")
    return all(part not in {"", ".", ".."} for part in parts)


class AssetCatalog:
    def __init__(self) -> None:
        self.entries: list[AssetEntry] = []

    def refresh(self, game_root: Path | None, content_root: Path | None) -> None:
        self.entries.clear()
        for name, root in (("gameAssets", game_root), ("contentWorkspace", content_root)):
            if root is None or not root.is_dir():
                continue
            for path in sorted(root.rglob("*"), key=lambda value: value.as_posix()):
                if path.is_file() and path.suffix.casefold() in {".png", ".jpg", ".jpeg", ".bmp", ".gif"}:
                    relative = path.relative_to(root)
                    if is_safe_relative_path(relative):
                        self.entries.append(AssetEntry(name, relative, path))

    def search(self, query: str) -> list[AssetEntry]:
        needle = query.casefold().strip()
        return [entry for entry in self.entries if not needle or needle in entry.relative_path.as_posix().casefold()]

