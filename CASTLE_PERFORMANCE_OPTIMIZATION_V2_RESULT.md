# CASTLE_PERFORMANCE_OPTIMIZATION_V2_RESULT

2026-11-26 · **64 档已保存到项目。冷启动确认生效。浮动 PIE 的周期性长帧在对照中消失。**

正式地图 `/Game/Demo/L_CastleDemo`、Seed 24680、同一固定机位。原值 300 的浮动 PIE 再现了 V1 的周期性长帧。切到 64 后平均 **14.61 → 50.81 FPS**，Frame P95 **468.46 → 22.82 ms**，超过 300 ms 的帧 **21 → 1**，64 档唯一一帧位于采集边界，原始数据保留。项目保存后重新启动的独立游戏进程读回 `64 / SystemSettingsIni`，测得 **68.52 FPS**、Frame P95 **17.81 ms**，24.81 秒内无超过 50 ms 的帧。独立游戏和浮动 PIE 分开比较。

## 工程、分工与测试口径

- E 盘工程 `E:/Unreal Projects/PCG_AI_demo/testfordemo.uproject`，UE 5.8.4 / CL 58902052，分支 `codex/castle-melee-v1`。官方 MCP 核对正式地图、进程及只读资产状态。保护原有工作树和未保存会话内容。主协调负责 Editor、运行、配置写入及交接。渲染 Agent `v2_render` 核查引擎 CVar、页预算和渲染事件。独立 Reviewer `v2_reviewer` 复算原始帧、检查九组画面及配置差异。 [工程门禁](Saved/CastlePerformanceOptimizationV2_20261126/Preflight.json) · [新 Editor 门禁](Saved/CastlePerformanceOptimizationV2_20261126/Editor-Validation-Gate.json)
- 当前无可用打包 Development Game，使用真实的独立 `UnrealEditor.exe -game` 进程加载未烘焙正式地图。另使用同工程正常渲染的浮动 PIE 验证 V1 问题。两个模式采用 D3D12 SM6、1280×720 输出、Screen Percentage 87、现有 Scalability 2、VSync 0、动态分辨率关闭。独立游戏 `t.MaxFPS=0`，`GETMAXTICKRATE=0`，Fixed Frame Rate 和 Frame Smoothing 关闭。独立进程出现明显低于 16.6 ms 的帧，所以 V1 的 60 tick rate 不能视为游戏的硬性 FPS 上限。未核定 PIE 在所有状态下的限速逻辑。 [独立进程日志](Saved/CastlePerformanceOptimizationV2_20261126/Game-Initial.log) · [浮动 PIE 日志](Saved/CastlePerformanceOptimizationV2_20261126/Editor-Validation.log)
- 同机位 `(-650,-720,90.15)`，Yaw 136.162°，Pitch 0°，Seed 24680。合格固定机位样本前后 HP 100，按原始 CSV 保留全部正帧。两次前台中断的 300-B/C 保留为偏离条件的诊断样本，未用于收益计算。GPU CSV 和卡片页数分别采集，卡片计数日志在主测期间关闭。 [机位复核](Saved/CastlePerformanceOptimizationV2_20261126/Game-Repeat-Poses.json) · [可复算汇总](Saved/CastlePerformanceOptimizationV2_20261126/CSV-Summary.json) · [统计脚本](Saved/CastlePerformanceOptimizationV2_20261126/Summarize-Csv.py)

## 非零预算筛选与重复对照

本机 UE 5.8.4 源码 `Renderer/Private/Lumen/LumenSceneRendering.cpp:104–109` 注册的完整 CVar 为 `r.LumenScene.SurfaceCache.CardCapturesPerFrame`，默认 300，初始运行读回 `Constructor`。该量约束每帧 Surface Cache 捕获页等工作，实际捕获会受到页分配、多页批次等影响。表中的 GPU / Lumen 是 CSV GPU 标记统计，包含该标记口径的等待，不等于纯着色器执行时间。

| 模式／预算 | 采样 s | 平均 FPS | Frame P50 / P95 / P99 ms | >50 / >100 / >300 ms | GT / GPU P95 ms | LumenSceneUpdate P95 ms |
| --- | ---: | ---: | --- | --- | --- | ---: |
| 独立游戏 300-A | 24.70 | 51.87 | 15.75 / 38.20 / 54.45 | 19 / 0 / 0 | 6.79 / 37.20 | 28.69 |
| 独立游戏 300-D | 24.81 | 52.65 | 15.81 / 36.66 / 57.63 | 39 / 0 / 0 | 7.30 / 38.23 | 31.23 |
| 独立游戏 64-A | 25.12 | 67.68 | 14.28 / 18.44 / 20.14 | 1 / 0 / 0 | 6.64 / 14.45 | 7.09 |
| 独立游戏 64-B | 24.78 | 69.33 | 13.98 / 18.00 / 19.81 | 0 / 0 / 0 | 6.75 / 15.53 | 8.06 |
| 独立游戏 16-A | 24.96 | 72.24 | 13.32 / 17.20 / 18.69 | 0 / 0 / 0 | 6.45 / 9.91 | 2.23 |
| 独立游戏 4-A | 24.75 | 73.25 | 13.19 / 16.85 / 18.25 | 0 / 0 / 0 | 6.25 / 8.87 | 1.12 |
| 浮动 PIE 300 | 20.80 | 14.61 | 30.73 / 468.46 / 488.29 | 70 / 37 / 21 | 24.38 / 105.81 | 373.69 |
| 浮动 PIE 64 | 20.70 | 50.81 | 18.23 / 22.82 / 43.60 | 6 / 1 / 1 | 21.95 / 14.31 | 6.56 |
| **独立游戏冷启动 64** | **24.81** | **68.52** | **14.23 / 17.81 / 19.55** | **0 / 0 / 0** | **6.80 / 13.93** | **7.16** |

- 独立游戏 300 在两份合格样本均未复现超过 100 ms 的帧，仍有明显的较小 GPU 尾部。浮动 PIE 300 则复现 V1 的约每 15 帧一次的严重尾部。64 在独立游戏两次重复，将平均 FPS 提高约 31%，将 Frame P95 降低约 51%。浮动 PIE 的 64 仍保留采集首行约 569 ms 的长帧，其后的样本无超过 100 ms 的帧。独立游戏较高的 FPS 不用于替代浮动 PIE 结果。 [PIE 300 原始 CSV](Saved/CastlePerformanceOptimizationV2_20261126/RawCSV/V2-PIE-300-A.csv) · [PIE 64 原始 CSV](Saved/CastlePerformanceOptimizationV2_20261126/RawCSV/V2-PIE-64-A.csv)
- 独立游戏在短时 `Surface Cache.LogUpdates 1` 诊断中，300 的单条正更新计数为 2–37 页，64 为 2–8 页且以 8 为主，16 为 2 页，4 为 1 页。这是正更新日志的页数统计，未把它换算成逐帧 GPU 绘制次数。实测随预算下降而逐步减轻，支持限制高峰的做法。16 和 4 未进入动态质量验收。64 已重复取得主要收益，并为新视角保留较高捕获预算。V2 未重跑 0 档或 V1 已证明无效的 Visibility、并行和刷新比例方案。 [300 计数](Saved/CastlePerformanceOptimizationV2_20261126/Game-300-A-Counts.json) · [64 计数](Saved/CastlePerformanceOptimizationV2_20261126/Game-64-A-Counts.json) · [16 计数](Saved/CastlePerformanceOptimizationV2_20261126/Game-16-A-Counts.json) · [4 计数](Saved/CastlePerformanceOptimizationV2_20261126/Game-4-A-Counts.json)

## 动态画面与 Gameplay

| 检查 | 结果与限制 |
| --- | --- |
| 门洞、新房间、快速视角变化 | 同 Seed 使用重复航点、恢复 Walking，并以 180° 视角切换施加压力。Reviewer 查看了 300/64 共九组画面。壁面蜡烛光、门洞阴影、地面高光和反射在所取帧中未见明显退化。门洞已能看到邻室的一部分。持续正常步行的输入没有可靠复现，走转全过程和新房间首个亚秒的收敛未建立。 |
| 房间、转场、宝库 | 两档都经实际 F 上楼并进入宝库，落点 `(0,10000,90.15)`。宝库首图请求分别约为 F 后 0.28 s／0.25 s，后图约 1.93 s／1.91 s。金色浮雕、暗石墙、地面反射和光照边缘未见明显黑块、漏光或延迟跳变。离散帧无法排除中间帧瞬态。动态画面对照仅在独立运行进程的当前玩家实例临时提高生命，保留敌人和伤害工作；正式玩法冒烟与最终交接使用原值。 |
| 正式混编 | 第一层实际 16 个敌人，11 远程、4 近战、1 精英，Director 正式近战比例沿用 1/3。未写地图或生成逻辑。二层探索、宝库和 Boss 区日志 Ready，真实交互有效。 |
| 远程火力与近战机制 | 64 档浮动 PIE 读到实际 `CastleBolt`。近战真实 NotifyBegin 命中玩家 Capsule，随后进入 Recovery，完整攻击日志 `Completed=1`。 |
| 实弹击杀、掉落、死亡 | 两名普通兵各用四发实弹击杀，弹药 24→16，奖励 35→47→59，实读 `RAPID FIRE` 掉落。两个独立代理进入 Ragdoll、各显示 10 秒 LifeSpan，在同一 PIE 内清理至 0。 |

画面对照使用游戏 PNG 和窗口 JPEG，存在截图边框、命中暗角、粒子和角色相位差异。结论限于可见场景表现。独立日志有部分材质 ISM usage 警告，两个预算同属这一路径。未另行修改共享材质。 [门洞 300](Saved/CastlePerformanceOptimizationV2_20261126/Route-300-Door-First.png) · [门洞 64](Saved/CastlePerformanceOptimizationV2_20261126/Route-64-Door-First.jpg) · [新房间 300](Saved/CastlePerformanceOptimizationV2_20261126/Route-300-NewRoom-Settled.png) · [新房间 64](Saved/CastlePerformanceOptimizationV2_20261126/Route-64-NewRoom-Settled.jpg) · [宝库首图 300](Saved/CastlePerformanceOptimizationV2_20261126/Route-300-Vault-First.jpg) · [宝库首图 64](Saved/CastlePerformanceOptimizationV2_20261126/Route-64-Vault-First.jpg) · [300 交互时点](Saved/CastlePerformanceOptimizationV2_20261126/Route-300-Completion.json) · [64 交互时点](Saved/CastlePerformanceOptimizationV2_20261126/Route-64-Completion.json) · [实弹与布娃娃](Saved/CastlePerformanceOptimizationV2_20261126/PIE-64-Gameplay-Verified.json) · [近战命中](Saved/CastlePerformanceOptimizationV2_20261126/PIE-64-Melee-Combat.json)

## 配置、重启和回滚

在 `Config/DefaultEngine.ini` 末尾新增一项项目级设置。

```ini
[ConsoleVariables]
r.LumenScene.SurfaceCache.CardCapturesPerFrame=64
```

本机引擎 `Core/Private/Misc/ConfigCacheIni.cpp:6970–6975` 将该节应用为 `SetBySystemSettingsIni`。全新独立游戏进程未传预算覆盖，启动日志设置为 64，实际控制台读回 `64 / SystemSettingsIni`。从同一机位开始与结束均 HP 100，项目设置下 1700 帧测得 68.52 FPS。平均超过 60 FPS，P95 仍为 17.81 ms，未达到全程 60 FPS 帧预算或 80–100 FPS 目标。冷启动实测型号为未烘焙 `-game`，冷启动后的浮动 PIE 单独对照未重跑。 [冷启动读回](Saved/CastlePerformanceOptimizationV2_20261126/Cold-Verification.json) · [冷启动日志](Saved/CastlePerformanceOptimizationV2_20261126/Game-Cold-64.log) · [冷启动 CSV](Saved/CastlePerformanceOptimizationV2_20261126/RawCSV/V2-Game-Cold-64.csv)

- 配置修改前 SHA256 为 `e0f95e3fc955e9c0cf243e97f3ce3c8ab6fe995e07e2a5f25c48905357364c28`。
- 修改后 SHA256 为 `8cd8911819bfa323f29f14b5a4f75c9e0ee335634c165ef805cb2e8ca282ec86`。
- 与 V1 冻结的 65 项核对，唯一变化是该 INI。地图、动画、AI、PCG、击杀、碰撞、DLL 和插件保持原状态，既有未提交工作保留。回滚时定向移除新增的节和配置行，再重启并读回 300。若当前 INI 仍与上面的修改后哈希一致，也可用本轮备份逐字节恢复。 [备份 INI](Saved/CastlePerformanceOptimizationV2_20261126/DefaultEngine.before.ini) · [变更](Saved/CastlePerformanceOptimizationV2_20261126/Config-Change.json) · [保护核查](Saved/CastlePerformanceOptimizationV2_20261126/Final-Protection.json)

V1 Insights 在卡片捕获范围内显示 GPU 标记间隙以及 GT、RT、RHI 的等待。V2 同机位预算曲线及浮动 PIE 复现验证了触发路径。Graphics、Compute、Copy 的实际工作与 Fence 信号之间的完整关系仍未定性，Editor 放大效应也未定位到某个特定视口或驱动行为。没有修改驱动、RHI、Nanite、LOD、阴影、画质等级或 Screen Percentage。 [V1 原始 Trace](Saved/CastlePerformanceOptimizationV1_20261126/Startup-Floor1.utrace) · [V1 时间线定位](Saved/CastlePerformanceOptimizationV1_20261126/Startup-Timeline-Summary.json) · [V2 64 档 PIE Trace](Saved/CastlePerformanceOptimizationV2_20261126/PIE-64-Gameplay.utrace) · [Epic Lumen 技术说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine)

最终仅保留一个正常渲染的正式游戏窗口，Seed 24680 首层入口，100 HP，正常 Walking。角色位于可直接开始游玩的基准机位。
