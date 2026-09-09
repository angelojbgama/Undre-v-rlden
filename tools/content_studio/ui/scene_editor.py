from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import QHBoxLayout, QInputDialog, QLabel, QListWidget, QPushButton, QSlider, QTableWidget, QTableWidgetItem, QVBoxLayout, QWidget

from ..model.map_document import MapDocument
from .widgets import StructuredInspector


class SceneEditorWidget(QWidget):
    changed = Signal()

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.document: MapDocument | None = None
        self.scenes = QListWidget()
        self.scenes.currentRowChanged.connect(self._scene_changed)
        self.new_button = QPushButton("New Scene")
        self.duplicate_button = QPushButton("Duplicate")
        self.delete_button = QPushButton("Delete")
        self.new_button.clicked.connect(self._new_scene)
        self.duplicate_button.clicked.connect(self._duplicate_scene)
        self.delete_button.clicked.connect(self._delete_scene)
        scene_buttons = QHBoxLayout()
        for button in (self.new_button, self.duplicate_button, self.delete_button):
            scene_buttons.addWidget(button)
        left = QVBoxLayout()
        left.addWidget(QLabel("Scenes"))
        left.addWidget(self.scenes, 1)
        left.addLayout(scene_buttons)
        self.inspector = StructuredInspector()
        self.inspector.changed.connect(self._edit_field)
        self.timeline = QTableWidget(0, 4)
        self.timeline.setHorizontalHeaderLabels(["Track", "Kind", "Start", "Duration"])
        self.playhead = QSlider(Qt.Orientation.Horizontal)
        self.playhead.setRange(0, 999999)
        self.playhead.valueChanged.connect(lambda value: self.status.setText(f"Playhead: {value} ticks"))
        self.status = QLabel("No scene selected")
        center = QVBoxLayout()
        center.addWidget(self.inspector, 2)
        center.addWidget(QLabel("Timeline"))
        center.addWidget(self.timeline, 1)
        center.addWidget(self.playhead)
        center.addWidget(self.status)
        layout = QHBoxLayout(self)
        left_widget = QWidget(); left_widget.setLayout(left); left_widget.setMinimumWidth(180)
        center_widget = QWidget(); center_widget.setLayout(center)
        layout.addWidget(left_widget); layout.addWidget(center_widget, 1)

    def set_document(self, document: MapDocument | None) -> None:
        self.document = document
        self.refresh()

    def refresh(self) -> None:
        self.scenes.blockSignals(True)
        self.scenes.clear()
        if not self.document:
            self.inspector.clear()
            self.timeline.setRowCount(0)
            self.status.setText("No scene selected")
            self.scenes.blockSignals(False)
            return
        values = self.document.data.get("scenes", [])
        if isinstance(values, list):
            for value in values:
                if isinstance(value, dict):
                    self.scenes.addItem(str(value.get("id", "scene")))
        self.scenes.blockSignals(False)
        if self.scenes.count():
            self.scenes.setCurrentRow(min(self.scenes.currentRow() if self.scenes.currentRow() >= 0 else 0, self.scenes.count() - 1))
        else:
            self.inspector.clear("No scene selected")
            self.timeline.setRowCount(0)

    def _current(self) -> dict | None:
        if not self.document or self.scenes.currentRow() < 0:
            return None
        values = self.document.data.get("scenes", [])
        return values[self.scenes.currentRow()] if isinstance(values, list) and self.scenes.currentRow() < len(values) and isinstance(values[self.scenes.currentRow()], dict) else None

    def _scene_changed(self, row: int) -> None:
        scene = self._current()
        if scene is None:
            self.inspector.clear("No scene selected")
            self.timeline.setRowCount(0)
            return
        self.inspector.set_object(str(scene.get("id", "Scene")), scene)
        self.playhead.setMaximum(max(1, int(scene.get("durationTicks", 1))))
        self._refresh_timeline(scene)

    def _refresh_timeline(self, scene: dict) -> None:
        tracks = scene.get("tracks", [])
        rows = []
        if isinstance(tracks, list):
            for track in tracks:
                if not isinstance(track, dict):
                    continue
                for clip in track.get("clips", []) if isinstance(track.get("clips", []), list) else []:
                    if isinstance(clip, dict):
                        rows.append((track.get("actorSlot", track.get("kind", "track")), clip.get("kind", "clip"), clip.get("startTick", 0), clip.get("durationTicks", 0)))
        self.timeline.setRowCount(len(rows))
        for row, values in enumerate(rows):
            for column, value in enumerate(values):
                self.timeline.setItem(row, column, QTableWidgetItem(str(value)))

    def _edit_field(self, path: str, value: object) -> None:
        scene = self._current()
        if scene is None or not self.document:
            return
        from .widgets import set_path
        clean = path
        self.document.mutate("Edit Scene", lambda: set_path(scene, clean, value))
        self.changed.emit()
        self._scene_changed(self.scenes.currentRow())

    def _new_scene(self) -> None:
        if not self.document:
            return
        count = len(self.document.data.get("scenes", [])) if isinstance(self.document.data.get("scenes", []), list) else 0
        scene = {"id": f"scene.editor.{count + 1}", "durationTicks": 180, "actors": [{"slotId": "player", "kind": "player", "instanceId": 0}], "tracks": [{"kind": "actor", "actorSlot": "player", "clips": []}, {"kind": "dialogue", "actorSlot": "", "clips": []}, {"kind": "world", "actorSlot": "", "clips": []}, {"kind": "presentation", "actorSlot": "", "clips": []}], "markers": []}
        self.document.mutate("Create Scene", lambda: self.document.data.setdefault("scenes", []).append(scene))
        self.refresh(); self.changed.emit()

    def _duplicate_scene(self) -> None:
        scene = self._current()
        if not self.document or scene is None:
            return
        from copy import deepcopy
        duplicate = deepcopy(scene)
        duplicate["id"] = f"{scene.get('id', 'scene')}.copy"
        self.document.mutate("Duplicate Scene", lambda: self.document.data.setdefault("scenes", []).append(duplicate))
        self.refresh(); self.changed.emit()

    def _delete_scene(self) -> None:
        if not self.document or self.scenes.currentRow() < 0:
            return
        row = self.scenes.currentRow()
        self.document.mutate("Delete Scene", lambda: self.document.data.get("scenes", []).pop(row))
        self.refresh(); self.changed.emit()

