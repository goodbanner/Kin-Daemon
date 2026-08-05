#!/system/bin/sh
MODDIR="${0%/*}"
[ "$MODDIR" = "." ] && MODDIR="$(pwd)"

LOG_FILE="$MODDIR/service.log"

log_msg() {
  ts="$(date '+%Y-%m-%d %H:%M:%S')"
  echo "[$ts] $*" >> "$LOG_FILE"
  log -t OnePlusVPro "$*"
}

log_msg "service.sh 启动"

cleanup_legacy() {
  for d in \
    /data/adb/modules/CoreTurboScheduler \
    /data/adb/modules/Oxygen \
    /data/adb/modules/Kinetic \
    /data/adb/modules/CTS \
    /data/adb/modules/Kin_legacy \
    /data/adb/modules/OPGCE \
    /data/adb/modules/BSwitcher \
    /data/adb/modules/cosa_injector \
    /data/adb/modules/qinghe8e \
    /data/adb/modules/Thread_Config_replace \
    /data/adb/modules/Scene_Config_replace \
    /data/adb/modules/Config_Mount
  do
    [ -n "$d" ] && [ "$d" != "$MODDIR" ] && [ -d "$d" ] && {
        command -v chattr >/dev/null 2>&1 && chattr -R -i "$d" 2>/dev/null
        rm -rf "$d" 2>/dev/null
        log_msg "已清理旧子模块: $d"
    }
  done
}
cleanup_legacy

wait_boot_completed() {
  i=0
  while [ "$i" -lt 180 ]; do
    if [ "$(getprop sys.boot_completed)" = "1" ]; then
      return 0
    fi
    i=$((i + 1))
    sleep 1
  done
  return 1
}

wait_until_login() {
    while [ "$(getprop sys.boot_completed)" != "1" ]; do
        sleep 5
    done
    local test_file="/sdcard/Android/.PERMISSION_TEST"
    true >"$test_file"
    while [ ! -f "$test_file" ]; do
        sleep 5
        true >"$test_file"
    done
    rm "$test_file"
}

wait_boot_completed || log_msg "开机等待超时"

verify_status() {
    local MY_MODEL="[$(getprop ro.product.model)]"
    local mode_desc=""
    local cts_ok=1
    [ -f "$MODDIR/.cts_enabled" ]        || cts_ok=0
    [ -f "$MODDIR/Kinetic" ]   || cts_ok=0
    [ -f "/sdcard/Android/Kin/config.json" ] \
        || [ -f "$MODDIR/config/config.json" ] \
        || [ -f "$MODDIR/config/.config_backup" ] \
        || cts_ok=0
    if [ "$cts_ok" -eq 1 ] && pidof Kinetic >/dev/null 2>&1; then
        mode_desc="✅Kin守护运行中"
    elif [ "$cts_ok" -eq 1 ] && [ ! -s "/sdcard/Android/Kin/license.json" ]; then
        mode_desc="⚠️未激活,请在 Kin App 输入卡密激活"
    elif [ "$cts_ok" -eq 1 ]; then
        mode_desc="⚠️Kin已部署未运行"
    else
        mode_desc="❌Kin模式失败"
    fi

    local desc="$MY_MODEL $mode_desc"
    if [ -f "$MODDIR/module.prop" ]; then
        # 仅在状态变化时改写 module.prop,避免无谓写盘;激活后守护起来会自动刷新为「运行中」
        local cur="$(sed -n 's/^description=//p' "$MODDIR/module.prop" 2>/dev/null)"
        if [ "$cur" != "$desc" ]; then
            sed -i "s/^description=.*/description=$desc/g" "$MODDIR/module.prop" 2>/dev/null
            log_msg "状态: $desc"
        fi
    fi
    # 返回:是否已达「运行中」(供刷新循环判断是否可停止)
    [ "$cts_ok" -eq 1 ] && pidof Kinetic >/dev/null 2>&1
}

wait_until_login

if [ -f "$MODDIR/.cts_enabled" ] && [ -f "$MODDIR/Kinetic" ]; then
    log_msg "开机自动启动 Kin 调度守护进程(完全独立运行,无需 Scene 接管)"

    if [ -d /data/data/com.omarea.vtools ] || [ -d /data/data/com.omarea.gesture ]; then
        ( sh "$MODDIR/vtools/init_vtools.sh" >/dev/null 2>&1 ) &
    fi

    # 单一常驻守护循环 —— 合并原来的三个 while/sleep 后台子进程(守护拉起 / cpuset 兜底 /
    # mode.txt 合法性)。在 busybox shell 下每个后台循环就是一个常驻 busybox 进程;合并后
    # 常驻 busybox 只剩这一个(守护进程 Kinetic 本身不是 busybox)。
    (
        CTS_DIR="/sdcard/Android/Kin"
        CTS_BIN="$MODDIR/Kinetic"
        CTS_LIB="$MODDIR/lib/arm64"
        MODE_FILE="$CTS_DIR/mode.txt"
        for i in $(seq 1 60); do
            [ -d "$CTS_DIR" ] && [ -w "$CTS_DIR" ] && break
            sleep 1
        done
        AUTH_CFG="$MODDIR/config/.config_backup"
        [ -f "$AUTH_CFG" ] || AUTH_CFG="$MODDIR/config/config.json"
        if [ -f "$AUTH_CFG" ] && [ ! -s "$CTS_DIR/config.json" ]; then
            command -v chattr >/dev/null 2>&1 && chattr -i "$CTS_DIR/config.json" 2>/dev/null
            cp -f "$AUTH_CFG" "$CTS_DIR/config.json" 2>/dev/null
        fi
        [ -f "$MODE_FILE" ] || echo "balance" > "$MODE_FILE"
        chmod 0755 "$CTS_BIN" 2>/dev/null
        chmod 0644 "$CTS_LIB/libc++_shared.so" 2>/dev/null

        CHIP=$(cat "$MODDIR/.cts_enabled" 2>/dev/null)
        case "$CHIP" in
            8G3) SMALL="0-4" ;;
            *)   SMALL="0-5" ;;
        esac
        FULL="0-7"
        GROUPS="top-app foreground background system-background restricted"
        set_cpuset() {
            r="$1"
            for g in $GROUPS; do
                p="/dev/cpuset/$g/cpus"
                [ -e "$p" ] || continue
                cur=$(cat "$p" 2>/dev/null)
                [ "$cur" = "$r" ] && continue
                chmod 0666 "$p" 2>/dev/null
                ( echo "$r" > "$p" ) 2>/dev/null
            done
        }
        is_base() { case "$1" in powersave|balance|performance|fast|auto) return 0 ;; *) return 1 ;; esac; }

        last_good="balance"
        tick=0
        while true; do
            # 守护拉起: 每 ~10s(每 5 个 tick)检一次
            if [ $((tick % 5)) -eq 0 ] && ! pidof Kinetic >/dev/null 2>&1; then
                LD_LIBRARY_PATH="$CTS_LIB:$LD_LIBRARY_PATH" nohup "$CTS_BIN" > "$CTS_DIR/log.txt" 2>&1 &
            fi
            # mode.txt 合法性 + cpuset 兜底(每 2s)
            m=$(cat "$MODE_FILE" 2>/dev/null | tr -d ' \t\r\n')
            if [ -n "$m" ]; then
                if is_base "$m"; then
                    last_good="$m"
                else
                    echo "$last_good" > "$MODE_FILE" 2>/dev/null
                    log_msg "mode.txt 非法值 '$m' 已纠回 $last_good"
                    m="$last_good"
                fi
                case "$m" in
                    video|powersave) set_cpuset "$SMALL" ;;
                    *)               set_cpuset "$FULL" ;;
                esac
            fi
            tick=$((tick + 1))
            sleep 2
        done
    ) &
    log_msg "Kin 守护循环已启动(合并: 拉起 + mode 校验 + cpuset, 单进程)"

fi

MODE="$(cat "/sdcard/Android/Kin/mode.txt" 2>/dev/null)"
[ -n "$MODE" ] || MODE="balance"

if [ -x /data/powercfg.sh ] || [ -s /data/powercfg.sh ]; then
  sh /data/powercfg.sh "$MODE" 2>/dev/null
  log_msg "powercfg 已执行: $MODE"
elif [ -f "$MODDIR/vtools/powercfg.sh" ]; then
  sh "$MODDIR/vtools/powercfg.sh" "$MODE" 2>/dev/null
  log_msg "vtools powercfg 已执行: $MODE"
else
  log_msg "未找到可执行的 powercfg.sh"
fi

settings put system oplus_customize_cta_user_experience 0
USER_EXP=$(settings get system oplus_customize_cta_user_experience 2>/dev/null)
if [ "$USER_EXP" = "0" ]; then
    log_msg "用户体验计划已关闭"
else
    log_msg "警告：用户体验计划设置可能未生效"
fi

(
    # 刷新模块状态:用户激活卡密后守护会在 ~10s 内由上面的循环拉起。
    # 每 30s 复查一次写入 module.prop;一旦确认「运行中」即停止刷新以省开销。
    while true; do
        verify_status && break   # 已运行 → 状态已写「运行中」,停止循环
        sleep 30
    done
) &

log_msg "service.sh 全部完成"
