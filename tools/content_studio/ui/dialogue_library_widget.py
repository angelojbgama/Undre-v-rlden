"""First-class dialogue authoring UI for the Content Studio.

Node/decision editor: pages, next-node chains, and choices with
conditions (flagSet/flagNotSet) and actions (setFlag, clearFlag,
startQuest, openShop) — validated through DialogueAuthoringService,
which mirrors the C++ ContentValidator.
"""

from __future__ import annotations

import copy

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMessageBox,
    QPushButton,
    QPlainTextEdit,
    QSplitter,
    QVBoxLayout,
    QWidget,
)

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition
from ..services.dialogue_authoring_service import (
    ACTION_KINDS,
    CONDITION_KINDS,
    DialogueAuthoringService,
)
from ..services.localization import Translator
from .icon_registry import icon
from .widgets import PayloadListWidget


class ConditionDialog(QDialog):
    """One dialogue condition (flagSet / flagNotSet on a flag id)."""

    def __init__(self, translator: Translator, data: dict | None = None,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.translate = translator
        self.setWindowTitle(self.translate("dialogue_condition_title"))

        self.kind = QComboBox(self)
        for kind in CONDITION_KINDS:
            self.kind.addItem(kind, kind)
        self.flag_id = QLineEdit(self)
        self.flag_id.setPlaceholderText("flag.quest.underworld.awakening.active")

        form = QFormLayout(self)
        form.addRow(self.translate("dialogue_field_kind"), self.kind)
        form.addRow(self.translate("dialogue_field_flag"), self.flag_id)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel,
            self)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        form.addRow(buttons)

        if isinstance(data, dict):
            index = self.kind.findData(str(data.get("kind", "flagSet")))
            self.kind.setCurrentIndex(max(index, 0))
            self.flag_id.setText(str(data.get("flagId") or ""))

    def value(self) -> dict[str, str]:
        return {"kind": self.kind.currentData(),
                "flagId": self.flag_id.text().strip()}


class ActionDialog(QDialog):
    """One dialogue action: setFlag/clearFlag (flag), startQuest (quest),
    openShop (shop). The target input adapts to the chosen kind."""

    def __init__(self, service: DialogueAuthoringService, translator: Translator,
                 data: dict | None = None, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.service = service
        self.translate = translator
        self.setWindowTitle(self.translate("dialogue_action_title"))

        self.kind = QComboBox(self)
        for kind in ACTION_KINDS:
            self.kind.addItem(kind, kind)

        self.flag_target = QLineEdit(self)
        self.quest_target = QComboBox(self)
        for quest in service.quests():
            self.quest_target.addItem(quest.definition_id, quest.definition_id)
        self.shop_target = QComboBox(self)
        for shop in service.shops():
            self.shop_target.addItem(shop.definition_id, shop.definition_id)

        self._flag_row = (self.flag_target, QLabel(self.translate("dialogue_field_flag"), self))
        self._quest_row = (self.quest_target, QLabel(self.translate("dialogue_field_quest"), self))
        self._shop_row = (self.shop_target, QLabel(self.translate("dialogue_field_shop"), self))

        form = QFormLayout(self)
        form.addRow(self.translate("dialogue_field_kind"), self.kind)
        for widget, label in (self._flag_row, self._quest_row, self._shop_row):
            form.addRow(label, widget)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel,
            self)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        form.addRow(buttons)

        self.kind.currentIndexChanged.connect(lambda _: self._adapt())
        if isinstance(data, dict):
            index = self.kind.findData(str(data.get("kind", "setFlag")))
            self.kind.setCurrentIndex(max(index, 0))
            target = str(data.get("targetId") or "")
            if self.kind.currentData() in ("setFlag", "clearFlag"):
                self.flag_target.setText(target)
            else:
                combo = self.quest_target if self.kind.currentData() == "startQuest" \
                    else self.shop_target
                found = combo.findData(target)
                if found >= 0:
                    combo.setCurrentIndex(found)
        self._adapt()

    def _adapt(self) -> None:
        kind = self.kind.currentData()
        self._flag_row[0].setVisible(kind in ("setFlag", "clearFlag"))
        self._flag_row[1].setVisible(kind in ("setFlag", "clearFlag"))
        self._quest_row[0].setVisible(kind == "startQuest")
        self._quest_row[1].setVisible(kind == "startQuest")
        self._shop_row[0].setVisible(kind == "openShop")
        self._shop_row[1].setVisible(kind == "openShop")

    def value(self) -> dict[str, str]:
        kind = self.kind.currentData()
        if kind in ("setFlag", "clearFlag"):
            target = self.flag_target.text().strip()
        elif kind == "startQuest":
            target = self.quest_target.currentData() or ""
        else:
            target = self.shop_target.currentData() or ""
        return {"kind": kind, "targetId": target}


class ChoiceDialog(QDialog):
    """One decision: label, target node, conditions and actions."""

    def __init__(self, service: DialogueAuthoringService, translator: Translator,
                 node_ids: list[str], data: dict | None = None,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.service = service
        self.translate = translator
        self.setWindowTitle(self.translate("dialogue_choice_title"))
        self._conditions: list[dict] = list(
            data.get("conditions", []) if isinstance(data, dict) else [])
        self._actions: list[dict] = list(
            data.get("actions", []) if isinstance(data, dict) else [])

        self.label = QLineEdit(self)
        self.target_node = QComboBox(self)
        for node_id in node_ids:
            self.target_node.addItem(node_id, node_id)

        form = QFormLayout()
        form.addRow(self.translate("dialogue_field_label"), self.label)
        form.addRow(self.translate("dialogue_field_target"), self.target_node)
        layout = QVBoxLayout(self)
        layout.addLayout(form)

        conditions_group = QGroupBox(self.translate("dialogue_conditions_group"), self)
        conditions_layout = QVBoxLayout(conditions_group)
        self.conditions_list = QListWidget(conditions_group)
        conditions_layout.addWidget(self.conditions_list)
        conditions_buttons = QHBoxLayout()
        for text, handler in (
                (self.translate("dialogue_add"), self._add_condition),
                (self.translate("dialogue_edit"), self._edit_condition),
                (self.translate("dialogue_remove"), self._remove_condition)):
            button = QPushButton(text, conditions_group)
            button.clicked.connect(handler)
            conditions_buttons.addWidget(button)
        conditions_buttons.addStretch(1)
        conditions_layout.addLayout(conditions_buttons)
        layout.addWidget(conditions_group)

        actions_group = QGroupBox(self.translate("dialogue_actions_group"), self)
        actions_layout = QVBoxLayout(actions_group)
        self.actions_list = QListWidget(actions_group)
        actions_layout.addWidget(self.actions_list)
        actions_buttons = QHBoxLayout()
        for text, handler in (
                (self.translate("dialogue_add"), self._add_action),
                (self.translate("dialogue_edit"), self._edit_action),
                (self.translate("dialogue_remove"), self._remove_action)):
            button = QPushButton(text, actions_group)
            button.clicked.connect(handler)
            actions_buttons.addWidget(button)
        actions_buttons.addStretch(1)
        actions_layout.addLayout(actions_buttons)
        layout.addWidget(actions_group)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel,
            self)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

        if isinstance(data, dict):
            self.label.setText(str(data.get("label") or ""))
            index = self.target_node.findData(str(data.get("targetNodeId") or ""))
            self.target_node.setCurrentIndex(max(index, 0))
        self._refresh()

    def _selected_index(self, listing: QListWidget) -> int:
        return listing.currentRow()

    def _add_condition(self) -> None:
        dialog = ConditionDialog(self.translate, parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self._conditions.append(dialog.value())
            self._refresh()

    def _edit_condition(self) -> None:
        index = self._selected_index(self.conditions_list)
        if index < 0:
            return
        dialog = ConditionDialog(self.translate, self._conditions[index], parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self._conditions[index] = dialog.value()
            self._refresh()

    def _remove_condition(self) -> None:
        index = self._selected_index(self.conditions_list)
        if index >= 0:
            self._conditions.pop(index)
            self._refresh()

    def _add_action(self) -> None:
        dialog = ActionDialog(self.service, self.translate, parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self._actions.append(dialog.value())
            self._refresh()

    def _edit_action(self) -> None:
        index = self._selected_index(self.actions_list)
        if index < 0:
            return
        dialog = ActionDialog(self.service, self.translate, self._actions[index],
                              parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self._actions[index] = dialog.value()
            self._refresh()

    def _remove_action(self) -> None:
        index = self._selected_index(self.actions_list)
        if index >= 0:
            self._actions.pop(index)
            self._refresh()

    def _refresh(self) -> None:
        self.conditions_list.clear()
        for condition in self._conditions:
            self.conditions_list.addItem(
                f"{condition.get('kind')}: {condition.get('flagId')}")
        self.actions_list.clear()
        for action in self._actions:
            self.actions_list.addItem(
                f"{action.get('kind')}: {action.get('targetId')}")

    def value(self) -> dict[str, object]:
        return {
            "label": self.label.text().strip(),
            "targetNodeId": self.target_node.currentData() or "",
            "conditions": copy.deepcopy(self._conditions),
            "actions": copy.deepcopy(self._actions),
        }


class DialogueEditorDialog(QDialog):
    """Create or edit one dialogue: nodes, pages, next chains, decisions."""

    def __init__(
        self,
        workspace: ContentWorkspace,
        translator: Translator,
        definition: ContentDefinition | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.translate = translator
        self.definition = definition
        self.service = DialogueAuthoringService(workspace)
        self.setWindowTitle(self.translate("dialogue_editor_title"))

        self.dialogue_id = QLineEdit(self)
        self.dialogue_id.setPlaceholderText("dialogue.new_dialogue")

        self.nodes: list[dict] = []
        self.entry_node_id = ""

        self.nodes_list = QListWidget(self)
        self.nodes_list.currentRowChanged.connect(self._node_selected)

        self.node_id = QLineEdit(self)
        self.speaker = QLineEdit(self)
        self.pages = QPlainTextEdit(self)
        self.pages.setPlaceholderText(self.translate("dialogue_pages_hint"))
        self.next_node = QComboBox(self)

        self.choices_list = QListWidget(self)
        choice_buttons = QHBoxLayout()
        for text, handler in (
                (self.translate("dialogue_add"), self._add_choice),
                (self.translate("dialogue_edit"), self._edit_choice),
                (self.translate("dialogue_remove"), self._remove_choice)):
            button = QPushButton(text, self)
            button.clicked.connect(handler)
            choice_buttons.addWidget(button)
        choice_buttons.addStretch(1)

        node_form = QFormLayout()
        node_form.addRow(self.translate("dialogue_field_node_id"), self.node_id)
        node_form.addRow(self.translate("dialogue_field_speaker"), self.speaker)
        node_form.addRow(self.translate("dialogue_field_pages"), self.pages)
        node_form.addRow(self.translate("dialogue_field_next"), self.next_node)
        choices_group = QGroupBox(self.translate("dialogue_choices_group"), self)
        choices_layout = QVBoxLayout(choices_group)
        choices_layout.addWidget(self.choices_list)
        choices_layout.addLayout(choice_buttons)
        node_form.addRow(self.translate("dialogue_field_choices"), choices_group)

        apply_node_button = QPushButton(self.translate("dialogue_apply_node"), self)
        apply_node_button.clicked.connect(self._apply_node_form)

        editor_panel = QWidget(self)
        editor_layout = QVBoxLayout(editor_panel)
        editor_layout.addLayout(node_form)
        editor_layout.addWidget(apply_node_button)

        left_panel = QWidget(self)
        left_layout = QVBoxLayout(left_panel)
        left_layout.addWidget(QLabel(self.translate("dialogue_nodes_label"), self))
        left_layout.addWidget(self.nodes_list)
        node_buttons = QHBoxLayout()
        add_node_button = QPushButton(self.translate("dialogue_add_node"), self)
        add_node_button.clicked.connect(self._add_node)
        remove_node_button = QPushButton(self.translate("dialogue_remove_node"), self)
        remove_node_button.clicked.connect(self._remove_node)
        node_buttons.addWidget(add_node_button)
        node_buttons.addWidget(remove_node_button)
        node_buttons.addStretch(1)
        left_layout.addLayout(node_buttons)

        splitter = QSplitter(self)
        splitter.addWidget(left_panel)
        splitter.addWidget(editor_panel)
        splitter.setStretchFactor(1, 1)

        top_form = QFormLayout()
        top_form.addRow(self.translate("dialogue_field_id"), self.dialogue_id)

        layout = QVBoxLayout(self)
        layout.addLayout(top_form)
        layout.addWidget(splitter, 1)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save | QDialogButtonBox.StandardButton.Cancel,
            self)
        buttons.accepted.connect(self._save)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

        if definition is not None:
            self._load(definition)
        else:
            self._new_blank()

    # -- data ---------------------------------------------------------------

    def _load(self, definition: ContentDefinition) -> None:
        self.dialogue_id.setText(definition.definition_id)
        self.dialogue_id.setEnabled(False)
        data = definition.data
        self.nodes = copy.deepcopy(data.get("nodes", []))
        self.entry_node_id = str(data.get("entryNodeId", ""))
        self._refresh_nodes()

    def _new_blank(self) -> None:
        self.nodes = copy.deepcopy(self.service.blank_dialogue("dialogue.new")["nodes"])
        self.entry_node_id = str(self.nodes[0]["id"]) if self.nodes else ""
        self._refresh_nodes()

    def _refresh_nodes(self, keep_row: int | None = None) -> None:
        self.nodes_list.blockSignals(True)
        self.nodes_list.clear()
        for node in self.nodes:
            marker = " ★" if node.get("id") == self.entry_node_id else ""
            choices = len(node.get("choices", []) or [])
            self.nodes_list.addItem(f"{node.get('id')}{marker} ({choices})")
        self.nodes_list.blockSignals(False)
        if keep_row is not None and 0 <= keep_row < self.nodes_list.count():
            self.nodes_list.setCurrentRow(keep_row)
        elif self.nodes:
            self.nodes_list.setCurrentRow(0)

    def _current_node(self) -> dict | None:
        row = self.nodes_list.currentRow()
        if 0 <= row < len(self.nodes):
            return self.nodes[row]
        return None

    # -- node editing ---------------------------------------------------------

    def _node_selected(self, row: int) -> None:
        node = self._current_node()
        if node is None:
            return
        self.node_id.setText(str(node.get("id") or ""))
        self.speaker.setText(str(node.get("speaker") or ""))
        pages = node.get("pages", [])
        self.pages.setPlainText(
            "\n".join(str(page) for page in pages) if isinstance(pages, list) else "")
        self._refresh_next_node_combo(node)

    def _refresh_next_node_combo(self, node: dict) -> None:
        self.next_node.clear()
        self.next_node.addItem(self.translate("dialogue_next_end"), "")
        for other in self.nodes:
            if other.get("id") != node.get("id"):
                self.next_node.addItem(str(other.get("id")), str(other.get("id")))
        current = str(node.get("nextNodeId") or "")
        index = self.next_node.findData(current)
        self.next_node.setCurrentIndex(max(index, 0))
        choices = node.get("choices", []) or []
        self.choices_list.clear()
        for choice in choices:
            target = choice.get("targetNodeId") or "—"
            self.choices_list.addItem(
                f"{choice.get('label')} → {target}")

    def _sync_current_node_from_form(self) -> dict | None:
        node = self._current_node()
        if node is None:
            return None
        node["id"] = self.node_id.text().strip()
        node["speaker"] = self.speaker.text().strip()
        node["pages"] = [line for line in self.pages.toPlainText().split("\n")]
        node["nextNodeId"] = str(self.next_node.currentData() or "")
        return node

    def _apply_node_form(self) -> None:
        old_id = str((self._current_node() or {}).get("id") or "")
        node = self._sync_current_node_from_form()
        if node is None:
            return
        new_id = node.get("id")
        if new_id and old_id and new_id != old_id:
            if any(other.get("id") == new_id for other in self.nodes
                   if other is not node):
                QMessageBox.warning(self, "Dialogue",
                                    self.translate("dialogue_duplicate_node"))
                node["id"] = old_id
                self.node_id.setText(old_id)
                return
            for other in self.nodes:
                if other.get("nextNodeId") == old_id:
                    other["nextNodeId"] = new_id
                for choice in other.get("choices", []) or []:
                    if choice.get("targetNodeId") == old_id:
                        choice["targetNodeId"] = new_id
            if self.entry_node_id == old_id:
                self.entry_node_id = new_id
        self._refresh_nodes(keep_row=self.nodes_list.currentRow())

    def _add_node(self) -> None:
        self._sync_current_node_from_form()
        base = "node"
        names = {str(node.get("id")) for node in self.nodes}
        index = len(self.nodes) + 1
        while f"{base}.{index}" in names:
            index += 1
        self.nodes.append({"id": f"{base}.{index}", "speaker": "",
                           "pages": ["..."], "nextNodeId": "", "choices": []})
        self._refresh_nodes(keep_row=len(self.nodes) - 1)

    def _remove_node(self) -> None:
        if len(self.nodes) <= 1:
            QMessageBox.warning(self, "Dialogue",
                                self.translate("dialogue_last_node"))
            return
        row = self.nodes_list.currentRow()
        if not (0 <= row < len(self.nodes)):
            return
        removed_id = str(self.nodes[row].get("id"))
        self.nodes.pop(row)
        for other in self.nodes:
            if other.get("nextNodeId") == removed_id:
                other["nextNodeId"] = ""
            for choice in other.get("choices", []) or []:
                if choice.get("targetNodeId") == removed_id:
                    choice["targetNodeId"] = ""
        if self.entry_node_id == removed_id:
            self.entry_node_id = str(self.nodes[0].get("id"))
        self._refresh_nodes(keep_row=min(row, len(self.nodes) - 1))

    # -- choices --------------------------------------------------------------

    def _add_choice(self) -> None:
        self._sync_current_node_from_form()
        node = self._current_node()
        if node is None:
            return
        node_ids = [str(other.get("id")) for other in self.nodes]
        dialog = ChoiceDialog(self.service, self.translate, node_ids, parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            node.setdefault("choices", []).append(dialog.value())
            self._refresh_nodes(keep_row=self.nodes_list.currentRow())

    def _edit_choice(self) -> None:
        self._sync_current_node_from_form()
        node = self._current_node()
        if node is None:
            return
        row = self.choices_list.currentRow()
        choices = node.get("choices", []) or []
        if not (0 <= row < len(choices)):
            return
        node_ids = [str(other.get("id")) for other in self.nodes]
        dialog = ChoiceDialog(self.service, self.translate, node_ids,
                              choices[row], parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            choices[row] = dialog.value()
            self._refresh_nodes(keep_row=self.nodes_list.currentRow())

    def _remove_choice(self) -> None:
        node = self._current_node()
        if node is None:
            return
        row = self.choices_list.currentRow()
        choices = node.get("choices", []) or []
        if 0 <= row < len(choices):
            choices.pop(row)
            self._refresh_nodes(keep_row=self.nodes_list.currentRow())

    # -- save -----------------------------------------------------------------

    def _save(self) -> None:
        # commit any pending node form edits before validating
        self._apply_node_form()
        data = {
            "id": self.dialogue_id.text().strip(),
            "entryNodeId": self.entry_node_id,
            "nodes": copy.deepcopy(self.nodes),
        }
        try:
            if self.definition is not None:
                self.service.configure(self.definition.definition_id, data)
            else:
                self.service.create_dialogue(self.dialogue_id.text().strip(), data)
        except ValueError as error:
            QMessageBox.warning(self, "Dialogue", str(error))
            return
        self.accept()


class DialogueLibraryWidget(QWidget):
    """Search, author and inspect dialogue definitions."""

    changed = Signal()
    status_changed = Signal(str)

    def __init__(
        self,
        workspace: ContentWorkspace | None,
        translator: Translator | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.translate = translator or Translator()
        self.service = DialogueAuthoringService(workspace)

        self.search = QLineEdit(self)
        self.search.setPlaceholderText(self.translate("dialogue_search"))
        self.search.textChanged.connect(self.refresh)

        self.dialogues_list = PayloadListWidget(self)
        self.dialogues_list.currentItemChanged.connect(self._selection_changed)

        self.create_button = QPushButton(self)
        self.create_button.setIcon(icon("add"))
        self.create_button.setToolTip(self.translate("dialogue_create"))
        self.create_button.clicked.connect(self.create_dialogue)

        self.configure_button = QPushButton(self)
        self.configure_button.setIcon(icon("configure"))
        self.configure_button.setToolTip(self.translate("dialogue_configure"))
        self.configure_button.clicked.connect(self.configure_current)

        self.delete_button = QPushButton(self)
        self.delete_button.setIcon(icon("delete"))
        self.delete_button.setToolTip(self.translate("dialogue_delete"))
        self.delete_button.clicked.connect(self.delete_current)

        button_row = QHBoxLayout()
        button_row.addWidget(self.create_button)
        button_row.addWidget(self.configure_button)
        button_row.addWidget(self.delete_button)
        button_row.addStretch(1)

        self.details = QLabel(self.translate("dialogue_none"), self)
        self.details.setWordWrap(True)
        self.details.setAlignment(Qt.AlignmentFlag.AlignTop)
        self.details.setMinimumSize(240, 200)
        self.details.setStyleSheet(
            "background:#161b22;"
            "color:#aeb8c4;"
            "border:1px solid #34404d;"
        )

        list_holder = QWidget(self)
        list_layout = QVBoxLayout(list_holder)
        list_layout.setContentsMargins(0, 0, 0, 0)
        list_layout.addWidget(self.search)
        list_layout.addWidget(self.dialogues_list, 1)
        list_layout.addLayout(button_row)

        splitter = QSplitter(Qt.Orientation.Horizontal, self)
        splitter.addWidget(list_holder)
        splitter.addWidget(self.details)
        splitter.setStretchFactor(0, 1)

        layout = QVBoxLayout(self)
        layout.addWidget(splitter)
        self.refresh()

    def set_context(self, workspace: ContentWorkspace | None) -> None:
        self.workspace = workspace
        self.service.set_context(workspace)
        self.refresh()

    def retranslate(self, translator: Translator) -> None:
        self.translate = translator
        self.search.setPlaceholderText(self.translate("dialogue_search"))
        self.create_button.setToolTip(self.translate("dialogue_create"))
        self.configure_button.setToolTip(self.translate("dialogue_configure"))
        self.delete_button.setToolTip(self.translate("dialogue_delete"))
        self.refresh()

    def refresh(self) -> None:
        self.dialogues_list.blockSignals(True)
        self.dialogues_list.clear()
        query = self.search.text() if hasattr(self, "search") else ""
        for dialogue in self.service.dialogues(query):
            nodes = dialogue.data.get("nodes", [])
            node_count = len(nodes) if isinstance(nodes, list) else 0
            choices = sum(
                len(node.get("choices", []) or [])
                for node in nodes if isinstance(node, dict)) if isinstance(nodes, list) else 0
            item = QListWidgetItem(
                f"{dialogue.definition_id}  [{node_count}n/{choices}d]",
                self.dialogues_list)
            item.setData(Qt.ItemDataRole.UserRole, dialogue)
        self.dialogues_list.blockSignals(False)
        if self.dialogues_list.count() == 0:
            self.details.setText(self.translate("dialogue_none"))

    def _selection_changed(self, current: QListWidgetItem | None, _previous=None) -> None:
        definition = current.data(Qt.ItemDataRole.UserRole) if current else None
        if definition is None:
            self.details.setText(self.translate("dialogue_none"))
            return
        self._show_details(definition)

    def _show_details(self, definition: ContentDefinition) -> None:
        nodes = [node for node in definition.data.get("nodes", [])
                 if isinstance(node, dict)]
        lines = [
            f"<b>{self.translate('dialogue_nodes_label')}</b>: {len(nodes)}",
        ]
        for node in nodes[:8]:
            choices = node.get("choices", []) or []
            choice_text = ", ".join(
                str(choice.get("label")) for choice in choices
                if isinstance(choice, dict)) or "—"
            lines.append(
                f"• {node.get('id')} — {choice_text}")
        quests = self.service.quests_referenced(definition.definition_id)
        if quests:
            lines.append(
                f"<br><b>{self.translate('dialogue_quests_started')}</b><br>"
                + "<br>".join(quests))
        shops = self.service.shops_referenced(definition.definition_id)
        if shops:
            lines.append(
                f"<br><b>{self.translate('dialogue_shops_opened')}</b><br>"
                + "<br>".join(shops))
        referencing = self.service.referenced_by(definition.definition_id)
        if referencing:
            lines.append(
                f"<br><b>{self.translate('npc_placed_in')}</b><br>"
                + "<br>".join(referencing))
        text = f"<b>{definition.definition_id}</b><br><br>" + "<br>".join(lines)
        self.details.setText(text)

    def create_dialogue(self) -> None:
        if self.workspace is None:
            return
        dialog = DialogueEditorDialog(self.workspace, self.translate, parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self.changed.emit()
            self.status_changed.emit(self.translate("dialogue_created"))
            self.refresh()

    def configure_current(self) -> None:
        definition = self._current_definition()
        if definition is None:
            return
        dialog = DialogueEditorDialog(
            self.workspace, self.translate, definition=definition, parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self.changed.emit()
            self.status_changed.emit(self.translate("dialogue_configured"))
            self.refresh()

    def delete_current(self) -> None:
        definition = self._current_definition()
        if definition is None:
            return
        confirm = QMessageBox.question(
            self, self.translate("dialogue_delete"),
            self.translate("dialogue_delete_confirm").format(
                dialogue=definition.definition_id),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        if confirm != QMessageBox.StandardButton.Yes:
            return
        try:
            self.service.delete(definition.definition_id)
        except ValueError as error:
            QMessageBox.warning(self, self.translate("dialogue_delete"), str(error))
            return
        self.changed.emit()
        self.status_changed.emit(self.translate("dialogue_deleted"))
        self.refresh()

    def _current_definition(self) -> ContentDefinition | None:
        current = self.dialogues_list.currentItem()
        return current.data(Qt.ItemDataRole.UserRole) if current else None
