<div align="center">

# Kin

**CPU scheduling daemon**

Battery-first · self-learning smart mode · per-app profiles · governor-driven frequency response

[简体中文](README.md) | English

[![License](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](#license)
[![Governor](https://img.shields.io/badge/Governor-walt%20·%20schedutil%20·%20hmbird-brightgreen.svg)](#)
[![App](https://img.shields.io/badge/Front--end-Kin%20Companion-orange)](https://github.com/kktwo2923-creator/Kin)

</div>

---

## What is this

**Kin** is a CPU scheduling daemon for Android. It sets per-mode frequency limits and governor parameters, then leaves the actual up/down-scaling rhythm to the kernel governor (walt / schedutil / hmbird) — power-efficient yet responsive. In smart mode it adds a load state machine and a frame-aware closed loop on top.

Use it standalone by writing `mode.txt`, or pair it with the GUI front-end [Kin Companion](https://github.com/kktwo2923-creator/Kin) for per-app automatic takeover.

- **Multi-SoC**: Snapdragon 8 Elite Gen 5 (8e5/8E) / 8G1 / 8G2 / 8G3 / 8G5 / 7P3 · Dimensity 9300 / 9400 — ships 10 factory configs, auto-selected at flash time.

## Features

- 🪶 **Minimal frequency model** — only limits + governor parameters; frequency response belongs to the governor
- 🔋 **Five modes** — `powersave / balance / performance / fast (FengChi) / auto (smart)`, switchable with one command
- 🎯 **Per-app profiles** — match by package name; games ramp up automatically and restore on exit
- 🧠 **Smart mode (auto)** — six-state load FSM + self-learned power bias + sleep-schedule learning + questionnaire correction; per-state frequency caps (`fsm_policy`) and per-category caps / core parking (`app_policy`)
- 🎮 **FAS (frame-aware scheduling)** — closed-loop `scaling_max` control from measured FPS while gaming: steady frames → lower clocks to save power, dropped frames → open up instantly
- 🌀 **FengChi governor switch** — apps profiled as *FengChi game* switch to `hmbird`/`scx` on game entry (fallback `walt`/`sugov_ext`), restored on exit; mutually exclusive with FAS per app
- **System-level tuning (ported from ASB)** — per-mode VM/network/WiFi tuning and background memory trim

## Build

Requirements: Android NDK (`ANDROID_NDK` pointing at the NDK directory), CMake >= 3.22, Python 3 (for packaging).

```bash
export ANDROID_NDK=/path/to/android-ndk   # build.sh exits with an error if unset
./build.sh                  # builds the arm64-v8a binary into output/arm64/Kinetic
python build_pack.py pack   # packs it with the magisk/ module sources into a flashable zip
```

`build.sh` honours `BUILD_TYPE` (default `Release`) and `ANDROID_PLATFORM` (default `android-29`).
The `Kinetic` file at the repo root is a prebuilt binary that `pack` reuses directly — run `build.sh` first if you changed the sources.

## Quick start

Vendor-cap attack writes and boot-time property/bind-mount overrides are removed. Legacy attack and BootTrim options no longer enable those actions. SysTune snapshots VM/TCP/queue values and conditionally restores them on disable or graceful shutdown, retrying failures. Ownership is in-memory, not a crash-recovery journal. Persistent settings changed by older versions require checking against the original device configuration.

Upgrades preserve existing `config.json` and App data; signature conflicts never trigger automatic App removal. Uninstall keeps Kin settings, profiles and learning data. Boot scripts no longer spoof shell temperature, stop horae or mask vendor frequency limits. Reboot after upgrading from older builds to clear their existing runtime changes.

Set `ANDROID_NDK` and run `python build_pack.py` to build and pack `output/Kinetic` without changing committed module assets. `pack` requires that build output and never falls back to the bundled binary. The bundled APK is retained unless `KIN_APK` explicitly selects a replacement.

```
/sdcard/Android/Kin/
├── config.json      # main config (hot-reloaded)
├── mode.txt         # current mode
└── log.txt          # runtime log
```

Switch modes (takes effect immediately):

```bash
echo "powersave"   > /sdcard/Android/Kin/mode.txt   # maximum battery
echo "balance"     > /sdcard/Android/Kin/mode.txt   # daily driver ← recommended
echo "performance" > /sdcard/Android/Kin/mode.txt   # performance first
echo "fast"        > /sdcard/Android/Kin/mode.txt   # FengChi (hmbird governor)
echo "auto"        > /sdcard/Android/Kin/mode.txt   # smart self-learning mode
```

## Smart mode (auto)

This is heuristic adaptation, not validated energy optimization. Apps, brightness and network conditions affect session current; reward signals alone do not establish power or performance gains. Validate with matched workloads on the target device.

Wi-Fi power saving is changed only after reading its original state. Balance mode, disabling SysTune and graceful shutdown conditionally restore values still owned by Kin; failed restores remain pending. Forced termination cannot guarantee restoration. Other modules are not deleted at boot; resolve conflicts in the module manager.

The learning/decision algorithm is ported from [AutoSystemBoost](https://github.com/krilikd/AutoSystemBoost)'s Smart Mode (MIT, © Dima Krylov): 12-bucket habit learning, confidence gating, graded fallback, night / thermal-veto / low-battery overrides.

On top of that, a six-state **FSM** (`DEEP_IDLE / LIGHT_IDLE / MODERATE / HEAVY / SUSTAINED / GAMING`) advances every ~2 s from GPU utilization / CPU load / temperature, with hysteresis and minimum dwell times, writing per-state frequency ceilings. Entering **GAMING** is driven by the per-app policy (`game_heavy` / `game_lite` profiles enter instantly) with GPU-threshold and system-category fallbacks.

- **Per-state caps** — override any state's ceiling via `Function.AutoMode.fsm_policy` (edited visually in the app)
- **Per-category caps** — video/social/music etc. get extra ceilings and optional core parking via `app_policy`
- **FAS** — in GAMING, `scaling_max` follows measured FPS (ported from [fas-rs](https://github.com/shadow3aaa/fas-rs), P-controller with util ceiling, jank fast-open, target auto-detection)
- **Questionnaire correction** — the app shows what has been learned (sleep window, power bias, per-state caps) and lets the user confirm or correct; corrections seed the learner via `auto_control.txt`

## Frequency model

Kin does not do scene detection. Three governor parameters do the heavy lifting (written under `gov_cN.params`):

| Parameter | Unit | Meaning |
|---|---|---|
| `up_rate_limit_us` | µs | minimum interval between frequency increases — small = snappy, large = calm |
| `down_rate_limit_us` | µs | minimum interval between decreases — small = drops fast (saves power) |
| `hispeed_load` | % | load threshold for hispeed boost — high = harder to boost |

Rule of thumb: `down_rate_limit_us ≤ 4000` in powersave/balance is the key to actually seeing clocks drop; `up_rate_limit_us ≤ 5000` in game/performance keeps things smooth.

## Configuration

```json
{
    "meta": { ... },            // basic info
    "Policy": { ... },          // CPU cluster index (c0/c1/... → policyN)
    "Function": { ... },        // feature switches (AutoMode / SysTune / ...)
    "package_blacklist": [...], // apps never taken over
    "models": [ ... ]           // per-profile frequency configs (game_heavy / video / ...)
}
```

All values are written as strings (`"true"`, `"75"`); the daemon parses them tolerantly. `config.json`, `applist.prop` and `perapp_powermode.txt` are all hot-reloaded via inotify — no restart needed. See the [Chinese README](README.md) for the full field-by-field reference and FAQ.

## Credits

Design references [yumi](https://github.com/imacte/yumi) and [MW_CpuTurboScheduler](https://github.com/MoWei-2077/MW_CpuTurboScheduler); smart mode ported from [AutoSystemBoost](https://github.com/krilikd/AutoSystemBoost) (MIT); FAS ported from [fas-rs](https://github.com/shadow3aaa/fas-rs); thread templates from SutoLiu's AppOpt / JZzz ecosystem (GPL-3.0); engine thread signatures aligned with Metric.

## License

GPL-3.0. Upstream attributions are kept at the top of the corresponding source files.
