#!/system/bin/sh
switch_mode() {
    mkdir -p /sdcard/Android/Kin 2>/dev/null
    echo "$1" > /sdcard/Android/Kin/mode.txt
}

case "$1" in
    powersave|balance|performance|fast|auto)
        switch_mode "$1" ;;
    init)     switch_mode "balance" ;;
    pedestal) switch_mode "performance" ;;
    *) ;;
esac
