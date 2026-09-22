"""Python mirror of the C++ UI registries (src/game/ui/ui_screens.cpp).

The canonical source is the C++ runtime; ``build/bin/ui_manifest.exe`` emits
these tables as JSON. ``test_ui_authoring.py`` runs the tool and fails loudly
when this mirror drifts, so edit both sides together.
"""

from __future__ import annotations

COMPONENTS: tuple[str, ...] = (
    "group", "panel", "image", "animatedImage", "text", "meter", "slot", "repeater",
)

COMPONENT_PROPERTIES: dict[str, tuple[str, ...]] = {
    "group": (),
    "panel": (),
    "image": (),
    "animatedImage": (),
    "text": ("text",),
    "meter": ("value", "maximum"),
    "slot": ("icon", "count"),
    "repeater": ("source",),
    "image": ("icon",),
}

ANCHORS: tuple[str, ...] = (
    "topLeft", "topCenter", "topRight",
    "centerLeft", "center", "centerRight",
    "bottomLeft", "bottomCenter", "bottomRight",
)

METER_MODES: tuple[str, ...] = ("segmented", "fillHorizontal", "fillVertical")

CONDITION_OPERATORS: tuple[str, ...] = ("equal", "lessOrEqual", "greaterOrEqual")

SCREEN_KINDS: tuple[str, ...] = ("hud", "overlay", "screen")

# Binding Registry paths over the GameViewModel snapshot. Only paths with a
# concrete runtime consumer are listed; overlays/collections join with their
# consumers (docs/UI_ENGINE.md block UI-4).
BINDING_PATHS: tuple[str, ...] = (
    "player.health.current",
    "player.health.max",
    "player.health.percentage",
    "player.gold",
    "player.ammo.itemId",
    "player.ammo.amount",
    "player.ammo.icon",
    "player.quickSlots.0.itemId",
    "player.quickSlots.0.amount",
    "player.quickSlots.0.icon",
    "player.quickSlots.1.itemId",
    "player.quickSlots.1.amount",
    "player.quickSlots.1.icon",
    "player.quickSlots.2.itemId",
    "player.quickSlots.2.amount",
    "player.quickSlots.2.icon",
    "player.quickSlots.3.itemId",
    "player.quickSlots.3.amount",
    "player.quickSlots.3.icon",
    "player.inventory.slots",
    "player.ammo.present",
    "player.quickSlots.slots",
    "quests.journal",
    "context.quest.title",
    "context.quest.completed",
    "saves.slot.1.label",
    "saves.slot.2.label",
    "saves.slot.3.label",
    "player.armor.icon",
    "player.accessory.icon",
    "overlay.equipment.armorSelected",
    "overlay.equipment.accessorySelected",
    "player.derivedMaxHealth",
    "player.attackDamageBonus",
    "bank.slots",
    "overlay.bank.inventorySelected",
    "overlay.bank.storageSelected",
    "bank.goldStored",
    "shop.offers",
    "context.offer.line",
    "overlay.shop.modeSell",
    "overlay.shop.buySelected",
    "overlay.shop.sellSelected",
    "overlay.shop.sellItemName",
    "overlay.shop.feedbackPresent",
    "context.index",
    "context.item.id",
    "context.item.icon",
    "context.item.amount",
    "overlay.inventory.slotSelected",
)

# Action Registry ids mapping onto existing PlayerCommand intents.
ACTIONS: tuple[str, ...] = (
    "game.save",
    "game.load",
    "inventory.toggle",
    "crafting.toggle",
    "quickSlot.1",
    "quickSlot.2",
    "quickSlot.3",
    "quickSlot.4",
    "screen.close",
    "screen.open.saves",
    "save.slot.1",
    "save.slot.2",
    "save.slot.3",
    "load.slot.1",
    "load.slot.2",
    "load.slot.3",
)

# UI action events supported by the runtime presenter.
ACTION_EVENTS: tuple[str, ...] = ("activate",)


def component_accepts_property(component: str, property: str) -> bool:
    return property in COMPONENT_PROPERTIES.get(component, ())


def is_container_component(component: str) -> bool:
    # The repeater accepts children too: they are its instance template.
    return component in ("group", "panel", "repeater")
