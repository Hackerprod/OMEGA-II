"""Write-once raw and derived artifacts for the collector r17 attempt format."""
from __future__ import annotations

import hashlib
import json
import os
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


class ArtifactError(RuntimeError):
    pass


def _bytes(value: bytes | bytearray | memoryview) -> bytes:
    if not isinstance(value, (bytes, bytearray, memoryview)):
        raise ArtifactError("artifact payload must be bytes")
    return bytes(value)


def _safe_name(name: str) -> str:
    path = Path(name)
    if not name or path.is_absolute() or len(path.parts) != 1 or name in {".", ".."}:
        raise ArtifactError(f"artifact name must be a plain filename: {name!r}")
    return name


def _create_new(path: Path, payload: bytes) -> None:
    try:
        with path.open("xb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
    except FileExistsError as exc:
        raise ArtifactError(f"write-once refusal: {path.name}") from exc


def _json_bytes(value: dict[str, Any]) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")


class SessionAttemptStore:
    def __init__(self, directory: str | Path):
        self.directory = Path(directory).resolve()
        if not self.directory.is_dir():
            raise ArtifactError(f"attempt directory missing: {self.directory}")

    @classmethod
    def create(cls, directory: str | Path, *, session_id: str, attempt_id: str) -> "SessionAttemptStore":
        target = Path(directory).resolve()
        if target.exists():
            raise ArtifactError(f"refusing reuse of existing attempt: {target}")
        target.mkdir(parents=True, exist_ok=False)
        store = cls(target)
        metadata = {"protocol": "D13-write-once-v1", "session_id": session_id,
                    "attempt_id": attempt_id, "created_utc": datetime.now(timezone.utc).isoformat(),
                    "store_implementation": "artifact_store_r17.py"}
        _create_new(target / "attempt_metadata.json", _json_bytes(metadata))
        (target / "raw_manifest_r17").mkdir()
        (target / "derived_lineage_r17").mkdir()
        return store

    def _raw_sidecar(self, name: str) -> Path:
        return self.directory / "raw_manifest_r17" / f"{_safe_name(name)}.json"

    def write_raw(self, name: str, payload: bytes) -> str:
        name = _safe_name(name)
        data = _bytes(payload)
        _create_new(self.directory / name, data)
        digest = hashlib.sha256(data).hexdigest()
        _create_new(self._raw_sidecar(name), _json_bytes({"name": name, "sha256": digest, "bytes": len(data)}))
        return digest

    def register_external_raw(self, name: str) -> str:
        """Bind a file acquired directly by the harness, without rewriting it."""
        name = _safe_name(name)
        path = self.directory / name
        if not path.is_file() or path.is_symlink():
            raise ArtifactError(f"external raw source missing or unsafe: {name}")
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        _create_new(self._raw_sidecar(name), _json_bytes({"name": name, "sha256": digest, "bytes": len(data)}))
        return digest

    def ingest_raw(self, source: str | Path, name: str) -> str:
        """Copy an already acquired source into the attempt as immutable raw."""
        return self.write_raw(name, Path(source).read_bytes())

    def verify_raw(self, name: str) -> str:
        name = _safe_name(name)
        path, sidecar_path = self.directory / name, self._raw_sidecar(name)
        try:
            sidecar = json.loads(sidecar_path.read_text(encoding="utf-8"))
            data = path.read_bytes()
        except OSError as exc:
            raise ArtifactError(f"raw source missing: {name}: {exc}") from exc
        digest = hashlib.sha256(data).hexdigest()
        if sidecar.get("name") != name or sidecar.get("sha256") != digest or sidecar.get("bytes") != len(data):
            raise ArtifactError(f"raw source hash/size mismatch: {name}")
        return digest

    def write_derived(self, name: str, payload: bytes, *, source_raw: Iterable[str],
                      metadata: dict[str, Any] | None = None) -> str:
        name = _safe_name(name)
        data = _bytes(payload)
        source_hashes = {source: self.verify_raw(source) for source in source_raw}
        digest = hashlib.sha256(data).hexdigest()
        _create_new(self.directory / name, data)
        lineage = {"name": name, "sha256": digest, "bytes": len(data),
                   "source_raw_sha256": source_hashes, "metadata": metadata or {}}
        _create_new(self.directory / "derived_lineage_r17" / f"{name}.json", _json_bytes(lineage))
        # S-20 D13 consumer contract: the derived config carries an adjacent
        # lineage document with explicit derived/source hashes.
        if name == "effective_config.json":
            compat = {"derived_sha256": digest, "sources": source_hashes,
                      "metadata": metadata or {}}
            _create_new(self.directory / "effective_config.json.lineage.json", _json_bytes(compat))
        return digest
