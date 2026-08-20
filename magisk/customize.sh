#!/system/bin/sh
SKIPMOUNT=false
PROPFILE=true
POSTFSDATA=true
LATESTARTSERVICE=true

log_info()  { ui_print "[INFO] $*"; }
log_warn()  { ui_print "[WARN] $*"; }
log_error() { ui_print "[ERROR] $*"; }

if command -v busybox >/dev/null 2>&1; then
    chattr() { busybox chattr "$@"; }
fi

# 旧 WIG 网络验证已弃用(改为 App 卡密激活 + license.json 离线校验),相关变量/脚本已移除。

identify_chip() {
    SOC_MODEL=$(getprop ro.soc.model | tr 'A-Z' 'a-z')
    SOC_PLAT=$(getprop ro.board.platform | tr 'A-Z' 'a-z')
    case "$SOC_MODEL" in
        sm8750|sm8750p)
            [ "$SOC_PLAT" = "sun" ] && { echo "8E"; return 0; }
            echo "8E"; return 0
            ;;
        sm8850|sm8850p)
            [ "$SOC_PLAT" = "canoe" ] && { echo "8E5"; return 0; }
            echo "8E5"; return 0
            ;;
        sm8650|sm8650p)
            echo "8G3"; return 0
            ;;
        sm8550|sm8550p)
            echo "8G2"; return 0
            ;;
        sm8450|sm8450p)
            echo "8G1"; return 0
            ;;
        sm8845|sm8845p)
            echo "8G5"; return 0
            ;;
        sm7675|sm7675p)
            echo "7P3"; return 0
            ;;
        mt6989)
            echo "D9300"; return 0
            ;;
        mt6991|mt6991w)
            echo "D9400"; return 0
            ;;
    esac
    case "$SOC_PLAT" in
        mt6989) echo "D9300"; return 0 ;;
        mt6991) echo "D9400"; return 0 ;;
    esac
    return 1
}

unlock_file() {
    [ -e "$1" ] && chattr -R -i -a "$1" 2>/dev/null
}
lock_file() {
    [ -e "$1" ] && chattr +i "$1" 2>/dev/null
}

# 安装时清理本项目的旧模块(改名前的代号), 免得新旧模块同时挂载互相打架。
# CoreTurboScheduler / Oxygen / Kinetic / CTS 都是本项目历史 id; 当前 id 为 Kin。
remove_legacy_modules() {
    for base in /data/adb/modules /data/adb/modules_update /data/adb/ksu/modules; do
        for id in CoreTurboScheduler Oxygen Kinetic CTS Kin_legacy; do
            d="$base/$id"
            [ "$d" = "$MODPATH" ] && continue
            [ -d "$d" ] || continue
            command -v chattr >/dev/null 2>&1 && chattr -R -i "$d" 2>/dev/null
            touch "$d/remove" 2>/dev/null     # 优雅卸载标记(重启移除)
            rm -rf "$d" 2>/dev/null           # 直接删, KernelSU/部分 Magisk 立即生效
            ui_print "  ↪ 已清理旧模块: $id"
        done
    done
}

deploy_cts_daemon() {
    ui_print "[INFO] 部署 Kin 调度调度进程 + 配置"

    if [ ! -f "$MODPATH/Kinetic" ]; then
        ui_print "⚠️ Kin 调度进程缺失,跳过"
        return 1
    fi

    local CHIP DEV_MODEL SRC_CFG
    DEV_MODEL=$(getprop ro.product.model 2>/dev/null)
    CHIP=$(identify_chip 2>/dev/null)
    ui_print "  机型: $DEV_MODEL  SoC 探测: $CHIP"

    case "$CHIP" in
        8E)  SRC_CFG="$MODPATH/config/config_8E.json" ;;
        8E5) SRC_CFG="$MODPATH/config/config_8E5.json" ;;
        8G3) SRC_CFG="$MODPATH/config/config_8G3.json" ;;
        8G2) SRC_CFG="$MODPATH/config/config_8G2.json" ;;
        8G1) SRC_CFG="$MODPATH/config/config_8G1.json" ;;
        8G5) SRC_CFG="$MODPATH/config/config_8G5.json" ;;
        7P3) SRC_CFG="$MODPATH/config/config_7P3.json" ;;
        D9300) SRC_CFG="$MODPATH/config/config_D9300.json" ;;
        D9400) SRC_CFG="$MODPATH/config/config_D9400.json" ;;
        *)   SRC_CFG="$MODPATH/config/config_8E5.json"
             # identify_chip 在 $(...) 子 shell 运行, 其内设置的 SOC_MODEL/SOC_PLAT 不会传出,
             # 故此处重新读取属性以输出可用的诊断值(供用户反馈/开发者增补 identify_chip)。
             _sm=$(getprop ro.soc.model 2>/dev/null)
             _sp=$(getprop ro.board.platform 2>/dev/null)
             ui_print "  ⚠️ 未识别 SoC, 频率表可能不匹配, 请手动选型"
             ui_print "     原始值: ro.soc.model=${_sm:-<空>} ro.board.platform=${_sp:-<空>}"
             ui_print "     (供开发者增补 identify_chip; 反馈时附上这两项)" ;;
    esac
    [ -f "$SRC_CFG" ] || SRC_CFG="$MODPATH/config/config.json"
    if [ ! -f "$SRC_CFG" ]; then
        ui_print "⚠️ config 缺失,跳过"
        return 1
    fi
    ui_print "  选用配置: ${SRC_CFG##*/}"

    local CTS_DIR="/sdcard/Android/Kin"
    mkdir -p "$CTS_DIR" 2>/dev/null
    local AI_DIR="$CTS_DIR/AI"
    mkdir -p "$AI_DIR" 2>/dev/null

    # ── 改名迁移(旧代号 → Kin): 把旧目录的学习记录/卡密/画像搬到新路径, 尽量不丢学习。 ──
    # 仅迁移一次: 新目录还没学习状态时, 从最近用过的旧目录(按新→旧顺序)搬过来。
    if [ ! -f "$CTS_DIR/auto_state.txt" ]; then
        for OLD_DIR in /sdcard/Android/Oxygen /sdcard/Android/Kinetic /sdcard/Android/CTS; do
            [ -d "$OLD_DIR" ] || continue
            ui_print "  ↪ 检测到旧数据 $OLD_DIR, 迁移到 Kin ..."
            for ff in auto_state.txt auto_status.txt mode.txt perapp_powermode.txt license.json night_state.txt; do
                [ -f "$OLD_DIR/$ff" ] && [ ! -f "$CTS_DIR/$ff" ] && cp -af "$OLD_DIR/$ff" "$CTS_DIR/$ff" 2>/dev/null
            done
            ui_print "  ✓ 旧学习记录/卡密已迁移(旧目录保留备份)"
            break
        done
    fi

    # ── 运行数据文件收进 AI/ 子目录(把旧版留在根目录的搬进来, 保持根目录整洁) ──
    for ff in perapp_powermode.txt auto_status.txt power_log.txt power_sessions.txt; do
        [ -f "$CTS_DIR/$ff" ] && [ ! -f "$AI_DIR/$ff" ] && mv -f "$CTS_DIR/$ff" "$AI_DIR/$ff" 2>/dev/null
    done

    command -v chattr >/dev/null 2>&1 && chattr -i "$CTS_DIR/config.json" 2>/dev/null
    # config.json 每次安装/更新都覆盖为模块最新默认(确保拿到新频率表/新字段)。
    # 注:学习状态 auto_state.txt 不在此处,更新不会被清除。
    rm -f "$CTS_DIR/config.json" 2>/dev/null
    cp -af "$SRC_CFG" "$CTS_DIR/config.json" 2>/dev/null
    cp -af "$SRC_CFG" "$MODPATH/config/.config_backup" 2>/dev/null
    if [ -s "$CTS_DIR/config.json" ]; then
        ui_print "  ✓ config.json 已部署到 $CTS_DIR"
    else
        ui_print "  ⚠️ config.json 部署失败"
    fi

    [ -f "$CTS_DIR/mode.txt" ] || echo "balance" > "$CTS_DIR/mode.txt"

    if [ ! -f "$AI_DIR/perapp_powermode.txt" ]; then
        if [ -f "$MODPATH/config/perapp_powermode.txt" ]; then
            cp -af "$MODPATH/config/perapp_powermode.txt" "$AI_DIR/perapp_powermode.txt" 2>/dev/null
            ui_print "  ✓ 已部署预填应用画像表(含游戏)"
        else
            {
                echo "# 应用画像: 包名 模式(powersave/balance/performance/fast/game_heavy/game_lite/video/social/reading/music)"
                echo "# 第一行 * 开头为全局默认"
                echo "* balance"
            } > "$AI_DIR/perapp_powermode.txt"
            ui_print "  ✓ 已生成默认 perapp_powermode.txt"
        fi
    elif [ -f "$MODPATH/config/perapp_powermode.txt" ]; then
        OLD="$AI_DIR/perapp_powermode.txt"
        NEW="$MODPATH/config/perapp_powermode.txt"
        MERGED="$AI_DIR/.perapp_merged.tmp"
        cp -af "$OLD" "$MERGED" 2>/dev/null
        added=0
        while IFS= read -r line; do
            case "$line" in
                ""|"#"*|"*"*) continue ;;
            esac
            pkg=$(echo "$line" | awk '{print $1}')
            [ -z "$pkg" ] && continue
            if awk -v p="$pkg" '$1==p{f=1} END{exit !f}' "$OLD" 2>/dev/null; then
                continue
            fi
            echo "$line" >> "$MERGED"
            added=$((added + 1))
        done < "$NEW"
        mv -f "$MERGED" "$OLD" 2>/dev/null
        if [ "$added" -gt 0 ]; then
            ui_print "  ✓ 应用画像表已智能合并(保留自定义,新增 $added 条预置)"
        else
            ui_print "  ✓ 应用画像表已是最新(保留用户自定义)"
        fi
    fi

    chmod 0755 "$MODPATH/Kinetic" 2>/dev/null

    # binary.version: 单独记录 Kinetic 二进制版本号(versionCode),供 App 检测二进制更新通道用。
    # 注意: 它与 module.prop 同步生成,但与模块版本解耦 —— App 通过它判断是否需要推送二进制热更新。
    local BIN_VER
    BIN_VER=$(grep -m1 '^versionCode=' "$MODPATH/module.prop" 2>/dev/null | cut -d= -f2 | tr -dc '0-9')
    [ -z "$BIN_VER" ] && BIN_VER=0
    echo "$BIN_VER" > "$MODPATH/binary.version" 2>/dev/null
    chmod 0644 "$MODPATH/binary.version" 2>/dev/null

    sh "$MODPATH/vtools/init_vtools.sh" 2>/dev/null
    [ -s /data/powercfg.sh ] && ui_print "  ✓ Scene 接管插件已生成(可选,不影响 Kin 自启)"

    echo "$CHIP" > "$MODPATH/.cts_enabled"
    ui_print "✅ Kin 调度调度已就绪(开机自启+自带场景识别; 装了 Scene 也可接管)"
}

install_cts_apk() {
    local APK="$MODPATH/Kin.apk"
    [ -f "$APK" ] || APK="$MODPATH/bin/Kin.apk"
    [ -f "$APK" ] || APK="$MODPATH/app/Kin.apk"

    if [ ! -f "$APK" ]; then
        ui_print "  ⚠️ 找不到内置 Kin.apk,跳过 APP 安装"
        return 1
    fi

    ui_print "[INFO] 安装内置 Kin 控制 APP..."

    # 改名清理: 旧版包名(com.ktwo.cts / com.ktwo.kinetic / com.ktwo.oxygen)与新包 com.ktwo.kin 不同,
    # 不会被覆盖安装替换 → 主动卸载旧 App, 免得桌面残留多个图标。学习/卡密已迁移到 /sdcard, 卸载不丢。
    if command -v pm >/dev/null 2>&1; then
        for oldpkg in com.ktwo.cts com.ktwo.kinetic com.ktwo.oxygen; do
            if pm list packages 2>/dev/null | grep -q "$oldpkg\$"; then
                pm uninstall "$oldpkg" >/dev/null 2>&1 && ui_print "  ↪ 已自动卸载旧版 App($oldpkg)"
            fi
        done
    fi

    if command -v pm >/dev/null 2>&1; then
        local size result
        size=$(wc -c < "$APK")
        result=$(pm install -r -d -S "$size" < "$APK" 2>&1) \
            || result=$(pm install -r -d "$APK" 2>&1)
        if echo "$result" | grep -qi "Success"; then
            ui_print "  ✓ Kin.apk 安装成功"
            rm -f "$MODPATH/.kin_apk_pending" "$MODPATH/Kin_pending.apk" 2>/dev/null
            return 0
        fi
        # 签名不一致(旧版与本包 debug 签名不同)→ 覆盖装会被拒。
        # 卸载旧包后全新安装;配置在 /sdcard/Android/Kin,卸载不受影响。
        if echo "$result" | grep -qiE "SIGNATURE|INSTALL_FAILED_UPDATE_INCOMPATIBLE|INCONSISTENT_CERTIFICATES|signatures do not match"; then
            ui_print "  ℹ 签名不一致,卸载旧版后重装(配置保留在 /sdcard)..."
            pm uninstall com.ktwo.cts >/dev/null 2>&1
            result=$(pm install -d -S "$size" < "$APK" 2>&1) \
                || result=$(pm install -d "$APK" 2>&1)
            if echo "$result" | grep -qi "Success"; then
                ui_print "  ✓ 卸载旧版后安装成功"
                rm -f "$MODPATH/.kin_apk_pending" "$MODPATH/Kin_pending.apk" 2>/dev/null
                return 0
            fi
        fi
        ui_print "  ⚠️ 当前环境安装失败,改为开机后重试"
    else
        ui_print "  ℹ pm 不可用(recovery 环境),改为开机后安装"
    fi

    cp -af "$APK" "$MODPATH/Kin_pending.apk" 2>/dev/null
    touch "$MODPATH/.kin_apk_pending"
    ui_print "  ✓ 已排队,重启后自动安装"
    return 0
}

ui_print "━━━━━━━━━━━━━━━━━━━━━━━━"
ui_print "             Kin · By Ktwo"
ui_print "━━━━━━━━━━━━━━━━━━━━━━━━"

ui_print " "
ui_print "[INFO] 运行环境..."
[ -n "$KSU" ]    && ui_print "  Root: KernelSU $KSU_VER"
[ -n "$APATCH" ] && ui_print "  Root: APatch"
[ -z "$KSU" ] && [ -z "$APATCH" ] && ui_print "  Root: Magisk"

ui_print " "
ui_print "━━━━━━━━━━━━━━━━━━━━━━━━"
ui_print "  授权方式: App 激活(自建系统)"
ui_print "━━━━━━━━━━━━━━━━━━━━━━━━"
ui_print "  安装后请打开 Kin App,输入卡密激活。"
ui_print "  激活会写入 /sdcard/Android/Kin/license.json,"
ui_print "  守护进程开机离线校验,未激活将不会运行。"
ui_print "  (不再需要在存储根目录放 km 文件)"
ui_print " "

ui_print " "
ui_print "━━━━━━━━━━━━━━━━━━━━━━━━"
ui_print "  清理旧版子模块残留"
ui_print "━━━━━━━━━━━━━━━━━━━━━━━━"
LEGACY_MODS="Thread_Config_replace Config_Mount Scene_Config_replace Kin_legacy OPGCE BSwitcher qinghe8e"
CLEANED=0
for mid in $LEGACY_MODS; do
    [ "$mid" = "Kin" ] && continue
    for base in /data/adb/modules /data/adb/modules_update; do
        target="$base/$mid"
        if [ -d "$target" ]; then
            command -v chattr >/dev/null 2>&1 && chattr -R -i "$target" 2>/dev/null
            if rm -rf "$target" 2>/dev/null; then
                ui_print "  ✓ 已清理: $base/$mid"
                CLEANED=$((CLEANED + 1))
            else
                ui_print "  ✗ 清理失败: $base/$mid"
            fi
        fi
    done
done
[ "$CLEANED" -eq 0 ] && ui_print "  ℹ 未发现旧版子模块残留" || ui_print "✅ 已清理 $CLEANED 个旧子模块"

command -v chattr >/dev/null 2>&1 && chattr -i /data/coreturbo_powermode 2>/dev/null
rm -f /data/coreturbo_powermode 2>/dev/null

ui_print " "
ui_print "[INFO] 已移除系统挂载/覆盖，模块仅运行 Kin 调度 daemon"

ui_print " "
ui_print "━━━━━━━━━━━━━━━━━━━━━━━━"
ui_print "  Kin 调度调度部署"
ui_print "━━━━━━━━━━━━━━━━━━━━━━━━"
ui_print " "

remove_legacy_modules

deploy_cts_daemon

ui_print " "
ui_print "━━━━━━━━━━━━━━━━━━━━━━━━"
ui_print "  安装 Kin 控制 APP"
ui_print "━━━━━━━━━━━━━━━━━━━━━━━━"
install_cts_apk

chmod 0755 "$MODPATH/post-fs-data.sh" 2>/dev/null
chmod 0755 "$MODPATH/service.sh"      2>/dev/null
chmod 0755 "$MODPATH/action.sh"       2>/dev/null

echo "A" > "$MODPATH/.install_mode" 2>/dev/null

# ── 更新热生效 ──
# 关键: Magisk 更新时新文件此刻在 modules_update/Kin, 而【正在运行的 keeper】仍指向 live 的
# modules/Kin/Kinetic(旧二进制)。只杀旧守护, keeper 会把【旧】二进制又拉起来 → 用户装了新模块却
# 一直跑旧守护(旧 bug 反复出现)。故: 先杀旧守护(避免覆盖运行中文件 ETXTBSY), 再把新二进制直接覆盖
# 到 live 模块目录, keeper 下一拍(~5s)即拉起【新】版, 真正做到更新免重启。全部非致命, 失败退回重启生效。
OLD_PID=$(pidof Kinetic 2>/dev/null)
[ -n "$OLD_PID" ] && kill $OLD_PID 2>/dev/null

HOT_OK=0
if [ -f "$MODPATH/Kinetic" ]; then
    for LIVE in /data/adb/modules/Kin /data/adb/ksu/modules/Kin; do
        [ -d "$LIVE" ] && [ "$LIVE" != "$MODPATH" ] || continue
        command -v chattr >/dev/null 2>&1 && chattr -i "$LIVE/Kinetic" 2>/dev/null
        if cp -f "$MODPATH/Kinetic" "$LIVE/Kinetic" 2>/dev/null; then
            chmod 0755 "$LIVE/Kinetic" 2>/dev/null
            HOT_OK=1
        fi
        # binary.version 一并同步到 live 目录, 让 App 立即识别新二进制版本号
        if [ -f "$MODPATH/binary.version" ]; then
            cp -f "$MODPATH/binary.version" "$LIVE/binary.version" 2>/dev/null
            chmod 0644 "$LIVE/binary.version" 2>/dev/null
        fi
    done
fi
if [ "$HOT_OK" = 1 ]; then
    ui_print "  ↪ 已结束旧守护并热更新二进制到运行目录, keeper 将在数秒内拉起新版"
elif [ -n "$OLD_PID" ]; then
    ui_print "  ↪ 已结束旧守护; 新守护将在【重启手机后】生效"
fi

# ── 安装期自检: 当场确认设备上跑的是不是这次装的新守护(免去手动开 log.txt 核对) ──
# 取本模块二进制内嵌的构建日期(启动横幅 __DATE__), 等 keeper 拉起守护后, 核对 log.txt 首个"构建"行是否含同一日期。
STR="strings"; command -v strings >/dev/null 2>&1 || STR="busybox strings"
NEW_BUILD=$($STR "$MODPATH/Kinetic" 2>/dev/null | grep -oE '(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ 0-9][0-9] 20[0-9][0-9]' | head -1)
i=0; while [ "$i" -lt 10 ]; do pidof Kinetic >/dev/null 2>&1 && break; sleep 1; i=$((i + 1)); done
sleep 2
RUN_BANNER=$(grep -m1 "构建" /sdcard/Android/Kin/log.txt 2>/dev/null)
ui_print " "
if [ -n "$RUN_BANNER" ]; then
    ui_print "  运行中守护: $RUN_BANNER"
    if [ -n "$NEW_BUILD" ] && echo "$RUN_BANNER" | grep -q "$NEW_BUILD"; then
        ui_print "  ✅ 自检通过: 已在跑本次新守护 ($NEW_BUILD)"
        ui_print "     基础模式应用画像与 info 前台切换日志随新守护生效"
    else
        ui_print "  ⚠️ 自检: 运行中守护构建日期与新模块不一致, 请【重启手机】后再看 log.txt 首行"
    fi
elif pidof Kinetic >/dev/null 2>&1; then
    ui_print "  ℹ 守护在运行但未打印启动横幅(可能 loglevel=OFF); 可到 log.txt 首行核对构建日期"
else
    ui_print "  ⚠️ 自检: 守护暂未拉起, 请【重启手机】使新守护生效"
fi

if [ -f "$MODPATH/updatelog.txt" ]; then
    ui_print " "
    cat "$MODPATH/updatelog.txt" 2>/dev/null
fi

ui_print " "
ui_print "************************************************"
ui_print "  ✅ 安装完成"
ui_print "  Kin daemon + config.json 已部署"
ui_print "  ⚠️ 若功能仍是旧版(如画像不生效/日志无前台切换),"
ui_print "     请【重启一次手机】确保新守护彻底生效"
ui_print "************************************************"

sleep 0.1
