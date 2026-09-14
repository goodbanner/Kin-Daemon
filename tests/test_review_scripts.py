import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ModuleRegressionTests(unittest.TestCase):
    def test_service_does_not_write_cpuset(self):
        source = (ROOT / "magisk/service.sh").read_text(encoding="utf-8")
        self.assertNotIn("set_cpuset", source)
        self.assertNotIn("/dev/cpuset/", source)
        self.assertIn('nohup "$CTS_BIN"', source)
        self.assertIn('is_base "$m"', source)

    def run_uninstall(self, fail_copy=False):
        source = (ROOT / "magisk/uninstall.sh").read_text(encoding="utf-8")
        start = source.index("if [ -d /storage/emulated/0/Android/Kin ]; then")
        end = source.index('[ -f /sdcard/Android/qinghe/logs.txt ]', start)
        with tempfile.TemporaryDirectory() as temp:
            parent = Path(temp) / "Android"
            data = parent / "Kin"
            data.mkdir(parents=True)
            payload = '{"mode":"powersave"}'
            (data / "config.json").write_text(payload, encoding="utf-8")
            script = source[start:end].replace(
                "/storage/emulated/0/Android", parent.as_posix())
            mocks = "log_msg() { :; };\n"
            if fail_copy:
                mocks += "cp() { return 1; };\n"
            subprocess.run(["bash", "-c", mocks + script], check=True)
            if fail_copy:
                self.assertEqual((data / "config.json").read_text(), payload)
            else:
                self.assertFalse(data.exists())
                self.assertEqual((parent / "Kin-config.json.bak").read_text(), payload)

    def test_uninstall_backup_survives(self):
        self.run_uninstall()

    def test_uninstall_copy_failure_preserves_data(self):
        self.run_uninstall(fail_copy=True)

    def test_install_permissions(self):
        source = (ROOT / "magisk/customize.sh").read_text(encoding="utf-8")
        self.assertNotIn("0777", source)
        start = source.index("set_module_permissions() {")
        end = source.index("\n}\n", start) + 3
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "vtools").mkdir()
            for name in ("Kinetic", "service.sh", "module.prop", "vtools/powercfg.sh"):
                (root / name).write_text("test")
            subprocess.run(["bash", "-c", source[start:end] +
                            '\nMODPATH="$1"; set_module_permissions', "test",
                            root.as_posix()], check=True)
            if os.name != "nt":
                for name in ("Kinetic", "service.sh", "vtools/powercfg.sh"):
                    self.assertEqual((root / name).stat().st_mode & 0o777, 0o755)
                self.assertEqual((root / "module.prop").stat().st_mode & 0o777, 0o644)
                self.assertEqual((root / "vtools").stat().st_mode & 0o777, 0o755)


if __name__ == "__main__":
    unittest.main()
