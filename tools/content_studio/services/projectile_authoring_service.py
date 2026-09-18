from __future__ import annotations

import copy
from dataclasses import dataclass

from ..model.content_workspace import ContentWorkspace


@dataclass(frozen=True, slots=True)
class ProjectileCatalogEntry:
    definition_id: str
    visual_id: str
    status: str


class ProjectileAuthoringService:
    """Author projectile definitions."""

    def __init__(self, workspace: ContentWorkspace) -> None:
        self.workspace = workspace

    def entries(self) -> list[ProjectileCatalogEntry]:
        result = [
            self._entry(
                definition.definition_id,
                definition.data,
                "authored",
            )
            for definition in self.workspace.definitions("projectiles")
        ]
        result = [
            entry
            for entry in result
            if self._has_visual(entry.visual_id)
        ]
        result.sort(key=lambda entry: entry.definition_id)
        return result

    def animation_ids(self) -> list[str]:
        return sorted(
            definition.definition_id
            for definition in self.workspace.definitions("animations")
            if definition.data.get("frames")
        )

    def definition_data(
        self,
        definition_id: str,
    ) -> dict[str, object] | None:
        authored = self.workspace.find(
            "projectiles",
            definition_id,
        )

        return copy.deepcopy(authored.data) if authored else None

    def update_spawn_offsets(
        self,
        definition_id: str,
        offsets: dict[str, dict[str, int]],
        canonical_facing: str | None = None,
        render_layer: str | None = None,
        render_layers: dict[str, str] | None = None,
        maximum_distance: int | None = None,
        impact_animation: str | None = None,
        expire_animation: str | None = None,
        expire_animations: dict[str, str] | None = None,
        impact_animation_facing: str | None = None,
        expire_animation_facing: str | None = None,
        flip_x: dict[str, bool] | None = None,
        impact_animations: dict[str, str] | None = None,
        impact_animation_facings: dict[str, str] | None = None,
        expire_animation_facings: dict[str, str] | None = None,
    ) -> None:
        data = self.definition_data(definition_id)

        if data is None:
            raise ValueError(
                f"unknown projectile: {definition_id}"
            )

        data["spawnOffsets"] = {
            direction: {
                "x": int(offsets[direction]["x"]),
                "y": int(offsets[direction]["y"]),
            }
            for direction in ("down", "up", "left", "right")
        }

        if canonical_facing is not None:
            if canonical_facing not in (
                "down",
                "up",
                "left",
                "right",
            ):
                raise ValueError(
                    "canonicalFacing must be one of "
                    "down/up/left/right"
                )

            data["canonicalFacing"] = canonical_facing

        if render_layer is not None:
            if render_layer not in ("actor", "world"):
                raise ValueError(
                    "renderLayer must be actor or world"
                )

            data["renderLayer"] = render_layer

        if render_layers is not None:
            normalized: dict[str, str] = {}

            for direction, layer in render_layers.items():
                if direction not in (
                    "down",
                    "up",
                    "left",
                    "right",
                ):
                    raise ValueError(
                        "renderLayers direction must be one of "
                        "down/up/left/right"
                    )

                if layer not in ("actor", "world"):
                    raise ValueError(
                        "renderLayers values must be actor or world"
                    )

                normalized[direction] = layer

            if normalized:
                data["renderLayers"] = normalized
            else:
                data.pop("renderLayers", None)

        if maximum_distance is not None:
            maximum_distance = int(maximum_distance)

            if maximum_distance < 0:
                raise ValueError(
                    "maximumDistancePixels must be zero or positive"
                )

            if maximum_distance > 0:
                data["maximumDistancePixels"] = maximum_distance
            else:
                data.pop("maximumDistancePixels", None)

        for field, field_name in (
            (impact_animation, "impactAnimationId"),
            (expire_animation, "expireAnimationId"),
        ):
            if field is None:
                continue

            if field:
                animation = self.workspace.find(
                    "animations",
                    field,
                )

                if animation is None:
                    raise ValueError(
                        f"unknown animation: {field}"
                    )

                if animation.data.get("loop"):
                    raise ValueError(
                        f"{field_name} animation must not loop"
                    )

                data[field_name] = field
            else:
                data.pop(field_name, None)

        for facing_value, facing_field in (
            (impact_animation_facing, "impactAnimationFacing"),
            (expire_animation_facing, "expireAnimationFacing"),
        ):
            if facing_value is None:
                continue

            if facing_value not in (
                "down",
                "up",
                "left",
                "right",
            ):
                raise ValueError(
                    f"{facing_field} must be one of down/up/left/right"
                )

            if facing_value == "up":
                data.pop(facing_field, None)
            else:
                data[facing_field] = facing_value

        for source, category, kind in (
            (impact_animations, "impactAnimations", "animation"),
            (impact_animation_facings, "impactAnimationFacings", "facing"),
            (expire_animation_facings, "expireAnimationFacings", "facing"),
        ):
            if source is None:
                continue

            normalized_entries: dict[str, str] = {}

            for direction, entry_value in source.items():
                if direction not in (
                    "down",
                    "up",
                    "left",
                    "right",
                ):
                    raise ValueError(
                        f"{category} direction must be one of down/up/left/right"
                    )

                if kind == "facing":
                    if entry_value not in (
                        "down",
                        "up",
                        "left",
                        "right",
                    ):
                        raise ValueError(
                            f"{category} values must be down/up/left/right"
                        )

                    normalized_entries[direction] = entry_value
                    continue

                animation_definition = self.workspace.find(
                    "animations",
                    entry_value,
                )

                if animation_definition is None:
                    raise ValueError(
                        f"unknown animation: {entry_value}"
                    )

                if animation_definition.data.get("loop"):
                    raise ValueError(
                        f"{category} animation must not loop"
                    )

                normalized_entries[direction] = entry_value

            if normalized_entries:
                data[category] = normalized_entries
            else:
                data.pop(category, None)

        if flip_x is not None:
            normalized_flip: dict[str, bool] = {}

            for direction, enabled in flip_x.items():
                if direction not in (
                    "down",
                    "up",
                    "left",
                    "right",
                ):
                    raise ValueError(
                        "flipX direction must be one of down/up/left/right"
                    )

                if enabled:
                    normalized_flip[direction] = True

            if normalized_flip:
                data["flipX"] = normalized_flip
            else:
                data.pop("flipX", None)

        if expire_animations is not None:
            normalized_expire: dict[str, str] = {}

            for direction, animation_id in expire_animations.items():
                if direction not in (
                    "down",
                    "up",
                    "left",
                    "right",
                ):
                    raise ValueError(
                        "expireAnimations direction must be one of "
                        "down/up/left/right"
                    )

                if not animation_id:
                    continue

                animation = self.workspace.find(
                    "animations",
                    animation_id,
                )

                if animation is None:
                    raise ValueError(
                        f"unknown animation: {animation_id}"
                    )

                if animation.data.get("loop"):
                    raise ValueError(
                        "expire animation must not loop"
                    )

                normalized_expire[direction] = animation_id

            if normalized_expire:
                data["expireAnimations"] = normalized_expire
            else:
                data.pop("expireAnimations", None)

        self.workspace.upsert_definition_bundle(
            "Update Projectile Spawn Offsets",
            [("projectiles", definition_id, data)],
        )

    def ensure_for_animation(self, animation_id: str) -> str:
        """Return the projectile bound to an animation, creating it on demand.

        The projectile id is derived from the animation id (an
        'animation.foo' becomes 'projectile.foo'), so selecting an
        animation in any dropdown never requires typing an id. An
        existing definition is rebound when the animation differs.
        """
        normalized_animation = animation_id.strip()

        if not normalized_animation:
            raise ValueError("animation id is required")

        animation = self.workspace.find(
            "animations",
            normalized_animation,
        )

        if animation is None:
            raise ValueError(
                f"unknown animation: {normalized_animation}"
            )

        projectile_id = self._projectile_id_for_animation(
            normalized_animation
        )

        existing = self.workspace.find(
            "projectiles",
            projectile_id,
        )

        if existing is None:
            self.create_from_animation(
                projectile_id,
                normalized_animation,
            )

            return projectile_id

        if existing.data.get("animationId") != normalized_animation:
            data = copy.deepcopy(existing.data)

            data["animationId"] = normalized_animation

            self.workspace.upsert_definition_bundle(
                "Rebind Projectile Animation",
                [("projectiles", projectile_id, data)],
            )

        return projectile_id

    @staticmethod
    def _projectile_id_for_animation(animation_id: str) -> str:
        if animation_id.startswith("animation."):
            return "projectile." + animation_id[len("animation."):]

        return "projectile." + animation_id

    def create_from_animation(
        self,
        definition_id: str,
        animation_id: str,
    ) -> str:
        normalized_id = definition_id.strip()
        normalized_animation = animation_id.strip()

        if not normalized_id:
            raise ValueError("projectile id is required")
        if self.workspace.find("projectiles", normalized_id) is not None:
            raise ValueError(
                f"projectile already exists: {normalized_id}"
            )

        animation = self.workspace.find(
            "animations",
            normalized_animation,
        )

        if animation is None:
            raise ValueError(
                f"unknown animation: {normalized_animation}"
            )

        frames = animation.data.get("frames")

        if not isinstance(frames, list) or not frames:
            raise ValueError(
                "projectile animation requires at least one frame"
            )

        frame = frames[0]

        if not isinstance(frame, dict):
            raise ValueError(
                "projectile animation frame is invalid"
            )

        image_id = animation.data.get("imageId")
        source = frame.get("source")
        anchor = frame.get("anchor")

        if not isinstance(image_id, str) or not image_id:
            raise ValueError(
                "projectile animation requires imageId"
            )
        if self.workspace.find("visualImages", image_id) is None:
            raise ValueError(
                f"unknown animation image: {image_id}"
            )
        if not isinstance(source, dict):
            raise ValueError(
                "projectile animation frame requires source"
            )
        if not isinstance(anchor, dict):
            source_width = int(source.get("width", 16))
            source_height = int(source.get("height", 16))
            anchor = {
                "x": source_width // 2,
                "y": max(0, source_height - 1),
            }

        width = max(1, int(source.get("width", 16)))
        height = max(1, int(source.get("height", 16)))
        hitbox = max(4, min(width, height) // 2)
        visual_id = self._visual_id(normalized_id)
        expected_sprite = {
            "id": visual_id,
            "imageId": image_id,
            "source": copy.deepcopy(source),
            "anchor": copy.deepcopy(anchor),
        }
        entries: list[tuple[str, str, dict[str, object]]] = []
        existing_sprite = self.workspace.find(
            "staticSprites",
            visual_id,
        )

        if existing_sprite is None:
            entries.append(
                (
                    "staticSprites",
                    visual_id,
                    expected_sprite,
                )
            )
        elif not self._same_sprite(
            existing_sprite.data,
            expected_sprite,
        ):
            raise ValueError(
                f"generated visual conflict: {visual_id}"
            )

        entries.append(
            (
                "projectiles",
                normalized_id,
                {
                    "id": normalized_id,
                    "visualId": visual_id,
                    "canonicalFacing": (
                        "right" if width >= height else "up"
                    ),
                    "speedPixelsPerTick": 4,
                    "lifetimeTicks": 120,
                    "hitboxWidth": hitbox,
                    "hitboxHeight": hitbox,
                    "spawnOffsets": {
                        "down": {"x": 0, "y": 0},
                        "up": {"x": 0, "y": 0},
                        "left": {"x": 0, "y": 0},
                        "right": {"x": 0, "y": 0},
                    },
                    "renderLayer": "actor",
                    "animationId": normalized_animation,
                },
            )
        )
        self.workspace.create_definition_bundle(
            "Create Projectile",
            entries,
        )
        return normalized_id

    def _has_visual(self, visual_id: str) -> bool:
        if not visual_id:
            return False

        sprite = self.workspace.find("staticSprites", visual_id)

        if sprite is None:
            return False

        image_id = sprite.data.get("imageId")

        if not isinstance(image_id, str) or not image_id:
            return False

        return (
            self.workspace.find("visualImages", image_id)
            is not None
        )

    def _entry(
        self,
        definition_id: str,
        data: dict[str, object],
        status: str,
    ) -> ProjectileCatalogEntry:
        visual_id = data.get("visualId")

        return ProjectileCatalogEntry(
            definition_id=definition_id,
            visual_id=visual_id if isinstance(visual_id, str) else "",
            status=status,
        )

    @staticmethod
    def _visual_id(definition_id: str) -> str:
        if definition_id.startswith("projectile."):
            return f"visual.{definition_id[len('projectile.'):]}"

        safe_id = definition_id.replace(".", "_")

        return f"visual.projectile.{safe_id}"

    @staticmethod
    def _same_sprite(
        current: object,
        expected: dict[str, object],
    ) -> bool:
        if not isinstance(current, dict):
            return False

        if current.get("imageId") != expected["imageId"]:
            return False
        if current.get("anchor") != expected["anchor"]:
            return False

        source = current.get("source")

        return source == expected["source"]
