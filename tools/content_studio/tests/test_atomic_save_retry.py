from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.content_studio.formats import json_io


def windows_error(
    code: int,
) -> PermissionError:
    error = PermissionError(
        13,
        "locked",
    )

    error.winerror = code

    return error


class AtomicSaveRetryTests(
    unittest.TestCase
):
    def test_write_atomic_retries_transient_windows_replace_lock(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = (
                Path(directory)
                / "world.uworld"
            )

            target.write_text(
                '{"old": true}\n',
                encoding="utf-8",
            )

            real_replace = (
                json_io.os.replace
            )

            attempts = 0

            def flaky_replace(
                source,
                destination,
            ):
                nonlocal attempts

                attempts += 1

                if attempts < 3:
                    raise windows_error(
                        5
                    )

                return real_replace(
                    source,
                    destination,
                )

            with (
                patch.object(
                    json_io.os,
                    "replace",
                    side_effect=flaky_replace,
                ),
                patch(
                    "time.sleep"
                ) as sleep,
            ):
                json_io.write_atomic(
                    target,
                    {
                        "value": 7,
                    },
                )

            self.assertEqual(
                3,
                attempts,
            )

            self.assertEqual(
                {
                    "value": 7,
                },
                json_io.load_json(
                    target
                ),
            )

            self.assertEqual(
                2,
                sleep.call_count,
            )

    def test_write_atomic_clears_windows_readonly_target(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = (
                Path(directory)
                / "content.json"
            )

            target.write_text(
                '{"old": true}\n',
                encoding="utf-8",
            )

            real_replace = (
                json_io.os.replace
            )

            attempts = 0

            def flaky_replace(
                source,
                destination,
            ):
                nonlocal attempts

                attempts += 1

                if attempts == 1:
                    raise windows_error(
                        5
                    )

                return real_replace(
                    source,
                    destination,
                )

            readonly_states = iter(
                (
                    True,
                    False,
                )
            )

            with (
                patch.object(
                    json_io.os,
                    "replace",
                    side_effect=flaky_replace,
                ),
                patch.object(
                    json_io,
                    "_is_windows_readonly",
                    side_effect=lambda path: next(
                        readonly_states
                    ),
                ),
                patch.object(
                    json_io.os,
                    "chmod",
                ) as chmod,
            ):
                json_io.write_atomic(
                    target,
                    {
                        "value": 11,
                    },
                )

            chmod.assert_called_once()

            self.assertEqual(
                {
                    "value": 11,
                },
                json_io.load_json(
                    target
                ),
            )

    def test_write_atomic_does_not_retry_unrelated_replace_error(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = (
                Path(directory)
                / "world.uworld"
            )

            error = windows_error(
                87
            )

            with (
                patch.object(
                    json_io.os,
                    "replace",
                    side_effect=error,
                ) as replace,
                patch(
                    "time.sleep"
                ) as sleep,
            ):
                with self.assertRaises(
                    PermissionError
                ):
                    json_io.write_atomic(
                        target,
                        {
                            "value": 7,
                        },
                    )

            self.assertEqual(
                1,
                replace.call_count,
            )

            sleep.assert_not_called()

            self.assertEqual(
                [],
                list(
                    Path(directory).glob(
                        ".world.uworld.*.tmp"
                    )
                ),
            )

    def test_write_atomic_falls_back_when_windows_lock_is_persistent(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = (
                Path(directory)
                / "world.uworld"
            )

            error = windows_error(
                32
            )

            with (
                patch.object(
                    json_io.os,
                    "replace",
                    side_effect=error,
                ) as replace,
                patch(
                    "time.sleep"
                ) as sleep,
                patch.object(
                    json_io,
                    "_clear_windows_readonly",
                ) as clear_readonly,
            ):
                json_io.write_atomic(
                    target,
                    {
                        "value": 7,
                    },
                )

            self.assertGreater(
                replace.call_count,
                1,
            )

            self.assertEqual(
                replace.call_count - 1,
                sleep.call_count,
            )

            clear_readonly.assert_called_once_with(target)

            self.assertEqual(
                {
                    "value": 7,
                },
                json_io.load_json(target),
            )

            self.assertEqual(
                [],
                list(
                    Path(directory).glob(
                        ".world.uworld.*.tmp"
                    )
                ),
            )

    def test_write_in_place_restores_original_when_write_fails(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = (
                Path(directory)
                / "content.json"
            )

            target.write_text(
                '{"original": true}\n',
                encoding="utf-8",
            )

            class FailingFile:
                def __enter__(self):
                    raise PermissionError(
                        13,
                        "denied",
                    )

                def __exit__(self, *unused):
                    del unused

                    return False

            original_open = Path.open

            calls = 0

            def open_then_fail(
                self,
                mode="r",
                *args,
                **kwargs,
            ):
                nonlocal calls

                if self == target and mode == "wb":
                    calls += 1

                    if calls == 1:
                        return FailingFile()

                return original_open(
                    self,
                    mode,
                    *args,
                    **kwargs,
                )

            with patch.object(
                Path,
                "open",
                open_then_fail,
            ):
                with self.assertRaises(
                    PermissionError
                ):
                    json_io._write_in_place(
                        target,
                        {
                            "partial": True,
                        },
                    )

            self.assertEqual(
                {
                    "original": True,
                },
                json_io.load_json(target),
            )

    def test_write_in_place_removes_new_file_when_write_fails(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = (
                Path(directory)
                / "content.json"
            )

            with patch.object(
                json_io.Path,
                "open",
                side_effect=PermissionError(
                    13,
                    "denied",
                ),
            ):
                with self.assertRaises(
                    PermissionError
                ):
                    json_io._write_in_place(
                        target,
                        {
                            "value": 3,
                        },
                    )

            self.assertFalse(
                target.exists(),
            )

    def test_write_in_place_writes_payload(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = (
                Path(directory)
                / "content.json"
            )

            target.write_text(
                '{"old": true}\n',
                encoding="utf-8",
            )

            json_io._write_in_place(
                target,
                {
                    "value": 9,
                },
            )

            self.assertEqual(
                {
                    "value": 9,
                },
                json_io.load_json(target),
            )
if __name__ == "__main__":
    unittest.main()
