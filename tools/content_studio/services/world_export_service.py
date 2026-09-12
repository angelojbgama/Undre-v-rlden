from __future__ import annotations

from pathlib import Path

from ..formats.dmap import DmapError, safe_dmap_filename, write_dmap
from ..model.content_workspace import ContentWorkspace
from ..model.types import Diagnostic, ToolResult
from ..model.world_project import WorldProject


class WorldExportService:
    """Validate and export the authored world to DMAP using only Python."""

    def export(self, project: WorldProject, output: Path,
               workspace: ContentWorkspace) -> tuple[ToolResult, list[Diagnostic]]:
        issues = list(project.validate_cross_map())
        issues.extend(issue for issue in workspace.diagnostics if issue.is_error)
        issues.extend(self._validate_content_references(project, workspace))
        if any(issue.is_error for issue in issues):
            return ToolResult(1, "", "Python DMAP validation failed", ["python-dmap"]), issues

        written: list[Path] = []
        try:
            output.mkdir(parents=True, exist_ok=True)
            for document in project.maps:
                target = output / safe_dmap_filename(document.map_id)
                write_dmap(target, document.data)
                written.append(target)
        except (OSError, DmapError, ValueError) as error:
            issues.append(Diagnostic(
                "error", str(error), code="python_dmap_export",
                source_path=project.path,
            ))
            return ToolResult(1, "", str(error), ["python-dmap"]), issues

        message = f"PASS\nmaps: {len(written)}\n" + "\n".join(str(path) for path in written)
        return ToolResult(0, message, "", ["python-dmap"]), issues

    @staticmethod
    def _validate_content_references(
            project: WorldProject,
            workspace: ContentWorkspace) -> list[Diagnostic]:
        issues: list[Diagnostic] = []
        ids = {
            category: {value.definition_id for value in workspace.definitions(category)}
            for category in (
                "tilesets", "enemies", "npcs", "objects", "items", "pickups",
                "staticSprites", "dialogues", "rewardGrants", "presentationEffects",
            )
        }
        tilesets = {
            value.definition_id: value.data
            for value in workspace.definitions("tilesets")
        }

        def missing(map_id: str, path: str, category: str, value: object) -> None:
            if not isinstance(value, str) or not value or value not in ids[category]:
                issues.append(Diagnostic(
                    "error", f"unknown {category} definition: {value!r}", path,
                    "missing_definition", str(value or ""), map_id=map_id,
                ))

        for document in project.maps:
            data = document.data
            for index, reference in enumerate(data.get("tileReferences", [])):
                if not isinstance(reference, dict):
                    continue
                tileset_id = reference.get("tilesetId")
                missing(document.map_id, f"tileReferences[{index}].tilesetId",
                        "tilesets", tileset_id)
                definition = tilesets.get(str(tileset_id))
                if definition is not None:
                    tile_size = definition.get("tileSize")
                    if tile_size != document.tile_size:
                        issues.append(Diagnostic(
                            "error", "tileset tile size does not match the map",
                            f"tileReferences[{index}].tilesetId", "tile_size_mismatch",
                            map_id=document.map_id,
                        ))
                    count = int(definition.get("columns", 0)) * int(definition.get("rows", 0))
                    source = reference.get("sourceIndex")
                    if not isinstance(source, int) or isinstance(source, bool) or not 0 <= source < count:
                        issues.append(Diagnostic(
                            "error", "tile source index is outside the tileset",
                            f"tileReferences[{index}].sourceIndex", "tile_index_out_of_range",
                            map_id=document.map_id,
                        ))
            for category in ("enemies", "npcs", "objects"):
                for index, placement in enumerate(data.get(category, [])):
                    if not isinstance(placement, dict):
                        continue
                    missing(document.map_id, f"{category}[{index}].definitionId",
                            category, placement.get("definitionId"))
                    if category == "objects":
                        for stack_index, stack in enumerate(placement.get("initialContents", [])):
                            if isinstance(stack, dict):
                                missing(document.map_id,
                                        f"objects[{index}].initialContents[{stack_index}].itemId",
                                        "items", stack.get("itemId"))
            for index, placement in enumerate(data.get("pickups", [])):
                if not isinstance(placement, dict):
                    continue
                missing(document.map_id, f"pickups[{index}].definitionId",
                        "pickups", placement.get("definitionId"))
                missing(document.map_id, f"pickups[{index}].visualId",
                        "staticSprites", placement.get("visualId"))
                payload = placement.get("payload")
                if isinstance(payload, dict) and payload.get("kind") == "item":
                    missing(document.map_id, f"pickups[{index}].payload.itemId",
                            "items", payload.get("itemId"))
            for index, region in enumerate(data.get("regions", [])):
                if isinstance(region, dict) and region.get("environmentEffectId"):
                    missing(document.map_id, f"regions[{index}].environmentEffectId",
                            "presentationEffects", region.get("environmentEffectId"))
            for index, encounter in enumerate(data.get("encounters", [])):
                if isinstance(encounter, dict) and encounter.get("rewardGrantId"):
                    missing(document.map_id, f"encounters[{index}].rewardGrantId",
                            "rewardGrants", encounter.get("rewardGrantId"))
            for scene_index, scene in enumerate(data.get("scenes", [])):
                if not isinstance(scene, dict):
                    continue
                for track_index, track in enumerate(scene.get("tracks", [])):
                    if not isinstance(track, dict):
                        continue
                    for clip_index, clip in enumerate(track.get("clips", [])):
                        if not isinstance(clip, dict):
                            continue
                        prefix = f"scenes[{scene_index}].tracks[{track_index}].clips[{clip_index}]"
                        if clip.get("kind") == "dialogue":
                            missing(document.map_id, prefix + ".dialogueId",
                                    "dialogues", clip.get("dialogueId"))
                        elif clip.get("kind") == "presentationEffect":
                            missing(document.map_id, prefix + ".effectId",
                                    "presentationEffects", clip.get("effectId"))
        return issues
