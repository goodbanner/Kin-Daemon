#!/system/bin/sh

MODDIR="${0%/*}"
[ "$MODDIR" = "." ] && MODDIR="$(pwd)"

export PATH="${PATH}:/data/adb/magisk:/data/adb/ksu/bin:/data/adb/ap/bin"
export TZ="Asia/Shanghai"

if command -v busybox >/dev/null 2>&1 && [ "$(which -a busybox)" != "" ]; then
    alias chattr="busybox chattr"
fi

log_msg() {
    log -t "OnePlusVPro-uninstall" "$*"
}

un_chattr_file() {
    local target="$1"
    [ -e "$target" ] || return 0
    if [ -d "$target" ]; then
        chattr -R -i -a "$target" 2>/dev/null
    else
        chattr -i -a "$target" 2>/dev/null
    fi
}

log_msg "开始卸载清理..."
un_chattr_file /data/powercfg.sh
un_chattr_file /data/powercfg.json
un_chattr_file /data/coreturbo_powermode
rm -f /data/powercfg.sh /data/powercfg.json /data/coreturbo_powermode 2>/dev/null
log_msg "√ 已清理 /data 调度配置"

for legacy in \
    /data/adb/modules/Scene_Config_replace \
    /data/adb/modules/Thermal_Config_Mount \
    /data/adb/modules/Thread_Config_replace \
    /data/adb/modules/Config_Mount \
    /data/adb/modules/qinghe8e \
    /data/adb/modules/OPGCE \
    /data/adb/modules/BSwitcher \
    /data/adb/modules/cosa_injector \
    /data/adb/modules/Kin_legacy
do
    if [ -d "$legacy" ]; then
        un_chattr_file "$legacy"
        rm -rf "$legacy" 2>/dev/null
    fi
done
log_msg "√ 已清理旧版子模块"

rm -rf /dev/opgce 2>/dev/null
rm -f /dev/.opgv07_mask_* 2>/dev/null

if [ -d /storage/emulated/0/Android/Kin ]; then
    if [ -f /storage/emulated/0/Android/Kin/config.json ]; then
        cp /storage/emulated/0/Android/Kin/config.json \
           /storage/emulated/0/Android/Kin/config.json.bak 2>/dev/null
        log_msg "√ 已备份用户配置到 config.json.bak"
    fi
    rm -rf /storage/emulated/0/Android/Kin 2>/dev/null
    log_msg "√ 已清理 Kin 目录"
fi

[ -f /sdcard/Android/qinghe/logs.txt ] && : > /sdcard/Android/qinghe/logs.txt 2>/dev/null

log_msg "════════════════════════════════════"
log_msg "卸载清理完成"
log_msg "建议重启设备以确保完全清理"
log_msg "════════════════════════════════════"
