from __future__ import annotations

import json
import os
import stat
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


def _is_windows_readonly(path: Path) -> bool:
    try:
        attributes = getattr(path.stat(), "st_file_attributes", 0)
    except OSError:
        return False

    readonly = getattr(stat, "FILE_ATTRIBUTE_READONLY", 0x0001)

    return bool(attributes & readonly)


def _clear_windows_readonly(path: Path) -> bool:
    if not path.exists() or not _is_windows_readonly(path):
        return False

    try:
        os.chmod(path, stat.S_IWRITE)
    except OSError:
        return False

    return not _is_windows_readonly(path)


def _write_in_place(path: Path, value: Any) -> None:
    """Last-resort Windows fallback when atomic replacement is denied."""

    payload = encode_json(value).encode("utf-8")

    original = path.read_bytes() if path.exists() else None

    try:
        with path.open("wb") as output:
            output.write(payload)

            output.flush()

            os.fsync(output.fileno())
    except BaseException:
        if original is not None:
            with path.open("wb") as recovery:
                recovery.write(original)

                recovery.flush()

                os.fsync(recovery.fileno())
        elif path.exists():
            path.unlink()

        raise


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
                    if transient_windows_lock:
                        _clear_windows_readonly(path)

                        _write_in_place(path, value)

                        return

                    raise

                if error.winerror == 5:
                    _clear_windows_readonly(path)

                time.sleep(
                    retry_delays[
                        attempt
                    ]
                )
    finally:
        if temporary.exists():
            temporary.unlink()

