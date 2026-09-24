"""Author the Undre världen short campaign through the Content Studio APIs.

Everything the game ships (definitions, maps, placements, doors, encounters,
regions) enters through the same ContentWorkspace / WorldProject / MapDocument
model the Studio UI drives — no JSON is hand-edited. Re-runnable: existing
definitions and placements are detected and skipped.

Campaign flow (title -> map.1):
  map.1 hub (authored by the owner)  --west door-->  map.2 spike corridor (key)
  map.1 gate (locked, eats key) --east-->            map.3 training room (puppet, TNT)
  map.3 --east door-->                               map.4 ranged gauntlet (arrow traps)
  map.4 --east door-->                               map.5 arena (final encounter gate)
  map.5 gate opens when the encounter completes -->  map.6 sanctuary (elder, escape quest)
"""

from __future__ import annotations

import copy
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO))

from tools.content_studio.model.content_workspace import ContentWorkspace  # noqa: E402
from tools.content_studio.model.map_document import MapDocument  # noqa: E402
from tools.content_studio.model.world_project import WorldProject  # noqa: E402
from tools.content_studio.services.world_export_service import WorldExportService  # noqa: E402

_WORKSPACE = None

TILESET = "tileset.imported"
FLOOR = [69, 70, 71]
WALL_NW, WALL_N, WALL_NE = 40, 41, 42
WALL_W, WALL_E = 59, 61
WALL_SW, WALL_S, WALL_SE = 78, 79, 80


def frame(x: int, y: int, w: int, h: int, anchor_x: int, anchor_y: int,
          ticks: int = 12) -> dict:
    return {
        "source": {"x": x, "y": y, "width": w, "height": h},
        "anchor": {"x": anchor_x, "y": anchor_y},
        "drawOffset": {"x": 0, "y": 0},
        "durationTicks": ticks,
        "markers": [],
        "flipX": False,
        "masks": [],
    }


def anim(animation_id: str, image_id: str, frames: list[dict], loop: bool) -> dict:
    return {"id": animation_id, "imageId": image_id, "loop": loop, "frames": frames}


def visual_image(image_id: str, relative_path: str) -> dict:
    return {"id": image_id, "root": "gameAssets", "relativePath": relative_path}


def static_sprite(sprite_id: str, image_id: str, anchor: tuple[int, int],
                  source: tuple[int, int, int, int] | None = None) -> dict:
    entry = {"id": sprite_id, "imageId": image_id, "anchor": {"x": anchor[0], "y": anchor[1]}}
    if source is not None:
        entry["source"] = {"x": source[0], "y": source[1], "width": source[2], "height": source[3]}
    return entry


def upsert(workspace: ContentWorkspace, category: str, definition: dict, id_key: str = "id") -> None:
    definition_id = definition[id_key]
    existing = workspace.find(category, definition_id)
    if existing is not None:
        if existing.data == definition:
            return
        workspace.replace_definition(existing, copy.deepcopy(definition))
        print(f"  ~ {category}:{definition_id}")
        return
    workspace.create_definition(category, definition_id)
    created = workspace.find(category, definition_id)
    assert created is not None, definition_id
    workspace.replace_definition(created, copy.deepcopy(definition))
    print(f"  + {category}:{definition_id}")


def directional(animation_id: str) -> dict:
    return {name: animation_id for name in ("down", "up", "left", "right")}


def paint_room(document: MapDocument, floor_variants: list[int] | None = None) -> None:
    """Ground layer everywhere, wall border on the Wall layer, border collision."""
    if floor_variants is None:
        floor_variants = FLOOR
    width, height = document.width, document.height
    ground = 0
    wall = 1
    for y in range(height):
        for x in range(width):
            variant = floor_variants[(x * 7 + y * 3) % len(floor_variants)]
            document.set_tile(ground, x, y, TILESET, variant)
    for x in range(width):
        document.set_tile(wall, x, 0, TILESET, WALL_N if 0 < x < width - 1 else (WALL_NW if x == 0 else WALL_NE))
        document.set_tile(wall, x, height - 1, TILESET, WALL_S if 0 < x < width - 1 else (WALL_SW if x == 0 else WALL_SE))
    for y in range(1, height - 1):
        document.set_tile(wall, 0, y, TILESET, WALL_W)
        document.set_tile(wall, width - 1, y, TILESET, WALL_E)
    border = [(x, y) for x in range(width) for y in range(height)
              if x in (0, width - 1) or y in (0, height - 1)]
    document.set_collision(border, True)


def feet(tx: int, ty: int) -> tuple[int, int]:
    return tx * 16 + 8, ty * 16 + 16


def place_pickup(document: MapDocument, definition_id: str, tx: int, ty: int) -> int:
    """Pickup placements carry their definition's visual/bounds/payload."""
    assert _WORKSPACE is not None
    definition = _WORKSPACE.find("pickups", definition_id)
    assert definition is not None, definition_id
    x, y = feet(tx, ty)
    return document.add_entity("pickups", definition_id, x, y,
                               definition_data=definition.data)


def open_door_gap(document: MapDocument, x: int, y: int) -> None:
    """A door opening in the border wall: clear the wall tile and collision."""
    document.set_tile(1, x, y, TILESET, FLOOR[0])
    document.set_collision([(x, y)], False)


def place(document: MapDocument, category: str, definition_id: str, tx: int, ty: int,
          facing: str = "down") -> int:
    x, y = feet(tx, ty)
    return document.add_entity(category, definition_id, x, y, facing=facing)


def main() -> int:
    global _WORKSPACE
    workspace = ContentWorkspace.open(REPO / "content" / "definitions")
    project, diagnostics = WorldProject.open(REPO / "content" / "world.uworld")
    assert project is not None, diagnostics
    workspace.world_project = project
    globals()["_WORKSPACE"] = workspace

    print("== definitions ==")
    add_definitions(workspace)

    print("== maps ==")
    hub = project.map_by_id("map.1")
    assert hub is not None
    configure_hub(project, hub)

    corridors = {
        "map.2": author_spike_corridor,
        "map.3": author_training_room,
        "map.4": author_ranged_gauntlet,
        "map.5": author_arena,
        "map.6": author_sanctuary,
    }
    # Rooms are authored from scratch on every run so placements never
    # duplicate; map.1 (the owner's authored hub) is only ever patched.
    for map_id in list(corridors):
        if project.map_by_id(map_id) is not None:
            project.remove_map(map_id)
    for map_id, author in corridors.items():
        project.add_map(MapDocument.new(map_id, 17, 14, 16))
        document = project.map_by_id(map_id)
        assert document is not None
        document.add_layer("Wall")
        author(document)
        print(f"  + {map_id} authored")

    project.set_entry_map("map.1")

    workspace.save_all()
    project.save()
    output = REPO / "maps" / "gameplay"
    result, export_diagnostics = WorldExportService().export(project, output, workspace)
    for diagnostic in export_diagnostics:
        print("  !", diagnostic.message)
    print("export ok" if result.ok else "export FAILED")
    return 0 if result.ok else 1


def add_definitions(workspace: ContentWorkspace) -> None:
    # --- visuals: slime -----------------------------------------------------
    upsert(workspace, "visualImages", visual_image(
        "image.enemy.slime.idle", "Characters/Enemies/Slime/slime_idle.png"))
    upsert(workspace, "visualImages", visual_image(
        "image.enemy.slime.death", "Characters/Enemies/Slime/slime_death.png"))
    upsert(workspace, "animations", anim(
        "anim.enemy.slime.idle", "image.enemy.slime.idle",
        [frame(32 * i, 0, 32, 16, 16, 15, 16) for i in range(4)], True))
    upsert(workspace, "animations", anim(
        "anim.enemy.slime.death", "image.enemy.slime.death",
        [frame(32 * i, 0, 32, 32, 16, 31, 14) for i in range(2)], False))
    # Hurt pose: the first death frame held briefly, authored per facing.
    upsert(workspace, "animations", anim(
        "anim.enemy.slime.hurt", "image.enemy.slime.death",
        [frame(0, 0, 32, 32, 16, 31, 12)], False))
    bite_clips = directional("anim.enemy.slime.idle")
    # The slime art has no walk sheet; the idle loop doubles as the move
    # clip (authored choice — the runtime requires resolved move clips and
    # rejects the visual set without one).
    upsert(workspace, "enemyVisuals", {
        "id": "visual.enemy.slime",
        "idle": directional("anim.enemy.slime.idle"),
        "move": directional("anim.enemy.slime.idle"),
        "hurt": directional("anim.enemy.slime.hurt"),
        "death": directional("anim.enemy.slime.death"),
        "actions": [{"visualActionId": "visual.action.slime.bite", "clips": bite_clips}],
    })

    # --- visuals: training puppet -------------------------------------------
    upsert(workspace, "visualImages", visual_image(
        "image.object.training_puppet", "Training_puppet/training_puppet.png"))
    upsert(workspace, "visualImages", visual_image(
        "image.object.training_puppet.damage", "Training_puppet/training_puppet_death and demange.png"))
    upsert(workspace, "animations", anim(
        "anim.object.training_puppet.idle", "image.object.training_puppet",
        [frame(0, 0, 32, 32, 16, 31, 20)], True))
    upsert(workspace, "animations", anim(
        "anim.object.training_puppet.damage", "image.object.training_puppet.damage",
        [frame(32 * i, 0, 32, 32, 16, 31, 10) for i in range(2)], False))
    upsert(workspace, "objectVisuals", {
        "id": "visual.object.training_puppet",
        "idleAnimationId": "anim.object.training_puppet.idle",
        "damagedAnimationId": "anim.object.training_puppet.damage",
    })

    # --- visuals: spikes + arrow trap ---------------------------------------
    upsert(workspace, "animations", anim(
        "anim.object.spike_trap.idle", "image.tile_spickes",
        [frame(16 * i, 0, 16, 16, 8, 15, 14) for i in range(3)], True))
    upsert(workspace, "objectVisuals", {
        "id": "visual.object.spike_trap",
        "idleAnimationId": "anim.object.spike_trap.idle",
    })
    upsert(workspace, "visualImages", visual_image(
        "image.object.arrow_trap", "Tileset/Walls_trap_arrows.png"))
    upsert(workspace, "animations", anim(
        "anim.object.arrow_trap.idle", "image.object.arrow_trap",
        [frame(0, 0, 12, 11, 6, 10, 20)], True))
    upsert(workspace, "objectVisuals", {
        "id": "visual.object.arrow_trap",
        "idleAnimationId": "anim.object.arrow_trap.idle",
    })

    # --- visuals: TNT, explosion, key, meat, big money -----------------------
    upsert(workspace, "visualImages", visual_image(
        "image.object.tnt", "Objects/tnt.png"))
    upsert(workspace, "animations", anim(
        "anim.object.tnt.idle", "image.object.tnt",
        [frame(16 * i, 0, 16, 16, 8, 15, 16) for i in range(2)], True))
    upsert(workspace, "objectVisuals", {
        "id": "visual.object.tnt",
        "idleAnimationId": "anim.object.tnt.idle",
    })
    upsert(workspace, "animations", anim(
        "anim.explosion.blast", "image.explosion",
        [frame(48 * i, 0, 48, 48, 24, 40, 5) for i in range(10)], False))
    upsert(workspace, "visualImages", visual_image("image.item.key", "Objects/key.png"))
    upsert(workspace, "staticSprites", static_sprite("visual.item.key", "image.item.key", (8, 8)))
    upsert(workspace, "visualImages", visual_image("image.pickup.meat", "Objects/meat.png"))
    upsert(workspace, "staticSprites", static_sprite("visual.item.meat", "image.pickup.meat", (8, 8)))
    upsert(workspace, "visualImages", visual_image("image.pickup.big_money", "Objects/big_money.png"))
    upsert(workspace, "staticSprites", static_sprite("visual.pickup.big_money", "image.pickup.big_money", (8, 8)))
    upsert(workspace, "staticSprites", static_sprite(
        "visual.item.tnt", "image.object.tnt", (8, 8), (0, 0, 16, 16)))

    # --- projectiles: TNT throw + trap arrow ---------------------------------
    upsert(workspace, "projectiles", {
        "id": "projectile.tnt",
        "visualId": "visual.item.tnt",
        "canonicalFacing": "right",
        "speedPixelsPerTick": 2,
        "lifetimeTicks": 60,
        "hitboxWidth": 8,
        "hitboxHeight": 8,
        "spawnOffsets": {"down": {"x": 0, "y": 10}, "up": {"x": 0, "y": -14},
                          "left": {"x": -12, "y": -6}, "right": {"x": 12, "y": -6}},
        "maximumDistancePixels": 64,
        "impactAnimationId": "anim.explosion.blast",
        "explosion": {
            "damageAmount": 2,
            "knockbackPixels": 16,
            "radiusPixels": 28,
        },
    })
    upsert(workspace, "projectiles", {
        "id": "projectile.arrow.trap",
        "visualId": "visual.arrow",
        "canonicalFacing": "right",
        "speedPixelsPerTick": 2,
        "lifetimeTicks": 90,
        "hitboxWidth": 6,
        "hitboxHeight": 6,
        "spawnOffsets": {"down": {"x": 0, "y": 6}, "up": {"x": 0, "y": -6},
                          "left": {"x": -6, "y": 0}, "right": {"x": 6, "y": 0}},
        "maximumDistancePixels": 180,
    })

    # --- items ---------------------------------------------------------------
    upsert(workspace, "items", {
        "id": "item.key", "visualId": "visual.item.key",
        "category": "key", "stackLimit": 1,
    })
    upsert(workspace, "items", {
        "id": "item.tnt", "visualId": "visual.item.tnt",
        "category": "consumable", "stackLimit": 9,
        "use": {"kind": "throwProjectile", "amount": 2,
                "projectileId": "projectile.tnt"},
    })
    upsert(workspace, "items", {
        "id": "item.meat", "visualId": "visual.item.meat",
        "category": "consumable", "stackLimit": 9,
        "use": {"kind": "restoreHealth", "amount": 1},
    })
    upsert(workspace, "items", {
        "id": "item.arrow", "visualId": "visual.item.arrow",
        "category": "misc", "stackLimit": 30,
    })

    # --- pickups -------------------------------------------------------------
    upsert(workspace, "pickups", {
        "id": "pickup.key", "visualId": "visual.item.key",
        "collectionBounds": {"x": -5, "y": -5, "width": 10, "height": 10},
        "payload": {"kind": "item", "itemId": "item.key", "quantity": 1},
    })
    upsert(workspace, "pickups", {
        "id": "pickup.arrow", "visualId": "visual.item.arrow",
        "collectionBounds": {"x": -5, "y": -5, "width": 10, "height": 10},
        "payload": {"kind": "item", "itemId": "item.arrow", "quantity": 3},
    })
    upsert(workspace, "pickups", {
        "id": "pickup.meat", "visualId": "visual.item.meat",
        "collectionBounds": {"x": -5, "y": -5, "width": 10, "height": 10},
        "payload": {"kind": "item", "itemId": "item.meat", "quantity": 1},
    })
    upsert(workspace, "pickups", {
        "id": "pickup.big_money", "visualId": "visual.pickup.big_money",
        "collectionBounds": {"x": -5, "y": -5, "width": 10, "height": 10},
        "payload": {"kind": "currency", "amount": 25},
    })
    upsert(workspace, "pickups", {
        "id": "pickup.tnt", "visualId": "visual.item.tnt",
        "collectionBounds": {"x": -5, "y": -5, "width": 10, "height": 10},
        "payload": {"kind": "item", "itemId": "item.tnt", "quantity": 1},
    })

    # --- hurt poses for the builtin-visual enemies ----------------------------
    # The soldier/skull death sheets are row-per-facing 32x32; the hurt pose
    # holds the first death frame of each row for the feedback window.
    upsert(workspace, "animations", anim(
        "anim.enemy.soldier.hurt", "image.enemy.soldier.death",
        [frame(0, 32 * row, 32, 32, 16, 31, 12) for row in range(3)], False))
    upsert(workspace, "animations", anim(
        "anim.enemy.skull.hurt", "image.enemy.skull.death",
        [frame(0, 32 * row, 32, 32, 16, 31, 12) for row in range(3)], False))
    for visual_set_id, hurt_anim in (("visual.enemy.evil_soldier", "anim.enemy.soldier.hurt"),
                                     ("visual.enemy.skull", "anim.enemy.skull.hurt")):
        visual_set = workspace.find("enemyVisuals", visual_set_id)
        if visual_set is not None and "hurt" not in visual_set.data:
            wired = copy.deepcopy(visual_set.data)
            wired["hurt"] = directional(hurt_anim)
            workspace.replace_definition(visual_set, wired)
            print(f"  ~ enemyVisuals:{visual_set_id} hurt wired")

    # --- behaviors / attacks / enemies ---------------------------------------
    # Authored balance: the copied soldier ran at 256 subpixels/tick — nearly
    # the player's 384 — making every encounter an inescapable gang-beating
    # for a 5 HP player. 128 keeps the pressure while allowing retreats.
    soldier = workspace.find("enemies", "enemy.evil_soldier")
    if soldier is not None and soldier.data.get("movementSpeedSubpixelsPerTick") not in (None, 128):
        balanced = copy.deepcopy(soldier.data)
        balanced["movementSpeedSubpixelsPerTick"] = 128
        workspace.replace_definition(soldier, balanced)
        print("  ~ enemies:enemy.evil_soldier speed -> 128")
    upsert(workspace, "behaviors", {
        "id": "behavior.slime.basic", "detectionRangePixels": 56,
        "disengageRangePixels": 88, "idleDurationTicks": 900, "wanderDurationTicks": 60,
    })
    upsert(workspace, "attacks", {
        "id": "attack.slime.contact", "kind": "meleeHitbox",
        "damage": {"amount": 1, "knockbackPixels": 16},
        "totalTicks": 24, "cooldownTicks": 50,
        "minimumRangePixels": 0, "maximumRangePixels": 12,
        "visualActionId": "visual.action.slime.bite",
        "meleeHitboxes": {
            "down": {"offsetX": -7, "offsetY": -2, "width": 14, "height": 10},
            "up": {"offsetX": -7, "offsetY": -16, "width": 14, "height": 10},
            "left": {"offsetX": -14, "offsetY": -9, "width": 12, "height": 12},
            "right": {"offsetX": 2, "offsetY": -9, "width": 12, "height": 12},
        },
        "timeline": [{"tick": 6, "kind": "activateHitbox"},
                      {"tick": 16, "kind": "deactivateHitbox"}],
    })
    upsert(workspace, "attacks", {
        "id": "attack.slime.champion.contact", "kind": "meleeHitbox",
        "damage": {"amount": 2, "knockbackPixels": 24},
        "totalTicks": 24, "cooldownTicks": 50,
        "minimumRangePixels": 0, "maximumRangePixels": 14,
        "visualActionId": "visual.action.slime.bite",
        "meleeHitboxes": {
            "down": {"offsetX": -8, "offsetY": -2, "width": 16, "height": 10},
            "up": {"offsetX": -8, "offsetY": -17, "width": 16, "height": 10},
            "left": {"offsetX": -15, "offsetY": -10, "width": 13, "height": 13},
            "right": {"offsetX": 2, "offsetY": -10, "width": 13, "height": 13},
        },
        "timeline": [{"tick": 6, "kind": "activateHitbox"},
                      {"tick": 16, "kind": "deactivateHitbox"}],
    })
    upsert(workspace, "enemyVisuals", {
        "id": "visual.enemy.slime.champion",
        "idle": directional("anim.enemy.slime.idle"),
        "move": directional("anim.enemy.slime.idle"),
        "hurt": directional("anim.enemy.slime.hurt"),
        "death": directional("anim.enemy.slime.death"),
        "actions": [],
    })
    upsert(workspace, "enemies", {
        "id": "enemy.slime", "visualSetId": "visual.enemy.slime",
        "behaviorProfileId": "behavior.slime.basic", "faction": "enemy",
        "maximumHealth": 2, "movementSpeedSubpixelsPerTick": 56,
        "collisionBody": {"offsetX": -5, "offsetY": -6, "width": 10, "height": 6},
        "hurtbox": {"offsetX": -7, "offsetY": -14, "width": 14, "height": 14},
        "attackIds": ["attack.slime.contact"],
        "rewardProfileId": "reward.enemy.slime",
    })
    upsert(workspace, "enemies", {
        "id": "enemy.slime.champion", "visualSetId": "visual.enemy.slime.champion",
        "behaviorProfileId": "behavior.slime.basic", "faction": "enemy",
        "maximumHealth": 8, "movementSpeedSubpixelsPerTick": 64,
        "collisionBody": {"offsetX": -6, "offsetY": -6, "width": 12, "height": 6},
        "hurtbox": {"offsetX": -8, "offsetY": -15, "width": 16, "height": 15},
        "attackIds": ["attack.slime.champion.contact"],
        "rewardProfileId": "reward.enemy.slime.champion",
    })
    # The training puppet is a destructible practice target object, not a
    # creature: it never fights back, grants nothing and returns each visit.
    upsert(workspace, "objects", {
        "id": "object.training_puppet",
        "visualSetId": "visual.object.training_puppet",
        "destructible": {"maximumHealth": 5,
                          "hurtbox": {"x": -16, "y": -32, "width": 32, "height": 32},
                          "destructionDurationTicks": 28, "damageDurationTicks": 8,
                          "leaveDestroyedResidue": False},
        "depthAnchor": {"x": 0, "y": 28},
    })

    # --- reward profiles ------------------------------------------------------
    upsert(workspace, "rewardProfiles", {
        "id": "reward.enemy.slime", "experience": 15,
        "loot": [{"pickupDefinitionId": "pickup.money", "chanceBasisPoints": 4000,
                   "minimumCount": 1, "maximumCount": 1}],
    })
    upsert(workspace, "rewardProfiles", {
        "id": "reward.enemy.slime.champion", "experience": 40,
        "loot": [{"pickupDefinitionId": "pickup.big_money", "chanceBasisPoints": 10000,
                   "minimumCount": 1, "maximumCount": 1}],
    })

    # --- trap objects ----------------------------------------------------------
    upsert(workspace, "objects", {
        "id": "object.spike_trap", "visualSetId": "visual.object.spike_trap",
        "hazard": {"damageAmount": 1, "knockbackPixels": 12,
                    "periodTicks": 150, "activeTicks": 40,
                    "hitbox": {"x": -6, "y": -6, "width": 12, "height": 12}},
        "depthAnchor": {"x": 0, "y": 8},
    })
    upsert(workspace, "objects", {
        "id": "object.arrow_trap", "visualSetId": "visual.object.arrow_trap",
        "hazard": {"damageAmount": 1, "knockbackPixels": 8,
                    "periodTicks": 180, "activeTicks": 60,
                    "hitbox": {"x": 0, "y": 0, "width": 2, "height": 2},
                    "projectileId": "projectile.arrow.trap", "facing": "down"},
        "depthAnchor": {"x": 0, "y": 8},
    })
    upsert(workspace, "objects", {
        "id": "object.arrow_trap.up", "visualSetId": "visual.object.arrow_trap",
        "hazard": {"damageAmount": 1, "knockbackPixels": 8,
                    "periodTicks": 180, "activeTicks": 60,
                    "hitbox": {"x": 0, "y": 0, "width": 2, "height": 2},
                    "projectileId": "projectile.arrow.trap", "facing": "up"},
        "depthAnchor": {"x": 0, "y": 8},
    })

    # --- reward grants / shop / final quest -------------------------------------
    upsert(workspace, "rewardGrants", {
        "id": "reward.quest.underworld.escape", "experience": 60, "gold": 100,
        "items": [{"itemId": "item.elixir", "quantity": 2}],
    })
    upsert(workspace, "quests", {
        "id": "quest.underworld.escape", "title": "Escape the Underworld",
        "objectives": [
            {"id": "quest.escape.enter", "kind": "enter",
             "targetId": "region.escape", "requiredCount": 1,
             "description": "Reach the sanctuary beyond the arena."}],
        "tags": ["story", "underworld"],
        "rewardGrantId": "reward.quest.underworld.escape",
    })

    # --- ending NPC --------------------------------------------------------------
    upsert(workspace, "npcs", {
        "id": "npc.elder", "visualSetId": "visual.npc.scholar",
        "interaction": {"x": -14, "y": -28, "width": 28, "height": 22},
        "interactionEnabled": True,
        "defaultDialogueId": "dialogue.elder.talk",
        "tags": ["npc", "story"],
    })
    upsert(workspace, "dialogues", {
        "id": "dialogue.elder.talk",
        "entryNodeId": "elder.entry",
        "nodes": [
            {"id": "elder.entry", "speaker": "Elder",
             "pages": [
                 "You crossed the arena few return from. The dark keeps its gate open for you now.",
                 "Rest, trade with the keeper, or simply breathe. The underworld is yours to walk."],
             "nextNodeId": "", "choices": []},
        ],
    })

    # --- trader stocks arrows / TNT now ------------------------------------------
    trader_shop = workspace.find("shops", "shop.underworld.general")
    if trader_shop is not None:
        updated = copy.deepcopy(trader_shop.data)
        offers = updated.setdefault("offers", [])
        known = {offer.get("itemId") for offer in offers}
        for offer in ({"itemId": "item.arrow", "playerBuyPrice": 5, "playerSellPrice": 2},
                      {"itemId": "item.tnt", "playerBuyPrice": 60, "playerSellPrice": 25},
                      {"itemId": "item.meat", "playerBuyPrice": 12, "playerSellPrice": 5}):
            if offer["itemId"] not in known:
                offers.append(offer)
        if updated != trader_shop.data:
            workspace.replace_definition(trader_shop, updated)
            print("  + shop.underworld.general: arrows/TNT/meat in stock")


def configure_hub(project: WorldProject, hub: MapDocument) -> None:
    """Wire the hub doors: west door to the corridor, locked gate to the training room."""
    gate = next((entry for entry in hub.data.get("objects", [])
                 if entry.get("definitionId") == "object.gate"), None)
    if gate is not None:
        hub.set_object_door_configuration(
            gate["id"],
            {"initialState": "locked", "consumeItem": True,
             "requiredItemId": "item.key"},
            "persistent")
        hub.set_object_transition_configuration(
            gate["id"], {"targetMapId": "map.3", "targetSpawnId": "entry.west"})
        print("  + map.1 gate: locked, eats key, leads to map.3")
    if hub.find_tile_reference(TILESET, 8) is None:
        pass  # keep the owner's painted ground untouched
    # The runtime prefers the documented entry.start spawn for startup and
    # respawn; adopt it as the canonical id for the owner's start point.
    for spawn in hub.data.setdefault("playerSpawns", []):
        if spawn.get("id") == "player.start":
            spawn["id"] = "entry.start"
    for link in hub.data.get("links", []):
        if link.get("targetSpawnId") == "player.start":
            link["targetSpawnId"] = "entry.start"
    if not any(spawn.get("id") == "entry.gate.side" for spawn in hub.data.get("playerSpawns", [])):
        hub.add_player_spawn("entry.gate.side", 96, 32, "down")
    # West wall opening for the corridor door handled as a map link trigger.
    links = {link.get("id") for link in hub.data.get("links", [])}
    if "link.west" not in links:
        hub.add_link("link.west", 0, 96, 8, 48, "map.2", "entry.east")
        open_door_gap(hub, 0, 7)
        print("  + map.1: west opening + link to map.2")


def author_spike_corridor(document: MapDocument) -> None:
    paint_room(document)
    width, height = document.width, document.height
    # Entry from the hub lands on the east side; the key waits at the far west.
    document.add_player_spawn("entry.east", (width - 2) * 16 + 8, 7 * 16 + 16, "left")
    document.add_player_spawn("entry.west", 2 * 16 + 8, 7 * 16 + 16, "right")
    if not any(link.get("id") == "link.exit" for link in document.data.get("links", [])):
        document.add_link("link.exit", 0, 96, 8, 48, "map.1", "entry.start")
        open_door_gap(document, 0, 7)
    # Pillar obstacles to break the sight lines.
    for tx, ty in ((5, 4), (5, 9), (9, 3), (9, 10), (12, 5), (12, 9)):
        document.set_tile(1, tx, ty, TILESET, 61)
        document.set_collision([(tx, ty)], True)
    # Spike traps pace the corridor.
    for tx, ty in ((6, 7), (8, 6), (10, 8)):
        place(document, "objects", "object.spike_trap", tx, ty)
    # Slime patrols between the spikes.
    for tx, ty in ((5, 6), (8, 9), (11, 6)):
        place(document, "enemies", "enemy.slime", tx, ty)
    # The prize: the gate key, under guard.
    place(document, "enemies", "enemy.slime.champion", 3, 7)
    place_pickup(document, "pickup.key", 1, 7)
    # Small supplies.
    place_pickup(document, "pickup.meat", 7, 3)
    place_pickup(document, "pickup.money", 10, 10)


def author_training_room(document: MapDocument) -> None:
    paint_room(document)
    width = document.width
    document.add_player_spawn("entry.west", 2 * 16 + 8, 7 * 16 + 16, "right")
    document.add_player_spawn("entry.east", (width - 2) * 16 + 8, 7 * 16 + 16, "left")
    if not any(link.get("id") == "link.exit" for link in document.data.get("links", [])):
        document.add_link("link.exit", 0, 96, 8, 48, "map.1", "entry.gate.side")
        open_door_gap(document, 0, 7)
    # East door to the gauntlet.
    open_door_gap(document, width - 1, 7)
    if not any(link.get("id") == "link.east" for link in document.data.get("links", [])):
        document.add_link("link.east", (width - 1) * 16, 96, 16, 48, "map.4", "entry.west")
    # Practice puppet against the north wall, between two pillars.
    place(document, "objects", "object.training_puppet", 8, 4)
    for tx, ty in ((6, 4), (10, 4)):
        document.set_tile(1, tx, ty, TILESET, 61)
        document.set_collision([(tx, ty)], True)
    # TNT cache: only container objects may hold initial contents.
    chest_id = place(document, "objects", "object.chest", 8, 9)
    for _ in range(2):
        document.add_object_initial_content(chest_id, "item.tnt", 1)
    place(document, "objects", "object.crate", 6, 9)
    place(document, "objects", "object.breaking_vase", 10, 9)
    place_pickup(document, "pickup.arrow", 12, 10)
    place_pickup(document, "pickup.tnt", 13, 11)


def author_ranged_gauntlet(document: MapDocument) -> None:
    paint_room(document)
    width, height = document.width, document.height
    document.add_player_spawn("entry.west", 2 * 16 + 8, 7 * 16 + 16, "right")
    document.add_player_spawn("entry.east", (width - 2) * 16 + 8, 7 * 16 + 16, "left")
    if not any(link.get("id") == "link.exit" for link in document.data.get("links", [])):
        document.add_link("link.exit", 0, 96, 8, 48, "map.3", "entry.east")
        open_door_gap(document, 0, 7)
    open_door_gap(document, width - 1, 7)
    if not any(link.get("id") == "link.east" for link in document.data.get("links", [])):
        document.add_link("link.east", (width - 1) * 16, 96, 16, 48, "map.5", "entry.west")
    # Arrow traps embedded in the north and south walls rake the corridor.
    for tx, ty in ((5, 1), (9, 1)):
        place(document, "objects", "object.arrow_trap", tx, ty)
    place(document, "objects", "object.arrow_trap.up", 7, 12)
    # Spikes under the crossing.
    for tx, ty in ((7, 7), (11, 6)):
        place(document, "objects", "object.spike_trap", tx, ty)
    # Skulls hold the room.
    for tx, ty in ((6, 5), (9, 9), (12, 7)):
        place(document, "enemies", "enemy.skull", tx, ty)
    # Supplies before the arena.
    place_pickup(document, "pickup.heart", 8, 4)
    place_pickup(document, "pickup.big_money", 13, 10)
    place(document, "objects", "object.breaking_vase", 4, 10)


def author_arena(document: MapDocument) -> None:
    paint_room(document)
    width = document.width
    document.add_player_spawn("entry.west", 2 * 16 + 8, 7 * 16 + 16, "right")
    document.add_player_spawn("entry.east", (width - 2) * 16 + 8, 7 * 16 + 16, "left")
    if not any(link.get("id") == "link.exit" for link in document.data.get("links", [])):
        document.add_link("link.exit", 0, 96, 8, 48, "map.4", "entry.east")
        open_door_gap(document, 0, 7)
    # The eastern gate stays sealed until the final encounter is cleared.
    open_door_gap(document, width - 1, 7)
    if not any(link.get("id") == "link.east" for link in document.data.get("links", [])):
        document.add_link("link.east", (width - 1) * 16, 96, 16, 48, "map.6", "entry.west")
    gate = next((entry for entry in document.data.get("objects", [])
                 if entry.get("definitionId") == "object.gate"), None)
    if gate is None:
        gate_id = place(document, "objects", "object.gate", 15, 7)
    else:
        gate_id = gate["id"]
    document.set_object_door_configuration(
        gate_id,
        {"initialState": "locked", "consumeItem": False,
         "encounterId": "encounter.underworld.final"},
        "persistent")
    # The war band. Participants become the authored encounter.
    participants = [
        place(document, "enemies", "enemy.evil_soldier", 6, 5),
        place(document, "enemies", "enemy.evil_soldier", 6, 9),
        place(document, "enemies", "enemy.skull", 10, 7),
        place(document, "enemies", "enemy.slime.champion", 12, 5),
    ]
    encounters = document.data.setdefault("encounters", [])
    if not any(entry.get("id") == "encounter.underworld.final" for entry in encounters):
        encounters.append({"id": "encounter.underworld.final",
                           "participants": participants,
                           "rewardGrantId": "reward.quest.underworld.escape"})
    # Supplies on the approach.
    place_pickup(document, "pickup.heart", 4, 7)
    place_pickup(document, "pickup.meat", 8, 11)


def author_sanctuary(document: MapDocument) -> None:
    paint_room(document)
    width = document.width
    document.add_player_spawn("entry.west", 2 * 16 + 8, 7 * 16 + 16, "right")
    if not any(link.get("id") == "link.exit" for link in document.data.get("links", [])):
        document.add_link("link.exit", 0, 96, 8, 48, "map.5", "entry.east")
        open_door_gap(document, 0, 7)
    # Elder watches over the shrine; the escape region wraps it.
    place(document, "npcs", "npc.elder", 12, 7)
    document.add_region("region.escape", 10 * 16, 4 * 16, 6 * 16, 7 * 16)
    chest = place(document, "objects", "object.chest", 14, 6)
    document.add_object_initial_content(chest, "item.elixir", 2)
    # Mechanic-scenario fixtures in the safe room: a breakable crate, a coin
    # and a potion so pickup/container assertions never need combat first.
    place(document, "objects", "object.breaking_crate", 9, 10)
    place(document, "objects", "object.crate", 10, 10)
    place_pickup(document, "pickup.money", 7, 10)
    place_pickup(document, "pickup.life_potion", 6, 4)
    place_pickup(document, "pickup.big_money", 13, 9)
    place_pickup(document, "pickup.heart", 11, 10)


if __name__ == "__main__":
    raise SystemExit(main())
