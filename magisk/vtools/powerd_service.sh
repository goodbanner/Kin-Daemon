#!/system/bin/sh
# powerd 开机自启服务 (Magisk late_start service)
# 等 60 秒让系统稳定,再拉起守护进程
MODDIR=${0%/*}

# 等系统起来
while [ "$(getprop sys.boot_completed)" != 1 ]; do
  sleep 2
done
sleep 60

# 不要重复拉
if [ -f /data/local/tmp/powerd.pid ]; then
  PID=$(cat /data/local/tmp/powerd.pid 2>/dev/null)
  if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
    echo "powerd already running ($PID), skip"
    exit 0
  fi
fi

# 后台常驻
nohup sh "$MODDIR/powerd.sh" start > /dev/null 2>&1 &
