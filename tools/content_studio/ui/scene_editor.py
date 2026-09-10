from __future__ import annotations

from PySide6.QtCore import QPoint, QTimer, Qt, Signal
from PySide6.QtGui import QColor, QPainter, QPen
from PySide6.QtWidgets import (
    QComboBox, QGridLayout, QHBoxLayout, QInputDialog, QLabel, QListWidget, QPushButton,
    QScrollArea, QSlider, QSplitter, QVBoxLayout, QWidget,
)

from ..model.map_document import MapDocument
from ..model.scene_timeline import (
    SCENE_ACTOR_KINDS, SCENE_CLIP_KINDS, SCENE_TRACK_KINDS, add_actor, add_clip,
    add_marker, add_track, duplicate_clip, evaluate_preview, fit_duration, move_clip,
    new_scene, remove_clip, remove_marker, rename_marker, validate_scene,
)
from ..model.types import JsonValue
from .widgets import StructuredInspector, set_path


class TimelineWidget(QWidget):
    """Scrollable, draggable scene timeline; editing remains document-owned."""

    clip_selected = Signal(int, int)
    clip_moved = Signal(int, int, int)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.scene: dict[str, JsonValue] | None = None
        self.selected: tuple[int, int] | None = None
        self.pixels_per_tick = 2.0
        self.row_height = 30
        self.header_height = 26
        self.left_width = 170
        self._drag: tuple[int, int, int, int] | None = None
        self.setMinimumHeight(130)
        self.setMouseTracking(True)

    def set_scene(self, scene: dict[str, JsonValue] | None, selected: tuple[int, int] | None = None) -> None:
        self.scene = scene
        self.selected = selected
        duration = int(scene.get("durationTicks", 1)) if scene else 1
        rows = len(scene.get("tracks", [])) if scene and isinstance(scene.get("tracks"), list) else 0
        self.setMinimumSize(max(640, self.left_width + int(duration * self.pixels_per_tick) + 40),
                            max(130, self.header_height + rows * self.row_height + 12))
        self.update()

    def set_zoom(self, pixels_per_tick: float) -> None:
        self.pixels_per_tick = max(0.5, min(8.0, pixels_per_tick))
        self.set_scene(self.scene, self.selected)

    def tick_at(self, x: int) -> int:
        return max(0, round((x - self.left_width) / self.pixels_per_tick))

    def x_for_tick(self, tick: int) -> int:
        return self.left_width + round(max(0, tick) * self.pixels_per_tick)

    def paintEvent(self, unused_event: object) -> None:
        del unused_event
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor("#20252b"))
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, False)
        if not self.scene:
            painter.setPen(QColor("#aeb8c4")); painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "No scene selected")
            painter.end(); return
        duration = int(self.scene.get("durationTicks", 1))
        painter.setPen(QPen(QColor("#6f7c88"), 1))
        step = max(1, 10 if self.pixels_per_tick < 2 else 5)
        for tick in range(0, duration + 1, step):
            x = self.x_for_tick(tick)
            painter.drawLine(x, 0, x, self.height()); painter.drawText(x + 2, 16, str(tick))
        tracks = self.scene.get("tracks", [])
        if isinstance(tracks, list):
            for track_index, track in enumerate(tracks):
                y = self.header_height + track_index * self.row_height
                painter.fillRect(0, y, self.width(), self.row_height - 1,
                                 QColor("#293139") if track_index % 2 == 0 else QColor("#252c33"))
                if isinstance(track, dict):
                    name = str(track.get("kind", "track"))
                    if track.get("actorSlot"): name += f" / {track.get('actorSlot')}"
                    painter.setPen(QColor("#d9e1e8")); painter.drawText(8, y + 19, name)
                    clips = track.get("clips", [])
                    if isinstance(clips, list):
                        for clip_index, clip in enumerate(clips):
                            if not isinstance(clip, dict): continue
                            start = int(clip.get("startTick", 0)); duration_ticks = int(clip.get("durationTicks", 0))
                            rect = (self.x_for_tick(start), y + 5,
                                    max(8, round(max(1, duration_ticks) * self.pixels_per_tick)), self.row_height - 10)
                            color = QColor("#668fd1") if self.selected == (track_index, clip_index) else QColor("#49657d")
                            painter.fillRect(*rect, color); painter.setPen(QColor("#f3f6f8")); painter.drawRect(*rect)
                            painter.drawText(rect[0] + 4, rect[1] + 16, str(clip.get("kind", "clip")))
        for marker in self.scene.get("markers", []) if isinstance(self.scene.get("markers"), list) else []:
            if isinstance(marker, dict):
                x = self.x_for_tick(int(marker.get("tick", 0)))
                painter.setPen(QPen(QColor("#f0b35b"), 2)); painter.drawLine(x, 0, x, self.height())
                painter.drawText(x + 3, self.height() - 4, str(marker.get("name", "marker")))
        painter.end()

    def mousePressEvent(self, event: object) -> None:
        if not self.scene or not hasattr(event, "position") or event.button() != Qt.MouseButton.LeftButton:
            return
        point = event.position().toPoint()  # type: ignore[attr-defined]
        if point.y() < self.header_height: return
        track_index = (point.y() - self.header_height) // self.row_height
        tracks = self.scene.get("tracks", [])
        if not isinstance(tracks, list) or track_index < 0 or track_index >= len(tracks) or not isinstance(tracks[track_index], dict):
            return
        clips = tracks[track_index].get("clips", [])
        if not isinstance(clips, list): return
        for clip_index, clip in enumerate(clips):
            if not isinstance(clip, dict): continue
            start = self.x_for_tick(int(clip.get("startTick", 0)))
            end = self.x_for_tick(int(clip.get("startTick", 0)) + max(1, int(clip.get("durationTicks", 0))))
            if start <= point.x() <= max(start + 8, end):
                self.selected = (track_index, clip_index); self.clip_selected.emit(track_index, clip_index)
                self._drag = (track_index, clip_index, point.x(), int(clip.get("startTick", 0)))
                self.update(); return

    def mouseMoveEvent(self, event: object) -> None:
        if self._drag and hasattr(event, "position"): self.update()

    def mouseReleaseEvent(self, event: object) -> None:
        if self._drag and hasattr(event, "position"):
            track, clip, start_x, original = self._drag
            point = event.position().toPoint()  # type: ignore[attr-defined]
            tick = original + round((point.x() - start_x) / self.pixels_per_tick)
            self.clip_moved.emit(track, clip, max(0, tick))
        self._drag = None


class ScenePreviewWidget(QWidget):
    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent); self.scene: dict[str, JsonValue] | None = None; self.tick = 0; self.setMinimumHeight(110)

    def set_state(self, scene: dict[str, JsonValue] | None, tick: int) -> None:
        self.scene = scene; self.tick = tick; self.update()

    def paintEvent(self, unused_event: object) -> None:
        del unused_event
        painter = QPainter(self); painter.fillRect(self.rect(), QColor("#15191d"))
        if self.scene:
            state = evaluate_preview(self.scene, self.tick); actors = state.get("actors", [])
            if isinstance(actors, list):
                for index, actor in enumerate(actors):
                    if not isinstance(actor, dict): continue
                    position = actor.get("position", {}); x = int(position.get("x", 0)) if isinstance(position, dict) else 0; y = int(position.get("y", 0)) if isinstance(position, dict) else 0
                    point = QPoint(self.width() // 2 + x, self.height() // 2 + y)
                    color = QColor("#72b7f2") if actor.get("kind") == "player" else QColor("#e68181")
                    painter.setBrush(color); painter.setPen(Qt.PenStyle.NoPen); painter.drawEllipse(point, 8, 8)
                    painter.setPen(QColor("#e8edf2")); painter.drawText(point + QPoint(12, 4), str(actor.get("slotId", index)))
        painter.setPen(QColor("#aeb8c4")); painter.drawText(8, 18, f"Preview: {self.tick} ticks"); painter.end()


class SceneEditorWidget(QWidget):
    changed = Signal()
    diagnostics_changed = Signal(object)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent); self.document: MapDocument | None = None; self.selected_clip: tuple[int, int] | None = None; self.selected_marker = -1
        self.scenes = QListWidget(); self.scenes.currentRowChanged.connect(self._scene_changed)
        self.new_button = QPushButton("New Scene"); self.duplicate_button = QPushButton("Duplicate"); self.delete_button = QPushButton("Delete")
        self.new_button.clicked.connect(self._new_scene); self.duplicate_button.clicked.connect(self._duplicate_scene); self.delete_button.clicked.connect(self._delete_scene)
        scene_buttons = QGridLayout()
        for index, button in enumerate((self.new_button, self.duplicate_button, self.delete_button)):
            scene_buttons.addWidget(button, index // 2, index % 2)
        left = QVBoxLayout(); left.addWidget(QLabel("Scenes")); left.addWidget(self.scenes, 1); left.addLayout(scene_buttons)
        left_widget = QWidget(); left_widget.setLayout(left)

        self.inspector = StructuredInspector(); self.inspector.changed.connect(self._edit_field)
        self.timeline = TimelineWidget(); self.timeline.clip_selected.connect(self._clip_selected); self.timeline.clip_moved.connect(self._clip_moved)
        self.timeline_scroll = QScrollArea(); self.timeline_scroll.setWidgetResizable(False); self.timeline_scroll.setWidget(self.timeline)
        self.timeline_zoom = QSlider(Qt.Orientation.Horizontal); self.timeline_zoom.setRange(5, 80); self.timeline_zoom.setValue(20); self.timeline_zoom.valueChanged.connect(lambda value: self.timeline.set_zoom(value / 10))
        self.playhead = QSlider(Qt.Orientation.Horizontal); self.playhead.setRange(0, 1); self.playhead.valueChanged.connect(self._playhead_changed)
        self.preview = ScenePreviewWidget(); self.status = QLabel("No scene selected")
        self.play_button = QPushButton("Play"); self.restart_button = QPushButton("Restart"); self.play_button.clicked.connect(self._toggle_play); self.restart_button.clicked.connect(self._restart)
        self.play_timer = QTimer(self); self.play_timer.setInterval(33); self.play_timer.timeout.connect(self._advance_playhead)
        self.track_selector = QComboBox(); self.track_selector.currentIndexChanged.connect(self._track_changed)
        self.clip_kind = QComboBox()
        self.add_clip_button = QPushButton("Add Clip"); self.duplicate_clip_button = QPushButton("Duplicate Clip"); self.remove_clip_button = QPushButton("Remove Clip"); self.add_track_button = QPushButton("Add Track")
        self.add_clip_button.clicked.connect(self._add_clip); self.duplicate_clip_button.clicked.connect(self._duplicate_clip); self.remove_clip_button.clicked.connect(self._remove_clip); self.add_track_button.clicked.connect(self._add_track)
        controls = QGridLayout()
        for index, widget in enumerate((self.track_selector, self.clip_kind, self.add_clip_button, self.duplicate_clip_button, self.remove_clip_button, self.add_track_button)):
            controls.addWidget(widget, index // 3, index % 3)
        self.add_marker_button = QPushButton("Add Marker"); self.rename_marker_button = QPushButton("Rename Marker"); self.remove_marker_button = QPushButton("Remove Marker"); self.fit_button = QPushButton("Fit Duration"); self.activation_button = QPushButton("Add Activation")
        self.add_marker_button.clicked.connect(self._add_marker); self.rename_marker_button.clicked.connect(self._rename_marker); self.remove_marker_button.clicked.connect(self._remove_marker); self.fit_button.clicked.connect(self._fit_duration); self.activation_button.clicked.connect(self._add_activation)
        marker_controls = QGridLayout()
        for index, widget in enumerate((self.add_marker_button, self.rename_marker_button, self.remove_marker_button, self.fit_button, self.activation_button)):
            marker_controls.addWidget(widget, index // 3, index % 3)
        self.markers = QListWidget(); self.markers.currentRowChanged.connect(self._marker_selected)
        self.actors = QListWidget(); self.add_actor_button = QPushButton("Add Actor"); self.remove_actor_button = QPushButton("Remove Actor"); self.add_actor_button.clicked.connect(self._add_actor); self.remove_actor_button.clicked.connect(self._remove_actor)
        actor_controls = QHBoxLayout(); actor_controls.addWidget(self.add_actor_button); actor_controls.addWidget(self.remove_actor_button)
        playback = QHBoxLayout(); playback.addWidget(self.play_button); playback.addWidget(self.restart_button); playback.addWidget(QLabel("Timeline zoom")); playback.addWidget(self.timeline_zoom); playback.addWidget(QLabel("Playhead")); playback.addWidget(self.playhead)
        center = QVBoxLayout(); center.addWidget(self.inspector, 2); center.addWidget(QLabel("Actors")); center.addWidget(self.actors); center.addLayout(actor_controls); center.addWidget(QLabel("Timeline")); center.addLayout(controls); center.addWidget(self.timeline_scroll, 2); center.addLayout(playback); center.addWidget(QLabel("Markers")); center.addWidget(self.markers); center.addLayout(marker_controls); center.addWidget(self.preview, 1); center.addWidget(self.status)
        center_widget = QWidget(); center_widget.setLayout(center)
        splitter = QSplitter(Qt.Orientation.Horizontal); splitter.setHandleWidth(8); splitter.setChildrenCollapsible(True)
        splitter.addWidget(left_widget); splitter.addWidget(center_widget); splitter.setStretchFactor(1, 1); splitter.setSizes([220, 700])
        layout = QHBoxLayout(self); layout.addWidget(splitter)

    def set_document(self, document: MapDocument | None) -> None:
        self.document = document; self.refresh()

    def refresh(self) -> None:
        selected_id = self._current().get("id") if self._current() else None
        self.scenes.blockSignals(True); self.scenes.clear()
        if self.document:
            for scene in self.document.data.get("scenes", []) if isinstance(self.document.data.get("scenes"), list) else []:
                if isinstance(scene, dict): self.scenes.addItem(str(scene.get("id", "scene")))
        if selected_id:
            rows = self.scenes.findItems(str(selected_id), Qt.MatchFlag.MatchExactly)
            if rows: self.scenes.setCurrentItem(rows[0])
        elif self.scenes.count(): self.scenes.setCurrentRow(0)
        self.scenes.blockSignals(False); self._scene_changed(self.scenes.currentRow())

    def _current(self) -> dict[str, JsonValue] | None:
        if not self.document or self.scenes.currentRow() < 0: return None
        values = self.document.data.get("scenes", [])
        return values[self.scenes.currentRow()] if isinstance(values, list) and self.scenes.currentRow() < len(values) and isinstance(values[self.scenes.currentRow()], dict) else None

    def _scene_changed(self, row: int) -> None:
        del row; scene = self._current(); self.selected_clip = None; self.selected_marker = -1
        if scene is None:
            self.inspector.clear("No scene selected"); self.timeline.set_scene(None); self.markers.clear(); self.actors.clear(); self.status.setText("No scene selected"); return
        self.inspector.set_object(str(scene.get("id", "Scene")), scene); duration = max(1, int(scene.get("durationTicks", 1))); self.playhead.setRange(0, duration); self.playhead.setValue(0)
        self.actors.clear(); [self.actors.addItem(f"{actor.get('slotId', '')} ({actor.get('kind', 'player')})") for actor in scene.get("actors", []) if isinstance(actor, dict)] if isinstance(scene.get("actors"), list) else None
        self.track_selector.blockSignals(True); self.track_selector.clear();
        for index, track in enumerate(scene.get("tracks", []) if isinstance(scene.get("tracks"), list) else []):
            if isinstance(track, dict): self.track_selector.addItem(f"{index}: {track.get('kind', 'track')} {track.get('actorSlot', '')}", index)
        self.track_selector.blockSignals(False); self._track_changed(self.track_selector.currentIndex()); self.markers.clear()
        for marker in scene.get("markers", []) if isinstance(scene.get("markers"), list) else []:
            if isinstance(marker, dict): self.markers.addItem(f"{marker.get('name', 'marker')} @ {marker.get('tick', 0)}")
        self._refresh_timeline(); self.preview.set_state(scene, 0); issues = validate_scene(scene, self.document.width * self.document.tile_size, self.document.height * self.document.tile_size); self.diagnostics_changed.emit(issues); self.status.setText(f"Scene {scene.get('id', '')} — {len(issues)} diagnostic(s)")

    def _refresh_timeline(self) -> None: self.timeline.set_scene(self._current(), self.selected_clip)
    def _clip_selected(self, track: int, clip: int) -> None: self.selected_clip = (track, clip); self._refresh_timeline()

    def _track_changed(self, index: int) -> None:
        scene = self._current()
        tracks = scene.get("tracks", []) if scene else []
        if not isinstance(tracks, list) or index < 0 or index >= len(tracks):
            return
        track = tracks[index]
        kind = str(track.get("kind", "")) if isinstance(track, dict) else ""
        allowed = {
            "actor": ("move", "face", "emote", "hop"),
            "dialogue": ("dialogue",),
            "world": ("worldEvent",),
            "presentation": ("presentationEffect",),
        }.get(kind, ())
        current = self.clip_kind.currentText()
        self.clip_kind.blockSignals(True); self.clip_kind.clear(); self.clip_kind.addItems(allowed)
        if current in allowed:
            self.clip_kind.setCurrentText(current)
        self.clip_kind.blockSignals(False)

    def _clip_moved(self, track: int, clip: int, start: int) -> None:
        scene = self._current()
        if self.document and scene:
            self.document.mutate("Move Scene Clip", lambda: move_clip(scene, track, clip, start)); self.changed.emit(); self._scene_changed(self.scenes.currentRow())

    def _edit_field(self, path: str, value: object) -> None:
        scene = self._current()
        if self.document and scene:
            self.document.mutate("Edit Scene", lambda: set_path(scene, path, value)); self.changed.emit(); self._scene_changed(self.scenes.currentRow())

    def _new_scene(self) -> None:
        if not self.document: return
        count = len(self.document.data.get("scenes", [])) if isinstance(self.document.data.get("scenes"), list) else 0; scene = new_scene(f"scene.editor.{count + 1}")
        self.document.mutate("Create Scene", lambda: self.document.data.setdefault("scenes", []).append(scene)); self.refresh(); self.changed.emit()

    def _duplicate_scene(self) -> None:
        scene = self._current()
        if not self.document or scene is None: return
        duplicate = __import__("copy").deepcopy(scene); duplicate["id"] = f"{scene.get('id', 'scene')}.copy"; self.document.mutate("Duplicate Scene", lambda: self.document.data.setdefault("scenes", []).append(duplicate)); self.refresh(); self.changed.emit()

    def _delete_scene(self) -> None:
        if self.document and self.scenes.currentRow() >= 0:
            row = self.scenes.currentRow(); self.document.mutate("Delete Scene", lambda: self.document.data.get("scenes", []).pop(row)); self.refresh(); self.changed.emit()

    def _add_actor(self) -> None:
        scene = self._current()
        if not self.document or scene is None: return
        slot, accepted = QInputDialog.getText(self, "Add Scene Actor", "Slot ID:")
        if not accepted or not slot.strip(): return
        kind, accepted = QInputDialog.getItem(self, "Add Scene Actor", "Kind:", list(SCENE_ACTOR_KINDS), 1, False)
        if accepted:
            instance_id: int | None = None
            if kind in {"npc", "enemy"}:
                category = "npcs" if kind == "npc" else "enemies"
                values = self.document.data.get(category, [])
                choices = [f"{value.get('id')}: {value.get('definitionId', '')}"
                           for value in values if isinstance(value, dict) and isinstance(value.get("id"), int)] if isinstance(values, list) else []
                if not choices:
                    self.status.setText(f"Place an {kind} instance on the map first")
                    return
                chosen, accepted = QInputDialog.getItem(self, "Add Scene Actor", "Instance:", choices, 0, False)
                if not accepted:
                    return
                instance_id = int(chosen.split(":", 1)[0])
            try: self.document.mutate("Add Scene Actor", lambda: add_actor(scene, slot, kind, instance_id)); self.changed.emit(); self._scene_changed(self.scenes.currentRow())
            except ValueError as error: self.status.setText(str(error))

    def _remove_actor(self) -> None:
        scene = self._current(); row = self.actors.currentRow()
        if self.document and scene and row >= 0:
            self.document.mutate("Remove Scene Actor", lambda: scene.get("actors", []).pop(row)); self.changed.emit(); self._scene_changed(self.scenes.currentRow())

    def _add_track(self) -> None:
        scene = self._current()
        if not self.document or scene is None: return
        kind, accepted = QInputDialog.getItem(self, "Add Scene Track", "Kind:", list(SCENE_TRACK_KINDS), 0, False)
        if accepted:
            actor_slot = ""
            if kind == "actor":
                actors = [str(value.get("slotId", "")) for value in scene.get("actors", []) if isinstance(value, dict) and value.get("slotId")]
                if not actors:
                    self.status.setText("Add a scene actor before adding an actor track")
                    return
                actor_slot, accepted = QInputDialog.getItem(self, "Add Scene Track", "Actor:", actors, 0, False)
                if not accepted:
                    return
            self.document.mutate("Add Scene Track", lambda: add_track(scene, kind, actor_slot)); self.changed.emit(); self._scene_changed(self.scenes.currentRow())

    def _add_clip(self) -> None:
        scene = self._current(); track = self.track_selector.currentData()
        if not self.document or scene is None or not isinstance(track, int): return
        start, accepted = QInputDialog.getInt(self, "Add Clip", "Start tick:", self.playhead.value(), 0, 2_147_483_647, 1)
        if not accepted: return
        duration, accepted = QInputDialog.getInt(self, "Add Clip", "Duration ticks:", 20, 0, 2_147_483_647, 1)
        if not accepted: return
        kind = str(self.clip_kind.currentText())
        tracks = scene.get("tracks", [])
        track_value = tracks[track] if isinstance(tracks, list) and track < len(tracks) else {}
        actor_slot = str(track_value.get("actorSlot", "")) if isinstance(track_value, dict) and kind in {"move", "face", "emote", "hop"} else ""
        clip: dict[str, JsonValue] = {"kind": kind, "actorSlot": actor_slot, "startTick": start,
                                      "durationTicks": duration if kind in {"move", "emote", "hop"} else 0,
                                      "targetPosition": {"x": 0, "y": 0}, "facing": "down",
                                      "emote": "surprise", "heightPixels": 0, "waitForCompletion": False}
        if kind == "dialogue":
            clip["dialogueId"] = self._first_definition_id("dialogues")
            clip["waitForCompletion"] = True
        elif kind == "presentationEffect":
            clip["effectId"] = self._first_definition_id("presentationEffects")
        elif kind == "worldEvent":
            clip["worldAction"] = {"kind": "setFlag", "target": "flag.scene"}
        self.document.mutate("Add Scene Clip", lambda: add_clip(scene, track, clip)); self.changed.emit(); self._scene_changed(self.scenes.currentRow())

    def _first_definition_id(self, category: str) -> str:
        workspace = getattr(self.inspector, "_workspace", None)
        if workspace:
            definitions = workspace.definitions(category)
            if definitions:
                return definitions[0].definition_id
        return ""

    def _duplicate_clip(self) -> None:
        scene = self._current()
        if self.document and scene and self.selected_clip:
            track, clip = self.selected_clip; self.document.mutate("Duplicate Scene Clip", lambda: duplicate_clip(scene, track, clip)); self.changed.emit(); self._scene_changed(self.scenes.currentRow())

    def _remove_clip(self) -> None:
        scene = self._current()
        if self.document and scene and self.selected_clip:
            track, clip = self.selected_clip; self.document.mutate("Remove Scene Clip", lambda: remove_clip(scene, track, clip)); self.selected_clip = None; self.changed.emit(); self._scene_changed(self.scenes.currentRow())

    def _add_marker(self) -> None:
        scene = self._current()
        if not self.document or scene is None: return
        name, accepted = QInputDialog.getText(self, "Add Marker", "Name:")
        if accepted and name.strip(): self.document.mutate("Add Scene Marker", lambda: add_marker(scene, name, self.playhead.value())); self.changed.emit(); self._scene_changed(self.scenes.currentRow())

    def _marker_selected(self, row: int) -> None:
        self.selected_marker = row; scene = self._current(); markers = scene.get("markers", []) if scene else []
        if isinstance(markers, list) and 0 <= row < len(markers) and isinstance(markers[row], dict): self.playhead.setValue(int(markers[row].get("tick", 0)))

    def _rename_marker(self) -> None:
        scene = self._current(); markers = scene.get("markers", []) if scene else []
        if not self.document or not scene or self.selected_marker < 0 or not isinstance(markers, list) or self.selected_marker >= len(markers): return
        old = markers[self.selected_marker]; name, accepted = QInputDialog.getText(self, "Rename Marker", "Name:", text=str(old.get("name", "marker")) if isinstance(old, dict) else "marker")
        if accepted and name.strip(): self.document.mutate("Rename Scene Marker", lambda: rename_marker(scene, self.selected_marker, name)); self.changed.emit(); self._scene_changed(self.scenes.currentRow())

    def _remove_marker(self) -> None:
        scene = self._current()
        if self.document and scene and self.selected_marker >= 0:
            self.document.mutate("Remove Scene Marker", lambda: remove_marker(scene, self.selected_marker)); self.selected_marker = -1; self.changed.emit(); self._scene_changed(self.scenes.currentRow())

    def _fit_duration(self) -> None:
        scene = self._current()
        if self.document and scene: self.document.mutate("Fit Scene Duration", lambda: fit_duration(scene)); self.changed.emit(); self._scene_changed(self.scenes.currentRow())

    def _add_activation(self) -> None:
        scene = self._current()
        if not self.document or scene is None:
            return
        scene_id = str(scene.get("id", ""))
        rules = self.document.data.setdefault("worldRules", [])
        if not isinstance(rules, list):
            self.status.setText("Map world rules are not an array")
            return
        existing_ids = {str(value.get("id")) for value in rules if isinstance(value, dict)}
        number = len(rules) + 1
        rule_id = f"rule.scene.{scene_id}.{number}"
        while rule_id in existing_ids:
            number += 1
            rule_id = f"rule.scene.{scene_id}.{number}"
        rule = {"id": rule_id, "once": True, "trigger": {"kind": "mapEntered"},
                "conditions": [], "actions": [{"kind": "startScene", "target": scene_id}]}
        self.document.mutate("Add Scene Activation", lambda: rules.append(rule))
        self.changed.emit()
        self.status.setText(f"Activation added for {scene_id}")

    def _playhead_changed(self, value: int) -> None: self.preview.set_state(self._current(), value)
    def _toggle_play(self) -> None:
        if self.play_timer.isActive(): self.play_timer.stop(); self.play_button.setText("Play")
        else: self.play_timer.start(); self.play_button.setText("Pause")
    def _restart(self) -> None: self.playhead.setValue(0); self.play_timer.stop(); self.play_button.setText("Play")
    def _advance_playhead(self) -> None:
        if self.playhead.value() >= self.playhead.maximum(): self._restart()
        else: self.playhead.setValue(self.playhead.value() + 1)
