from __future__ import annotations

import json
import os
import tempfile
import time
from pathlib import Path
from typing import Any


class DuplicateKeyError(ValueError):
    pass


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise DuplicateKeyError(f"duplicate JSON field: {key}")
        result[key] = value
    return result


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_reject_duplicate_keys)


def decode_json(text: str) -> Any:
    return json.loads(text, object_pairs_hook=_reject_duplicate_keys)


def encode_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2, separators=(",", ": ")) + "\n"


def write_atomic(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as output:
            output.write(encode_json(value))
            output.flush()
            os.fsync(output.fileno())
        # Windows scanners, indexers and file watchers can briefly keep the
        # destination without FILE_SHARE_DELETE. Preserve atomic replacement
        # semantics, but tolerate those transient sharing/access locks.
        retry_delays = (
            0.02,
            0.04,
            0.08,
            0.12,
            0.20,
            0.25,
            0.30,
        )

        for attempt in range(
            len(retry_delays) + 1
        ):
            try:
                os.replace(
                    temporary,
                    path,
                )

                break
            except PermissionError as error:
                transient_windows_lock = (
                    getattr(
                        error,
                        "winerror",
                        None,
                    )
                    in {
                        5,
                        32,
                        33,
                    }
                )

                if (
                    not transient_windows_lock
                    or attempt
                    >= len(retry_delays)
                ):
                    raise

                time.sleep(
                    retry_delays[
                        attempt
                    ]
                )
    finally:
        if temporary.exists():
            temporary.unlink()

