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

    def test_write_atomic_eventually_reraises_persistent_windows_lock(
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

            self.assertGreater(
                replace.call_count,
                1,
            )

            self.assertEqual(
                replace.call_count - 1,
                sleep.call_count,
            )

            self.assertEqual(
                [],
                list(
                    Path(directory).glob(
                        ".world.uworld.*.tmp"
                    )
                ),
            )


if __name__ == "__main__":
    unittest.main()