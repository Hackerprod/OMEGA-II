"""Write-once raw artifacts and hash-linked derived outputs (D13)."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path, PurePosixPath
from typing import Any


class ArtifactError(RuntimeError):
    pass


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class SessionAttemptStore:
    """One object represents one fresh, never-reused session/attempt folder."""

    def __init__(self, directory: str | Path):
        supplied = Path(directory).absolute()
        if supplied.is_symlink():
            raise ArtifactError("attempt directory must not be a symlink")
        self.directory = supplied.resolve(strict=True)
        if not self.directory.is_dir():
            raise ArtifactError("attempt path is not a directory")
        if self.directory.is_symlink():
            raise ArtifactError("attempt directory must not be a symlink")

    @classmethod
    def create(cls, directory: str | Path, *, session_id: str, attempt_id: str) -> "SessionAttemptStore":
        target = Path(directory).absolute()
        target.parent.mkdir(parents=True, exist_ok=True)
        try:
            target.mkdir(exist_ok=False)
        except FileExistsError as exc:
            raise ArtifactError(f"attempt directory already exists; refusing reuse: {target}") from exc
        store = cls(target)
        metadata = json.dumps({"session_id": session_id, "attempt_id": attempt_id, "protocol": "D13-write-once-v1"},
                             ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8") + b"\n"
        store._exclusive_write("attempt_metadata.json", metadata)
        return store

    def _relative(self, name: str) -> Path:
        posix = PurePosixPath(name.replace("\\", "/"))
        if posix.is_absolute() or not posix.parts or any(part in {"", ".", ".."} for part in posix.parts):
            raise ArtifactError(f"unsafe artifact name: {name!r}")
        path = self.directory.joinpath(*posix.parts)
        parent = path.parent
        parent.mkdir(parents=True, exist_ok=True)
        resolved_parent = parent.resolve(strict=True)
        if self.directory != resolved_parent and self.directory not in resolved_parent.parents:
            raise ArtifactError("artifact path escapes the attempt directory")
        if path.exists() or path.is_symlink():
            raise ArtifactError(f"artifact already exists; write-once refusal: {name}")
        return path

    def _exclusive_write(self, name: str, data: bytes) -> Path:
        path = self._relative(name)
        try:
            with path.open("xb") as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
        except FileExistsError as exc:
            raise ArtifactError(f"artifact already exists; write-once refusal: {name}") from exc
        return path

    def write_raw(self, name: str, data: bytes) -> str:
        path = self._exclusive_write(name, data)
        digest = sha256_bytes(data)
        sidecar = f"{name}.sha256"
        self._exclusive_write(sidecar, f"{digest}  {Path(name).name}\n".encode("ascii"))
        return digest

    def ingest_raw(self, source: str | Path, name: str) -> str:
        """Copy an existing source into this attempt using exclusive write-once creation."""
        src = Path(source)
        if src.is_symlink() or not src.is_file():
            raise ArtifactError(f"raw source must be a regular non-symlink file: {src}")
        return self.write_raw(name, src.read_bytes())

    def register_external_raw(self, name: str) -> str:
        """Register a raw file created by EMON with its create-new output mode."""
        posix = PurePosixPath(name.replace("\\", "/"))
        if posix.is_absolute() or not posix.parts or any(part in {"", ".", ".."} for part in posix.parts):
            raise ArtifactError(f"unsafe artifact name: {name!r}")
        path = self.directory.joinpath(*posix.parts)
        parent = path.parent.resolve(strict=True)
        if self.directory != parent and self.directory not in parent.parents:
            raise ArtifactError("artifact path escapes the attempt directory")
        if path.is_symlink():
            raise ArtifactError("external raw path must not be a symlink")
        if not path.is_file():
            raise ArtifactError(f"external raw file does not exist: {name}")
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        sidecar = f"{name}.sha256"
        self._exclusive_write(sidecar, f"{digest}  {Path(name).name}\n".encode("ascii"))
        return digest

    def verify_raw(self, name: str) -> str:
        posix = PurePosixPath(name.replace("\\", "/"))
        if posix.is_absolute() or not posix.parts or any(part in {"", ".", ".."} for part in posix.parts):
            raise ArtifactError(f"unsafe artifact name: {name!r}")
        path = self.directory.joinpath(*posix.parts)
        parent = path.parent.resolve(strict=True)
        if self.directory != parent and self.directory not in parent.parents:
            raise ArtifactError("artifact path escapes the attempt directory")
        if path.is_symlink():
            raise ArtifactError("raw artifact must not be a symlink")
        sidecar = self.directory.joinpath(*(posix.parts[:-1] + (posix.parts[-1] + ".sha256",)))
        if sidecar.is_symlink():
            raise ArtifactError("raw hash record must not be a symlink")
        if not path.is_file() or not sidecar.is_file():
            raise ArtifactError(f"raw file or hash record missing: {name}")
        recorded = sidecar.read_text(encoding="ascii").split()[0]
        observed = hashlib.sha256(path.read_bytes()).hexdigest()
        if observed != recorded:
            raise ArtifactError(f"raw hash mismatch: {name}")
        return observed

    def write_derived(self, name: str, data: bytes, *, source_raw: list[str], metadata: dict[str, Any] | None = None) -> str:
        sources = {source: self.verify_raw(source) for source in source_raw}
        path = self._exclusive_write(name, data)
        digest = sha256_bytes(data)
        lineage = {
            "derived_file": path.name,
            "derived_sha256": digest,
            "sources": sources,
            "metadata": metadata or {},
        }
        self._exclusive_write(f"{name}.lineage.json", json.dumps(lineage, ensure_ascii=False, sort_keys=True,
                                                                  separators=(",", ":")).encode("utf-8") + b"\n")
        return digest


def _cli() -> int:
    import argparse
    parser = argparse.ArgumentParser(description="Biblioteca D13 de artefactos inmutables")
    sub = parser.add_subparsers(dest="action", required=True)
    create = sub.add_parser("create-attempt")
    create.add_argument("directory")
    create.add_argument("--session-id", required=True)
    create.add_argument("--attempt-id", required=True)
    register = sub.add_parser("register-external-raw")
    register.add_argument("directory")
    register.add_argument("name")
    args = parser.parse_args()
    if args.action == "create-attempt":
        try:
            store = SessionAttemptStore.create(args.directory, session_id=args.session_id, attempt_id=args.attempt_id)
        except ArtifactError as exc:
            parser.error(str(exc))
        print(f"ATTEMPT_CREATED={store.directory}")
        return 0
    if args.action == "register-external-raw":
        try:
            digest = SessionAttemptStore(args.directory).register_external_raw(args.name)
        except ArtifactError as exc:
            parser.error(str(exc))
        print(f"RAW_SHA256={digest}  {args.name}")
        return 0
    return 2


if __name__ == "__main__":
    raise SystemExit(_cli())
