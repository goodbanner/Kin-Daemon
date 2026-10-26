#!/system/bin/sh
# 启动互斥。service.sh 的监督循环和动作面板都会拉起 Kinetic,
# 而守护 Init 会 SIGTERM/SIGKILL 其它同名进程。两路同时启动会互相杀掉。
# 用 mkdir 做锁: 不依赖 flock(旧 toybox 上不一定有)。

kinetic_self_pid() {
    # 后台 (...) 里 $$ 可能仍是已经退出的 service.sh。子 shell 的 PPID 才是当前脚本。
    sh -c 'echo "$PPID"'
}

kinetic_lock_write() {
    # 第二列是拿到锁的秒数。重启后旧 pid 可能被别的进程复用, 超过 30 秒一律视为过期。
    printf '%s %s\n' "$1" "$(date +%s 2>/dev/null || echo 0)" > "$2/pid" 2>/dev/null || true
}

kinetic_lock_held() {
    local _pid="$1" _ts="$2" _now
    [ -n "$_pid" ] && kill -0 "$_pid" 2>/dev/null || return 1
    case "$_ts" in
        ''|*[!0-9]*) return 1 ;;
    esac
    _now=$(date +%s 2>/dev/null || echo 0)
    case "$_now" in
        ''|*[!0-9]*) return 1 ;;
    esac
    [ $((_now - _ts)) -lt 30 ]
}

kinetic_lock_acquire() {
    local _dir="$1/.kinetic.start.d"
    local _me _line _hp _ts
    [ -n "$1" ] || return 1
    _me=$(kinetic_self_pid)
    if mkdir "$_dir" 2>/dev/null; then
        kinetic_lock_write "$_me" "$_dir"
        return 0
    fi
    _line=$(cat "$_dir/pid" 2>/dev/null || true)
    _hp=${_line%% *}
    _ts=${_line#* }
    [ "$_ts" = "$_line" ] && _ts=
    if kinetic_lock_held "$_hp" "$_ts"; then
        return 1
    fi
    rm -f "$_dir/pid"
    rmdir "$_dir" 2>/dev/null || return 1
    mkdir "$_dir" 2>/dev/null || return 1
    kinetic_lock_write "$_me" "$_dir"
    return 0
}

kinetic_lock_release() {
    local _dir="$1/.kinetic.start.d"
    rm -f "$_dir/pid"
    rmdir "$_dir" 2>/dev/null || true
}
