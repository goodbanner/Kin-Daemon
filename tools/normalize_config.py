#!/usr/bin/env python3
"""把 config JSON 的键顺序规整成 Kin-默认 模板顺序(值不动)。

需求:"config 的格式顺序按模板来,参数不管" —— 即仅统一键的排列顺序与结构,
不改动任何数值。模板里没有的、但守护进程需要的块(如 Function.AutoMode)按原值保留,
追加到所属对象的已知键之后。

用法:
    python3 tools/normalize_config.py            # 规整 Json/ 与 magisk/config/ 下所有 *.json
    python3 tools/normalize_config.py <文件...>  # 只规整指定文件
"""
import json
import os
import sys
from collections import OrderedDict

# ---- 模板键顺序(来自 Kin-默认) ----
TOP = ["meta", "Policy", "Function", "package_blacklist", "models"]
META = ["name", "version", "author", "loglevel"]
POLICY = ["c0", "c1", "c2", "c3"]
FUNCTION = ["Cpuset", "LaunchBoost", "OfficialMode", "Scheduler", "SceneDetect", "AppProfile", "AutoMode"]
CPUSET = ["enable", "top_app", "foreground", "restricted", "system_background", "background"]
SCHEDULER = ["enable", "sched_energy_aware", "sched_schedstats", "sched_nr_migrate"]
MODEL = [
    "model_name", "mode", "is_game", "packages",
    "freq_min_c0", "freq_max_c0", "freq_min_c1", "freq_max_c1",
    "gpu_min_mhz", "gpu_max_mhz",
    "CoreOnline", "gov_c0", "gov_c1",
]
COREONLINE = ["Core0", "Core1", "Core2", "Core3", "Core4", "Core5", "Core6", "Core7"]
GOV = ["governor", "params"]
GOV_PARAMS = ["up_rate_limit_us", "down_rate_limit_us"]


def reorder(d, order):
    """按 order 重排 dict 的键;order 中没有的键(如 AutoMode)按原顺序追加在后面。"""
    if not isinstance(d, dict):
        return d
    out = OrderedDict()
    for k in order:
        if k in d:
            out[k] = d[k]
    for k in d:  # 保留模板未列出的键(不丢数据)
        if k not in out:
            out[k] = d[k]
    return out


def norm_gov(g):
    g = reorder(g, GOV)
    if isinstance(g.get("params"), dict):
        g["params"] = reorder(g["params"], GOV_PARAMS)
    return g


def norm_model(m):
    m = reorder(m, MODEL)
    if isinstance(m.get("CoreOnline"), dict):
        m["CoreOnline"] = reorder(m["CoreOnline"], COREONLINE)
    if isinstance(m.get("gov_c0"), dict):
        m["gov_c0"] = norm_gov(m["gov_c0"])
    if isinstance(m.get("gov_c1"), dict):
        m["gov_c1"] = norm_gov(m["gov_c1"])
    return m


def norm_config(cfg):
    cfg = reorder(cfg, TOP)
    if isinstance(cfg.get("meta"), dict):
        cfg["meta"] = reorder(cfg["meta"], META)
    if isinstance(cfg.get("Policy"), dict):
        cfg["Policy"] = reorder(cfg["Policy"], POLICY)
    if isinstance(cfg.get("Function"), dict):
        fn = reorder(cfg["Function"], FUNCTION)
        if isinstance(fn.get("Cpuset"), dict):
            fn["Cpuset"] = reorder(fn["Cpuset"], CPUSET)
        if isinstance(fn.get("Scheduler"), dict):
            fn["Scheduler"] = reorder(fn["Scheduler"], SCHEDULER)
        cfg["Function"] = fn
    if isinstance(cfg.get("models"), list):
        cfg["models"] = [norm_model(m) for m in cfg["models"]]
    return cfg


def collect_targets():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    targets = []
    for sub in ("Json", os.path.join("magisk", "config")):
        d = os.path.join(root, sub)
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if f.endswith(".json"):
                targets.append(os.path.join(d, f))
    return targets


def main():
    files = sys.argv[1:] or collect_targets()
    for path in files:
        with open(path, "r", encoding="utf-8") as f:
            cfg = json.load(f)
        cfg = norm_config(cfg)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(cfg, f, ensure_ascii=False, indent=4)
            f.write("\n")
        print(f"[OK] normalized {path}")


if __name__ == "__main__":
    main()
