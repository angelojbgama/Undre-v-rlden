from __future__ import annotations

import os
import unittest

os.environ.setdefault(
    "QT_QPA_PLATFORM",
    "offscreen",
)

try:
    from PySide6.QtWidgets import QApplication
except ImportError:  # pragma: no cover
    QApplication = None  # type: ignore[assignment]

from tools.content_studio.interaction.selection_controller import (
    Selection,
)
from tools.content_studio.model.map_document import (
    MapDocument,
)


@unittest.skipIf(
    QApplication is None,
    "PySide6 is not installed",
)
class MapCanvasMoveReentrancyTests(
        unittest.TestCase
):
    @classmethod
    def setUpClass(cls) -> None:
        assert QApplication is not None

        cls.application = (
            QApplication.instance()
            or QApplication([])
        )

    def test_document_refresh_may_clear_moving_without_crashing(
            self,
    ) -> None:
        from tools.content_studio.ui.map_canvas import (
            MapCanvas,
        )

        document = MapDocument.new(
            "map.reentrant-move",
            8,
            8,
            16,
        )

        object_id = document.add_entity(
            "objects",
            "object.crate",
            16,
            16,
        )

        canvas = MapCanvas()

        self.addCleanup(
            canvas.deleteLater
        )

        canvas.set_context(
            document,
            None,
            None,
        )

        canvas.set_tool(
            "select"
        )

        selection = Selection(
            "objects",
            object_id,
        )

        canvas._moving = selection

        canvas.renderer.moving_selection = (
            selection.as_tuple()
        )

        # Simula exatamente a reentrancia que acontece
        # durante document_changed no Studio.
        #
        # Um callback de UI pode legitimamente trocar
        # a ferramenta e, portanto, zerar _moving.
        canvas.document_changed.connect(
            lambda:
            canvas.set_tool(
                "none"
            )
        )

        canvas._move_selection(
            (
                32,
                32,
            )
        )

        moved = document.entity(
            "objects",
            object_id,
        )

        self.assertIsNotNone(
            moved
        )

        assert moved is not None

        self.assertEqual(
            {
                "x": 32,
                "y": 32,
            },
            moved["position"],
        )

        # O callback pode mudar o estado atual da UI.
        # Isso nao pode invalidar a operacao que ja estava
        # em andamento.
        self.assertEqual(
            "none",
            canvas.tool,
        )

        self.assertIsNone(
            canvas._moving
        )


if __name__ == "__main__":
    unittest.main()
