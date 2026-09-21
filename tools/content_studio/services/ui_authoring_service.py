"""Typed UI screen authoring on top of the ContentWorkspace (docs/UI_ENGINE.md, UI-3).

Mirrors the C++ ``ui`` registries and validation rules so authored screens
compile on the first try: unknown components/binding paths/actions, duplicate
node ids, meter rules and property/binding pairing are rejected at edit time.

Builtin screens (shipped inside the C++ runtime and exported by the
``ui_manifest --screens`` tool into ``assets/builtin_ui_screens.json``) are
listed read-only; ``override_builtin`` copies one into the workspace, which
shadows the builtin by id — the C++ overlay semantics, expressed as authoring.
"""

from __future__ import annotations

import copy
import json
from pathlib import Path

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition, JsonValue
from . import ui_registry

SCREEN_ID_PREFIX = "screen."
BUILTIN_SCREEN_ASSET = Path(__file__).resolve().parent.parent / "assets" / "builtin_ui_screens.json"


def _unset() -> object:
    return object()


UNSET = _unset()


class UiAuthoringService:
    """CRUD facade over the ``uiScreens`` category with C++-mirrored rules."""

    def __init__(self, workspace: ContentWorkspace | None = None,
                 builtin_pack: dict | None = None) -> None:
        self.workspace = workspace
        self.builtin_pack = builtin_pack if builtin_pack is not None else load_builtin_pack()

    def set_context(self, workspace: ContentWorkspace | None) -> None:
        self.workspace = workspace

    # -- screens -----------------------------------------------------------

    def screens(self, query: str = "") -> tuple[ContentDefinition, ...]:
        return tuple(self._require_workspace().definitions("uiScreens", query))

    def find(self, screen_id: str) -> ContentDefinition | None:
        return self._require_workspace().find("uiScreens", screen_id)

    def create_screen(self, screen_id: str, kind: str = "hud",
                      root_node_id: str | None = None) -> ContentDefinition:
        workspace = self._require_workspace()
        normalized_id = self._normalize_screen_id(screen_id)
        if kind not in ui_registry.SCREEN_KINDS:
            raise ValueError(f"unknown screen kind: {kind}")
        if workspace.find("uiScreens", normalized_id):
            raise ValueError(f"screen already exists: {normalized_id}")
        root_id = (root_node_id or f"{normalized_id}.root").strip()
        data: dict[str, JsonValue] = {
            "id": normalized_id,
            "kind": kind,
            "root": self._new_node(root_id, "group"),
        }
        workspace.create_definition("uiScreens", normalized_id)
        created = workspace.find("uiScreens", normalized_id)
        assert created is not None
        workspace.replace_definition(created, data)
        result = workspace.find("uiScreens", normalized_id)
        assert result is not None
        return result

    # -- builtin screens (read-only source, workspace overrides by id) ------

    def builtin_screens(self) -> list[dict]:
        return list(self.builtin_pack.get("uiScreens", []))

    def builtin_find(self, screen_id: str) -> dict | None:
        for screen in self.builtin_pack.get("uiScreens", []):
            if screen.get("id") == screen_id:
                return screen
        return None

    def is_builtin_only(self, screen_id: str) -> bool:
        """True when the id ships with the engine and has no workspace copy."""
        return self.find(screen_id) is None and self.builtin_find(screen_id) is not None

    def override_builtin(self, screen_id: str) -> ContentDefinition:
        """Copies a builtin screen into the workspace; the copy shadows it."""
        workspace = self._require_workspace()
        builtin = self.builtin_find(screen_id)
        if builtin is None:
            raise ValueError(f"screen is not a builtin screen: {screen_id}")
        existing = self.find(screen_id)
        if existing is not None:
            return existing
        created = workspace.create_definition("uiScreens", str(builtin["id"]))
        workspace.replace_definition(created, copy.deepcopy(builtin))
        result = self.find(screen_id)
        assert result is not None
        return result

    def delete_screen(self, screen_id: str) -> bool:
        workspace = self._require_workspace()
        definition = self.find(screen_id)
        if definition is None:
            return False
        workspace.delete_definition(definition)
        return True

    # -- nodes -------------------------------------------------------------

    def add_node(
        self,
        screen_id: str,
        node_id: str,
        component: str,
        *,
        parent_id: str | None = None,
        anchor: str = "topLeft",
        offset: tuple[int, int] = (0, 0),
    ) -> None:
        workspace = self._require_workspace()
        if component not in ui_registry.COMPONENTS:
            raise ValueError(f"unknown ui component: {component}")
        if anchor not in ui_registry.ANCHORS:
            raise ValueError(f"unknown ui anchor: {anchor}")
        data, definition = self._editable_screen(screen_id)
        if not node_id or not node_id.strip():
            raise ValueError("ui node id must not be empty")
        node_id = node_id.strip()
        parent = data["root"] if parent_id is None else self._find_node(data["root"], parent_id)
        if parent is None:
            raise ValueError(f"parent node not found: {parent_id}")
        if not ui_registry.is_container_component(str(parent["component"])):
            raise ValueError(f"parent {parent_id} is not a container component")
        if self._find_node(data["root"], node_id) is not None:
            raise ValueError(f"ui node id is duplicated within the screen: {node_id}")
        parent.setdefault("children", []).append(
            self._new_node(node_id, component, anchor=anchor, offset=offset))
        # Structural completeness is validate_screen()'s job: newly added nodes
        # stay editable before their meter/sprite/text configuration exists,
        # mirroring the Studio's "semantic errors block export, not editing".
        workspace.replace_definition(definition, data)

    def remove_node(self, screen_id: str, node_id: str) -> None:
        data, definition = self._editable_screen(screen_id)
        if data["root"].get("id") == node_id:
            raise ValueError("the root node cannot be removed")
        parent = self._find_parent(data["root"], node_id)
        if parent is None:
            raise ValueError(f"node not found: {node_id}")
        parent["children"] = [
            child for child in parent.get("children", []) if child.get("id") != node_id]
        workspace_replace(self.workspace, definition, data)

    def set_layout(
        self,
        screen_id: str,
        node_id: str,
        *,
        anchor: str | None = None,
        offset_x: int | None = None,
        offset_y: int | None = None,
        width: int | None | object = None,
        height: int | None | object = None,
        z: int | None = None,
        visible: bool | None = None,
    ) -> None:
        data, definition = self._editable_screen(screen_id)
        node = self._require_node(data, node_id)
        layout = node["layout"]
        if anchor is not None:
            if anchor not in ui_registry.ANCHORS:
                raise ValueError(f"unknown ui anchor: {anchor}")
            layout["anchor"] = anchor
        if offset_x is not None:
            layout["offsetX"] = offset_x
        if offset_y is not None:
            layout["offsetY"] = offset_y
        if width is not UNSET:
            self._apply_optional_size(layout, "width", width)
        if height is not UNSET:
            self._apply_optional_size(layout, "height", height)
        if z is not None:
            layout["z"] = z
        if visible is not None:
            layout["visible"] = visible
        workspace_replace(self.workspace, definition, data)

    # -- component properties ---------------------------------------------

    def set_component(self, screen_id: str, node_id: str, component: str) -> None:
        if component not in ui_registry.COMPONENTS:
            raise ValueError(f"unknown ui component: {component}")
        data, definition = self._editable_screen(screen_id)
        node = self._require_node(data, node_id)
        node["component"] = component
        workspace_replace(self.workspace, definition, data)

    def set_sprite(self, screen_id: str, node_id: str, sprite_id: str | None) -> None:
        workspace = self._require_workspace()
        data, definition = self._editable_screen(screen_id)
        node = self._require_node(data, node_id)
        if node["component"] != "image":
            raise ValueError("sprite belongs to the image component")
        if sprite_id is not None:
            self._require_definition(workspace, "staticSprites", sprite_id)
        self._assign_optional(node, "sprite", sprite_id)
        workspace_replace(self.workspace, definition, data)

    def set_animation(self, screen_id: str, node_id: str, animation_id: str | None) -> None:
        workspace = self._require_workspace()
        data, definition = self._editable_screen(screen_id)
        node = self._require_node(data, node_id)
        if node["component"] != "animatedImage":
            raise ValueError("animation belongs to the animatedImage component")
        if animation_id is not None:
            self._require_definition(workspace, "animations", animation_id)
        self._assign_optional(node, "animation", animation_id)
        workspace_replace(self.workspace, definition, data)

    def set_text(self, screen_id: str, node_id: str, text: str | None) -> None:
        data, definition = self._editable_screen(screen_id)
        node = self._require_node(data, node_id)
        if node["component"] != "text":
            raise ValueError("text belongs to the text component")
        if text is None:
            node.pop("text", None)
        else:
            node["text"] = text
        workspace_replace(self.workspace, definition, data)

    def set_meter(
        self,
        screen_id: str,
        node_id: str,
        *,
        mode: str | None = None,
        segment_value: int | None = None,
        sprites: dict[str, str] | None = None,
        spacing: int | None = None,
        empty_rect: dict[str, JsonValue] | None | object = None,
    ) -> None:
        workspace = self._require_workspace()
        data, definition = self._editable_screen(screen_id)
        node = self._require_node(data, node_id)
        if node["component"] != "meter":
            raise ValueError("meter configuration belongs to the meter component")
        meter = node.setdefault("meter", {"mode": "segmented", "segmentValue": 1,
                                          "sprites": {}, "spacing": 0})
        if mode is not None:
            if mode not in ui_registry.METER_MODES:
                raise ValueError(f"unknown ui meter mode: {mode}")
            meter["mode"] = mode
        if segment_value is not None:
            if segment_value <= 0:
                raise ValueError("meter segment value must be positive")
            meter["segmentValue"] = segment_value
        if spacing is not None:
            meter["spacing"] = spacing
        if sprites is not None:
            resolved: dict[str, str] = {}
            for role, sprite_id in sprites.items():
                if role not in ("fill", "full", "half", "empty"):
                    raise ValueError(f"unknown meter sprite role: {role}")
                self._require_definition(workspace, "staticSprites", str(sprite_id))
                resolved[role] = str(sprite_id)
            meter["sprites"] = resolved
        if empty_rect is not UNSET:
            if empty_rect is None:
                meter.pop("emptyRect", None)
            else:
                assert isinstance(empty_rect, dict)
                if int(empty_rect.get("width", 0)) <= 0 or int(empty_rect.get("height", 0)) <= 0:
                    raise ValueError("meter empty rect must have positive size")
                meter["emptyRect"] = empty_rect
        self._validate_meter(workspace, node)
        workspace_replace(self.workspace, definition, data)

    # -- bindings / states / actions --------------------------------------

    def bind_property(self, screen_id: str, node_id: str, property: str, source: str) -> None:
        data, definition = self._editable_screen(screen_id)
        node = self._require_node(data, node_id)
        if not ui_registry.component_accepts_property(str(node["component"]), property):
            raise ValueError(f"binding property is not valid for this component: {property}")
        if source not in ui_registry.BINDING_PATHS:
            raise ValueError(f"unknown ui binding path: {source}")
        bindings = node.setdefault("bindings", [])
        for binding in bindings:
            if binding.get("property") == property:
                raise ValueError(f"component property is bound more than once: {property}")
        bindings.append({"property": property, "source": source})
        workspace_replace(self.workspace, definition, data)

    def unbind_property(self, screen_id: str, node_id: str, property: str) -> None:
        data, definition = self._editable_screen(screen_id)
        node = self._require_node(data, node_id)
        bindings = node.get("bindings", [])
        node["bindings"] = [b for b in bindings if b.get("property") != property]
        workspace_replace(self.workspace, definition, data)

    def add_state(self, screen_id: str, node_id: str, state_id: str,
                  visual: dict[str, JsonValue],
                  condition: dict[str, JsonValue] | None = None) -> None:
        data, definition = self._editable_screen(screen_id)
        node = self._require_node(data, node_id)
        if not state_id or not state_id.strip():
            raise ValueError("ui state id must not be empty")
        state_id = state_id.strip()
        states = node.setdefault("states", [])
        for state in states:
            if state.get("id") == state_id:
                raise ValueError(f"ui state id is duplicated within the node: {state_id}")
        if not isinstance(visual, dict) or not visual:
            raise ValueError("ui state must change at least one visual property")
        if "sprite" in visual:
            self._require_definition(self._require_workspace(), "staticSprites",
                                     str(visual["sprite"]))
        if condition is not None:
            if condition.get("source") not in ui_registry.BINDING_PATHS:
                raise ValueError(f"unknown ui binding path: {condition.get('source')}")
            if condition.get("operator") not in ui_registry.CONDITION_OPERATORS:
                raise ValueError(
                    f"unknown ui condition operator: {condition.get('operator')}")
        entry: dict[str, JsonValue] = {"id": state_id, "visual": visual}
        if condition is not None:
            entry["condition"] = condition
        states.append(entry)
        workspace_replace(self.workspace, definition, data)

    def remove_state(self, screen_id: str, node_id: str, state_id: str) -> None:
        data, definition = self._editable_screen(screen_id)
        node = self._require_node(data, node_id)
        states = node.get("states", [])
        node["states"] = [s for s in states if s.get("id") != state_id]
        workspace_replace(self.workspace, definition, data)

    def add_action(self, screen_id: str, node_id: str, action: str,
                   event: str = "activate") -> None:
        data, definition = self._editable_screen(screen_id)
        node = self._require_node(data, node_id)
        if event not in ui_registry.ACTION_EVENTS:
            raise ValueError(f"ui action event is not supported: {event}")
        if action not in ui_registry.ACTIONS:
            raise ValueError(f"unknown ui action id: {action}")
        node.setdefault("actions", []).append({"event": event, "action": action})
        workspace_replace(self.workspace, definition, data)

    def remove_action(self, screen_id: str, node_id: str, action: str) -> None:
        data, definition = self._editable_screen(screen_id)
        node = self._require_node(data, node_id)
        node["actions"] = [a for a in node.get("actions", []) if a.get("action") != action]
        workspace_replace(self.workspace, definition, data)

    # -- validation mirror -------------------------------------------------

    def validate_screen(self, screen_id: str) -> list[str]:
        data, _ = self._editable_screen(screen_id)
        return self._validate_screen_data(self._require_workspace(), data)

    # -- internals ---------------------------------------------------------

    def _require_workspace(self) -> ContentWorkspace:
        if self.workspace is None:
            raise ValueError("content workspace is unavailable")
        return self.workspace

    def _editable_screen(self, screen_id: str) -> tuple[dict[str, JsonValue], ContentDefinition]:
        definition = self.find(screen_id)
        if definition is None:
            raise ValueError(f"screen not found: {screen_id}")
        return copy.deepcopy(definition.data), definition

    def _require_node(self, data: dict[str, JsonValue], node_id: str) -> dict[str, JsonValue]:
        node = self._find_node(data["root"], node_id)
        if node is None:
            raise ValueError(f"node not found: {node_id}")
        return node

    @staticmethod
    def _new_node(node_id: str, component: str, *, anchor: str = "topLeft",
                  offset: tuple[int, int] = (0, 0)) -> dict[str, JsonValue]:
        return {
            "id": node_id,
            "component": component,
            "layout": {"anchor": anchor, "offsetX": offset[0], "offsetY": offset[1], "z": 0},
            "children": [],
        }

    @staticmethod
    def _find_node(node: dict[str, JsonValue], node_id: str) -> dict[str, JsonValue] | None:
        if node.get("id") == node_id:
            return node
        for child in node.get("children", []):
            found = UiAuthoringService._find_node(child, node_id)
            if found is not None:
                return found
        return None

    @staticmethod
    def _find_parent(node: dict[str, JsonValue], node_id: str) -> dict[str, JsonValue] | None:
        for child in node.get("children", []):
            if child.get("id") == node_id:
                return node
            found = UiAuthoringService._find_parent(child, node_id)
            if found is not None:
                return found
        return None

    @staticmethod
    def _apply_optional_size(layout: dict[str, JsonValue], field: str,
                             value: int | None | object) -> None:
        if value is None:
            layout.pop(field, None)
        else:
            layout[field] = value

    @staticmethod
    def _assign_optional(node: dict[str, JsonValue], field: str, value: str | None) -> None:
        if value is None:
            node.pop(field, None)
        else:
            node[field] = value

    def _require_definition(self, workspace: ContentWorkspace, category: str,
                            definition_id: str) -> None:
        if workspace.find(category, definition_id) is not None:
            return
        # Builtin visuals resolve like the C++ overlay does: the workspace
        # shadows builtin definitions by id, so an override may reference the
        # engine-shipped sprites/animations.
        if category in ("visualImages", "staticSprites", "animations"):
            for definition in self.builtin_pack.get(category, []):
                if definition.get("id") == definition_id:
                    return
        raise ValueError(f"{category} definition does not exist: {definition_id}")

    def _validate_screen_data(self, workspace: ContentWorkspace,
                              data: dict[str, JsonValue]) -> list[str]:
        issues: list[str] = []
        seen_nodes: set[str] = set()
        root = data["root"]

        def visit(node: dict[str, JsonValue]) -> None:
            node_id = str(node.get("id", ""))
            if not node_id:
                issues.append("ui node id must not be empty")
            elif node_id in seen_nodes:
                issues.append(f"ui node id is duplicated within the screen: {node_id}")
            else:
                seen_nodes.add(node_id)
            component = str(node.get("component", ""))
            if component not in ui_registry.COMPONENTS:
                issues.append(f"unknown ui component: {component}")
                return
            if node.get("children") and not ui_registry.is_container_component(component):
                issues.append(f"node {node_id} has children but is not a container")
            if node.get("sprite") and component != "image":
                issues.append(f"node {node_id}: sprite belongs to the image component")
            if node.get("animation") and component != "animatedImage":
                issues.append(f"node {node_id}: animation belongs to the animatedImage component")
            if node.get("text") and component != "text":
                issues.append(f"node {node_id}: text belongs to the text component")
            if node.get("meter") and component != "meter":
                issues.append(f"node {node_id}: meter configuration belongs to the meter component")
            if component == "image" and not node.get("sprite"):
                issues.append(f"image node {node_id} requires a sprite")
            if component == "animatedImage" and not node.get("animation"):
                issues.append(f"animatedImage node {node_id} requires an animation")
            if component == "text":
                bound = any(b.get("property") == "text" for b in node.get("bindings", []))
                if not node.get("text") and not bound:
                    issues.append(f"text node {node_id} requires literal text or a text binding")
            if component == "meter":
                if not node.get("meter"):
                    issues.append(f"meter node {node_id} requires meter configuration")
                else:
                    issues.extend(self._validate_meter(workspace, node))
            properties: set[str] = set()
            for binding in node.get("bindings", []):
                prop = str(binding.get("property", ""))
                if not ui_registry.component_accepts_property(component, prop):
                    issues.append(f"node {node_id}: binding property {prop} is not valid")
                elif prop in properties:
                    issues.append(f"node {node_id}: property {prop} is bound more than once")
                else:
                    properties.add(prop)
                if binding.get("source") not in ui_registry.BINDING_PATHS:
                    issues.append(f"node {node_id}: unknown ui binding path {binding.get('source')}")
            state_ids: set[str] = set()
            for state in node.get("states", []):
                state_id = str(state.get("id", ""))
                if not state_id:
                    issues.append("ui state id must not be empty")
                elif state_id in state_ids:
                    issues.append(f"ui state id duplicated within the node: {state_id}")
                else:
                    state_ids.add(state_id)
                if not state.get("visual"):
                    issues.append(f"state {state_id} must change at least one visual property")
            for action in node.get("actions", []):
                if action.get("event") not in ui_registry.ACTION_EVENTS:
                    issues.append(f"node {node_id}: unsupported action event {action.get('event')}")
                if action.get("action") not in ui_registry.ACTIONS:
                    issues.append(f"node {node_id}: unknown ui action id {action.get('action')}")
            for child in node.get("children", []):
                visit(child)

        visit(root)
        if issues:
            raise ValueError("; ".join(issues))
        return []

    def _validate_meter(self, workspace: ContentWorkspace, node: dict[str, JsonValue]) -> list[str]:
        meter = node.get("meter")
        assert isinstance(meter, dict)
        node_id = str(node.get("id", ""))
        if int(meter.get("segmentValue", 0)) <= 0:
            raise ValueError(f"meter segment value must be positive: {node_id}")
        mode = str(meter.get("mode", "segmented"))
        if mode not in ui_registry.METER_MODES:
            raise ValueError(f"unknown ui meter mode: {mode}")
        sprites = meter.get("sprites", {})
        if mode == "segmented" and not sprites.get("full"):
            raise ValueError(f"segmented meter requires a full segment sprite: {node_id}")
        if mode != "segmented" and not sprites.get("fill"):
            raise ValueError(f"fill meter requires a fill sprite: {node_id}")
        for role, sprite_id in sprites.items():
            self._require_definition(workspace, "staticSprites", str(sprite_id))
        empty_rect = meter.get("emptyRect")
        if empty_rect is not None:
            if int(empty_rect.get("width", 0)) <= 0 or int(empty_rect.get("height", 0)) <= 0:
                raise ValueError(f"meter empty rect must have positive size: {node_id}")
        return []

    def _normalize_screen_id(self, screen_id: str) -> str:
        normalized = screen_id.strip()
        if not normalized:
            raise ValueError("screen id must not be empty")
        if not normalized.startswith(SCREEN_ID_PREFIX):
            normalized = SCREEN_ID_PREFIX + normalized
        return normalized


def workspace_replace(workspace: ContentWorkspace | None, definition: ContentDefinition,
                      data: dict[str, JsonValue]) -> None:
    if workspace is None:
        raise ValueError("content workspace is unavailable")
    workspace.replace_definition(definition, data)


def load_builtin_pack(path: Path | None = None) -> dict:
    """Loads the builtin UI pack exported by ``ui_manifest --screens``."""
    asset = path or BUILTIN_SCREEN_ASSET
    if not asset.is_file():
        return {}
    with open(asset, encoding="utf-8") as handle:
        return json.load(handle)
