#!/system/bin/sh
# powerd - UCLAMP功耗自适应调度 (v2, 对齐Kin体系)
# 改进: 多SoC支持 / 模式联动 / 场景策略 / Kin频率表

MODDIR=${0%/*}
[ "$MODDIR" = "$0" ] && MODDIR=/data/adb/modules/powerd_uclamp
CONF=$MODDIR/powerd.conf
[ -f "$CONF" ] && . "$CONF"

# ── SoC 检测(同 Kin identify_chip) ──
SOC_MODEL=$(getprop ro.soc.model | tr 'A-Z' 'a-z')
SOC_PLAT=$(getprop ro.board.platform | tr 'A-Z' 'a-z')
detect_chip() {
    case "$SOC_MODEL" in
        sm8850|sm8850p) echo "8E5"; return 0 ;;
        sm8750|sm8750p) echo "8E"; return 0 ;;
        sm8650|sm8650p) echo "8G3"; return 0 ;;
        sm8550|sm8550p) echo "8G2"; return 0 ;;
        sm8450|sm8450p) echo "8G1"; return 0 ;;
        sm8845|sm8845p) echo "8G5"; return 0 ;;
        mt6989) echo "D9300"; return 0 ;;
        mt6991|mt6991w) echo "D9400"; return 0 ;;
    esac
    case "$SOC_PLAT" in
        mt6989) echo "D9300"; return 0 ;;
        mt6991) echo "D9400"; return 0 ;;
    esac
    echo "generic"
}
CHIP=$(detect_chip)

# SoC频率表
eval BIG_FREQ=\$FREQ_${CHIP}_BIG
eval LITTLE_FREQ=\$FREQ_${CHIP}_LITTLE
eval GPU_MAX=\$GPU_${CHIP}
eval GPU_H=\$GPU_${CHIP}_HEAT
eval GPU_C=\$GPU_${CHIP}_CRIT
: ${BIG_FREQ:=3000000}
: ${LITTLE_FREQ:=1800000}
: ${GPU_MAX:=900000000}
: ${GPU_H:=600000000}
: ${GPU_C:=400000000}

# ── 模式偏移 ──
MODE_FILE="${MODE_FILE:-/sdcard/Android/Kin/mode.txt}"
read_mode() {
    [ -f "$MODE_FILE" ] && cat "$MODE_FILE" 2>/dev/null || echo "balance"
}

# ── 默认值 ──
INTERVAL="${INTERVAL:-2}"; LOG_FILE="${LOG_FILE:-/data/local/tmp/powerd.log}"
LOG_LEVEL="${LOG_LEVEL:-2}"; PID_FILE="/data/local/tmp/powerd.pid"
STATE_FILE="/data/local/tmp/powerd.state"
P_LOW="${P_LOW:-2.5}"; P_MID="${P_MID:-5.0}"; P_HIGH="${P_HIGH:-7.5}"; P_CRITICAL="${P_CRITICAL:-9.5}"
T_WARN="${T_WARN:-43}"; T_HIGH="${T_HIGH:-46}"; T_CRITICAL="${T_CRITICAL:-49}"
UCLAMP_MIN_BOOST="${UCLAMP_MIN_BOOST:-128}"; UCLAMP_MAX_FULL="${UCLAMP_MAX_FULL:-1024}"
UCLAMP_MAX_L1="${UCLAMP_MAX_L1:-768}"; UCLAMP_MAX_L2="${UCLAMP_MAX_L2:-512}"; UCLAMP_MAX_L3="${UCLAMP_MAX_L3:-256}"
GAME_P_CRITICAL="${GAME_P_CRITICAL:-10.5}"; GAME_T_WARN="${GAME_T_WARN:-46}"

# ── 节点 ──
PWR_CUR="/sys/class/power_supply/battery/current_now"
PWR_VOL="/sys/class/power_supply/battery/voltage_now"
PWR_TEMP="/sys/class/power_supply/battery/temp"
SCHED_BOOST="/proc/sys/kernel/sched_boost"
SYSCTL_UCLAMP_MIN="/proc/sys/kernel/sched_util_clamp_min"
SYSCTL_UCLAMP_MAX="/proc/sys/kernel/sched_util_clamp_max"
GPU_DEV="/sys/class/kgsl/kgsl-3d0/devfreq"
THERMAL="/sys/class/thermal"

log() { [ "$1" -le "$LOG_LEVEL" ] || return; printf '%s [%s][%s][%s] %s\n' "$(date '+%H:%M:%S')" "$2" "$CHIP" "$MODE" "$3" >> "$LOG_FILE"; }
MODE=$(read_mode)

# ── 探测 ──
detect_clusters() {
  BIG_CPUS=""; LITTLE_CPUS=""
  for c in /sys/devices/system/cpu/cpu[0-9]*; do
    n=${c##*cpu}; [ -f "$c/cpu_capacity" ] || continue
    cap=$(cat "$c/cpu_capacity" 2>/dev/null); [ -z "$cap" ] && continue
    if [ "$cap" -ge 1000 ]; then BIG_CPUS="$BIG_CPUS $n"
    else LITTLE_CPUS="$LITTLE_CPUS $n"; fi
  done
  BIG_CPUS=$(echo $BIG_CPUS); LITTLE_CPUS=$(echo $LITTLE_CPUS)
}
detect_uclamp() {
  UCLAMP_MODE=none; HAVE_SCHED_BOOST=0
  [ -f "$SCHED_BOOST" ] && HAVE_SCHED_BOOST=1
  for cg in /sys/fs/cgroup /dev/cpuset/top-app /dev/cgexec/top-app /sys/fs/cgroup/top-app; do
    if [ -f "$cg/cpu.uclamp.min" ] && [ -f "$cg/cpu.uclamp.max" ]; then
      UCLAMP_CG_MIN="$cg/cpu.uclamp.min"; UCLAMP_CG_MAX="$cg/cpu.uclamp.max"
      UCLAMP_MODE=cgroup; return
    fi
  done
  [ -f "$SYSCTL_UCLAMP_MIN" ] && { UCLAMP_MODE=sysctl; return; }
  [ "$HAVE_SCHED_BOOST" = "1" ] && { UCLAMP_MODE=boost; return; }
}

# ── 保存/恢复 ──
saved_state_file="/data/local/tmp/powerd.saved"
save_defaults() {
  : > "$saved_state_file"
  for n in $BIG_CPUS $LITTLE_CPUS; do
    f="/sys/devices/system/cpu/cpu$n/cpufreq/scaling_max_freq"
    o="/sys/devices/system/cpu/cpu$n/online"
    [ -f "$f" ] && echo "maxfreq_$n=$(cat $f 2>/dev/null)" >> "$saved_state_file"
    [ -f "$o" ] && echo "online_$n=$(cat $o 2>/dev/null)" >> "$saved_state_file"
  done
  [ -f "$SCHED_BOOST" ] && echo "sched_boost=$(cat $SCHED_BOOST 2>/dev/null)" >> "$saved_state_file"
  [ -f "$SYSCTL_UCLAMP_MIN" ] && echo "uclamp_min=$(cat $SYSCTL_UCLAMP_MIN 2>/dev/null)" >> "$saved_state_file"
  [ -n "$UCLAMP_CG_MIN" ] && [ -f "$UCLAMP_CG_MIN" ] && echo "cg_umin=$(cat $UCLAMP_CG_MIN 2>/dev/null)" >> "$saved_state_file"
  [ -n "$UCLAMP_CG_MAX" ] && [ -f "$UCLAMP_CG_MAX" ] && echo "cg_umax=$(cat $UCLAMP_CG_MAX 2>/dev/null)" >> "$saved_state_file"
  [ -f "$GPU_DEV/max_freq" ] && echo "gpu_max=$(cat $GPU_DEV/max_freq 2>/dev/null)" >> "$saved_state_file"
}
restore_defaults() {
  [ -f "$saved_state_file" ] || return
  while IFS='=' read -r k v; do
    case "$k" in
      maxfreq_*) n=${k#maxfreq_}; f="/sys/devices/system/cpu/cpu$n/cpufreq/scaling_max_freq"; [ -f "$f" ] && echo "$v" > "$f" 2>/dev/null ;;
      online_*) n=${k#online_}; o="/sys/devices/system/cpu/cpu$n/online"; [ -f "$o" ] && echo "$v" > "$o" 2>/dev/null ;;
      sched_boost) [ -f "$SCHED_BOOST" ] && echo "$v" > "$SCHED_BOOST" 2>/dev/null ;;
      uclamp_min) [ -f "$SYSCTL_UCLAMP_MIN" ] && echo "$v" > "$SYSCTL_UCLAMP_MIN" 2>/dev/null ;;
      cg_umin) [ -n "$UCLAMP_CG_MIN" ] && [ -f "$UCLAMP_CG_MIN" ] && echo "$v" > "$UCLAMP_CG_MIN" 2>/dev/null ;;
      cg_umax) [ -n "$UCLAMP_CG_MAX" ] && [ -f "$UCLAMP_CG_MAX" ] && echo "$v" > "$UCLAMP_CG_MAX" 2>/dev/null ;;
      gpu_max) [ -f "$GPU_DEV/max_freq" ] && echo "$v" > "$GPU_DEV/max_freq" 2>/dev/null ;;
    esac
  done < "$saved_state_file"
  log 2 INFO "defaults restored"
}
cleanup() { log 2 INFO "stopping, restore..."; restore_defaults; rm -f "$PID_FILE" "$STATE_FILE"; }
trap cleanup EXIT INT TERM

# ── 采样 ──
read_power() {
  [ -f "$PWR_CUR" ] && [ -f "$PWR_VOL" ] || { echo "0"; return; }
  cur=$(cat "$PWR_CUR" 2>/dev/null); vol=$(cat "$PWR_VOL" 2>/dev/null)
  [ -z "$cur" ] || [ -z "$vol" ] && { echo "0"; return; }
  echo "$cur $vol" | awk '{ p=($1*$2); if(p<0)p=-p; printf "%.2f", p/1000000000000 }'
}
read_temp() {
  [ -f "$PWR_TEMP" ] || { echo "30"; return; }
  echo "$(cat $PWR_TEMP 2>/dev/null)" | awk '{printf "%.1f", $1/10}'
}
read_max_thermal() {
  max=0
  for tz in $THERMAL/thermal_zone*; do
    [ -f "$tz/temp" ] || continue
    v=$(cat "$tz/temp" 2>/dev/null)
    case "$v" in ''|*[!0-9-]*) continue ;; esac
    [ "$v" -gt "$max" ] && max=$v
  done
  [ "$max" -gt 1000 ] && echo "$max" | awk '{printf "%.1f", $1/1000}' || echo "$max" | awk '{printf "%.1f", $1}'
}
read_scene() {
  wake=$(dumpsys power 2>/dev/null | grep -i 'mWakefulness=' | head -n1 | sed 's/.*=//')
  case "$wake" in *Asleep*) echo "screen_off"; return ;; esac
  pkg=$(dumpsys activity activities 2>/dev/null | grep -i 'mResumedActivity' | head -n1 | sed 's/.*u0 \([^ ]*\).*/\1/' | cut -d/ -f1)
  case " $GAME_PKGS " in *" $pkg "*) echo "game"; return ;; esac
  case " $CAMERA_PKGS " in *" $pkg "*) echo "camera"; return ;; esac
  case " $VIDEO_PKGS " in *" $pkg "*) echo "video"; return ;; esac
  case " $SOCIAL_PKGS " in *" $pkg "*) echo "social"; return ;; esac
  echo "normal"
}

# ── UCLAMP ──
set_uclamp() {
  umin=$1; umax=$2
  case "$UCLAMP_MODE" in
    cgroup)
      [ -n "$UCLAMP_CG_MIN" ] && echo "$umin" > "$UCLAMP_CG_MIN" 2>/dev/null
      [ -n "$UCLAMP_CG_MAX" ] && echo "$umax" > "$UCLAMP_CG_MAX" 2>/dev/null
      [ "$HAVE_SCHED_BOOST" = "1" ] && { [ "$umin" -gt 0 ] && echo 1 > "$SCHED_BOOST" 2>/dev/null || echo 0 > "$SCHED_BOOST" 2>/dev/null; }
      ;;
    sysctl)
      [ -f "$SYSCTL_UCLAMP_MIN" ] && { [ "$umin" -gt 0 ] && echo 1024 > "$SYSCTL_UCLAMP_MIN" 2>/dev/null || echo 0 > "$SYSCTL_UCLAMP_MIN" 2>/dev/null; }
      [ "$HAVE_SCHED_BOOST" = "1" ] && { [ "$umin" -gt 0 ] && echo 1 > "$SCHED_BOOST" 2>/dev/null || echo 0 > "$SCHED_BOOST" 2>/dev/null; }
      ;;
    boost)
      [ "$HAVE_SCHED_BOOST" = "1" ] && { [ "$umin" -gt 0 ] && echo 1 > "$SCHED_BOOST" 2>/dev/null || echo 0 > "$SCHED_BOOST" 2>/dev/null; }
      ;;
  esac
}

# ── cpufreq / gpu ──
set_max_freq() { f="/sys/devices/system/cpu/cpu$1/cpufreq/scaling_max_freq"; [ -f "$f" ] && echo "$2" > "$f" 2>/dev/null; }
set_cpu_online() { o="/sys/devices/system/cpu/cpu$1/online"; [ -f "$o" ] && echo "$2" > "$o" 2>/dev/null; }
set_gpu_max() {
  f="$GPU_DEV/max_freq"; [ -f "$f" ] || return
  if [ "$1" = "0" ]; then
    mx=$(cat "$GPU_DEV/available_frequencies" 2>/dev/null | tr ' ' '\n' | sort -n | tail -n1)
    [ -n "$mx" ] && echo "$mx" > "$f" 2>/dev/null
  else echo "$1" > "$f" 2>/dev/null; fi
}

# 决策: 场景 × 功率 × 温度 × 模式 -> L0~L3
decide_level() {
  scene=$1; p=$2; t=$3; mode=$4
  # 模式偏移: 省电模式阈值降低(更激进降频), 性能模式阈值提高
  case "$mode" in
    powersave) p_crit=$(echo "$P_CRITICAL * 0.8" | awk '{printf "%.1f", $1}'); t_high=$(($T_HIGH - 4)); t_crit=$(($T_CRITICAL - 3)) ;;
    performance|fast) p_crit=$(echo "$P_CRITICAL * 1.2" | awk '{printf "%.1f", $1}'); t_high=$(($T_HIGH + 3)); t_crit=$(($T_CRITICAL + 2)) ;;
    *) p_crit=$P_CRITICAL; t_high=$T_HIGH; t_crit=$T_CRITICAL ;;
  esac

  if awk "BEGIN{exit !($t>=$t_crit)}"; then echo "3"; return; fi
  if awk "BEGIN{exit !($t>=$t_high)}"; then
    if awk "BEGIN{exit !($p>=$P_MID)}"; then echo "3"; else echo "2"; fi; return
  fi
  case "$scene" in
    screen_off) echo "3" ;;
    game)
      if awk "BEGIN{exit !($p>=$GAME_P_CRITICAL)}"; then echo "2"
      elif awk "BEGIN{exit !($t>=$GAME_T_WARN)}"; then echo "1"
      else echo "0"; fi ;;
    camera)
      if awk "BEGIN{exit !($p>=$P_HIGH)}"; then echo "2"; else echo "1"; fi ;;
    video|social)
      if awk "BEGIN{exit !($p>=$p_crit)}"; then echo "2"; else echo "1"; fi ;;
    *)
      if awk "BEGIN{exit !($p>=$p_crit)}"; then echo "3"
      elif awk "BEGIN{exit !($p>=$P_HIGH)}"; then echo "2"
      elif awk "BEGIN{exit !($p>=$P_MID)}"; then echo "1"
      else echo "0"; fi ;;
  esac
}

# 应用档位(用SoC专属频率表)
apply_level() {
  lvl=$1; scene=$2
  case "$lvl" in
    0) set_uclamp "$UCLAMP_MIN_BOOST" "$UCLAMP_MAX_FULL"
       for n in $BIG_CPUS; do set_cpu_online "$n" 1; set_max_freq "$n" "$BIG_FREQ"; done
       for n in $LITTLE_CPUS; do set_max_freq "$n" "$LITTLE_FREQ"; done
       set_gpu_max "$GPU_MAX" ;;
    1) set_uclamp 0 "$UCLAMP_MAX_L1"
       for n in $BIG_CPUS; do set_max_freq "$n" "$(echo $BIG_FREQ | awk '{printf "%d", $1*0.75}')"; done
       set_gpu_max "$GPU_H" ;;
    2) set_uclamp 0 "$UCLAMP_MAX_L2"
       for n in $BIG_CPUS; do set_max_freq "$n" "$(echo $BIG_FREQ | awk '{printf "%d", $1*0.5}')"; done
       set_gpu_max "$GPU_H" ;;
    3) set_uclamp 0 "$UCLAMP_MAX_L3"
       for n in $BIG_CPUS; do set_cpu_online "$n" 0; done
       set_gpu_max "$GPU_C" ;;
  esac
}

last_lvl=-1
run_once() {
  MODE=$(read_mode)  # 每轮重读模式
  scene=$(read_scene); p=$(read_power); t=$(read_temp); th=$(read_max_thermal)
  lvl=$(decide_level "$scene" "$p" "$t" "$MODE")
  if [ "$lvl" != "$last_lvl" ]; then
    log 1 WARN "scene=$scene P=${p}W bat=${t}C th=${th}C mode=$MODE -> L$lvl (was $last_lvl) [chip=$CHIP uclamp=$UCLAMP_MODE]"
    apply_level "$lvl" "$scene"
    last_lvl=$lvl
  else
    log 3 DEBUG "scene=$scene P=${p}W bat=${t}C th=${th}C lvl=$lvl mode=$MODE"
  fi
  printf 'chip=%s scene=%s power=%.2fW bat=%.1fC thermal=%.1fC level=%s mode=%s uclamp=%s\n' \
    "$CHIP" "$scene" "$p" "$t" "$th" "$lvl" "$MODE" "$UCLAMP_MODE" > "$STATE_FILE"
}

main_loop() {
  detect_clusters; detect_uclamp; save_defaults
  echo $$ > "$PID_FILE"
  log 2 INFO "start chip=$CHIP big=[$BIG_CPUS] little=[$LITTLE_CPUS] uclamp=$UCLAMP_MODE freq_big=$BIG_FREQ freq_little=$LITTLE_FREQ"
  while :; do run_once; sleep "$INTERVAL"; done
}

case "${1:-start}" in
  probe)
    detect_clusters; detect_uclamp
    echo "=== powerd v2 (Kin-aligned) ==="
    echo "chip:      $CHIP ($SOC_MODEL / $SOC_PLAT)"
    echo "big:       $BIG_CPUS ($BIG_FREQ Hz)"
    echo "little:    $LITTLE_CPUS ($LITTLE_FREQ Hz)"
    echo "gpu max:   $GPU_MAX Hz"
    echo "uclamp:    $UCLAMP_MODE"
    [ -n "$UCLAMP_CG_MIN" ] && echo "  cgroup:  $UCLAMP_CG_MIN"
    [ -f "$SYSCTL_UCLAMP_MIN" ] && echo "  sysctl:  $SYSCTL_UCLAMP_MIN"
    echo "mode:      $(read_mode)"
    echo "power:     $(read_power)W  bat: $(read_temp)C  thermal: $(read_max_thermal)C"
    ;;
  once)
    detect_clusters; detect_uclamp; run_once; cat "$STATE_FILE"
    ;;
  status) cat "$STATE_FILE" 2>/dev/null || echo "not running" ;;
  stop)
    [ -f "$PID_FILE" ] && kill "$(cat "$PID_FILE")" 2>/dev/null && echo "stopped" || echo "not running"
    ;;
  start|"")
    [ -f "$PID_FILE" ] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null && { echo "already running: $(cat $PID_FILE)"; exit 1; }
    main_loop
    ;;
  *) echo "usage: powerd.sh [start|stop|once|status|probe]"; exit 1 ;;
esac
