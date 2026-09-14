#!/system/bin/sh

MODDIR="${0%/*}"

rm -f /dev/.opgv07_mask_* 2>/dev/null

lock_val() {
    local value="$1"
    local target="$2"

    [ -e "$target" ] || return 0

    umount "$target" 2>/dev/null
    chmod +w "$target" 2>/dev/null

    # 优先直接写入，失败后回退到 mount bind
    if echo "$value" > "$target" 2>/dev/null; then
        # 使用 bind mount 只读文件锁定值
        local mask_file="/dev/.opgv07_mask_$(basename "$target")"
        echo "$value" > "$mask_file" 2>/dev/null || touch "$mask_file" 2>/dev/null
        if [ -f "$mask_file" ]; then
            mount -o bind,ro "$mask_file" "$target" 2>/dev/null
        fi
    fi
}

lock_targets_empty() {
    for target in \
        /system_ext/bin/horae \
        /proc/game_opt/cpu_max_freq \
        /proc/game_opt/fake_cpu7_cpuinfo_max_freq \
        /proc/game_opt/disable_cpufreq_limit \
        /proc/game_opt/early_detect/fst_cpu_max_freq \
        /proc/game_opt/early_detect/flt_cpu_max_freq \
        /proc/game_opt/early_detect/edb_cpu_max_freq
    do
        lock_val "" "$target"
    done
}

lock_targets_zero() {
    for target in \
        /sys/module/oplus_gpu_boost/parameters/gpu_boost_enable \
        /sys/devices/virtual/gpu_boost/gpu_boost/enable \
        /proc/gpu_boost/enable
    do
        lock_val "0" "$target"
    done
}

# CPU 输入提频(高通 cpu_boost / cpu_input_boost)现由守护按「触控加速」开关动态管理:
# 关 → 守护写 enabled=0(同旧行为, 交互也省电); 开 → 守护按档配 input_boost_freq/ms 并打开。
# 因此这里【不再 bind-mount 锁死】这些节点(锁死会让守护写不进), 仅保留 OPlus 帧提频的禁用。
disable_cpu_input_boost() {
    # OPlus 帧提频(cfbt/oplus_frame_boost): 滑动/渲染时把 CPU 频率顶高, 覆盖 Kin 的设置 →
    # 关掉总开关 + 挂起, 让配置稳定生效。bind-mount 锁住防 HAL 改回。
    # (这块与触控加速无关, 仍保留禁用。)
    lock_val "0" /proc/oplus_frame_boost/cfbt_enable
    lock_val "1" /proc/oplus_frame_boost/cfbt_suspend
}

set_shell_temp() {
    [ -e /proc/shell-temp ] || return 0

    for zone in 0 1 2 3 4 5 6 7; do
        echo "$zone 36000" > /proc/shell-temp 2>/dev/null
    done
}
set_shell_temp
lock_targets_empty
lock_targets_zero
disable_cpu_input_boost
stop horae 2>/dev/null

ANYFS_UPPER="/dev/anyfs/upper"
SLC_XML="sys_slc_dispatch_config_list.xml"
SLC_TARGET="/my_product/etc/extension/$SLC_XML"

if [ -f "$MODDIR/$SLC_XML" ] && [ -f "$SLC_TARGET" ]; then
    anyfs_dir="$ANYFS_UPPER/my_product/etc/extension"
    if [ -d "$ANYFS_UPPER/my_product/etc" ]; then
        mkdir -p "$anyfs_dir"
        cp -f "$MODDIR/$SLC_XML" "$anyfs_dir/$SLC_XML"
        chcon --reference "$SLC_TARGET" "$anyfs_dir/$SLC_XML" 2>/dev/null
    fi
fi