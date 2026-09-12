from __future__ import annotations

from dataclasses import dataclass

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition, JsonValue


@dataclass(frozen=True, slots=True)
class PlayerAuthoringRequest:
    player_id: str
    display_name: str
    progression_id: str
    idle_down: str
    idle_up: str
    idle_side: str
    walk_down: str
    walk_up: str
    walk_side: str
    hurt_down: str = ""
    hurt_up: str = ""
    hurt_side: str = ""
    sword_down: str = ""
    sword_up: str = ""
    sword_side: str = ""
    bow_down: str = ""
    bow_up: str = ""
    bow_side: str = ""


class PlayerAuthoringService:
    """Create/edit authored Player profiles without migrating runtime composition yet."""

    def create(self, workspace: ContentWorkspace,
               request: PlayerAuthoringRequest) -> ContentDefinition:
        return self._write(workspace, request, editing=False)

    def update(self, workspace: ContentWorkspace,
               request: PlayerAuthoringRequest) -> ContentDefinition:
        return self._write(workspace, request, editing=True)

    @staticmethod
    def _selection(request: PlayerAuthoringRequest, prefix: str) -> tuple[str, str, str]:
        return (
            str(getattr(request, f"{prefix}_down")).strip(),
            str(getattr(request, f"{prefix}_up")).strip(),
            str(getattr(request, f"{prefix}_side")).strip(),
        )

    @staticmethod
    def _directional(values: tuple[str, str, str]) -> dict[str, JsonValue]:
        down, up, side = values
        return {"down": down, "up": up, "side": side}

    @staticmethod
    def _validate_group(workspace: ContentWorkspace, label: str,
                        values: tuple[str, str, str], required: bool) -> bool:
        filled = [bool(value) for value in values]
        if required and not all(filled):
            raise ValueError(f"{label}: down/up/side animations are required")
        if not required and any(filled) and not all(filled):
            raise ValueError(
                f"{label}: fill down/up/side or leave the whole action empty")
        if not any(filled):
            return False
        for animation_id in values:
            if workspace.find("animations", animation_id) is None:
                raise ValueError(
                    f"{label}: animation does not exist: {animation_id}")
        return True

    def _write(self, workspace: ContentWorkspace, request: PlayerAuthoringRequest,
               editing: bool) -> ContentDefinition:
        player_id = request.player_id.strip()
        display_name = request.display_name.strip()
        progression_id = request.progression_id.strip()
        if not player_id or not display_name or not progression_id:
            raise ValueError(
                "player ID, display name and progression are required")

        idle = self._selection(request, "idle")
        walk = self._selection(request, "walk")
        hurt = self._selection(request, "hurt")
        sword = self._selection(request, "sword")
        bow = self._selection(request, "bow")
        self._validate_group(workspace, "idle", idle, True)
        self._validate_group(workspace, "walk", walk, True)
        has_hurt = self._validate_group(workspace, "hurt", hurt, False)
        has_sword = self._validate_group(workspace, "sword", sword, False)
        has_bow = self._validate_group(workspace, "bow", bow, False)

        if (progression_id != "progression.player.default" and
                workspace.find("playerProgressions", progression_id) is None):
            raise ValueError(
                f"player progression does not exist: {progression_id}")

        existing = workspace.find("players", player_id)
        if editing:
            if existing is None:
                raise ValueError(f"player does not exist: {player_id}")
            visual_id = (
                str(existing.data.get("visualSetId", ""))
                or f"visual.{player_id}")
        else:
            visual_id = f"visual.{player_id}"
            for category, definition_id in (
                    ("players", player_id),
                    ("playerVisuals", visual_id),
                    ("authoringDescriptors", player_id)):
                if workspace.find(category, definition_id) is not None:
                    raise ValueError(
                        f"definition already exists: {category}/{definition_id}")

        actions: list[JsonValue] = []
        if has_sword:
            actions.append(
                {"actionId": "sword", "clips": self._directional(sword)})
        if has_bow:
            actions.append(
                {"actionId": "bow", "clips": self._directional(bow)})

        visual_data: dict[str, JsonValue] = {
            "id": visual_id,
            "idle": self._directional(idle),
            "walk": self._directional(walk),
            "hurt": self._directional(hurt) if has_hurt else None,
            "actions": actions,
        }
        player_data: dict[str, JsonValue] = {
            "id": player_id,
            "visualSetId": visual_id,
            "progressionId": progression_id,
        }
        descriptor_data: dict[str, JsonValue] = {
            "definitionId": player_id,
            "displayName": display_name,
            "category": "player",
            "tags": ["player"],
        }
        bundle = [
            ("playerVisuals", visual_id, visual_data),
            ("players", player_id, player_data),
            ("authoringDescriptors", player_id, descriptor_data),
        ]
        values = (
            workspace.upsert_definition_bundle("Configure Player", bundle)
            if editing else
            workspace.create_definition_bundle("Create Player", bundle)
        )
        return next(value for value in values if value.category == "players")

    def request_for(self, workspace: ContentWorkspace,
                    definition: ContentDefinition) -> PlayerAuthoringRequest:
        if definition.category != "players":
            raise ValueError("definition is not a player")
        visual = workspace.find(
            "playerVisuals", str(definition.data.get("visualSetId", "")))
        if visual is None:
            raise ValueError("player visual definition is missing")
        descriptor = workspace.find(
            "authoringDescriptors", definition.definition_id)

        def refs(value: object) -> tuple[str, str, str]:
            data = value if isinstance(value, dict) else {}
            return (
                str(data.get("down", "")),
                str(data.get("up", "")),
                str(data.get("side", "")),
            )

        action_refs: dict[str, tuple[str, str, str]] = {}
        actions = visual.data.get("actions", [])
        if isinstance(actions, list):
            for action in actions:
                if not isinstance(action, dict):
                    continue
                action_id = str(action.get("actionId", ""))
                if action_id:
                    action_refs[action_id] = refs(action.get("clips"))

        idle = refs(visual.data.get("idle"))
        walk = refs(visual.data.get("walk"))
        hurt = refs(visual.data.get("hurt"))
        sword = action_refs.get("sword", ("", "", ""))
        bow = action_refs.get("bow", ("", "", ""))
        return PlayerAuthoringRequest(
            player_id=definition.definition_id,
            display_name=(
                descriptor.display_name if descriptor is not None
                else definition.display_name),
            progression_id=str(
                definition.data.get(
                    "progressionId", "progression.player.default")),
            idle_down=idle[0], idle_up=idle[1], idle_side=idle[2],
            walk_down=walk[0], walk_up=walk[1], walk_side=walk[2],
            hurt_down=hurt[0], hurt_up=hurt[1], hurt_side=hurt[2],
            sword_down=sword[0], sword_up=sword[1], sword_side=sword[2],
            bow_down=bow[0], bow_up=bow[1], bow_side=bow[2],
        )

    def delete(self, workspace: ContentWorkspace,
               definition: ContentDefinition) -> None:
        if definition.category != "players":
            raise ValueError("definition is not a player")
        visual_id = str(definition.data.get("visualSetId", ""))
        descriptor = workspace.find(
            "authoringDescriptors", definition.definition_id)
        visual = workspace.find("playerVisuals", visual_id)
        if descriptor is not None and descriptor.origin == "project":
            workspace.delete_definition(descriptor)
        if visual is not None and visual.origin == "project":
            workspace.delete_definition(visual)
        workspace.delete_definition(definition)
