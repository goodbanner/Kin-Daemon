import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ModuleRegressionTests(unittest.TestCase):
    def test_boot_has_no_unowned_system_overrides(self):
        source = (ROOT / "magisk/post-fs-data.sh").read_text(encoding="utf-8")
        for forbidden in ("mount -o", "umount", "chmod", "resetprop", "lock_val", "cp -f"):
            self.assertNotIn(forbidden, source)

    def test_anticlamp_has_no_vendor_writer(self):
        source = (ROOT / "include/scheduler.hpp").read_text(encoding="utf-8")
        self.assertNotIn("applyAntiClampNode", source)
        controller = (ROOT / "include/AutoMode.hpp").read_text(encoding="utf-8")
        self.assertNotIn("antiClampTick", controller)
        config = (ROOT / "include/JsonConfig.hpp").read_text(encoding="utf-8")
        self.assertNotIn('readBool(a, "anticlamp_attack_enable"', config)

    def test_system_tuning_has_no_persistent_shell_overrides(self):
        source = (ROOT / "include/SysTweaks.hpp").read_text(encoding="utf-8")
        for forbidden in ("pm disable", "settings put", "resetprop", "ip link set"):
            self.assertNotIn(forbidden, source)

    def test_system_tuning_restore_is_wired(self):
        source = (ROOT / "include/SysTweaks.hpp").read_text(encoding="utf-8")
        scheduler = (ROOT / "include/scheduler.hpp").read_text(encoding="utf-8")
        self.assertIn("if (!Config::SysTune::enable) { restoreAll(); return; }", source)
        self.assertIn("settings_.apply(path, val, io)", source)
        self.assertIn("settings_.restore(io)", source)
        self.assertIn("settings_.retryRestore(nodes)", source)
        self.assertIn("sysTweaks_.restoreAll();", scheduler)
        self.assertIn("sysTweaks_.retryRestores();", scheduler)

    def test_validation_report_is_sanitized_and_scheduled(self):
        source = (ROOT / "include/DeviceValidation.hpp").read_text(encoding="utf-8")
        scheduler = (ROOT / "include/scheduler.hpp").read_text(encoding="utf-8")
        self.assertIn("kind=beta_device_validation", source)
        self.assertIn("anonymous_read_only", source)
        self.assertIn("validationTask", scheduler)
        self.assertIn("DeviceValidation::requestPath()", scheduler)
        self.assertIn("kRequestPath", source)
        self.assertNotIn("ro.serialno", source)
        self.assertNotIn("ro.serialno", source.lower())
        self.assertNotIn("auth.json", source.lower())

    def test_boot_preserves_vendor_thermal_control(self):
        source = (ROOT / "magisk/post-fs-data.sh").read_text(encoding="utf-8")
        for forbidden in ("/proc/shell-temp", "stop horae", "/system_ext/bin/horae",
                          "/proc/game_opt/", "lock_targets_empty"):
            self.assertNotIn(forbidden, source)

    def test_service_preserves_other_modules(self):
        source = (ROOT / "magisk/service.sh").read_text(encoding="utf-8")
        self.assertNotIn("cleanup_legacy", source)
        self.assertNotIn("rm -rf", source)

    def test_installer_preserves_other_modules(self):
        source = (ROOT / "magisk/customize.sh").read_text(encoding="utf-8")
        self.assertNotIn("remove_legacy_modules", source)
        self.assertNotIn("rm -rf", source)
        self.assertNotIn('touch "$d/remove"', source)

    def test_installer_preserves_installed_apps(self):
        source = (ROOT / "magisk/customize.sh").read_text(encoding="utf-8")
        self.assertNotIn("pm uninstall", source)

    def test_uninstaller_preserves_user_data_and_other_modules(self):
        source = (ROOT / "magisk/uninstall.sh").read_text(encoding="utf-8")
        self.assertNotIn("rm -rf", source)
        self.assertNotIn("/data/adb/modules/", source)
        self.assertNotIn("/Android/qinghe/", source)
        self.assertNotIn("/dev/opgce", source)

    def test_uninstaller_only_removes_owned_vtools(self):
        source = (ROOT / "magisk/uninstall.sh").read_text(encoding="utf-8")
        start = source.index("remove_owned_vtools() {")
        end = source.index("\n}\n", start) + 3
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            original = root / "original.sh"
            target = root / "powercfg.sh"
            original.write_text("Kin")
            for content in ("Other module", "Kin"):
                target.write_text(content)
                subprocess.run(["bash", "-c", 'un_chattr_file() { :; };\n' +
                                source[start:end] + '\nremove_owned_vtools "$1" "$2"',
                                "test", target.as_posix(), original.as_posix()], check=True)
                self.assertEqual(target.exists(), content != "Kin")

    def test_release_requires_host_tests(self):
        source = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
        for job in ("bundle", "rolling-release"):
            body = source.split("\n  " + job + ":", 1)[1].split("\n  #", 1)[0]
            self.assertIn("needs: [build, host-tests]", body)

    def test_service_does_not_write_cpuset(self):
        source = (ROOT / "magisk/service.sh").read_text(encoding="utf-8")
        self.assertNotIn("set_cpuset", source)
        self.assertNotIn("/dev/cpuset/", source)
        self.assertIn('nohup "$CTS_BIN"', source)
        self.assertIn('is_base "$m"', source)

    def run_uninstall(self, fail_copy=False):
        source = (ROOT / "magisk/uninstall.sh").read_text(encoding="utf-8")
        start = source.index("if [ -d /storage/emulated/0/Android/Kin ]; then")
        end = source.index('log_msg "════════', start)
        with tempfile.TemporaryDirectory() as temp:
            parent = Path(temp) / "Android"
            data = parent / "Kin"
            data.mkdir(parents=True)
            payload = '{"mode":"powersave"}'
            (data / "config.json").write_text(payload, encoding="utf-8")
            (data / "settings.json").write_text('{"custom":true}')
            (data / "auto_state.txt").write_text("learned-state")
            script = source[start:end].replace(
                "/storage/emulated/0/Android", parent.as_posix())
            mocks = "log_msg() { :; };\n"
            if fail_copy:
                mocks += "cp() { return 1; };\n"
            subprocess.run(["bash", "-c", mocks + script], check=True)
            if fail_copy:
                self.assertEqual((data / "config.json").read_text(), payload)
            else:
                self.assertEqual((data / "config.json").read_text(), payload)
                self.assertEqual((parent / "Kin-config.json.bak").read_text(), payload)
            self.assertEqual((data / "settings.json").read_text(), '{"custom":true}')
            self.assertEqual((data / "auto_state.txt").read_text(), "learned-state")

    def run_config_install(self, existing, fail_copy=False):
        source = (ROOT / "magisk/customize.sh").read_text(encoding="utf-8")
        start = source.index('    command -v chattr', source.index('# ── 运行数据文件'))
        end = source.index('    [ -f "$CTS_DIR/mode.txt" ]', start)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "config").mkdir()
            default = root / "default.json"
            default.write_text('{"factory":true}')
            config = root / "config.json"
            if existing:
                config.write_text('{"custom":true}')
            mocks = 'ui_print() { :; }; chattr() { :; };\n'
            if fail_copy:
                mocks += 'cp() { return 1; };\n'
            subprocess.run(["bash", "-c", mocks +
                            'CTS_DIR="$1"; MODPATH="$1"; SRC_CFG="$1/default.json";\n' +
                            source[start:end], "test", root.as_posix()], check=True)
            if existing:
                self.assertEqual(config.read_text(), '{"custom":true}')
            elif fail_copy:
                self.assertFalse(config.exists())
            else:
                self.assertEqual(config.read_text(), default.read_text())

    def test_config_upgrade_preserves_customization(self):
        self.run_config_install(existing=True)

    def test_config_upgrade_copy_failure_preserves_customization(self):
        self.run_config_install(existing=True, fail_copy=True)

    def test_config_first_install(self):
        self.run_config_install(existing=False)

    def test_config_first_install_copy_failure(self):
        self.run_config_install(existing=False, fail_copy=True)

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
