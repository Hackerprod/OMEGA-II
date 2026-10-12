"""Documented S-20 adapter example for harness/qualifier integration.

The caller supplies immutable paths plus a never-used attempt directory. The
adapter snapshots every input with exclusive creation, validates those
snapshots, and writes a hash-linked derived result.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from artifact_store import SessionAttemptStore
from s20_validator import load_config, validate_capture


def validate_attempt(
    *, emon_raw: str | Path, clock_map: str | Path, block_marks: str | Path,
    effective_config: str | Path | dict[str, Any], attempt_directory: str | Path,
    session_id: str, attempt_id: str, mode: str | None = None,
) -> dict[str, Any]:
    """Run S-20 in a new D13 attempt and persist the result with source hashes."""
    config = load_config(effective_config)
    if isinstance(effective_config, (str, Path)):
        source_config_bytes = Path(effective_config).read_bytes()
    else:
        source_config_bytes = (json.dumps(effective_config, ensure_ascii=False, sort_keys=True,
                                          separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")
    store = SessionAttemptStore.create(attempt_directory, session_id=session_id, attempt_id=attempt_id)
    hashes = {
        "emon_raw.txt": store.ingest_raw(emon_raw, "emon_raw.txt"),
        "clock_map.csv": store.ingest_raw(clock_map, "clock_map.csv"),
        "block_marks.csv": store.ingest_raw(block_marks, "block_marks.csv"),
        "effective_config_source.json": store.write_raw("effective_config_source.json", source_config_bytes),
    }
    effective = dict(config)
    derived_config_sources = ["effective_config_source.json"]
    if config["profile"] == "s20":
        hashes["events.edp"] = store.ingest_raw(config["edp_path"], "events.edp")
        effective["edp_path"] = str((store.directory / "events.edp").resolve())
        derived_config_sources.append("events.edp")
    effective_bytes = (json.dumps(effective, ensure_ascii=False, sort_keys=True,
                                  separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")
    effective_hash = store.write_derived("effective_config.json", effective_bytes,
        source_raw=derived_config_sources, metadata={"transformation": "EDP path rebased to immutable attempt snapshot"})
    result = validate_capture(store.directory / "emon_raw.txt", store.directory / "clock_map.csv",
                              store.directory / "block_marks.csv", store.directory / "effective_config.json",
                              mode=mode)
    output = (json.dumps(result, ensure_ascii=False, sort_keys=True, indent=2, allow_nan=False) + "\n").encode("utf-8")
    result_hash = store.write_derived("s20_validation.json", output,
        source_raw=list(hashes), metadata={"validator": "S20-R2-v1", "mode": mode or config["mode"],
                                          "effective_config_sha256": effective_hash})
    return {"attempt_directory": str(store.directory), "source_sha256": hashes,
            "result_sha256": result_hash, "result": result}


__all__ = ["validate_attempt"]
