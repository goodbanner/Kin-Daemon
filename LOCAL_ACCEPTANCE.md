# Kin-Daemon 本地验收记录

## 已验证

- 主机 CMake/Ninja 构建：通过。
- 主机 CTest：14/14 通过。
- Android NDK daemon 构建/链接：通过，产物 `build-android-arm64/bin/Kinetic`。
- `git diff --check`：通过。
- PolicyResolver：普通 CPU caps、thermal/battery/power/app/camera/FAS 来源、顺序无关安全限制、非法输入回退均有测试。
- 前台：WindowFocus 多用户解析、Activity cache TTL/失败退避/单 in-flight、presence cache、来源诊断已测试。
- 节点：NodeValue 长度/空白/漂移边界、NodeWriter existing-only/变化/回读、WriteStatus batch、恢复持有语义已测试。
- 配置：基础 AutoMode、功率、FAS、App/FSM policy 条目和 cores 的校验已测试。
- 诊断：policy、FAS、battery、thermal、foreground source、write batch 字段已接入。

## 最新本地验证

- 最后一次主机 CTest：14/14 通过。
- 最后一次 Android NDK 增量构建：成功链接 `bin/Kinetic`。
- 最后一次 `git diff --check`：通过。
- 本地代码阶段可执行验证已完成；不得把下面的设备项目伪造为已验证。

## 明确未验证

- Android 真机行为：尚未 adb 推送/安装/启动，符合用户要求。
- Linux sysfs 专用测试：Windows 主机不运行；需要 Linux 环境或 Android 设备。
- 真机功耗、唤醒次数、温度传感器质量、帧率、ROM 外部覆盖和实际恢复。
- `clang-tidy`：Android build 目录缺少 compile_commands.json，直接运行只产生“找不到 scheduler.hpp”，不能作为源码诊断证据。
- `NodeWriter.hpp` 是已验证原语，未机械替换所有 write-only/触发型生产节点。
- 生产代码仍有低频/兼容性 `popen/dumpsys`；没有实机调用计数证明其功耗。
