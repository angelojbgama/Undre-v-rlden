"""Tests for the structured diagnostics panel and toolchain parsing (audit S2)."""

from __future__ import annotations

import os
import unittest
from pathlib import Path

try:
    from PySide6.QtWidgets import QApplication
except ImportError:  # pragma: no cover - exercised on minimal CI images
    QApplication = None  # type: ignore[assignment,misc]

from tools.content_studio.model.types import Diagnostic, ToolResult
from tools.content_studio.services.toolchain import CppToolchain
from tools.content_studio.ui.diagnostics_panel import DiagnosticsPanel


@unittest.skipIf(QApplication is None, "PySide6 is not installed")
class ToolchainDiagnosticsParsingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        cls.application = QApplication.instance() or QApplication([])

    def test_severity_prefixes_are_parsed(self) -> None:
        result = ToolResult(0, "[error] missing required field x\n"
                               "[warning] unused visual\n"
                               "[info] definitions: 313", "")
        parsed = CppToolchain.diagnostics(result)
        self.assertEqual(["error", "warning", "info"], [item.severity for item in parsed])
        self.assertEqual("missing required field x", parsed[0].message)
        self.assertEqual("unused visual", parsed[1].message)

    def test_summary_counts_become_info(self) -> None:
        result = ToolResult(0, "[warning] files: 1\n[warning] definitions: 313", "")
        parsed = CppToolchain.diagnostics(result)
        self.assertEqual(["info", "info"], [item.severity for item in parsed])
        self.assertFalse(any(item.is_error for item in parsed))

    def test_source_root_prefix_is_stripped(self) -> None:
        root = Path("/tmp/content/definitions")
        result = ToolResult(0, f"{root}: [warning] files: 1", "")
        parsed = CppToolchain.diagnostics(result, source_path=root)
        self.assertEqual("files: 1", parsed[0].message)

    def test_failing_tool_lines_stay_errors(self) -> None:
        result = ToolResult(1, "unexpected failure", "")
        parsed = CppToolchain.diagnostics(result)
        self.assertEqual("error", parsed[0].severity)


@unittest.skipIf(QApplication is None, "PySide6 is not installed")
class DiagnosticsPanelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        cls.application = QApplication.instance() or QApplication([])

    def _panel(self):
        panel = DiagnosticsPanel()
        self.addCleanup(panel.deleteLater)
        return panel

    def test_items_render_with_severity_icons_and_counts(self) -> None:
        panel = self._panel()
        panel.set_diagnostics([
            Diagnostic("error", "boom", definition_id="object.chest"),
            Diagnostic("warning", "careful"),
            Diagnostic("info", "files: 3"),
        ])
        self.assertEqual(3, panel.list.count())
        self.assertIn("Erros: 1", panel.counts.text())
        self.assertIn("Avisos: 1", panel.counts.text())
        self.assertIn("Info: 1", panel.counts.text())
        self.assertFalse(panel.list.item(0).icon().isNull())

    def test_filter_limits_visible_items(self) -> None:
        panel = self._panel()
        panel.set_diagnostics([
            Diagnostic("error", "boom"),
            Diagnostic("warning", "careful"),
            Diagnostic("info", "files: 3"),
        ])
        panel.filter.setCurrentIndex(1)  # errors only
        self.assertEqual(1, panel.list.count())
        panel.filter.setCurrentIndex(0)  # back to everything
        self.assertEqual(3, panel.list.count())

    def test_activation_emits_definition_navigation(self) -> None:
        panel = self._panel()
        received: list[str] = []
        panel.definition_requested.connect(received.append)
        panel.set_diagnostics([Diagnostic("warning", "conflict", definition_id="tileset.x")])
        item = panel.list.item(0)
        panel._item_activated(item)
        self.assertEqual(["tileset.x"], received)

    def test_empty_state_when_no_diagnostics(self) -> None:
        panel = self._panel()
        panel.set_diagnostics([])
        self.assertEqual(1, panel.list.count())
        self.assertIn("Nenhum problema", panel.list.item(0).text())


if __name__ == "__main__":
    unittest.main()
