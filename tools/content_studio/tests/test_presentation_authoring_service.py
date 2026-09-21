from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from tools.content_studio.formats.content_json import (
    CONTENT_CATEGORIES,
    CONTENT_VERSION,
)
from tools.content_studio.model.content_workspace import (
    ContentWorkspace,
)


def presentation_content() -> dict[str, object]:
    data: dict[str, object] = {
        "format": "dungeon-underworld-content",
        "version": CONTENT_VERSION,
    }

    data.update({
        category: []
        for category in CONTENT_CATEGORIES
    })

    return data


def make_workspace(data: dict[str, object]) -> tuple[tempfile.TemporaryDirectory, ContentWorkspace]:
    temporary = tempfile.TemporaryDirectory()

    root = Path(temporary.name)

    (root / "content.json").write_text(
        json.dumps(data, indent=2),
        encoding="utf-8",
    )

    return temporary, ContentWorkspace.open(root)


def heavy_impact_data() -> dict[str, object]:
    return {
        "id": "effect.world.heavy_impact",
        "lifetime": "transient",
        "durationTicks": 24,
        "priority": 80,
        "cameraShake": {"amplitudePixels": 6},
        "overlay": {
            "color": {"r": 255, "g": 255, "b": 255, "a": 100},
            "mode": "linearFadeOut",
            "pulsePeriodTicks": 0,
            "layer": "final",
        },
    }


class PresentationAuthoringServiceTests(unittest.TestCase):
    def test_create_and_configure_transient_effect(self) -> None:
        from tools.content_studio.services.presentation_authoring_service import (
            PresentationAuthoringService,
        )

        temporary, workspace = make_workspace(presentation_content())

        try:
            service = PresentationAuthoringService(workspace)

            created = service.create_effect(
                "effect.world.heavy_impact",
                heavy_impact_data(),
            )

            self.assertEqual(
                "effect.world.heavy_impact",
                created.definition_id,
            )
            self.assertEqual(
                "transient",
                created.data["lifetime"],
            )
            self.assertEqual(
                24,
                created.data["durationTicks"],
            )
            self.assertEqual(
                {"amplitudePixels": 6},
                created.data["cameraShake"],
            )

            updated = dict(heavy_impact_data())
            updated["durationTicks"] = 40
            updated["cameraShake"] = {"amplitudePixels": 2}

            service.configure(
                "effect.world.heavy_impact",
                updated,
            )

            stored = service.find("effect.world.heavy_impact")

            assert stored is not None

            self.assertEqual(40, stored.data["durationTicks"])
            self.assertEqual({"amplitudePixels": 2}, stored.data["cameraShake"])
        finally:
            temporary.cleanup()

    def test_create_normalizes_id_and_rejects_duplicates(self) -> None:
        from tools.content_studio.services.presentation_authoring_service import (
            PresentationAuthoringService,
        )

        temporary, workspace = make_workspace(presentation_content())

        try:
            service = PresentationAuthoringService(workspace)
            service.create_effect("effect.dark", {"lifetime": "persistent", "priority": 10,
                                                  "visionMask": {
                                                      "innerRadiusPixels": 48,
                                                      "outerRadiusPixels": 72,
                                                      "outsideAlpha": 220,
                                                      "color": {"r": 0, "g": 0, "b": 0, "a": 255}}})

            self.assertIsNotNone(service.find("effect.dark"))

            with self.assertRaisesRegex(ValueError, "already exists"):
                service.create_effect("effect.dark", {"lifetime": "persistent"})

            with self.assertRaisesRegex(ValueError, "effect\\..*namespace"):
                service.create_effect("dark_ambient", {"lifetime": "persistent"})
        finally:
            temporary.cleanup()

    def test_persistent_effect_drops_duration_and_rejects_transient_primitives(self) -> None:
        from tools.content_studio.services.presentation_authoring_service import (
            PresentationAuthoringService,
        )

        temporary, workspace = make_workspace(presentation_content())

        try:
            service = PresentationAuthoringService(workspace)

            data = {
                "id": "effect.environment.dark",
                "lifetime": "persistent",
                "durationTicks": 99,
                "priority": 10,
                "visionMask": {
                    "innerRadiusPixels": 48,
                    "outerRadiusPixels": 72,
                    "outsideAlpha": 220,
                    "color": {"r": 0, "g": 0, "b": 0, "a": 255},
                },
            }

            service.create_effect("effect.environment.dark", data)

            stored = service.find("effect.environment.dark")

            assert stored is not None

            # The runtime ignores persistent durations; the Studio normalizes.
            self.assertEqual(0, stored.data["durationTicks"])

            with self.assertRaisesRegex(ValueError, "camera shake"):
                service.configure("effect.environment.dark", {
                    **data,
                    "cameraShake": {"amplitudePixels": 4},
                })

            with self.assertRaisesRegex(ValueError, "fade"):
                service.configure("effect.environment.dark", {
                    **data,
                    "fade": {
                        "color": {"r": 0, "g": 0, "b": 0, "a": 255},
                        "startAlpha": 0,
                        "endAlpha": 255,
                    },
                })
        finally:
            temporary.cleanup()

    def test_validation_rejects_invalid_configurations(self) -> None:
        from tools.content_studio.services.presentation_authoring_service import (
            PresentationAuthoringService,
        )

        temporary, workspace = make_workspace(presentation_content())

        try:
            service = PresentationAuthoringService(workspace)
            invalid_payloads = (
                {"lifetime": "forever"},
                {"lifetime": "transient", "durationTicks": 0,
                 "cameraShake": {"amplitudePixels": 4}},
                {"lifetime": "transient", "durationTicks": 100_001,
                 "cameraShake": {"amplitudePixels": 4}},
                {"lifetime": "transient", "durationTicks": 12, "priority": 1001,
                 "cameraShake": {"amplitudePixels": 4}},
                {"lifetime": "transient", "durationTicks": 12},
                {"lifetime": "transient", "durationTicks": 12,
                 "cameraShake": {"amplitudePixels": 0}},
                {"lifetime": "transient", "durationTicks": 12,
                 "cameraShake": {"amplitudePixels": 65}},
                {"lifetime": "transient", "durationTicks": 12,
                 "overlay": {"color": {"r": 0, "g": 0, "b": 0, "a": 0},
                             "mode": "pulse", "pulsePeriodTicks": 0}},
                {"lifetime": "transient", "durationTicks": 12,
                 "visionMask": {"innerRadiusPixels": 80,
                                "outerRadiusPixels": 72,
                                "outsideAlpha": 220,
                                "color": {"r": 0, "g": 0, "b": 0, "a": 255}}},
                {"lifetime": "transient", "durationTicks": 12,
                 "overlay": {"color": {"r": 0, "g": 0, "b": 300, "a": 0},
                             "mode": "constant", "pulsePeriodTicks": 0}},
            )

            for invalid in invalid_payloads:
                with self.assertRaises(ValueError, msg=str(invalid)):
                    service.create_effect("effect.invalid", invalid)
        finally:
            temporary.cleanup()

    def test_delete_blocks_referenced_effect(self) -> None:
        from tools.content_studio.services.attack_authoring_service import (
            AttackAuthoringService,
        )
        from tools.content_studio.services.presentation_authoring_service import (
            PresentationAuthoringService,
        )

        data = presentation_content()
        data["attacks"] = [
            {
                "id": "attack.player.sword",
                "kind": "meleeHitbox",
                "damage": {"amount": 1, "knockbackPixels": 4},
                "totalTicks": 12,
                "cooldownTicks": 0,
                "minimumRangePixels": 0,
                "maximumRangePixels": 24,
                "visualActionId": "visual.player.sword",
                "meleeHitboxes": {
                    direction: {"offsetX": 0, "offsetY": 0, "width": 8, "height": 8}
                    for direction in ("down", "up", "left", "right")
                },
                "timeline": [],
                "presentationEffectId": "effect.world.heavy_impact",
            },
        ]

        temporary, workspace = make_workspace(data)

        try:
            service = PresentationAuthoringService(workspace)
            service.create_effect(
                "effect.world.heavy_impact",
                heavy_impact_data(),
            )

            with self.assertRaisesRegex(ValueError, "referenced by attacks"):
                service.delete("effect.world.heavy_impact")

            attack = workspace.find("attacks", "attack.player.sword")

            assert attack is not None

            without_reference = dict(attack.data)
            without_reference.pop("presentationEffectId")

            AttackAuthoringService(workspace).configure(
                "attack.player.sword",
                without_reference,
            )

            service.delete("effect.world.heavy_impact")

            self.assertIsNone(service.find("effect.world.heavy_impact"))
        finally:
            temporary.cleanup()


if __name__ == "__main__":
    unittest.main()
