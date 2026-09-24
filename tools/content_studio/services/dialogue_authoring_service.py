"""First-class dialogue authoring on top of the ContentWorkspace.

Mirrors the C++ ``ContentValidator`` rules for dialogues:

- id lives in the ``dialogue.`` namespace;
- at least one node and an entry node that exists;
- node ids are non-empty and unique; ``nextNodeId`` and choice targets
  must reference nodes of the same dialogue;
- choice conditions need a non-empty flag id;
- choice actions need a non-empty target, and ``startQuest`` /
  ``openShop`` targets must exist in quests / shops.

Usage guards: deleting a dialogue referenced by an NPC (workspace or
builtin) is blocked. Quests referenced by choices are reported so the
author sees the dialogue→quest links.
"""

from __future__ import annotations

import copy
import re

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition

_DIALOGUE_ID_PATTERN = re.compile(r"^[A-Za-z0-9_.]+$")

ACTION_KINDS = ("setFlag", "clearFlag", "startQuest", "openShop")
CONDITION_KINDS = ("flagSet", "flagNotSet")

# Builtin read-only NPCs whose default dialogue would break on deletion.
_BUILTIN_NPC_DIALOGUES: tuple[str, ...] = (
    "dialogue.guard.greeting",
    "dialogue.scholar.greeting",
    "dialogue.merchant.greeting",
)


class DialogueAuthoringService:
    def __init__(self, workspace: ContentWorkspace | None = None) -> None:
        self.workspace = workspace

    def set_context(self, workspace: ContentWorkspace | None) -> None:
        self.workspace = workspace

    def dialogues(self, query: str = "") -> tuple[ContentDefinition, ...]:
        return tuple(self._require_workspace().definitions("dialogues", query))

    def find(self, dialogue_id: str) -> ContentDefinition | None:
        return self._require_workspace().find("dialogues", dialogue_id)

    def quests(self) -> tuple[ContentDefinition, ...]:
        return tuple(self._require_workspace().definitions("quests"))

    def shops(self) -> tuple[ContentDefinition, ...]:
        return tuple(self._require_workspace().definitions("shops"))

    def npcs(self) -> tuple[ContentDefinition, ...]:
        return tuple(self._require_workspace().definitions("npcs"))

    def quests_referenced(self, dialogue_id: str) -> tuple[str, ...]:
        """Quests started by any choice action of this dialogue."""
        definition = self.find(dialogue_id)
        if definition is None:
            return ()
        quests: list[str] = []
        for node in definition.data.get("nodes", []) or []:
            if not isinstance(node, dict):
                continue
            for choice in node.get("choices", []) or []:
                if not isinstance(choice, dict):
                    continue
                for action in choice.get("actions", []) or []:
                    if (isinstance(action, dict)
                            and action.get("kind") == "startQuest"
                            and isinstance(action.get("targetId"), str)):
                        quests.append(action["targetId"])
        return tuple(dict.fromkeys(quests))

    def shops_referenced(self, dialogue_id: str) -> tuple[str, ...]:
        definition = self.find(dialogue_id)
        if definition is None:
            return ()
        shops: list[str] = []
        for node in definition.data.get("nodes", []) or []:
            if not isinstance(node, dict):
                continue
            for choice in node.get("choices", []) or []:
                if not isinstance(choice, dict):
                    continue
                for action in choice.get("actions", []) or []:
                    if (isinstance(action, dict)
                            and action.get("kind") == "openShop"
                            and isinstance(action.get("targetId"), str)):
                        shops.append(action["targetId"])
        return tuple(dict.fromkeys(shops))

    def referenced_by(self, dialogue_id: str) -> tuple[str, ...]:
        """NPCs (and builtin NPCs) whose default dialogue is this one."""
        workspace = self._require_workspace()
        referencing = [
            npc.definition_id for npc in workspace.definitions("npcs")
            if npc.data.get("defaultDialogueId") == dialogue_id
        ]
        if dialogue_id in _BUILTIN_NPC_DIALOGUES:
            referencing.append("<builtin npc>")
        return tuple(referencing)

    def create_dialogue(self, dialogue_id: str,
                        data: dict[str, object]) -> ContentDefinition:
        workspace = self._require_workspace()
        normalized_id = self._normalize_dialogue_id(dialogue_id)
        if workspace.find("dialogues", normalized_id):
            raise ValueError(f"dialogue already exists: {normalized_id}")
        payload = dict(data)
        authored_id = payload.get("id")
        if isinstance(authored_id, str) and authored_id and authored_id != normalized_id:
            raise ValueError(
                f"dialogue id does not match the created definition: "
                f"{authored_id} != {normalized_id}")
        payload["id"] = normalized_id
        normalized = self._validate(payload)
        workspace.create_definition_bundle(
            "Create Dialogue", [("dialogues", normalized_id, normalized)])
        result = workspace.find("dialogues", normalized_id)
        if result is None:
            raise RuntimeError("created dialogue could not be indexed")
        return result

    def configure(self, dialogue_id: str, data: dict[str, object]) -> None:
        workspace = self._require_workspace()
        definition = workspace.find("dialogues", dialogue_id)
        if definition is None:
            raise ValueError(f"unknown dialogue: {dialogue_id}")
        if not isinstance(data.get("id"), str) or data["id"] != dialogue_id:
            raise ValueError("dialogue id does not match the edited definition")
        normalized = self._validate(data)
        workspace.upsert_definition_bundle(
            "Edit Dialogue", [("dialogues", dialogue_id, normalized)])

    def delete(self, dialogue_id: str) -> None:
        workspace = self._require_workspace()
        definition = workspace.find("dialogues", dialogue_id)
        if definition is None:
            raise ValueError(f"unknown dialogue: {dialogue_id}")
        referencing = self.referenced_by(dialogue_id)
        if referencing:
            raise ValueError(
                "dialogue is referenced by NPCs: " + ", ".join(referencing))
        workspace.delete_definition(definition)

    def blank_dialogue(self, dialogue_id: str) -> dict[str, object]:
        normalized = self._normalize_dialogue_id(dialogue_id)
        entry_id = "entry"
        return {
            "id": normalized,
            "entryNodeId": entry_id,
            "nodes": [{
                "id": entry_id,
                "speaker": "",
                "pages": ["..."],
                "nextNodeId": "",
                "choices": [],
            }],
        }

    def _validate(self, data: dict[str, object]) -> dict[str, object]:
        workspace = self._require_workspace()
        dialogue_id = data.get("id")
        if not isinstance(dialogue_id, str) or not _DIALOGUE_ID_PATTERN.fullmatch(dialogue_id):
            raise ValueError(f"invalid dialogue id: {dialogue_id!r}")

        nodes = data.get("nodes")
        if not isinstance(nodes, list) or not nodes:
            raise ValueError("dialogue requires at least one node")

        entry_node_id = data.get("entryNodeId")
        node_ids: list[str] = []
        for index, node in enumerate(nodes):
            if not isinstance(node, dict):
                raise ValueError(f"node {index} is not an object")
            node_id = node.get("id")
            if not isinstance(node_id, str) or not node_id:
                raise ValueError(f"node {index} needs a non-empty id")
            if node_id in node_ids:
                raise ValueError(f"duplicate node id: {node_id}")
            node_ids.append(node_id)
        for index, node in enumerate(nodes):
            pages = node.get("pages")
            if not isinstance(pages, list) or any(
                    not isinstance(page, str) or not page for page in pages):
                raise ValueError(f"node {node_id} pages must be non-empty strings")
            next_node = node.get("nextNodeId")
            if not isinstance(next_node, str):
                raise ValueError(f"node {node_id} nextNodeId must be a string")
            if next_node and next_node not in node_ids:
                raise ValueError(
                    f"node {node_id} nextNodeId targets unknown node: {next_node}")
            choices = node.get("choices", [])
            if not isinstance(choices, list):
                raise ValueError(f"node {node_id} choices must be a list")
            for choice_index, choice in enumerate(choices):
                choice_path = f"{node_id}.choices[{choice_index}]"
                if not isinstance(choice, dict):
                    raise ValueError(f"{choice_path} is not an object")
                if not isinstance(choice.get("label"), str) or not choice["label"]:
                    raise ValueError(f"{choice_path} needs a label")
                target = choice.get("targetNodeId")
                if not isinstance(target, str):
                    raise ValueError(f"{choice_path} targetNodeId must be a string")
                if target and target not in node_ids:
                    raise ValueError(
                        f"{choice_path} targets unknown node: {target}")
                conditions = choice.get("conditions", [])
                if not isinstance(conditions, list):
                    raise ValueError(f"{choice_path} conditions must be a list")
                for condition in conditions:
                    if not isinstance(condition, dict):
                        raise ValueError(f"{choice_path} condition is not an object")
                    if condition.get("kind") not in CONDITION_KINDS:
                        raise ValueError(
                            f"{choice_path} condition kind must be one of "
                            f"{CONDITION_KINDS}")
                    flag_id = condition.get("flagId")
                    if not isinstance(flag_id, str) or not flag_id:
                        raise ValueError(
                            f"{choice_path} condition needs a non-empty flag id")
                for action in choice.get("actions", []) or []:
                    if not isinstance(action, dict):
                        raise ValueError(f"{choice_path} action is not an object")
                    kind = action.get("kind")
                    if kind not in ACTION_KINDS:
                        raise ValueError(
                            f"{choice_path} action kind must be one of {ACTION_KINDS}")
                    target = action.get("targetId")
                    if not isinstance(target, str) or not target:
                        raise ValueError(f"{choice_path} action needs a target")
                    if kind == "startQuest" and workspace.find("quests", target) is None:
                        raise ValueError(f"quest does not exist: {target}")
                    if kind == "openShop" and workspace.find("shops", target) is None:
                        raise ValueError(f"shop does not exist: {target}")

        if not isinstance(entry_node_id, str) or entry_node_id not in node_ids:
            raise ValueError(f"entry node does not exist: {entry_node_id!r}")

        return copy.deepcopy(data)

    def _require_workspace(self) -> ContentWorkspace:
        if self.workspace is None:
            raise ValueError("dialogue authoring requires an open content workspace")
        return self.workspace

    @staticmethod
    def _normalize_dialogue_id(dialogue_id: str) -> str:
        normalized = dialogue_id.strip()
        if not _DIALOGUE_ID_PATTERN.fullmatch(normalized):
            raise ValueError(f"invalid dialogue id: {dialogue_id!r}")
        if not normalized.startswith("dialogue."):
            normalized = f"dialogue.{normalized}"
        return normalized
