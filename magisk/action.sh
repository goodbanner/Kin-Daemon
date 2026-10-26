#!/system/bin/sh
# Kin 模块「动作」面板:实时状态(移植 ASB action 的展示) + 挂载健康检查 + 守护兜底重启。
MODDIR="${0%/*}"
[ "$MODDIR" = "." ] && MODDIR="$(pwd)"
cd "$MODDIR" 2>/dev/null
[ -f "$MODDIR/kinetic_lock.sh" ] && . "$MODDIR/kinetic_lock.sh"

KIN_DIR="/sdcard/Android/Kin"
ST="$KIN_DIR/AI/auto_status.txt"
getv() { grep -m1 "^$1=" "$ST" 2>/dev/null | cut -d= -f2; }

MODE="$(cat "$KIN_DIR/mode.txt" 2>/dev/null | tr -d ' \r\n')"
[ -z "$MODE" ] && MODE="balance"
mode_cn() {
    case "$1" in
        auto)        echo "智能调度" ;;
        powersave)   echo "省电" ;;
        balance)     echo "均衡" ;;
        performance) echo "性能" ;;
        fast)        echo "风驰" ;;
        *)           echo "$1" ;;
    esac
}

# CPU 温度: 优先用守护已过滤的值(auto_status.txt temp, 与监控面板/FSM 一致, 已做尖峰守卫),
# 守护没运行才回退裸扫 thermal_zone(裸扫会抓到性能核瞬时尖峰, 偏高不可靠)。
cputemp=$(grep '^temp=' "$ST" 2>/dev/null | cut -d= -f2 | tr -dc '0-9-')
if [ -z "$cputemp" ] || [ "$cputemp" -le 0 ] 2>/dev/null; then
    cputemp=0
    for z in /sys/class/thermal/thermal_zone*/; do
        tp=$(cat "${z}type" 2>/dev/null)
        case "$tp" in
            *cpu*|*CPU*)
                tv=$(cat "${z}temp" 2>/dev/null)
                if [ -n "$tv" ] && [ "$tv" -gt 1000 ] 2>/dev/null; then
                    dc=$((tv / 1000)); [ "$dc" -gt "$cputemp" ] && cputemp=$dc
                fi ;;
        esac
    done
fi

# 电池电量/温度/容量
lvl=$(dumpsys battery 2>/dev/null | grep -m1 ' level:' | awk '{print $2}')
btemp=$(cat /sys/class/power_supply/battery/temp 2>/dev/null)
capuah=$(cat /sys/class/power_supply/battery/charge_full 2>/dev/null)
if [ -n "$btemp" ] && [ "$btemp" -gt 0 ] 2>/dev/null; then bC=$((btemp / 10)); bF=$((btemp % 10)); else bC=0; bF=0; fi

echo "━━━━━━━━━━━━━━━━━━━━━━━━"
echo "             Kin · 状态面板"
echo "━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# ── 运行状态 + 当前模式 ──
if pidof Kinetic >/dev/null 2>&1; then
    echo "  ✅ Kin 运行中 (PID=$(pidof Kinetic))"
else
    echo "  ❌ Kin 未运行"
fi
if [ "$MODE" = "auto" ]; then
    echo "  🧠 当前模式 : 智能调度"
else
    echo "  ⚙️ 当前模式 : $(mode_cn "$MODE")"
fi
echo ""
echo "  🌡 CPU 温度 : ${cputemp}°C"
if [ "$bC" -gt 0 ]; then
    echo "  🔋 电池     : ${bC}.${bF}°C   ${lvl:-?}%"
else
    echo "  🔋 电池     : ${lvl:-?}%"
fi

# ── 智能调度详情(读 auto_status.txt,仅 auto 时有意义) ──
if [ "$MODE" = "auto" ] && [ -r "$ST" ]; then
    fsm=$(getv fsm); alpha=$(getv alpha); conf=$(getv conf)
    bucket=$(getv bucket); daypart=$(getv daypart)
    gpu=$(getv gpu); load=$(getv load1); lowbat=$(getv lowbat)
    cml=$(getv cpu_max_little); cmb=$(getv cpu_max_big)
    case "$fsm" in
        DEEP_IDLE)  fsmcn="空闲" ;;
        LIGHT_IDLE) fsmcn="低负载" ;;
        MODERATE)   fsmcn="中负载" ;;
        HEAVY)      fsmcn="重负载" ;;
        SUSTAINED)  fsmcn="持续高负载" ;;
        GAMING)     fsmcn="游戏" ;;
        *)          fsmcn="$fsm" ;;
    esac
    case "$daypart" in
        0) dpcn="凌晨" ;; 1) dpcn="清晨" ;; 2) dpcn="上午" ;;
        3) dpcn="下午" ;; 4) dpcn="傍晚" ;; 5) dpcn="夜间" ;; *) dpcn="—" ;;
    esac
    we="工作日"; [ -n "$bucket" ] && [ $((bucket % 2)) -eq 1 ] 2>/dev/null && we="周末"
    echo ""
    [ -n "$fsmcn" ] && echo "  🚦 状态     : $fsmcn"
    if [ -n "$cml" ] && [ -n "$cmb" ] && [ "$cml" -gt 0 ] 2>/dev/null; then
        echo "  ⛰ 频率上限 : 小核 ${cml}MHz · 大核 ${cmb}MHz"
    fi
    echo "  📈 GPU/负载 : ${gpu:-?}%  load ${load:-?}"
    ap=0; cp=0
    [ -n "$alpha" ] && [ "$alpha" -ge 0 ] 2>/dev/null && ap=$((alpha / 10))
    [ -n "$conf" ]  && [ "$conf"  -ge 0 ] 2>/dev/null && cp=$((conf / 10))
    echo "  🤖 AI 学习  : ${dpcn} · ${we}"
    echo "                省电倾向 ${ap}%   学习程度 ${cp}%"
    [ "$lowbat" = "1" ] && echo "  🔻 低电省电中"
fi

# ── 续航估算(启发式,同 ASB 回退算法:按模式给经验电流) ──
on_ma=500
case "$MODE" in
    performance|fast) on_ma=650 ;;
    powersave)        on_ma=380 ;;
    auto)             on_ma=460 ;;
esac
off_ma=$((on_ma / 10)); [ "$off_ma" -lt 40 ] && off_ma=40; [ "$off_ma" -gt 90 ] && off_ma=90
remain=0
if [ -n "$capuah" ] && [ "$capuah" -gt 0 ] 2>/dev/null && [ -n "$lvl" ]; then
    remain=$(( (capuah / 1000) * lvl / 100 ))
fi
if [ "$remain" -gt 0 ] 2>/dev/null; then
    th=$(( remain * 60 / on_ma / 60 ));  tm=$(( remain * 60 / on_ma % 60 ))
    oh=$(( remain * 60 / off_ma / 60 )); om=$(( remain * 60 / off_ma % 60 ))
    echo ""
    echo "  ⏱ 续航估算 (估值):"
    echo "     📱 亮屏 ~${th}h ${tm}m    💤 息屏 ~${oh}h ${om}m"
fi

echo ""
echo "  ────────────────────────────────────"

# ── 挂载健康检查(vendor 覆盖等) ──
BB=""; [ -x "$MODDIR/bin/busybox" ] && BB="$MODDIR/bin/busybox"
md5_of() { if [ -n "$BB" ]; then "$BB" md5sum "$1" 2>/dev/null | awk '{print $1}'; else md5sum "$1" 2>/dev/null | awk '{print $1}'; fi; }
mount_ok=1
check_pair() {
    local src_dir="$MODDIR/$1"; local dst_dir="$2"
    [ ! -d "$src_dir" ] && return 0
    local f bn dst m1 m2
    for f in "$src_dir"/*; do
        [ -f "$f" ] || continue
        bn="${f##*/}"; dst="$dst_dir/$bn"
        [ ! -f "$dst" ] && { mount_ok=0; return 1; }
        m1=$(md5_of "$f"); m2=$(md5_of "$dst")
        [ -z "$m1" ] || [ "$m1" != "$m2" ] && { mount_ok=0; return 1; }
    done
    return 0
}
check_pair "system/vendor/etc/display"         "/vendor/etc/display"
check_pair "my_product/etc"                    "/my_product/etc"
check_pair "my_stock/etc/ThermalServiceConfig" "/my_stock/etc/ThermalServiceConfig"
check_pair "data/oplus/os/startup"             "/data/oplus/os/startup"
check_pair "data/oplus/os/config"              "/data/oplus/os/config"
check_pair "data/oplus/os/bpm"                 "/data/oplus/os/bpm"
[ "$mount_ok" -eq 1 ] && echo "  ✅ 系统覆盖挂载正常" || echo "  ❌ 系统覆盖挂载异常"

# ── 守护兜底:未运行则尝试重启 + 重新部署配置 ──
redeploy_cts() {
    [ -f "$MODDIR/Kinetic" ] || { echo "  ⚠️ Kin 缺失,无法部署"; return 1; }
    mkdir -p "$KIN_DIR" 2>/dev/null
    local AUTH_CFG="$MODDIR/config/.config_backup"
    [ -f "$AUTH_CFG" ] || AUTH_CFG="$MODDIR/config/config.json"
    if [ ! -s "$KIN_DIR/config.json" ] && [ -f "$AUTH_CFG" ]; then
        command -v chattr >/dev/null 2>&1 && chattr -i "$KIN_DIR/config.json" 2>/dev/null
        cp -af "$AUTH_CFG" "$KIN_DIR/config.json" 2>/dev/null
    fi
    [ -f "$KIN_DIR/mode.txt" ] || echo "balance" > "$KIN_DIR/mode.txt"
    chmod 0755 "$MODDIR/Kinetic" 2>/dev/null
    sh "$MODDIR/vtools/init_vtools.sh" 2>/dev/null
    _locked=0
    _try=0
    while [ "$_try" -lt 5 ]; do
        if ! command -v kinetic_lock_acquire >/dev/null 2>&1; then
            break
        fi
        if kinetic_lock_acquire "$KIN_DIR"; then
            _locked=1
            break
        fi
        _try=$((_try + 1))
        sleep 1
    done
    if command -v kinetic_lock_acquire >/dev/null 2>&1 && [ "$_locked" -ne 1 ]; then
        echo "  ⚠️ 另一路正在启动守护, 请稍后重新打开面板"
        return 1
    fi
    _kp=$(pidof Kinetic); [ -n "$_kp" ] && kill $_kp 2>/dev/null; sleep 1
    if ! pidof Kinetic >/dev/null 2>&1; then
        LD_LIBRARY_PATH="$MODDIR/lib/arm64:$LD_LIBRARY_PATH" nohup "$MODDIR/Kinetic" >> "$KIN_DIR/log.txt" 2>&1 &
        # 持锁直到新进程能被 pidof 看到, 避免和 service.sh 的监督循环各拉起一份。
        sleep 1
    fi
    [ "$_locked" -eq 1 ] && kinetic_lock_release "$KIN_DIR"
    if pidof Kinetic >/dev/null 2>&1; then
        echo "  ✓ 守护已重启 (PID=$(pidof Kinetic))"
    else
        echo "  ✗ 守护启动失败,查看 $KIN_DIR/log.txt"
    fi
}
if ! pidof Kinetic >/dev/null 2>&1; then
    echo "  ⚠️ 守护未运行,尝试重启…"
    redeploy_cts
fi

echo ""
echo "✅ 完成"
exit 0
