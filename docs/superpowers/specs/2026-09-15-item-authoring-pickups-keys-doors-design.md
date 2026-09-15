# Item Authoring, Pickups, Chests, Keys and Doors Design

## Goal

Add first-class item authoring to the Python Content Studio and connect authored items to pickups, chest contents, and keyed doors without duplicating content concepts.

## Core model

The content flow is:

```text
Visual asset / animation frame
        -> StaticSprite
        -> ItemDefinition
        -> generated Item Pickup
        -> inventory / bank / chest / shop / rewards
```

An Item is inventory gameplay content. It is not a World Object. World Objects remain doors, chests, scenery, destructibles and similar map entities.

Existing C++ ItemDefinition remains authoritative: id, visualId, category, stackLimit, optional use and optional equipment. Display names stay authoring metadata rather than runtime gameplay fields.
## Studio item authoring

Add an `Itens` section to Map mode alongside Objects, Players, Spritesheets and Tilesets. It owns CRUD for authored `items` definitions.

Initial editable fields:

- display name;
- item id (`item.*`);
- visual/static sprite;
- category: consumable, equipment, key or misc;
- stack limit.

Default stack limit is 66 for stackable items. Equipment is always stackLimit 1. Key defaults to 1 but can be changed later if a design needs stackable keys.

Advanced consumable effects and equipment modifiers are outside the first UI slice. The existing runtime schema is preserved so those capabilities can be exposed later without replacing the item model.

The visual picker must reuse existing assets. It can select an existing StaticSprite or create a StaticSprite from an existing animation frame by reusing the same image/source rectangle/anchor. It must not duplicate PNG data.
## Automatic item pickup

Every authored Item owns one generated item Pickup. The pickup references the Item through payload `{kind: "item", itemId, quantity: 1}` and uses the Item static sprite as its visual.

The relationship is discovered by pickup payload itemId, not only by naming convention. A conventional id such as `pickup.item.life_potion` may be generated for readability.

Creating an Item creates its pickup atomically. Renaming an Item updates the owned pickup and references. Deleting an Item is blocked while referenced by maps, chests, doors, shops or rewards; after references are removed, the owned pickup can be removed with it.

The Item library exposes `Colocar no mapa`, which places the generated Pickup rather than treating the Item itself as a map object.

## Chest contents

`initialContents` must no longer use the generic collection placeholder `{itemId: "", quantity: 1}`.

Use a typed item-stack editor with:

```text
Item [valid authored item dropdown]   Quantity [1..stackLimit]
+ Add item                            Remove
```

Adding a row is allowed only when at least one valid authored Item exists. This prevents the current empty-itemId save/export failure.
## Keyed doors

Keys are ordinary Items with category `key`. The key does not know which doors it opens. A door instance references the required key.

Door placement data gains an optional configuration:

```json
"door": {
  "initialState": "locked",
  "requiredItemId": "item.key.castle",
  "consumeItem": false
}
```

The object definition continues to provide door capability/visual behavior. Placement data controls instance-specific unlock behavior. When no placement override exists, legacy/default door behavior remains valid.

The Studio door editor shows only category `key` items in `requiredItemId`. `consumeItem` and persistence are independent.

Persistence continues to use the existing object placement policy:

- `persistent`: opened/unlocked state survives map changes and save/load;
- `resetOnMapEnter`: door returns to initialState when the map is entered again.
## Runtime unlock behavior

On interaction with a locked door:

1. If requiredItemId is empty, use the existing door behavior.
2. If a key is required and the Player lacks it, the door stays locked.
3. If the Player owns the key, the door transitions to open/unlocked behavior.
4. If consumeItem is true, remove exactly one key only after the unlock succeeds.
5. State capture follows the existing object persistence/save system.

The runtime must validate that requiredItemId exists and is category `key`.

## Compatibility and scope

Existing maps and object definitions remain readable. New door-placement fields are optional. The authored map and DMAP formats must version/encode the new optional door configuration without breaking legacy content.

This first implementation does not add advanced consumable-effect UI, equipment stat UI, generic requirement graphs, master-key predicates, or arbitrary scripted door conditions.

## Validation

Each implementation slice is delivered by one PowerShell script and focused tests. Do not run `build.bat` after every Python-only slice. Run the complete native build only when native map/runtime changes are introduced and again for the final integrated validation if production C++ changed after the previous build.

## Deterministic generated ids

For an Item id `item.<suffix>`:

- generated pickup id is `pickup.<suffix>`;
- when an animation frame must be materialized as a dedicated StaticSprite, default id is `visual.item.<suffix>`;
- existing explicitly selected StaticSprite ids are reused unchanged.

Generated definitions may overlay matching built-in definitions through the existing content-source overlay behavior; no duplicate runtime concepts are introduced.
