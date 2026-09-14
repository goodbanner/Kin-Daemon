#!/system/bin/sh
MODDIR="${0%/*}"
[ "$MODDIR" = "." ] && MODDIR="$(pwd)"

LOG_FILE="$MODDIR/service.log"

log_msg() {
  ts="$(date '+%Y-%m-%d %H:%M:%S')"
  echo "[$ts] $*" >> "$LOG_FILE"
  if command -v log >/dev/null 2>&1; then
    log -t OnePlusVPro "$*"
  fi
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
    # /sdcard 可能在 FUSE 就绪前不可访问；不要无限阻塞整个模块启动。
    local test_file=""
    local candidates="/sdcard/Android/.PERMISSION_TEST /storage/emulated/0/Android/.PERMISSION_TEST"
    i=1
    while [ "$i" -le 60 ]; do
        for candidate in $candidates; do
            mkdir -p "${candidate%/*}" 2>/dev/null
            if printf '1\n' > "$candidate" 2>/dev/null && [ -f "$candidate" ]; then
                test_file="$candidate"
                break 2
            fi
        done
        i=$((i + 1))
        sleep 5
    done
    if [ -n "$test_file" ]; then
        rm -f "$test_file"
    else
        log_msg "共享存储登录检测超时，继续启动并由后续路径回退处理"
    fi
}

wait_boot_completed || log_msg "开机等待超时"

verify_status() {
    local MY_MODEL="[$(getprop ro.product.model)]"
    local DATA_DIR="${KIN_DIR:-/sdcard/Android/Kin}"
    local mode_desc=""
    local cts_ok=1
    [ -f "$MODDIR/.cts_enabled" ]        || cts_ok=0
    [ -f "$MODDIR/Kinetic" ]   || cts_ok=0
    [ -f "$DATA_DIR/config.json" ] \
        || [ -f "$MODDIR/config/config.json" ] \
        || [ -f "$MODDIR/config/.config_backup" ] \
        || cts_ok=0
    if [ "$cts_ok" -eq 1 ] && pidof Kinetic >/dev/null 2>&1; then
        mode_desc="✅Kin守护运行中"
    elif [ "$cts_ok" -eq 1 ] && [ ! -s "$DATA_DIR/license.json" ]; then
        mode_desc="⚠️未激活,请在 Kin App 输入卡密激活"
    elif [ "$cts_ok" -eq 1 ]; then
        mode_desc="⚠️Kin已部署未运行"
    else
        mode_desc="❌Kin模式失败"
    fi

    local desc="$MY_MODEL $mode_desc"
    if [ -f "$MODDIR/module.prop" ]; then
        # 仅在状态变化时改写 module.prop,避免无谓写盘;激活后守护起来会自动刷新为「运行中」
        local cur="$(awk -F= '/^description=/{print substr($0, index($0, "=")+1); exit}' "$MODDIR/module.prop" 2>/dev/null)"
        if [ "$cur" != "$desc" ]; then
            local tmp_prop="$MODDIR/module.prop.tmp"
            if awk -v desc="$desc" 'BEGIN { done=0 } /^description=/ { print "description=" desc; done=1; next } { print } END { if (!done) print "description=" desc }' "$MODDIR/module.prop" > "$tmp_prop" 2>/dev/null && mv -f "$tmp_prop" "$MODDIR/module.prop" 2>/dev/null; then
                log_msg "状态: $desc"
            else
                rm -f "$tmp_prop" 2>/dev/null
                log_msg "状态写入失败: $MODDIR/module.prop"
            fi
        fi
    fi
    # 返回:是否已达「运行中」(供刷新循环判断是否可停止)
    [ "$cts_ok" -eq 1 ] && pidof Kinetic >/dev/null 2>&1
}

wait_until_login

# mode.txt 是 App 与守护进程共用的控制面。安装阶段可能早于 FUSE 用户存储挂载，
# 因此这里再次创建并验证，不能依赖 customize.sh 或后台循环的时序。
ensure_kin_data() {
    local dir="$1"
    local file="$dir/mode.txt"
    mkdir -p "$dir" 2>/dev/null || return 1
    if [ ! -f "$file" ]; then
        printf '%s\n' balance > "$file" 2>/dev/null || return 1
    fi
    [ -r "$file" ] && [ -w "$file" ]
}

KIN_DIR="/sdcard/Android/Kin"
if ! ensure_kin_data "$KIN_DIR"; then
    KIN_DIR="/storage/emulated/0/Android/Kin"
    if ensure_kin_data "$KIN_DIR"; then
        log_msg "使用真实共享存储路径: $KIN_DIR"
    else
        log_msg "mode.txt 创建失败: /sdcard/Android/Kin 和 /storage/emulated/0/Android/Kin 均不可用"
    fi
fi

if [ -f "$MODDIR/.cts_enabled" ] && [ -f "$MODDIR/Kinetic" ]; then
    log_msg "开机自动启动 Kin 调度守护进程(完全独立运行,无需 Scene 接管)"

    if [ -d /data/data/com.omarea.vtools ] || [ -d /data/data/com.omarea.gesture ]; then
        ( sh "$MODDIR/vtools/init_vtools.sh" >/dev/null 2>&1 ) &
    fi

    # 单一常驻守护循环 —— 合并原来的三个 while/sleep 后台子进程(守护拉起 / cpuset 兜底 /
    # mode.txt 合法性)。在 busybox shell 下每个后台循环就是一个常驻 busybox 进程;合并后
    # 常驻 busybox 只剩这一个(守护进程 Kinetic 本身不是 busybox)。
    (
        CTS_DIR="${KIN_DIR:-/sdcard/Android/Kin}"
        CTS_BIN="$MODDIR/Kinetic"
        CTS_LIB="$MODDIR/lib/arm64"
        MODE_FILE="$CTS_DIR/mode.txt"
        # /sdcard 是 FUSE 别名，登录前可能尚未可用；显式创建并验证，失败时切到真实路径。
        i=1
        while [ "$i" -le 60 ]; do
            mkdir -p "$CTS_DIR" 2>/dev/null
            if [ -d "$CTS_DIR" ] && { [ -w "$CTS_DIR" ] || [ "$(id -u)" = "0" ]; }; then
                if [ -f "$MODE_FILE" ] || echo "balance" > "$MODE_FILE" 2>/dev/null; then
                    break
                fi
            fi
            if [ "$i" -eq 60 ]; then
                CTS_DIR="/storage/emulated/0/Android/Kin"
                MODE_FILE="$CTS_DIR/mode.txt"
                mkdir -p "$CTS_DIR" 2>/dev/null
                [ -f "$MODE_FILE" ] || echo "balance" > "$MODE_FILE" 2>/dev/null
                log_msg "/sdcard/Android/Kin 不可写，切换真实路径 $CTS_DIR"
            fi
            i=$((i + 1))
            sleep 1
        done
        if [ ! -r "$MODE_FILE" ]; then
            log_msg "mode.txt 创建/读取失败: $MODE_FILE"
        fi
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

MODE="$(cat "$KIN_DIR/mode.txt" 2>/dev/null)"
if [ -z "$MODE" ]; then
  MODE="$(cat "/sdcard/Android/Kin/mode.txt" 2>/dev/null)"
fi
if [ -z "$MODE" ]; then
  MODE="$(cat "/storage/emulated/0/Android/Kin/mode.txt" 2>/dev/null)"
fi
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
