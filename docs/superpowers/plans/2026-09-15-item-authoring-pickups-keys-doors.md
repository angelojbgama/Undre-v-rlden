# Item Authoring, Pickups, Keys and Doors Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add first-class Item authoring to the Python Content Studio, automatically provide a pickup for each Item, make chest contents type-safe, and support per-door key requirements with independent consumption and persistence.

**Architecture:** Reuse the existing authored `items`, `pickups`, StaticSprite, object persistence, inventory, and door systems. Python Studio owns authoring UX and data orchestration; C++ remains runtime authority. Door-specific instance behavior is an optional map-placement override, preserving existing object definitions and legacy maps.

**Tech Stack:** Python 3, PySide6, unittest, JSON authored content, C++20 runtime, authored UMAP/UWORLD, DMAP, PowerShell validation scripts.

**Spec:** `docs/superpowers/specs/2026-09-15-item-authoring-pickups-keys-doors-design.md`

## Global Constraints

- Baseline commit before this feature: `703147ab6e89bddb92d8032e5f18077565208d05`.
- The user's current `content/*.json` and `content/*.uworld` edits are authored game content and must not be accidentally staged by implementation scripts.
- `apply_object_collision.ps1` remains a local helper and must not be committed.
- One PowerShell step at a time; each script stops on first failure.
- Python-only stages use focused tests and do not run `build.bat`.
- Full native build occurs only when C++/DMAP/runtime changes require it and at final integration if production C++ changed afterward.
- Each code commit stages an explicit allowlist, never `git add .`.

---
### Task 1 / PS1-1: Item authoring service and automatic pickup contract

**Files:**
- Create: `tools/content_studio/services/item_authoring_service.py`
- Create: `tools/content_studio/tests/test_item_authoring.py`
- Modify only if required: `tools/content_studio/services/localization.py`

**Interfaces:**
- Produces `ItemAuthoringService` with create/update/delete/find operations over `items`.
- Produces `pickup_for_item(item_id)` and guarantees exactly one generated item pickup per authored Item.
- Generated pickup payload is `{kind: "item", itemId: <id>, quantity: 1}` and shares the Item `visualId`.

- [ ] Write RED tests using a temporary ContentWorkspace: creating `item.life_potion` creates one Item and one matching Pickup; equipment forces stackLimit 1; normal stackable default is 66.
- [ ] Add tests that duplicate Item IDs, missing visual IDs and zero stack limits are rejected before workspace mutation.
- [ ] Add tests that rename updates the owned pickup payload/id and delete is rejected while external references remain.
- [ ] Implement the minimal service using existing ContentWorkspace history/mutation APIs; do not hand-edit JSON files.
- [ ] Run only `test_item_authoring.py` plus existing content-format tests.
- [ ] `git diff --check` and commit only service/tests/docs with `feat(studio): add item authoring service`.

**No build.bat. No real authored content mutation.**

---
### Task 2 / PS1-2: Reusable Item visual selection

**Files:**
- Create: `tools/content_studio/services/item_visual_service.py`
- Create: `tools/content_studio/ui/item_visual_picker.py`
- Extend: `tools/content_studio/tests/test_item_authoring.py`
- Extend: `tools/content_studio/tests/test_qt_smoke.py`

**Interfaces:**
- Consumes existing `staticSprites`, `animations` and `visualImages` definitions.
- Produces a StaticSprite id suitable for `Item.visualId`.
- Selecting an animation frame reuses its imageId/source/anchor; image bytes are never copied.

- [ ] RED: test selecting an existing StaticSprite returns it unchanged.
- [ ] RED: test converting animation frame N creates a deterministic dedicated StaticSprite that reuses image/source/anchor.
- [ ] RED Qt smoke: picker lists available static sprites and animation frames and returns one visual id.
- [ ] Implement `ItemVisualService` independently of the dialog.
- [ ] Implement the picker as presentation only; all authored mutations go through the service/workspace.
- [ ] Run focused service + Qt smoke tests and `git diff --check`.
- [ ] Commit explicit files with `feat(studio): add reusable item visual picker`.

**No build.bat.**

---
### Task 3 / PS1-3: Item Library UI and map placement

**Files:**
- Create: `tools/content_studio/ui/item_library_widget.py`
- Modify: `tools/content_studio/ui/main_window.py`
- Modify: `tools/content_studio/services/localization.py`
- Extend: `tools/content_studio/tests/test_qt_smoke.py`
- Extend: `tools/content_studio/tests/test_item_authoring.py`

**Interfaces:**
- Consumes `ItemAuthoringService` and the visual picker from Tasks 1-2.
- Emits placement requests using the generated Pickup definition, never the Item definition itself.

- [ ] RED Qt smoke: Map mode exposes an `Itens` section and opens it without a workspace crash.
- [ ] RED: create dialog requires name, `item.*` id, visual, category and valid stack limit.
- [ ] RED: list/search shows authored item display name, id and category.
- [ ] RED: `Colocar no mapa` resolves the Item's generated Pickup and uses the existing pickup placement flow.
- [ ] Implement create/edit/delete/search and sprite preview following ObjectLibraryWidget patterns without copying its object-specific behavior.
- [ ] Ensure equipment UI locks stackLimit to 1 and normal stackable default remains 66.
- [ ] Run Item tests + Qt smoke; no native build.
- [ ] Commit explicit files with `feat(studio): add item library`.

At the end of this step the Studio can author Items and place their pickups, but chest and door UX are not changed yet.

---
### Task 4 / PS1-4: Typed chest contents editor and empty-item guardrail

**Files:**
- Create: `tools/content_studio/ui/item_stack_editor.py`
- Modify: `tools/content_studio/ui/widgets.py`
- Modify: `tools/content_studio/model/map_document.py`
- Extend: `tools/content_studio/tests/test_formats_and_documents.py`
- Extend: `tools/content_studio/tests/test_qt_smoke.py`

**Interfaces:**
- Produces a reusable ItemStack editor for `{itemId, quantity}` collections.
- StructuredInspector delegates `initialContents` to this typed editor instead of the generic collection placeholder.

- [ ] RED: adding initialContents with zero authored Items creates no row and no empty itemId.
- [ ] RED: with Items present, Add chooses a real Item id and quantity starts at 1.
- [ ] RED: quantity is clamped to the selected Item stackLimit.
- [ ] RED: existing invalid legacy row `{itemId: "", quantity: 1}` can be repaired by selecting a valid Item or deleting the row.
- [ ] Remove `_default_map_collection_value("initialContents") -> {itemId:"",...}` behavior from generic collection editing.
- [ ] Integrate ItemStackEditor without special-casing unrelated arrays.
- [ ] Run focused map/document + Qt tests; verify WorldExportService no longer receives empty itemIds from newly authored chest rows.
- [ ] Commit explicit files with `fix(studio): make chest item contents type safe`.

This step fixes the root cause of the current `objects[13].initialContents[0].itemId` failure. Existing invalid rows are not silently rewritten; the Studio makes them directly repairable.

**No build.bat.**

---
### Task 5 / PS1-5: Keyed doors end-to-end (single native milestone)

**Files:**
- Create: `tools/content_studio/ui/door_instance_editor.py`
- Modify: `tools/content_studio/model/map_document.py`
- Modify: `tools/content_studio/formats/umap.py`
- Modify: `tools/content_studio/formats/dmap.py`
- Modify: `tools/content_studio/ui/main_window.py` and/or `ui/widgets.py`
- Modify: `src/game/gameplay/world_objects.h/.cpp`
- Modify: `src/game/maps/map_data.h/.cpp`
- Modify: `src/game/maps/authored_map.cpp`
- Modify: `src/game/maps/dmap.h/.cpp`
- Modify: `src/game/maps/runtime_world.cpp`
- Extend: `tests/test_main.cpp`, Python format tests and Qt smoke tests.

**Runtime interface:** add optional `ObjectDoorInstanceConfig` with `initialState`, optional `requiredItemId`, and `consumeItem`. Add optional `door` configuration to `ObjectPlacement`. If absent, preserve definition-level legacy behavior.

- [ ] RED Python tests: door placement round-trips optional config; requiredItemId selector only accepts authored category `key`; consumeItem without a key is invalid.
- [ ] RED native tests: authored/DMAP roundtrip preserves door config and older DMAP/maps still load without it.
- [ ] RED runtime tests: wrong/no key keeps locked; correct non-consuming key opens and remains in inventory; consuming key removes exactly one only after successful unlock.
- [ ] RED persistence tests: `persistent` door remains open across map leave/return and save/load; `resetOnMapEnter` returns to authored initialState and can require the same non-consumed key again.
- [ ] Implement Python authored format/editor and C++ map/runtime contract together so the Studio never exposes a door option the runtime ignores.
- [ ] Bump DMAP minor version once; old minor versions default `door = nullopt`.
- [ ] Validate requiredItemId exists in ItemCatalog and has category `key` when catalogs are available.
- [ ] Run one complete `build.bat`, native `tests.exe`, focused Python door tests, then `git diff --check`.
- [ ] Commit explicit source/test files with `feat(doors): support authored key requirements`.

This is the only intentionally large PS1 because splitting authoring from runtime would create a misleading half-working door feature.

---
### Task 6 / PS1-6: Integrated validation and current-map repair check

**Files:**
- No new production files expected.
- May update tests only if validation exposes a real missing regression.

- [ ] Ask the user to close the Studio before the final script so autosave cannot race file validation.
- [ ] Verify HEAD contains Tasks 1-5 commits and no implementation script stages `content/` or `apply_object_collision.ps1`.
- [ ] Run the complete Python/PySide6 Content Studio suite using `.venv`.
- [ ] Run `build\bin\tests.exe`; rerun `build.bat` only if production C++ changed after PS1-5.
- [ ] Run `content_check.exe` against authored definitions.
- [ ] Validate a temporary authored world containing: item pickup, chest stack, consuming key door, reusable key door, persistent door and resetOnMapEnter door.
- [ ] Inspect the real `content/world.uworld` for legacy empty `initialContents[*].itemId`. If one remains, report the exact object and require repair through the new typed editor; do not silently delete user-authored rows.
- [ ] Run `git diff --check`, inspect final status and verify only intentionally modified code/docs are staged by any final corrective commit.

Expected final user workflow:

```text
Itens -> Novo Item -> escolher sprite -> categoria -> stack
                       |
                       +-> Pickup gerado -> Colocar no mapa

Baú no mapa -> Conteúdo inicial -> escolher Item + quantidade

Porta no mapa -> locked -> chave necessária -> consumir? -> persistence
```

---

## Execution order

Run exactly one script at a time and review its output before generating the next:

1. `PS1-1` Item authoring service + generated pickup data contract.
2. `PS1-2` reusable visual/static-sprite selection.
3. `PS1-3` Item Library UI + pickup placement.
4. `PS1-4` typed chest contents + empty-item guardrail.
5. User repairs the current chest `objects[13].initialContents[0]` through the new UI.
6. `PS1-5` keyed-door authoring + native runtime + one full build.
7. `PS1-6` complete integration validation.

Do not jump directly to door runtime before Item authoring exists: keyed-door validation and UX depend on a stable authored Item catalog.
