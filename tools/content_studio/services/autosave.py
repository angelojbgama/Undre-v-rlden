from __future__ import annotations

from pathlib import Path

from ..formats.json_io import write_atomic
from ..model.content_workspace import ContentWorkspace
from ..model.world_project import WorldProject


def autosave(project: WorldProject, workspace: ContentWorkspace | None) -> list[Path]:
    """Write recoverable snapshots without clearing authored dirty state."""
    written: list[Path] = []
    if project.path is not None:
        world_target = project.path.with_name(project.path.name + ".autosave")
        write_atomic(world_target, project.authored_data())
        written.append(world_target)
    if workspace is not None:
        for content_file in workspace.files:
            if not content_file.dirty:
                continue
            target = content_file.path.with_name(content_file.path.name + ".autosave")
            write_atomic(target, content_file.data)
            written.append(target)
    return written
