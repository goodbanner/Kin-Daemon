#!/system/bin/sh

MODDIR="${0%/*}"
[ "$MODDIR" = "." ] && MODDIR="$(pwd)"

export PATH="${PATH}:/data/adb/magisk:/data/adb/ksu/bin:/data/adb/ap/bin"
export TZ="Asia/Shanghai"

if command -v busybox >/dev/null 2>&1 && [ "$(which -a busybox)" != "" ]; then
    chattr() { busybox chattr "$@"; }
fi

log_msg() {
    if command -v log >/dev/null 2>&1; then
        log -t "OnePlusVPro-uninstall" "$*"
    fi
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
remove_owned_vtools() {
    local target="$1"
    local source="$2"
    if [ -f "$source" ] && [ -f "$target" ] && cmp -s "$source" "$target"; then
        un_chattr_file "$target"
        rm -f "$target" 2>/dev/null
    fi
}
remove_owned_vtools /data/powercfg.sh "$MODDIR/vtools/powercfg.sh"
remove_owned_vtools /data/powercfg.json "$MODDIR/vtools/powercfg.json"
log_msg "仅清理内容仍与 Kin 一致的 /data 调度文件"

# Other modules and their runtime files are not owned by this uninstaller.

if [ -d /storage/emulated/0/Android/Kin ]; then
    if [ -f /storage/emulated/0/Android/Kin/config.json ]; then
        backup=/storage/emulated/0/Android/Kin-config.json.bak
        if cp /storage/emulated/0/Android/Kin/config.json "$backup.tmp" 2>/dev/null \
            && cmp -s /storage/emulated/0/Android/Kin/config.json "$backup.tmp" \
            && mv -f "$backup.tmp" "$backup"; then
            log_msg "已备份用户配置到 $backup"
        else
            rm -f "$backup.tmp" 2>/dev/null
            log_msg "配置备份失败，保留 Kin 数据目录"
        fi
    fi
    log_msg "保留 Kin 用户配置、应用策略和学习数据；需要时请手动删除。"
fi

log_msg "════════════════════════════════════"
log_msg "卸载清理完成"
log_msg "建议重启设备以确保完全清理"
log_msg "════════════════════════════════════"
