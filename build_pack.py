#!/usr/bin/env python3
"""
Kin Build & Pack Script
Correct workflow: Compile -> Pack output/Kinetic with module sources

Usage:
    python build_pack.py              # Full build + pack
    python build_pack.py build        # Build only
    python build_pack.py pack         # Pack only (output/Kinetic must exist)
    python build_pack.py clean        # Clean all

Environment:
    ANDROID_NDK    Path to Android NDK (required for build)
"""
from datetime import datetime
import subprocess
import sys
import os
import platform
import shutil
from pathlib import Path
import stat
import zipfile

# ============ Configuration ============
PROJECT_NAME = "Kin"
BINARY_NAME = "Kinetic"
MODULE_ID = "Kin"

now = datetime.now()
DATE_CODE = now.strftime("%Y%m%d")
TIME_STR = now.strftime("%Y %m %d: %H:%M:%S")
# versionCode 必须【每次构建严格递增】, 否则同一天多次构建 versionCode 相同, KernelSU/Magisk 视为
# 同版本 → updateJson/管理器认为"无更新", 用户看到的还是旧安装脚本/旧模块。用 Unix 纪元秒(单调递增,
# 2038 年前 < int32 max), 保证每次构建都被识别为更新。
VERSION_CODE = int(now.timestamp())

# Android API level (可由环境变量覆盖, 默认 29 = Android 10, 兼容最广)
ANDROID_API = os.environ.get("ANDROID_API", "29")

# Paths
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MAGISK_DIR = os.path.join(SCRIPT_DIR, "magisk")
SRC_FILE = os.path.join(SCRIPT_DIR, "src", "main.cpp")
# Ed25519 授权验证(公有领域 C 实现),需作为独立 C 编译单元参与链接
CRYPTO_SRC = os.path.join(SCRIPT_DIR, "src", "tweetnacl.c")
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "output")

# NDK detection
NDK_PATHS = {
    "Windows": [
        "D:/Android-NDK",
        "C:/Users/%USERNAME%/AppData/Local/Android/Sdk/ndk",
        "C:/Android-NDK",
    ],
    "Linux": [
        os.path.expanduser("~/Android/Sdk/ndk"),
        "/opt/android-ndk",
        os.path.expanduser("~/android-ndk"),
    ],
    "Darwin": [
        os.path.expanduser("~/Library/Android/sdk/ndk"),
        "/opt/android-ndk",
        os.path.expanduser("~/android-ndk"),
    ],
}

# ============ Logging ============
def log(msg):
    print(f"[{TIME_STR}] {msg}")

def error(msg):
    print(f"[ERROR] {msg}", file=sys.stderr)

def success(msg):
    print(f"[OK] {msg}")

# ============ NDK Detection ============
def find_ndk():
    """Find Android NDK installation"""
    # Check environment variable first
    env_ndk = os.environ.get("ANDROID_NDK", os.environ.get("ANDROID_NDK_HOME", ""))
    if env_ndk and os.path.exists(env_ndk):
        return env_ndk

    # Check predefined paths
    system = platform.system()
    for p in NDK_PATHS.get(system, []):
        if "%USERNAME%" in p:
            p = p.replace("%USERNAME%", os.environ.get("USERNAME", ""))
        if os.path.exists(p):
            subdirs = [d for d in os.listdir(p) if os.path.isdir(os.path.join(p, d)) and d[0].isdigit()]
            if subdirs:
                return os.path.join(p, sorted(subdirs)[-1])
            return p
    return None

def get_clang(ndk_root):
    """Get clang++ path from NDK"""
    system = platform.system()
    hosts = {"Windows": "windows-x86_64", "Linux": "linux-x86_64", "Darwin": "darwin-x86_64"}
    host = hosts.get(system, "linux-x86_64")
    clang_name = "clang++.exe" if system == "Windows" else "clang++"
    clang_path = os.path.join(ndk_root, "toolchains", "llvm", "prebuilt", host, "bin", clang_name)
    return clang_path if os.path.exists(clang_path) else None

def get_sysroot(ndk_root):
    """Get sysroot path from NDK"""
    system = platform.system()
    hosts = {"Windows": "windows-x86_64", "Linux": "linux-x86_64", "Darwin": "darwin-x86_64"}
    host = hosts.get(system, "linux-x86_64")
    return os.path.join(ndk_root, "toolchains", "llvm", "prebuilt", host, "sysroot")

# ============ Build ============
def build_binary():
    """Build the binary for Android arm64"""
    ndk = find_ndk()
    if not ndk:
        log("=" * 50)
        error("Android NDK not found!")
        log("Please install Android NDK and set ANDROID_NDK environment variable.")
        log("Example: export ANDROID_NDK=/path/to/android-ndk")
        log("=" * 50)
        return False

    log(f"Using NDK: {ndk}")

    clang = get_clang(ndk)
    if not clang:
        error("clang++ not found in NDK!")
        return False

    sysroot = get_sysroot(ndk)
    include_dir = os.path.join(SCRIPT_DIR, "include")

    os.makedirs(OUTPUT_DIR, exist_ok=True)

    # Compiler flags — 与 CMakeLists.txt 的 Release 配置保持一致：
    # 本进程是纯整数 + sysfs IO 的常驻 daemon，瓶颈在 IO 而非算力。
    #   - 去掉 -ffast-math：会改变 NaN/Inf 语义、可能引入 UB，对本进程零收益；
    #   - 去掉 -O3/-funroll-loops：徒增二进制体积与常驻 RSS，速度无差；
    #   - 改用 -Os：更小代码段 / 更低常驻内存 / 更快载入。
    cpp_flags = [
        f"--sysroot={sysroot}",
        f"--target=aarch64-linux-android{ANDROID_API}",
        "-std=c++2b",
        "-static",          # Static linking for standalone binary
        "-s",               # Strip symbol table
        "-Os",              # Optimize for size (IO 密集型 daemon，体积优先)
        "-flto",            # Link-time optimization
        "-finline-functions",  # Function inlining
        "-fomit-frame-pointer",  # Omit frame pointer
        "-Wall", "-Wextra", "-Wshadow",  # Warnings
        "-fno-rtti",        # No RTTI (smaller binary)
        "-fno-threadsafe-statics",  # Optimization for static init
        "-fvisibility=hidden",  # Hide symbols
        "-ffunction-sections",  # Remove unused functions (learned from CoreTaskOptimizer)
        "-fdata-sections",      # Remove unused data
        "-Wl,--gc-sections",    # Garbage collect unused sections
        f"-I{include_dir}",
    ]

    # 构建开关(非发布变体, 只影响本地调试): NO_LICENSE=ON/1/true → 定义 KIN_NO_LICENSE,
    # lic::readAuth() 恒返回已付费。授权已改为 App 驱动(守护无条件运行, 只读 App 下发的
    # auth.json 知道是否付费), 此开关仅供内部免授权调试用。
    if os.environ.get("NO_LICENSE", "").upper() in ("1", "ON", "TRUE", "YES"):
        cpp_flags.append("-DKIN_NO_LICENSE")
        log("Build: 免授权调试构建 (NO_LICENSE — license checks disabled)")
    else:
        log("Build: 授权版 (正式发布唯一版本)")

    output_bin = os.path.join(OUTPUT_DIR, BINARY_NAME)

    # 先把 tweetnacl.c 以 C 模式单独编成对象文件(C++ 严格模式无法直接编它)。
    crypto_obj = os.path.join(OUTPUT_DIR, "tweetnacl.o")
    c_flags = [
        f"--sysroot={sysroot}",
        f"--target=aarch64-linux-android{ANDROID_API}",
        "-x", "c", "-std=c99",
        "-Os", "-flto",
        "-fomit-frame-pointer",
        "-ffunction-sections", "-fdata-sections",
        "-fvisibility=hidden",
        f"-I{include_dir}",
        "-c", CRYPTO_SRC, "-o", crypto_obj,
    ]
    log("Compiling crypto (tweetnacl.c, C)...")
    rc = subprocess.run([clang] + c_flags, capture_output=True, text=True)
    if rc.returncode != 0:
        error("Crypto compilation failed!")
        if rc.stderr:
            log(rc.stderr)
        return False

    command = [clang] + cpp_flags + [SRC_FILE, crypto_obj, "-o", output_bin]

    log(f"Build command: {' '.join(command)}")
    log("Compiling...")

    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        error("Compilation failed!")
        if result.stderr:
            log(result.stderr)
        return False

    # Strip binary further
    strip_tool = os.path.join(os.path.dirname(clang), "llvm-strip" if platform.system() != "Windows" else "llvm-strip.exe")
    if os.path.exists(strip_tool):
        subprocess.run([strip_tool, "--strip-all", output_bin], capture_output=True)

    size_kb = os.path.getsize(output_bin) / 1024
    success(f"Build successful! ({size_kb:.1f} KB)")
    success(f"Output: {output_bin}")
    return True

# ============ Copy Binary to Magisk ============
def copy_to_magisk():
    """Copy compiled binary and config to magisk directory"""
    src_bin = os.path.join(OUTPUT_DIR, BINARY_NAME)

    if not os.path.exists(src_bin):
        error(f"Binary not found: {src_bin}")
        error("Run 'python build_pack.py build' first, or manually compile.")
        return False

    # Copy binary
    dst_bin = os.path.join(MAGISK_DIR, BINARY_NAME)
    shutil.copy2(src_bin, dst_bin)
    log(f"Copied binary to: {dst_bin}")

    # Copy config.json
    src_config = os.path.join(SCRIPT_DIR, "Json", "config.json")
    dst_config = os.path.join(MAGISK_DIR, "config.json")
    shutil.copy2(src_config, dst_config)
    log(f"Copied config to: {dst_config}")

    # Copy Kin.apk(随包安装的 App)。来源优先级:环境变量 CTS_APK > cts 仓库默认产物路径。
    # 找不到新 APK 时跳过——沿用已入库的 magisk/Kin.apk。
    # 兄弟目录名大小写在不同环境不一致(cts / CTS), Linux 文件系统区分大小写 → 两种都试,
    # 否则找不到新 APK 会静默沿用已入库的旧 magisk/Kin.apk(用户会装到过期 App)。
    parent = os.path.dirname(SCRIPT_DIR)
    # 优先新环境变量 KIN_APK, 兼容旧 CTS_APK(老 CI/脚本不破)
    apk_candidates = [os.environ.get("KIN_APK", ""), os.environ.get("CTS_APK", "")]
    for repo in ("CTS", "cts"):
        apk_candidates.append(os.path.join(parent, repo, "app", "build",
                                           "outputs", "apk", "release", "app-release.apk"))
        apk_candidates.append(os.path.join(parent, repo, "app", "build",
                                           "outputs", "apk", "debug", "app-debug.apk"))
    dst_apk = os.path.join(MAGISK_DIR, "Kin.apk")
    for apk in apk_candidates:
        if apk and os.path.exists(apk):
            shutil.copy2(apk, dst_apk)
            log(f"Copied APK to: {dst_apk}")
            break
    else:
        if os.path.exists(dst_apk):
            log("No fresh APK found (set CTS_APK to refresh); keeping committed magisk/Kin.apk")
        else:
            log("⚠️ No Kin.apk available — module will install without bundled App")

    # Update module.prop versionCode
    prop_file = os.path.join(MAGISK_DIR, "module.prop")
    if os.path.exists(prop_file):
        with open(prop_file, "r", encoding="utf-8") as f:
            lines = f.readlines()
        with open(prop_file, "w", encoding="utf-8") as f:
            for line in lines:
                if line.startswith("versionCode="):
                    f.write(f"versionCode={VERSION_CODE}\n")
                else:
                    f.write(line)
        log(f"Updated versionCode: {VERSION_CODE} (epoch, strictly increasing)")

    success("Magisk module files prepared.")
    return True

# ============ Pack Magisk ZIP ============
def pack_magisk():
    """Rebuild the archive atomically without modifying committed module assets."""
    zip_name = f"{PROJECT_NAME}-v3.2-{DATE_CODE}.zip"
    zip_path = Path(OUTPUT_DIR) / zip_name
    temporary = zip_path.with_suffix(".zip.tmp")
    try:
        binary = (Path(OUTPUT_DIR) / BINARY_NAME).read_bytes()
        if (len(binary) < 64 or binary[:6] != b"\x7fELF\x02\x01" or
                int.from_bytes(binary[18:20], "little") != 183):
            raise ValueError("output/Kinetic is not a little-endian ARM64 ELF")
        module = Path(MAGISK_DIR)
        files = {}
        for path in sorted(module.rglob("*")):
            name = path.relative_to(module).as_posix()
            if (not path.is_file() or any(p.startswith(".") for p in path.relative_to(module).parts)
                    or path.suffix in (".log", ".tmp", ".bak") or path.name == "Kin_pending.apk"):
                continue
            files[name] = path.read_bytes()
        files[BINARY_NAME] = binary
        apk = os.environ.get("KIN_APK") or os.environ.get("CTS_APK")
        if apk:
            files["Kin.apk"] = Path(apk).read_bytes()
        for path in sorted((Path(SCRIPT_DIR) / "Json").glob("*")):
            if path.is_file() and path.suffix in (".json", ".txt"):
                files["config/" + path.name] = path.read_bytes()
        files["config.json"] = (Path(SCRIPT_DIR) / "Json/config.json").read_bytes()
        prop = files["module.prop"].decode("utf-8").splitlines()
        old_codes = [int(line.split("=", 1)[1]) for line in prop if line.startswith("versionCode=")]
        version = max(VERSION_CODE, max(old_codes, default=0) + 1)
        prop = [line for line in prop if not line.startswith("versionCode=")]
        files["module.prop"] = ("\n".join(prop) + f"\nversionCode={version}\n").encode("utf-8")
        zip_path.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in sorted(files.items()):
                executable = name == BINARY_NAME or name.endswith(".sh") or name.endswith("/update-binary")
                if name != BINARY_NAME and (executable or name.endswith((".prop", ".json", ".txt"))):
                    data = data.decode("utf-8").replace("\r\n", "\n").encode("utf-8")
                info = zipfile.ZipInfo(name)
                info.create_system = 3
                info.external_attr = (stat.S_IFREG | (0o755 if executable else 0o644)) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(info, data)
        with zipfile.ZipFile(temporary) as archive:
            if archive.testzip() is not None or archive.read(BINARY_NAME) != binary:
                raise ValueError("Archive validation failed")
        os.replace(temporary, zip_path)
        success(f"Output: {zip_path}")
        return True
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as exc:
        temporary.unlink(missing_ok=True)
        error(f"Failed to create ZIP: {exc}")
        return False

# ============ Clean ============
def clean():
    """Clean build artifacts"""
    # Remove output directory
    if os.path.exists(OUTPUT_DIR):
        shutil.rmtree(OUTPUT_DIR)
        log("Cleaned output/")

    # Remove binary from magisk dir
    magisk_bin = os.path.join(MAGISK_DIR, BINARY_NAME)
    if os.path.exists(magisk_bin):
        os.remove(magisk_bin)
        log(f"Removed {magisk_bin}")

    success("Clean complete.")

# ============ Main ============
def show_usage():
    print("""
Usage: python build_pack.py [command]

Commands:
    (none)     Full workflow: build -> pack (module sources remain unchanged)
    build      Compile binary only
    copy       Copy binary and config to magisk/
    pack       Create Magisk ZIP from output/Kinetic and module sources
    clean      Remove all build artifacts
    help       Show this help

Environment:
    ANDROID_NDK    Path to Android NDK (required for build)

Examples:
    # Full workflow
    export ANDROID_NDK=/path/to/ndk
    python build_pack.py

    # Build only (for manual testing)
    python build_pack.py build

    # If you compiled manually, just pack
    cp Kinetic output/Kinetic
    python build_pack.py pack
""")

def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "full"

    if cmd in ("full", ""):
        log("=" * 50)
        log("Step 1/2: Building binary...")
        log("=" * 50)
        if not build_binary():
            sys.exit(1)

        log("")
        log("=" * 50)
        log("Step 2/2: Packing Magisk ZIP...")
        log("=" * 50)
        if not pack_magisk():
            sys.exit(1)

        log("")
        log("=" * 50)
        log("ALL DONE!")
        log("=" * 50)

    elif cmd == "build":
        if not build_binary():
            sys.exit(1)

    elif cmd == "copy":
        if not copy_to_magisk():
            sys.exit(1)

    elif cmd == "pack":
        if not pack_magisk():
            sys.exit(1)

    elif cmd == "clean":
        clean()

    elif cmd in ("help", "-h", "--help"):
        show_usage()

    else:
        error(f"Unknown command: {cmd}")
        show_usage()
        sys.exit(1)

if __name__ == "__main__":
    main()
