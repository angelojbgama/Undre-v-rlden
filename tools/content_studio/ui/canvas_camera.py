from __future__ import annotations

from dataclasses import dataclass


@dataclass(slots=True)
class CanvasCamera:
    """Editor-only viewport state and coordinate conversion."""

    zoom: float = 1.0
    pan_x: int = 0
    pan_y: int = 0
    snap_enabled: bool = True

    def set_pan(self, x: int, y: int) -> None:
        self.pan_x, self.pan_y = int(x), int(y)

    def pan_by(self, dx: int, dy: int) -> None:
        self.pan_x += int(dx)
        self.pan_y += int(dy)

    def fit(self, map_width: int, map_height: int, viewport_width: int, viewport_height: int) -> None:
        if map_width <= 0 or map_height <= 0:
            return
        self.zoom = max(0.25, min(4.0, min(viewport_width / map_width, viewport_height / map_height) * 0.92))
        self.set_pan(0, 0)

    def world_to_screen(self, x: int, y: int, map_width: int, map_height: int,
                        viewport_width: int, viewport_height: int) -> tuple[int, int]:
        origin_x = (viewport_width - map_width * self.zoom) / 2 + self.pan_x
        origin_y = (viewport_height - map_height * self.zoom) / 2 + self.pan_y
        return round(origin_x + x * self.zoom), round(origin_y + y * self.zoom)

    def screen_to_world(self, x: int, y: int, map_width: int, map_height: int,
                        viewport_width: int, viewport_height: int) -> tuple[int, int]:
        origin_x = (viewport_width - map_width * self.zoom) / 2 + self.pan_x
        origin_y = (viewport_height - map_height * self.zoom) / 2 + self.pan_y
        return round((x - origin_x) / self.zoom), round((y - origin_y) / self.zoom)

    def zoom_by(self, factor: float) -> None:
        self.zoom = max(0.25, min(8.0, self.zoom * factor))
