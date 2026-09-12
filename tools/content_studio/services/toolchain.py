from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path

from ..formats.dmap import safe_dmap_filename
from ..formats.json_io import encode_json
from ..model.types import Diagnostic, ToolResult
from .world_export_service import WorldExportService


class ToolchainError(RuntimeError):
    pass


class CppToolchain:
    """Runs the authoritative C++ validation/compiler/runtime tools."""

    def __init__(self, repository_root: Path | None = None, *, content_check: Path | None = None,
                 map_compile: Path | None = None,
                 game: Path | None = None, asset_root: Path | None = None) -> None:
        self.repository_root = (repository_root or Path(__file__).resolve().parents[3]).resolve()
        self.asset_root = asset_root
        self.content_check = content_check or self._find("content_check")
        self.map_compile = map_compile or self._find("map_compile")
        self.game = game or self._find("game")

    def _find(self, name: str) -> Path | None:
        candidates = [
            self.repository_root / "build" / "bin" / f"{name}.exe",
            self.repository_root / "build" / "bin" / name,
            self.repository_root / "build" / "linux" / name,
        ]
        for candidate in candidates:
            if candidate.is_file():
                return candidate
        found = shutil.which(name)
        return Path(found) if found else None

    @staticmethod
    def _run(command: list[str], cwd: Path | None = None) -> ToolResult:
        try:
            completed = subprocess.run(command, cwd=cwd, text=True, capture_output=True, check=False)
        except OSError as error:
            return ToolResult(127, "", str(error), command)
        return ToolResult(completed.returncode, completed.stdout, completed.stderr, command)

    @staticmethod
    def diagnostics(result: ToolResult, source_path: Path | None = None) -> list[Diagnostic]:
        diagnostics: list[Diagnostic] = []
        text = "\n".join(part for part in (result.stderr, result.stdout) if part)
        for line in text.splitlines():
            if not line.strip() or line.strip() == "PASS":
                continue
            diagnostics.append(Diagnostic("error" if not result.ok else "warning", line, source_path=source_path, code="cpp_tool"))
        if not result.ok and not diagnostics:
            diagnostics.append(Diagnostic("error", f"C++ tool failed with exit code {result.returncode}", source_path=source_path, code="cpp_tool"))
        return diagnostics

    def validate_workspace(self, content_root: Path) -> tuple[ToolResult, list[Diagnostic]]:
        if self.content_check is None:
            result = ToolResult(127, "", "content_check executable was not found", ["content_check"])
        else:
            result = self._run([str(self.content_check), str(content_root)])
        return result, self.diagnostics(result, content_root)

    def compile_map(self, source: Path, output: Path, content_root: Path | None = None) -> tuple[ToolResult, list[Diagnostic]]:
        command = [str(self.map_compile)] if self.map_compile else ["map_compile"]
        if content_root is not None:
            command.extend(["--content", str(content_root)])
        command.extend([str(source), str(output)])
        result = self._run(command, self.repository_root)
        return result, self.diagnostics(result, source)

    def launch_playtest(self, map_path: Path, content_root: Path | None = None,
                        asset_root: Path | None = None,
                        map_root: Path | None = None) -> subprocess.Popen[str]:
        if self.game is None:
            raise ToolchainError("game executable was not found")
        command = [str(self.game), "--map", str(map_path)]
        if map_root is not None:
            command.extend(["--map-root", str(map_root)])
        if content_root is not None:
            command.extend(["--content", str(content_root)])
        if asset_root or self.asset_root:
            command.extend(["--asset-root", str(asset_root or self.asset_root)])
        return subprocess.Popen(command, cwd=self.repository_root, text=True)

    def compile_and_launch(self, project: object, content_root: object | None,
                           asset_root: Path | None = None) -> tuple[subprocess.Popen[str] | None, list[Diagnostic]]:
        # Keep this convenience entry point usable for integrations while the
        # PlaytestService owns temporary authored artifacts and cleanup.
        service = getattr(self, "_playtest_service", None)
        if service is None:
            service = PlaytestService(self)
            self._playtest_service = service
        return_value = service.start(project, content_root, asset_root)
        return service.process if return_value[0] else None, return_value[1]


class PlaytestService:
    def __init__(self, toolchain: CppToolchain) -> None:
        self.toolchain = toolchain
        self._temporary_roots: list[Path] = []
        self.process: subprocess.Popen[str] | None = None

    def start(self, project: object, content_workspace: object | None,
              asset_root: Path | None = None) -> tuple[bool, list[Diagnostic]]:
        from ..model.world_project import WorldProject
        from ..model.content_workspace import ContentWorkspace

        if not isinstance(project, WorldProject):
            return False, [Diagnostic("error", "playtest requires a WorldProject", code="playtest_input")]
        if not isinstance(content_workspace, ContentWorkspace):
            return False, [Diagnostic("error", "playtest requires an authored content workspace", code="playtest_input")]
        active_map = project.active_map
        spawns = active_map.data.get("playerSpawns", [])
        if not isinstance(spawns, list) or not any(isinstance(value, dict) for value in spawns):
            return False, [Diagnostic("error", f"playtest unavailable: map {active_map.map_id} has no player spawn", active_map.map_id, "missing_player_spawn", map_id=active_map.map_id)]
        temporary = Path(tempfile.mkdtemp(prefix="underworld-studio-playtest-"))
        self._temporary_roots.append(temporary)
        content_copy = temporary / "content"
        content_copy.mkdir()
        for content_file in content_workspace.files:
            relative = content_file.path.relative_to(content_workspace.root)
            destination = content_copy / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(encode_json(content_file.data), encoding="utf-8")
        output = temporary / "maps"
        result, diagnostics = WorldExportService().export(
            project, output, content_workspace)
        if not result.ok:
            self.stop()
            return False, diagnostics
        # The world is still compiled in full so cross-map transitions work,
        # but iteration must launch from the map currently being authored.
        map_path = output / safe_dmap_filename(active_map.map_id)
        if not map_path.is_file():
            candidates = list(output.glob("*.dmap"))
            map_path = candidates[0] if candidates else map_path
        try:
            self.process = self.toolchain.launch_playtest(
                map_path, content_copy, asset_root, output)
        except ToolchainError as error:
            self.stop()
            return False, [Diagnostic("error", str(error), code="playtest_launch")]
        return True, []

    def stop(self) -> None:
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
        self.process = None
        for root in self._temporary_roots:
            import shutil as _shutil
            _shutil.rmtree(root, ignore_errors=True)
        self._temporary_roots.clear()
