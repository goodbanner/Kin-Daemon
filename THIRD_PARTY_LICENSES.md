# 第三方组件与数据来源

本项目包含或借鉴了以下第三方成果，版权归原作者所有。

## 运行期依赖

| 组件 | 用途 | 许可 |
|---|---|---|
| [spdlog](https://github.com/gabime/spdlog) | 日志（仅部分构建路径） | MIT |
| [nlohmann/json](https://github.com/nlohmann/json)（内置 `include/Json/`） | 配置解析 | MIT |
| [TweetNaCl](https://tweetnacl.cr.yp.to/)（`src/tweetnacl.c`） | Ed25519 验签 | Public Domain |

## 算法与实现参考

| 来源 | 借鉴内容 | 许可 |
|---|---|---|
| [AutoSystemBoost](https://github.com/krilikd/AutoSystemBoost) | Smart Mode 自学习算法（12 桶 / 置信门控 / 分级回退） | MIT |
| [fas-rs](https://github.com/shadow3aaa/fas-rs) | 帧感知调频控制器（P 控制器闭环） | GPL-3.0 |
| [helloklf/vtools](https://github.com/helloklf/vtools) | 接管逻辑思路 | GPL-3.0 |
| SutoLiu AppOpt / JZzz 生态 | 线程模板与 `applist.prop` 格式 | GPL-3.0 |
| [SukiSU-Ultra](https://github.com/SukiSU-Ultra/SukiSU-Ultra) | UI 风格 | GPL-3.0 |
| Metric | 界面与引擎线程签名 | — |

## 数据

| 数据 | 来源 | 许可 |
|---|---|---|
| `include/GameList.hpp` 内置游戏包名清单（531 条） | [Encore Tweaks](https://github.com/Rem01Gaming/encore) 的 `gamelist.txt` | Apache-2.0，© 2024-2026 Rem01Gaming |

> Encore Tweaks 的 NOTICE 要求：再分发时保留其版权与许可声明。上述清单为包名事实集合，
> 已在此处与 `include/GameList.hpp` 文件头注明来源；本项目整体仍以 GPL-3.0 分发
> （Apache-2.0 与 GPL-3.0 单向兼容）。
>
> 若你认为某项归属有遗漏或标注有误，请开 issue，我们会尽快修正。
