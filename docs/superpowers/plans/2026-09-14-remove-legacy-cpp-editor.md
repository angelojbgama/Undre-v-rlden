# Legacy C++ Editor Retirement Implementation Plan

> Historical note: the compatibility `world_compile` retained by this completed
> plan was retired in the subsequent Python-authority cleanup.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Remove the obsolete C++ editor implementation while preserving the Python Content Studio and legitimate C++ runtime/tooling.

**Architecture:** Delete the isolated `src/editor` subsystem and remove its only external consumers from native tests, the headless runner, and both build definitions. Keep runtime coverage by retaining mixed C++ test portions that use runtime/authored DTOs directly, while authoring behavior and the architectural boundary are protected by Python tests.

**Tech Stack:** C++20, Standard Library, Python 3 `unittest`, MSVC batch build, GNU Make Linux build

**Spec:** `docs/superpowers/specs/2026-09-14-remove-legacy-cpp-editor-design.md`

## Global Constraints

- Python/PySide6 is the only official map and content authoring implementation.
- Do not change UMAP, UWORLD, DMAP, DSAV, gameplay, or runtime behavior for this retirement.
- Keep `game`, `content_check`, `playtest_runner`, and compatibility `world_compile` native targets.
- Do not add dependencies or recreate C++ authoring abstractions.
- Preserve current working-tree changes and exclude `apply_object_collision.ps1` from commits.
- Record preexisting Python, C++ build, and whitespace failures separately.

---

### Task 1: Lock the retirement boundary with a failing Python test

**Files:**
- Modify: `tools/content_studio/tests/test_native_authoring_retirement.py`
- Test: `tools/content_studio/tests/test_native_authoring_retirement.py`

**Interfaces:**
- Consumes: repository paths and source text only
- Produces: regression checks for absent C++ editor files, dependencies, objects, and targets

- [x] **Step 1: Extend the architectural test**

Add checks equivalent to:

```python
self.assertFalse((REPOSITORY / "src" / "editor").exists())
for source in (REPOSITORY / "tests" / "test_main.cpp",
               REPOSITORY / "src" / "tools" / "playtest_runner.cpp"):
    text = source.read_text(encoding="utf-8")
    self.assertNotIn('"editor/', text)
    self.assertNotIn("underworld::editor", text)
self.assertNotIn("TEST_EDITOR_", self.linux_build)
self.assertNotIn("src\\editor", self.windows_build)
```

- [x] **Step 2: Run the focused test and verify it fails for live legacy dependencies**

Run: `python3 -m unittest tools.content_studio.tests.test_native_authoring_retirement`

Expected: FAIL because `src/editor` and its build/test/runner references still exist.

### Task 2: Remove native editor tests without losing runtime coverage

**Files:**
- Modify: `tests/test_main.cpp`
- Delete: `src/editor/*.h`
- Delete: `src/editor/*.cpp`
- Test: `build/linux/tests`

**Interfaces:**
- Consumes: runtime/authored DTO APIs under `src/game` and `src/engine`
- Produces: native test executable with no `editor/*` includes or `underworld::editor` references

- [x] **Step 1: Remove editor-only includes, test functions, and main registrations**

Delete tests for the retired editor UI, document, commands, workspace, visual preview,
timeline authoring, preferences, localization, and project playtest implementation.

- [x] **Step 2: Preserve runtime portions of mixed tests**

For functions such as world-object persistence, NPC runtime, official map discovery,
multi-tileset runtime, world closure, and presentation feedback, remove only editor
round-trip assertions. Build their fixtures directly from `MapData`,
`AuthoredMapSource`, content DTOs, or existing checked-in authored files.

- [x] **Step 3: Delete every file under `src/editor`**

Use an explicit patch deletion so the resulting Git diff records all retired files.

- [x] **Step 4: Compile the native test translation unit**

Run: `make -f docker/linux_build.mk build/linux/obj/tests/test_main.o`

Expected after Task 4 build edits: compilation succeeds, or stops only at a documented
preexisting shared-source warning before reaching this object.

### Task 3: Make the playtest runner runtime-only

**Files:**
- Modify: `src/tools/playtest_runner.cpp`

**Interfaces:**
- Consumes: `GameRuntime`, authored/runtime map DTOs, content runtime, headless platform
- Produces: runtime scenarios only; no Content Studio authoring scenarios

- [x] **Step 1: Remove all `editor/*` includes**

Keep only engine/game/runtime headers used by the remaining scenarios.

- [x] **Step 2: Remove retired authoring scenarios**

Delete `runContentStudioVisuals`, `runContentStudioGameplay`, and
`runContentStudioUnified`, their dispatch branches, and their names in
`allScenarios`.

- [x] **Step 3: Verify source-level independence**

Run:

```bash
rg -n 'editor/|underworld::editor|editor::|ContentWorkspaceDocument|EditorDocument|EditorVisualPreview' src/tools/playtest_runner.cpp
```

Expected: no matches.

### Task 4: Remove editor objects from Windows and Linux builds

**Files:**
- Modify: `build.bat`
- Modify: `docker/linux_build.mk`

**Interfaces:**
- Consumes: remaining C++ sources
- Produces: tests and playtest runner linked only with shared runtime/game objects

- [x] **Step 1: Remove Windows editor compilation and link inputs**

Delete every `src\editor` compile command and every retired editor `.obj` from the
`tests.exe` and `playtest_runner.exe` link commands. Preserve stale binary cleanup.

- [x] **Step 2: Remove Linux editor source/object variables and rule**

Remove `TEST_EDITOR_SOURCES`, `TEST_EDITOR_OBJECTS`, the editor pattern rule, and
their dependencies from `ALL_OBJECTS`, tests, and playtest links. Let the normal
`COMMON_SOURCES` search run without a redundant `src/editor` exclusion once the
directory is gone.

- [x] **Step 3: Re-run the focused architectural test**

Run: `python3 -m unittest tools.content_studio.tests.test_native_authoring_retirement`

Expected: PASS.

### Task 5: Document the completed migration

**Files:**
- Modify: `docs/CONTENT_STUDIO_PYTHON_MIGRATION.md`
- Modify only if currently false: `docs/ENGINE_ARCHITECTURE.md`, `docs/ROADMAP.md`, `docs/CONTENT_STUDIO.md`, `docs/CONTENT_STUDIO_AUDIT.md`

**Interfaces:**
- Consumes: final source/build architecture
- Produces: current documentation distinguishing historical C++ editor references from official Python authoring

- [x] **Step 1: Record final ownership**

State explicitly that `src/editor` was removed, Python/PySide6 is the sole official
authoring implementation, and native runtime executables do not depend on the Studio.

- [x] **Step 2: Correct live-product statements only**

Keep historical/migration discussion where clearly labeled; remove or update claims
that the current C++ editor still exists.

### Task 6: Verify the repository and commit the complete worktree except the excluded script

**Files:**
- Include: all tracked/untracked repository changes validated in this session
- Exclude: `apply_object_collision.ps1`

**Interfaces:**
- Consumes: final repository state
- Produces: evidence-backed commit containing validated worktree changes

- [x] **Step 1: Run complete Python tests**

Run: `python3 -m unittest discover -s tools/content_studio/tests -p 'test_*.py'`

Expected: retirement tests pass; any preexisting content-schema failure is reported
with its original file and diagnostic.

- [x] **Step 2: Run Linux build and native tests**

Run: `make -f docker/linux_build.mk all`

If successful, run: `./build/linux/tests`

Expected: success, or only the preexisting `builtin_content.cpp` missing-field
initializer failure already recorded at baseline.

- [x] **Step 3: Search for forbidden live references and tracked binaries**

Run repository-wide `rg` searches for legacy identifiers, classify documentation
history, and run `git ls-files` checks for executable/binary artifacts.

- [x] **Step 4: Review diffs and whitespace**

Run `git status`, `git diff`, and `git diff --check`. Separate retirement-path
results from preexisting whitespace failures in the other worktree changes.

- [x] **Step 5: Stage everything except the excluded script and commit**

Stage tracked changes, the retirement test, plan, and deletions. Explicitly leave
`apply_object_collision.ps1` untracked. Commit with:

```text
refactor(studio): remove legacy C++ editor implementation
```

- [x] **Step 6: Report evidence**

Report initial SHA, removed files, test/build changes, Python/native results,
preexisting failures, reference-search classification, final status, and final SHA.
