from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from artifact_store import ArtifactError, SessionAttemptStore


class D13WriteOnceTests(unittest.TestCase):
    def test_overwriting_a_raw_file_is_rejected_and_bytes_stay_identical(self):
        with tempfile.TemporaryDirectory(prefix="d13_raw_") as root:
            attempt = Path(root) / "session-1" / "attempt-1"
            store = SessionAttemptStore.create(attempt, session_id="session-1", attempt_id="attempt-1")
            original = b"immutable EMON raw bytes\n"
            digest = store.write_raw("emon_raw.txt", original)
            with self.assertRaisesRegex(ArtifactError, "write-once refusal"):
                store.write_raw("emon_raw.txt", b"replacement\n")
            self.assertEqual((attempt / "emon_raw.txt").read_bytes(), original)
            self.assertEqual(store.verify_raw("emon_raw.txt"), hashlib.sha256(original).hexdigest())
            self.assertEqual(digest, store.verify_raw("emon_raw.txt"))

    def test_reusing_an_existing_attempt_directory_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="d13_reuse_") as root:
            attempt = Path(root) / "session-1" / "attempt-1"
            SessionAttemptStore.create(attempt, session_id="session-1", attempt_id="attempt-1")
            with self.assertRaisesRegex(ArtifactError, "already exists"):
                SessionAttemptStore.create(attempt, session_id="session-1", attempt_id="attempt-1")

    def test_ingesting_external_raw_uses_hash_and_write_once_creation(self):
        with tempfile.TemporaryDirectory(prefix="d13_ingest_") as root:
            base = Path(root)
            source = base / "source.txt"
            source.write_bytes(b"pre-existing immutable source\n")
            attempt = base / "session-1" / "attempt-1"
            store = SessionAttemptStore.create(attempt, session_id="session-1", attempt_id="attempt-1")
            digest = store.ingest_raw(source, "raw/source.txt")
            self.assertEqual(store.verify_raw("raw/source.txt"), digest)
            self.assertEqual((attempt / "raw/source.txt").read_bytes(), source.read_bytes())
            with self.assertRaisesRegex(ArtifactError, "write-once refusal"):
                store.ingest_raw(source, "raw/source.txt")

    def test_derived_output_links_source_raw_hash_without_changing_it(self):
        with tempfile.TemporaryDirectory(prefix="d13_derived_") as root:
            attempt = Path(root) / "session-1" / "attempt-1"
            store = SessionAttemptStore.create(attempt, session_id="session-1", attempt_id="attempt-1")
            raw = b"raw once\n"
            source_hash = store.write_raw("raw.txt", raw)
            derived_hash = store.write_derived("coverage.json", b'{"coverage":0.96}\n', source_raw=["raw.txt"], metadata={"rule": "R2"})
            lineage = json.loads((attempt / "coverage.json.lineage.json").read_text(encoding="utf-8"))
            self.assertEqual(lineage["sources"]["raw.txt"], source_hash)
            self.assertEqual(lineage["derived_sha256"], derived_hash)
            self.assertEqual((attempt / "raw.txt").read_bytes(), raw)


if __name__ == "__main__":
    unittest.main(verbosity=2)
