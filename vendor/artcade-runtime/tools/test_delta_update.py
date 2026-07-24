#!/usr/bin/env python3
# =============================================================================
# test_delta_update — content-addressed publish + delta update pipeline
#
# Run:  python -m pytest tools/test_delta_update.py
#   or: python tools/test_delta_update.py
# =============================================================================

import importlib.util
import os
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))


def _load(mod_name: str, file_name: str):
    spec = importlib.util.spec_from_file_location(mod_name, HERE / file_name)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


publish_mod = _load("publish_release", "publish-release.py")
update_mod = _load("update_client", "update-client.py")


def _write(root: Path, rel: str, data: bytes) -> None:
    p = root / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data)


def _make_v1(proj: Path) -> None:
    _write(proj, "project.json",
           b'{"projectName":"D","version":"1.0.0","mainScriptPath":"scripts/main.lua"}')
    _write(proj, "scripts/main.lua", b"function tick(dt) end -- v1\n")
    _write(proj, "assets/a.bin", b"\x00\x01\x02\x03" * 64)  # unchanged across versions


def _mutate_to_v2(proj: Path) -> None:
    _write(proj, "project.json",
           b'{"projectName":"D","version":"2.0.0","mainScriptPath":"scripts/main.lua"}')
    _write(proj, "scripts/main.lua", b"function tick(dt) end -- v2 CHANGED\n")
    _write(proj, "assets/b.bin", b"\x09" * 128)  # new file


class DeltaUpdateTest(unittest.TestCase):
    def test_delta_transfers_only_changed_objects(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            proj, store, install = d / "proj", d / "store", d / "install"
            _make_v1(proj)
            self.assertTrue(publish_mod.publish(str(proj), str(store), "1.0.0"))

            # Fresh install -> v1: every file transferred.
            r1 = update_mod.update(str(store), str(install))
            self.assertEqual(r1["version"], "1.0.0")
            self.assertEqual(set(r1["transferred"]),
                             {"project.json", "scripts/main.lua", "assets/a.bin"})

            # v2: change main.lua, add b.bin, leave a.bin identical.
            _mutate_to_v2(proj)
            self.assertTrue(publish_mod.publish(str(proj), str(store), "2.0.0"))

            r2 = update_mod.update(str(store), str(install))
            self.assertEqual(r2["version"], "2.0.0")
            # a.bin is deduped (same hash) and NOT re-transferred; project.json
            # changed (version field) so it ships too.
            self.assertEqual(set(r2["transferred"]),
                             {"project.json", "scripts/main.lua", "assets/b.bin"})
            self.assertNotIn("assets/a.bin", r2["transferred"])

            # Install matches the v2 source byte-for-byte.
            for rel in ("project.json", "scripts/main.lua", "assets/a.bin", "assets/b.bin"):
                self.assertEqual((install / rel).read_bytes(), (proj / rel).read_bytes(), rel)

    def test_removed_file_is_deleted_on_update(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            proj, store, install = d / "proj", d / "store", d / "install"
            _make_v1(proj)
            publish_mod.publish(str(proj), str(store), "1.0.0")
            update_mod.update(str(store), str(install))
            self.assertTrue((install / "assets/a.bin").exists())

            (proj / "assets/a.bin").unlink()  # removed in v2
            publish_mod.publish(str(proj), str(store), "2.0.0")
            r = update_mod.update(str(store), str(install))
            self.assertIn("assets/a.bin", r["removed"])
            self.assertFalse((install / "assets/a.bin").exists())

    def test_tampered_object_aborts_without_partial_apply(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            proj, store, install = d / "proj", d / "store", d / "install"
            _make_v1(proj)
            publish_mod.publish(str(proj), str(store), "1.0.0")
            update_mod.update(str(store), str(install))

            _mutate_to_v2(proj)
            publish_mod.publish(str(proj), str(store), "2.0.0")

            # Corrupt every object's last byte: the changed files can't apply.
            for obj in (store / "objects").iterdir():
                b = bytearray(obj.read_bytes())
                b[-1] ^= 0x01
                obj.write_bytes(bytes(b))

            before = (install / "scripts/main.lua").read_bytes()
            with self.assertRaises(SystemExit):
                update_mod.update(str(store), str(install))
            # No partial apply: v1 content intact, new file not created.
            self.assertEqual((install / "scripts/main.lua").read_bytes(), before)
            self.assertFalse((install / "assets/b.bin").exists())


if __name__ == "__main__":
    unittest.main()
