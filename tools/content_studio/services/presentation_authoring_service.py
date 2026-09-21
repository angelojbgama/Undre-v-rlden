"""First-class Presentation Effect authoring on top of the ContentWorkspace.

Validation mirrors the C++ ContentValidator rules for the
``presentationEffects`` category (bounded durations, priorities, primitives,
and per-lifetime restrictions) so the Studio rejects up front exactly what the
runtime would reject at load.
"""

from __future__ import annotations

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition, JsonValue

EFFECT_ID_PREFIX = "effect."

LIFETIMES = ("transient", "persistent")
OVERLAY_MODES = ("constant", "linearFadeOut", "pulse")
COMPOSITION_LAYERS = ("world", "final")

MAX_DURATION_TICKS = 100_000
MIN_PRIORITY = -1000
MAX_PRIORITY = 1000
MAX_SHAKE_AMPLITUDE = 64
MAX_VISION_RADIUS = 4096
MAX_ALPHA = 255

_COLOR_CHANNELS = ("r", "g", "b", "a")


class PresentationAuthoringService:
    """CRUD facade for authored presentation effect definitions."""

    def __init__(self, workspace: ContentWorkspace | None = None) -> None:
        self.workspace = workspace

    def set_context(self, workspace: ContentWorkspace | None) -> None:
        self.workspace = workspace

    def effects(self, query: str = "") -> tuple[ContentDefinition, ...]:
        workspace = self._require_workspace()
        return tuple(workspace.definitions("presentationEffects", query))

    def find(self, effect_id: str) -> ContentDefinition | None:
        workspace = self._require_workspace()
        return workspace.find("presentationEffects", effect_id)

    def referenced_by(self, effect_id: str) -> tuple[str, ...]:
        """Attacks in the content workspace that bind this effect.

        Map-level references (region ``environmentEffectId``, world-rule
        actions and scene clips) live in map documents; the runtime validates
        those against the compiled catalog when a map loads.
        """
        workspace = self._require_workspace()
        referencing: list[str] = []
        for attack in workspace.definitions("attacks"):
            if attack.data.get("presentationEffectId") == effect_id:
                referencing.append(attack.definition_id)
        return tuple(referencing)

    def create_effect(
        self,
        effect_id: str,
        data: dict[str, object],
    ) -> ContentDefinition:
        workspace = self._require_workspace()
        normalized_id = self._normalize_effect_id(effect_id)
        if workspace.find("presentationEffects", normalized_id):
            raise ValueError(f"presentation effect already exists: {normalized_id}")

        payload = dict(data)
        payload["id"] = normalized_id
        normalized = self._validate(payload)

        workspace.create_definition_bundle(
            "Create Presentation Effect",
            [
                (
                    "presentationEffects",
                    normalized_id,
                    normalized,
                ),
            ],
        )

        result = workspace.find("presentationEffects", normalized_id)
        if result is None:
            raise RuntimeError("created presentation effect could not be indexed")
        return result

    def configure(
        self,
        effect_id: str,
        data: dict[str, object],
    ) -> None:
        workspace = self._require_workspace()
        definition = workspace.find("presentationEffects", effect_id)

        if definition is None:
            raise ValueError(
                f"unknown presentation effect: "
                f"{effect_id}"
            )

        if not isinstance(
            data.get("id"),
            str,
        ) or data["id"] != effect_id:
            raise ValueError(
                "presentation effect id does not match the edited definition"
            )

        normalized = self._validate(data)

        workspace.upsert_definition_bundle(
            "Edit Presentation Effect",
            [
                (
                    "presentationEffects",
                    effect_id,
                    normalized,
                )
            ],
        )

    def delete(self, effect_id: str) -> None:
        workspace = self._require_workspace()
        definition = workspace.find("presentationEffects", effect_id)

        if definition is None:
            raise ValueError(
                f"unknown presentation effect: "
                f"{effect_id}"
            )

        referencing = self.referenced_by(effect_id)

        if referencing:
            raise ValueError(
                "presentation effect is referenced by attacks: "
                + ", ".join(referencing)
            )

        workspace.delete_definition(definition)

    def _validate(
        self,
        data: dict[str, object],
    ) -> dict[str, object]:
        lifetime = data.get(
            "lifetime"
        )

        if lifetime not in LIFETIMES:
            raise ValueError(
                f"unknown presentation effect lifetime: {lifetime!r}"
            )

        priority = data.get(
            "priority",
            0,
        )

        if not self._is_int(priority):
            raise ValueError(
                "presentation effect priority must be an integer"
            )

        if not MIN_PRIORITY <= priority <= MAX_PRIORITY:
            raise ValueError(
                "presentation effect priority is outside bounds"
            )

        camera_shake = self._validate_camera_shake(
            data.get("cameraShake"),
            str(lifetime),
        )

        overlay = self._validate_overlay(
            data.get("overlay")
        )

        vision_mask = self._validate_vision_mask(
            data.get("visionMask")
        )

        fade = self._validate_fade(
            data.get("fade"),
            str(lifetime),
        )

        if (
            camera_shake is None
            and overlay is None
            and vision_mask is None
            and fade is None
        ):
            raise ValueError(
                "presentation effect must define at least one primitive"
            )

        normalized: dict[str, object] = {
            "id": data["id"],
            "lifetime": lifetime,
            "durationTicks": 0,
            "priority": int(priority),
        }

        if str(lifetime) == "transient":
            duration = data.get(
                "durationTicks"
            )

            if not self._is_int(duration):
                raise ValueError(
                    "transient presentation effect durationTicks must be "
                    "an integer"
                )

            if duration <= 0 or duration > MAX_DURATION_TICKS:
                raise ValueError(
                    "transient presentation effect duration is outside "
                    "bounds"
                )

            normalized["durationTicks"] = int(duration)

        if camera_shake is not None:
            normalized["cameraShake"] = camera_shake

        if overlay is not None:
            normalized["overlay"] = overlay

        if vision_mask is not None:
            normalized["visionMask"] = vision_mask

        if fade is not None:
            normalized["fade"] = fade

        return normalized

    def _validate_camera_shake(
        self,
        camera_shake: object,
        lifetime: str,
    ) -> dict[str, object] | None:
        if camera_shake is None:
            return None

        if not isinstance(camera_shake, dict):
            raise ValueError(
                "presentation effect cameraShake must be an object"
            )

        if lifetime == "persistent":
            raise ValueError(
                "persistent presentation effects cannot contain camera "
                "shake"
            )

        amplitude = camera_shake.get(
            "amplitudePixels"
        )

        if not self._is_int(amplitude):
            raise ValueError(
                "camera shake amplitudePixels must be an integer"
            )

        if amplitude <= 0 or amplitude > MAX_SHAKE_AMPLITUDE:
            raise ValueError(
                "camera shake amplitude is outside bounds"
            )

        return {
            "amplitudePixels": int(amplitude),
        }

    def _validate_overlay(
        self,
        overlay: object,
    ) -> dict[str, object] | None:
        if overlay is None:
            return None

        if not isinstance(overlay, dict):
            raise ValueError(
                "presentation effect overlay must be an object"
            )

        mode = overlay.get(
            "mode",
            "constant",
        )

        if mode not in OVERLAY_MODES:
            raise ValueError(
                f"overlay mode is invalid: {mode!r}"
            )

        pulse_period = overlay.get(
            "pulsePeriodTicks",
            0,
        )

        if not self._is_int(pulse_period):
            raise ValueError(
                "overlay pulsePeriodTicks must be an integer"
            )

        if mode == "pulse" and (
            pulse_period <= 0
            or pulse_period > MAX_DURATION_TICKS
        ):
            raise ValueError(
                "pulse period must be positive and bounded"
            )

        layer = overlay.get(
            "layer",
            "world",
        )

        if layer not in COMPOSITION_LAYERS:
            raise ValueError(
                f"overlay layer is invalid: {layer!r}"
            )

        return {
            "color": self._validate_color(
                overlay.get("color"),
                "overlay.color",
            ),
            "mode": mode,
            "pulsePeriodTicks": int(pulse_period),
            "layer": layer,
        }

    def _validate_vision_mask(
        self,
        vision_mask: object,
    ) -> dict[str, object] | None:
        if vision_mask is None:
            return None

        if not isinstance(vision_mask, dict):
            raise ValueError(
                "presentation effect visionMask must be an object"
            )

        inner = vision_mask.get(
            "innerRadiusPixels",
            0,
        )

        outer = vision_mask.get(
            "outerRadiusPixels",
        )

        outside_alpha = vision_mask.get(
            "outsideAlpha",
            MAX_ALPHA,
        )

        for name, value in (
            ("innerRadiusPixels", inner),
            ("outerRadiusPixels", outer),
            ("outsideAlpha", outside_alpha),
        ):
            if not self._is_int(value):
                raise ValueError(
                    f"vision mask {name} must be an integer"
                )

        assert isinstance(inner, int)
        assert isinstance(outer, int)

        if inner < 0 or outer <= 0 or outer < inner or outer > MAX_VISION_RADIUS:
            raise ValueError(
                "vision mask radii are invalid"
            )

        if not 0 <= outside_alpha <= MAX_ALPHA:
            raise ValueError(
                "vision mask outsideAlpha must be between 0 and 255"
            )

        return {
            "innerRadiusPixels": int(inner),
            "outerRadiusPixels": int(outer),
            "outsideAlpha": int(outside_alpha),
            "color": self._validate_color(
                vision_mask.get("color"),
                "visionMask.color",
            ),
        }

    def _validate_fade(
        self,
        fade: object,
        lifetime: str,
    ) -> dict[str, object] | None:
        if fade is None:
            return None

        if not isinstance(fade, dict):
            raise ValueError(
                "presentation effect fade must be an object"
            )

        if lifetime == "persistent":
            raise ValueError(
                "persistent presentation effects cannot contain fades"
            )

        start_alpha = fade.get(
            "startAlpha"
        )

        end_alpha = fade.get(
            "endAlpha"
        )

        for name, value in (
            ("startAlpha", start_alpha),
            ("endAlpha", end_alpha),
        ):
            if not self._is_int(value):
                raise ValueError(
                    f"fade {name} must be an integer"
                )

            if not 0 <= value <= MAX_ALPHA:
                raise ValueError(
                    f"fade {name} must be between 0 and 255"
                )

        assert isinstance(start_alpha, int)
        assert isinstance(end_alpha, int)

        return {
            "color": self._validate_color(
                fade.get("color"),
                "fade.color",
            ),
            "startAlpha": int(start_alpha),
            "endAlpha": int(end_alpha),
        }

    def _validate_color(
        self,
        color: object,
        field: str,
    ) -> dict[str, int]:
        if not isinstance(color, dict):
            raise ValueError(
                f"{field} must be an object with r/g/b/a channels"
            )

        channels: dict[str, int] = {}

        for channel in _COLOR_CHANNELS:
            value = color.get(
                channel
            )

            if not self._is_int(value):
                raise ValueError(
                    f"{field}.{channel} must be an integer"
                )

            if not 0 <= value <= MAX_ALPHA:
                raise ValueError(
                    f"{field}.{channel} must be between 0 and 255"
                )

            channels[channel] = int(value)

        return channels

    def _require_workspace(self) -> ContentWorkspace:
        if self.workspace is None:
            raise ValueError("content workspace is unavailable")
        return self.workspace

    def _normalize_effect_id(self, effect_id: str) -> str:
        normalized = effect_id.strip()
        if (
            not normalized.startswith(EFFECT_ID_PREFIX)
            or len(normalized) <= len(EFFECT_ID_PREFIX)
        ):
            raise ValueError(
                "presentation effect id must use the effect.* namespace"
            )
        return normalized

    @staticmethod
    def _is_int(
        value: object,
    ) -> bool:
        return isinstance(value, int) and not isinstance(
            value, bool
        )
