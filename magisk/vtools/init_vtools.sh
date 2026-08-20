#!/system/bin/sh
BASEDIR="$(dirname $(readlink -f "$0"))"

command -v chattr >/dev/null 2>&1 && chattr -i /data/powercfg.sh /data/powercfg.json 2>/dev/null
cp -af "$BASEDIR/powercfg.sh" /data/powercfg.sh
cp -af "$BASEDIR/powercfg.json" /data/powercfg.json
chmod 755 /data/powercfg.sh
chmod 644 /data/powercfg.json

mkdir -p /sdcard/Android/Kin 2>/dev/null
cur="/sdcard/Android/Kin/mode.txt"
[ -f "$cur" ] || echo "balance" > "$cur"
