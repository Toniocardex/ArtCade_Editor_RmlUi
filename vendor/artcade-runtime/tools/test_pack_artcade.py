import hashlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.dont_write_bytecode = True

SCRIPT_PATH = Path(__file__).with_name("pack-artcade.py")
# pack-artcade.py imports artcade_keytool from this dir; ensure it resolves when
# the module is loaded by file path (the CLI gets this for free, importlib not).
sys.path.insert(0, str(SCRIPT_PATH.parent))
SPEC = importlib.util.spec_from_file_location("pack_artcade", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
PACK_ARTCADE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACK_ARTCADE)


class PackArtcadeOverrideTest(unittest.TestCase):
    def test_override_replaces_archive_entry_without_changing_source(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            scripts = root / "scripts"
            scripts.mkdir()
            source = "function tick() return 'manual' end"
            combined = "-- combined\nfunction tick() return 'combined' end"
            main_path = scripts / "main.lua"
            main_path.write_bytes(source.encode("utf-8"))
            (root / "project.json").write_text(
                json.dumps({
                    "projectName": "Pack Test",
                    "version": "1.0.0",
                    "mainScriptPath": "scripts/main.lua",
                }),
                encoding="utf-8",
            )
            override = root / "combined.lua"
            override.write_bytes(combined.encode("utf-8"))
            output = root / "game.artcade"

            self.assertTrue(
                PACK_ARTCADE.pack(str(root), str(output), str(override), encrypt=False)
            )
            self.assertEqual(main_path.read_text(encoding="utf-8"), source)
            with zipfile.ZipFile(output) as archive:
                self.assertEqual(
                    archive.read("scripts/main.lua").decode("utf-8"),
                    combined,
                )
                manifest = json.loads(archive.read("manifest.json"))
            expected = hashlib.sha256(combined.encode("utf-8")).hexdigest()
            self.assertEqual(manifest["files"]["scripts/main.lua"], expected)


def _write_project(root: Path) -> None:
    (root / "scripts").mkdir()
    (root / "project.json").write_text(
        json.dumps({
            "projectName": "Enc Test",
            "version": "1.0.0",
            "mainScriptPath": "scripts/main.lua",
        }),
        encoding="utf-8",
    )
    (root / "scripts" / "main.lua").write_text("function tick(dt) end\n", encoding="utf-8")


class PackEncryptionTest(unittest.TestCase):
    """Default pack() output is an encrypted .artcade container."""

    def _decrypt(self, container: bytes) -> bytes:
        from artcade_keytool import resolve_key
        from nacl import bindings as nb
        key, _ = resolve_key(warn=False)
        nonce, mac, ct = container[10:34], container[34:50], container[50:]
        return nb.crypto_aead_xchacha20poly1305_ietf_decrypt(ct + mac, b"", nonce, key)

    def test_encrypted_roundtrip(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            _write_project(root)
            output = root / "game.artcade"
            self.assertTrue(PACK_ARTCADE.pack(str(root), str(output)))
            data = output.read_bytes()
            self.assertEqual(data[:8], b"ARTCADE1")
            self.assertEqual(data[8], 1)         # version
            self.assertEqual(data[9] & 1, 1)     # encrypted flag
            with zipfile.ZipFile(io.BytesIO(self._decrypt(data))) as archive:
                names = archive.namelist()
            self.assertIn("manifest.json", names)
            self.assertIn("scripts/main.lua", names)

    def test_tamper_fails_auth(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            _write_project(root)
            output = root / "game.artcade"
            self.assertTrue(PACK_ARTCADE.pack(str(root), str(output)))
            data = bytearray(output.read_bytes())
            data[-1] ^= 0x01
            with self.assertRaises(Exception):
                self._decrypt(bytes(data))

    def test_no_encrypt_is_plain_zip(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            _write_project(root)
            output = root / "game.artcade"
            self.assertTrue(PACK_ARTCADE.pack(str(root), str(output), encrypt=False))
            data = output.read_bytes()
            self.assertNotEqual(data[:8], b"ARTCADE1")
            self.assertEqual(data[:2], b"PK")
            with zipfile.ZipFile(output) as archive:
                self.assertIn("manifest.json", archive.namelist())


if __name__ == "__main__":
    unittest.main()
