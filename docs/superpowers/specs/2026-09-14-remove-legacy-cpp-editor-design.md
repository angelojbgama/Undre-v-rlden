# Legacy C++ Editor Retirement Design

## Goal

Make the Python/PySide6 Content Studio the only implementation of map and
content authoring. Remove the obsolete C++ editor subsystem without changing
runtime formats, gameplay, or legitimate native tools.

## Architectural boundary

Python owns UMAP/UWORLD/content authoring, editing commands, workspace state,
asset previews, validation orchestration, DMAP export, and launching playtests
from authored state. The official flow remains:

```text
Python authored state
    -> WorldExportService / Python DMAP writer
    -> temporary DMAP and content files
    -> C++ game runtime
```

C++ continues to own runtime map loading, simulation, rendering, gameplay,
`ContentValidator`, `ContentCompiler`, `content_check`, the compatibility
`world_compile` tool, and the runtime-only headless `playtest_runner`. No C++
runtime target may include or link editor code.

## Removal strategy

Delete every source and header under `src/editor/`. Repository searches show
that its only external consumers are `tests/test_main.cpp`,
`src/tools/playtest_runner.cpp`, and the Windows/Linux build definitions; the
game runtime has no dependency on it.

Remove C++ tests whose subject is the retired authoring implementation. Where
a test combines runtime and editor assertions, retain the runtime assertions
and construct fixtures directly with runtime/authored DTOs. Do not create a
replacement C++ authoring abstraction merely to preserve a historical test.

Remove the `content_studio_visuals`, `content_studio_gameplay`, and
`content_studio_unified` authoring scenarios from `playtest_runner`. Keep all
runtime gameplay scenarios and the runner itself.

Remove editor compilation, object lists, dependency rules, and link inputs
from `build.bat` and `docker/linux_build.mk`. Preserve the existing cleanup of
stale `map_compile` and `map_editor` binaries.

## Python coverage

Use `tools/content_studio/tests/` as the authoritative authoring suite. Existing
tests already cover map editing, commands and undo/redo, workspace CRUD,
references, scenes, multi-map projects, Python DMAP export, C++ compatibility,
toolchain resolution, asset previews, localization, preferences, and playtest
launching. Add focused coverage only for important behavior found missing
during the C++ test audit.

Extend the architectural regression test to assert that:

- `src/editor` does not exist;
- native `map_compile` and `map_editor` sources and targets remain absent;
- neither build compiles or links editor objects;
- `tests/test_main.cpp` and `playtest_runner.cpp` do not depend on the retired
  editor namespace or headers.

Avoid checks based on irrelevant whitespace or exact build formatting.

## Documentation

Update `docs/CONTENT_STUDIO_PYTHON_MIGRATION.md` to state that `src/editor` has
been removed, Python/PySide6 is the sole official authoring implementation, and
the game/runtime is independent from the Studio. Correct other documentation
only where it still describes the retired C++ editor as current.

## Verification and commit scope

Run the full Python Content Studio suite, Linux native build and tests when the
build succeeds, `git diff --check`, repository-wide legacy-reference searches,
and checks for tracked binary artifacts. Record baseline failures separately
from regressions caused by this change.

The working tree contains unrelated user changes. Preserve them, edit shared
files by narrow hunks, and stage only retirement-related paths/hunks for the
implementation commit. Do not repair unrelated gameplay warnings or whitespace
issues as part of this work.
