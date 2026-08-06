#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CTS 授权服务器 —— 远程命令下发「测试客户端」

它模拟你的正式 App:周期性调用 /api/verify 校验授权,并把后台下发的
pending_commands 在【本机】执行,再把结果回报 /api/device/command-result。
用来在不改正式 App 的情况下,端到端测通「后台下发命令 → 设备执行 → 回报」链路。

⚠️ 仅在【你自己拥有】的设备/电脑上运行。它会执行后台下发的任意命令,
   只用于自有设备的非破坏运维测试,切勿在他人设备上运行。

依赖:仅 Python 3 标准库(curl/jq 都不需要)。PC(Win/Mac/Linux)与安卓 Termux 均可。

用法:
  # 单次:验证一次,执行并回报本次拿到的命令
  python3 test-client.py \
      --server https://license.example.com \
      --api-key SK_xxx \
      --license KEY-XXXX-XXXX-XXXX-XXXX \
      --device  $(getprop ro.serialno)        # 安卓真机;PC 上随便填个唯一串

  # 循环:每 15 秒验证一次,持续接收并执行新命令(Ctrl+C 退出)
  python3 test-client.py ... --loop 15

也可用环境变量代替参数:CTS_SERVER / CTS_API_KEY / CTS_LICENSE / CTS_DEVICE
"""
import argparse
import json
import os
import platform
import subprocess
import sys
import time
import urllib.request
import urllib.error

DEFAULT_SERVER = "https://license.example.com"


def http_post(url, api_key, payload, timeout=20):
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(url, data=data, method="POST")
    req.add_header("Content-Type", "application/json")
    req.add_header("X-API-Key", api_key)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", "replace")
        try:
            return e.code, json.loads(body)
        except Exception:
            return e.code, {"raw": body}
    except Exception as e:
        return 0, {"error": str(e)}


def run_command(command, timeout):
    """在本机执行一条命令,返回 (exit_code, stdout, stderr, duration_ms)。"""
    start = time.time()
    try:
        p = subprocess.run(
            command, shell=True, capture_output=True, text=True, timeout=timeout
        )
        dur = int((time.time() - start) * 1000)
        return p.returncode, p.stdout, p.stderr, dur
    except subprocess.TimeoutExpired as e:
        dur = int((time.time() - start) * 1000)
        out = e.stdout.decode() if isinstance(e.stdout, bytes) else (e.stdout or "")
        err = e.stderr.decode() if isinstance(e.stderr, bytes) else (e.stderr or "")
        return 124, out, (err + f"\n[超时 {timeout}s 被终止]").strip(), dur
    except Exception as e:
        dur = int((time.time() - start) * 1000)
        return 1, "", f"[执行失败] {e}", dur


def verify_once(args):
    """调一次 verify,执行拿到的命令并回报。返回处理的命令条数。"""
    s, j = http_post(
        args.server.rstrip("/") + "/api/verify",
        args.api_key,
        {"license_key": args.license, "device_id": args.device,
         "device_info": f"test-client/{platform.system()} {platform.release()}"},
    )
    if not isinstance(j, dict) or j.get("code") != 200 or not j.get("success"):
        print(f"  [验证失败] HTTP {s} -> {j.get('message') or j}")
        return 0

    d = j.get("data", {})
    rem = d.get("remaining_days")
    rem_txt = "永久" if rem == -1 else f"剩 {rem} 天"
    print(f"  [验证成功] 设备 {d.get('device_id')} · {rem_txt} · token={str(d.get('token'))[:16]}…")

    cmds = d.get("pending_commands") or []
    if not cmds:
        print("  暂无待执行命令。")
        return 0

    print(f"  收到 {len(cmds)} 条待执行命令:")
    for c in cmds:
        cid, command = c.get("id"), c.get("command", "")
        print(f"  ──$ {command}")
        code, out, err, dur = run_command(command, args.timeout)
        if out:
            print("    stdout:", out.rstrip()[:1000])
        if err:
            print("    stderr:", err.rstrip()[:1000])
        print(f"    [退出码 {code} · {dur}ms] 回报中…", end=" ")
        rs, rj = http_post(
            args.server.rstrip("/") + "/api/device/command-result",
            args.api_key,
            {"id": cid, "device_id": args.device, "exit_code": code,
             "stdout": out, "stderr": err, "duration_ms": dur},
        )
        print("已回报" if isinstance(rj, dict) and rj.get("success") else f"回报失败({rs})")
    return len(cmds)


def main():
    ap = argparse.ArgumentParser(description="CTS 远程命令下发测试客户端")
    ap.add_argument("--server", default=os.getenv("CTS_SERVER", DEFAULT_SERVER))
    ap.add_argument("--api-key", default=os.getenv("CTS_API_KEY"))
    ap.add_argument("--license", default=os.getenv("CTS_LICENSE"))
    ap.add_argument("--device", default=os.getenv("CTS_DEVICE"))
    ap.add_argument("--loop", type=int, default=0, metavar="SEC",
                    help="循环模式:每 SEC 秒验证一次(默认 0=只跑一次)")
    ap.add_argument("--timeout", type=int, default=30, help="单条命令本地执行超时秒数(默认 30)")
    args = ap.parse_args()

    missing = [n for n in ("api_key", "license", "device") if not getattr(args, n)]
    if missing:
        ap.error("缺少参数:" + ", ".join("--" + m.replace("_", "-") for m in missing)
                 + "(也可用环境变量 CTS_API_KEY / CTS_LICENSE / CTS_DEVICE)")

    print(f"服务器:{args.server}")
    print(f"设备:  {args.device}")
    print(f"模式:  {'循环每 ' + str(args.loop) + 's' if args.loop else '单次'}\n")

    if not args.loop:
        verify_once(args)
        return
    try:
        while True:
            print(time.strftime("[%H:%M:%S] 验证中…"))
            verify_once(args)
            time.sleep(args.loop)
    except KeyboardInterrupt:
        print("\n已退出。")


if __name__ == "__main__":
    main()
