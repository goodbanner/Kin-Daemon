import importlib.util
from pathlib import Path
import stat
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("build_pack", ROOT / "build_pack.py")
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


class PackTests(unittest.TestCase):
    def setUp(self):
        env = patch.dict(pack.os.environ, {"KIN_APK": "", "CTS_APK": ""})
        env.start()
        self.addCleanup(env.stop)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.module = self.root / "magisk"
        self.output = self.root / "output"
        self.module.mkdir()
        self.output.mkdir()
        (self.root / "Json").mkdir()
        (self.root / "Json/config.json").write_text('{"fresh":true}')
        (self.module / "config").mkdir()
        (self.module / "config/config.json").write_text('{"stale":true}')
        (self.module / "Kinetic").write_bytes(b"stale binary")
        (self.module / "module.prop").write_bytes(b"id=Kin\r\nversionCode=1\r\n")
        (self.module / "service.sh").write_bytes(b"#!/system/bin/sh\r\necho test\r\n")
        self.binary = bytearray(64)
        self.binary[:6] = b"\x7fELF\x02\x01"
        self.binary[18:20] = (183).to_bytes(2, "little")
        (self.output / "Kinetic").write_bytes(self.binary)
        for key, val in {"SCRIPT_DIR": str(self.root), "MAGISK_DIR": str(self.module),
                         "OUTPUT_DIR": str(self.output), "VERSION_CODE": 123}.items():
            p = patch.object(pack, key, val)
            p.start()
            self.addCleanup(p.stop)
        self.archive = self.output / f"Kin-v3.2-{pack.DATE_CODE}.zip"

    def test_fresh_binary_configs_and_permissions(self):
        with zipfile.ZipFile(self.archive, "w") as z:
            z.writestr("obsolete.txt", "old")
        (self.module / ".config_backup").write_text("private")
        (self.module / "service.log").write_text("runtime")
        self.assertTrue(pack.pack_magisk())
        with zipfile.ZipFile(self.archive) as z:
            self.assertIsNone(z.testzip())
            self.assertEqual(z.read("Kinetic"), self.binary)
            self.assertEqual(z.read("config/config.json"), b'{"fresh":true}')
            self.assertNotIn("obsolete.txt", z.namelist())
            self.assertNotIn(".config_backup", z.namelist())
            self.assertNotIn("service.log", z.namelist())
            self.assertNotIn(b"\r", z.read("service.sh"))
            self.assertIn(b"versionCode=123\n", z.read("module.prop"))
            self.assertEqual(stat.S_IMODE(z.getinfo("Kinetic").external_attr >> 16), 0o755)
            self.assertEqual(stat.S_IMODE(z.getinfo("service.sh").external_attr >> 16), 0o755)
        self.assertEqual((self.module / "Kinetic").read_bytes(), b"stale binary")
        self.assertIn(b"versionCode=1", (self.module / "module.prop").read_bytes())

    def test_missing_build_never_uses_committed_binary(self):
        (self.output / "Kinetic").unlink()
        self.assertFalse(pack.pack_magisk())
        self.assertFalse(self.archive.exists())

    def test_invalid_binary_preserves_previous_archive(self):
        self.archive.write_bytes(b"previous archive")
        (self.output / "Kinetic").write_bytes(b"not an arm64 ELF")
        self.assertFalse(pack.pack_magisk())
        self.assertEqual(self.archive.read_bytes(), b"previous archive")

    def test_explicit_apk_override(self):
        apk = self.root / "current.apk"
        apk.write_bytes(b"explicit APK")
        with patch.dict(pack.os.environ, {"KIN_APK": str(apk)}):
            self.assertTrue(pack.pack_magisk())
        with zipfile.ZipFile(self.archive) as z:
            self.assertEqual(z.read("Kin.apk"), apk.read_bytes())


if __name__ == "__main__":
    unittest.main()
