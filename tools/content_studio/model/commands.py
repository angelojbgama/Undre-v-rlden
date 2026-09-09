from __future__ import annotations

import copy
from dataclasses import dataclass
from typing import Any, Callable, Protocol


class SnapshotTarget(Protocol):
    def restore_snapshot(self, snapshot: Any) -> None: ...


@dataclass(slots=True)
class Command:
    label: str
    target: SnapshotTarget
    before: Any
    after: Any

    def apply(self) -> None:
        self.target.restore_snapshot(copy.deepcopy(self.after))

    def revert(self) -> None:
        self.target.restore_snapshot(copy.deepcopy(self.before))


class CommandHistory:
    """Small document-scoped command history; UI state is never snapshotted."""

    def __init__(self) -> None:
        self._commands: list[Command] = []
        self._cursor = 0

    def execute(self, command: Command) -> None:
        del self._commands[self._cursor:]
        command.apply()
        self._commands.append(command)
        self._cursor += 1

    def undo(self) -> bool:
        if self._cursor == 0:
            return False
        self._cursor -= 1
        self._commands[self._cursor].revert()
        return True

    def redo(self) -> bool:
        if self._cursor == len(self._commands):
            return False
        self._commands[self._cursor].apply()
        self._cursor += 1
        return True

    @property
    def can_undo(self) -> bool:
        return self._cursor > 0

    @property
    def can_redo(self) -> bool:
        return self._cursor < len(self._commands)

    def clear(self) -> None:
        self._commands.clear()
        self._cursor = 0

