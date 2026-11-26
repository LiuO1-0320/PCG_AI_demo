# CASTLE_PERFORMANCE_OPTIMIZATION_V1_RESULT

2026-11-26 · **真实性能基线及瓶颈诊断已完成，正式优化未落地，60 FPS 目标未达到。**

正式地图 Seed 24680 的固定无交战机位可反复出现周期性长帧。最强因果证据指向 **Lumen Surface Cache 卡片捕获路径**。将卡片捕获预算临时设为 0 后，同机位从约 15 FPS 升至 55.10 FPS，P95 从约 422–459 ms 降至 19.55 ms。恢复预算后卡顿复现。此探针影响缓存更新，正式配置已还原，卡片捕获、可见性与 Lumen 并行调度的其他试验没有取得可复现的改善。

## 工程、分工与采样条件

- 工程 `E:/Unreal Projects/PCG_AI_demo/testfordemo.uproject`，UE 5.8.4 / CL 58902052，Development Editor。Epic MCP 完成握手、工具发现和实际地图、PIE、Director、资产 Dirty 核查。现有进程 PID 12344 实际加载本项目 `Binaries/Win64/UnrealEditor-CastleDemo.dll`。正式地图的近战概率读回 `0.3333333432674408`，远程为既有 Art Ragdoll 类。四份指定历史报告、全局 AGENTS 和工程 `README_DEMO.md` 已读取，当前检出中未定位到独立命名的项目 SOP。 [冻结清单](Saved/CastlePerformanceOptimizationV1_20261126/Frozen-Baseline.json) · [MCP 编辑器核查](Saved/CastlePerformanceOptimizationV1_20261126/Editor-Preflight.json)
- 主协调掌管 Editor、测试、数据与报告。Agent A `perf_ai` 核查 AI、BT、近战和弹体。Agent B `perf_render` 核查 PCG、场景与渲染。Agent C `perf_anim` 核查动画、物理与近战通知。Agent D `perf_lifetime` 核查内存、UI、生命周期，并独立复算性能与结论。四人均使用同批正式图原始证据，未直接改代码或资产。
- i7-9750H（6 核 12 线程）、15.88 GiB 物理内存、RTX 2070（8192 MiB）、NVIDIA 621.01，Windows 10 22H2。桌面 1920×1080，正常浮动 PIE 为 D3D12 SM6，CSV 记录输出 1280×720。项目 `r.ScreenPercentage=87`、`sg.ResolutionQuality=87`、其他已读 Scalability 组均为 2，动态分辨率关闭，VSync 为 0，`t.MaxFPS=0`。未直接读取内部渲染像素矩形。实际常见最快帧间隔约 16.6 ms，PIE 启动也打印 60 tick rate，留待单独的非编辑器 Development Game 验证真实上限和 80–100 FPS 目标。
- 无交战主样本使用第一层入口同一机位 `(-650,-720,90.15)`、Yaw 136.16°，玩家维持 100 HP／60 护盾，敌人真实生成，预览正常渲染。通过仅 PIE 的定位固定机位，通过正式游戏逻辑完成战斗和交互。稳定样本每次目标 24 秒，按原始 CSV 有效帧汇总约 24.8–25.3 秒。抓取期间无持续截图或逐帧 MCP 轮询，边界验证了前台。每段保留所有有效长帧，也保留采集切换首尾。诊断 Trace、临时 CVar 以及混战测量另列。 [首轮机位边界](Saved/CastlePerformanceOptimizationV1_20261126/Quiet-A-boundaries.json) · [末轮机位边界](Saved/CastlePerformanceOptimizationV1_20261126/Quiet-Final-B-boundaries.json) · [统计脚本](Saved/CastlePerformanceOptimizationV1_20261126/Summarize-Csv.py)

## 正式设置前后对照

同机位、同项目、恢复原值后复测。每格为 A／B 两次，帧时间单位 ms。改动栏记录同一生产配置下的实测差异。

| 指标 | 前两次 | 恢复后两次 | 实际变化 |
| --- | ---: | ---: | --- |
| 有效帧数／时长 | 390／381，25.29／25.02 s | 380／385，25.10／24.83 s | 四份原始全段 |
| 平均 FPS | 15.42／15.23 | 15.14／15.51 | 两组约 15.3 |
| Frame P50 | 27.00／28.36 | 28.02／27.12 | 同量级 |
| Frame P95 | 422.02／422.11 | 457.04／458.39 | 末组更长，周期长帧持续 |
| Frame P99 | 449.77／426.31 | 468.80／465.58 | 末组更长 |
| GameThread P50 | 16.13／15.64 | 17.07／16.73 | 执行列略增，等待另看 Trace |
| RenderThread | CSV P50 约 0.001；等待见 Trace | CSV 同样约 0.001 | 此列无法代表 RT 执行与等待总量 |
| RHI Thread P50 | 0.31／0.32 | 0.30／0.31 | CSV CPU 时间小，RHI Fence 等待见 Trace |
| GPU P50 | 26.23／25.91 | 27.96／26.45 | 同量级 |
| Animation GT P50 | 0.56／0.56 | 0.63／0.59 | 同量级 |
| Physics GT P50 | 0.042／0.038 | 0.042／0.040 | 同量级 |
| 系统可用内存最低 | 3031／2827 MiB | 2753／2779 MiB | 主机内存负载有变化 |
| GPU 本地驻留峰值 | 3771／3771 MiB | 3803／3803 MiB | +32 MiB，同为约 7238 MiB 预算 |
| 长帧数 >100 ms | 46／42 | 38／36 | 数量变化，长帧尾部仍在 |
| 长帧数 >300 ms | 27／26 | 26／26 | 周期长帧复现 |
| 楼层生成耗时 | 首层 367 ms；第二层至所有区域 Ready 约 1148 ms | 固定机位验收段无重新生成 | 无优化前后生成增益结论 |

CSV 外的进程终点工作集约 4.85 GiB，进程自启动峰值约 4.96 GiB，终点私有提交约 8.21 GiB；系统端采样提交约 21.48／36.88 GiB。进程峰值跨该 Editor 会话，未按每个区段重置。部分长帧遇到 GC，但其他样本无 GC 标记时仍反复出现同规模长帧，短时内存记录不足以判断长期泄漏、分页影响或多层资源回收趋势。 [汇总](Saved/CastlePerformanceOptimizationV1_20261126/RawCSV-Summary.json) · [前 A](Saved/CastlePerformanceOptimizationV1_20261126/RawCSV/CastlePerf-Quiet-A.csv) · [前 B](Saved/CastlePerformanceOptimizationV1_20261126/RawCSV/CastlePerf-Quiet-B.csv) · [复测 A](Saved/CastlePerformanceOptimizationV1_20261126/RawCSV/CastlePerf-Quiet-Final-A.csv) · [复测 B](Saved/CastlePerformanceOptimizationV1_20261126/RawCSV/CastlePerf-Quiet-Final-B.csv)

## 定位与定向诊断

**主要瓶颈 · 渲染临界路径。** 同机位长帧在两个稳定样本中大致每 15 帧出现，其他机位有约 12 帧的重复周期。CSV 中，长帧相邻行曾出现约 352 ms 的 RenderThread Visibility 等待和约 407–410 ms 的 GameThread 事件等待。初始运行的 Insights 时间线在约 4142.3 s 的代表帧中读到 GT `Sync_RenderingThread` 435.8 ms、RT `WaitForVisibilityTasks` 373.6 ms、RHI `SyncPoint_Wait` 370.7 ms。Graphics 队列 `LumenSceneUpdate` 标记区间 384.4 ms，两段约 1.2 ms 的 `MeshCardCapture` 标记之间有约 375 ms 间隔。它们是同一等待链的不同观测，不能相加。卡片路径的因果证据来自临时探针，GPU 标记间隔本身不足以区分 GPU 执行、Fence 或命令提交间隙。底层队列停顿来源仍未知。独立无 CSV Trace 期间仍观察到低推进速率，但其事件导出不完整，仅用于排除“长帧只能在 CSV 捕获中复现”的解释。 [Startup 原始 Trace](Saved/CastlePerformanceOptimizationV1_20261126/Startup-Floor1.utrace) · [关键时间线统计](Saved/CastlePerformanceOptimizationV1_20261126/Startup-Timeline-Summary.json) · [无 CSV 原始 Trace](Saved/CastlePerformanceOptimizationV1_20261126/Quiet-NoCSV.utrace)

| 短时隔离探针（同一预热固定机位） | FPS | Frame P95 | 结论 |
| --- | ---: | ---: | --- |
| Visibility 调度设为 0／恢复 1 | 15.34／15.20 | 421.90／422.55 ms | 无稳定收益 |
| Lumen `CardCapturesPerFrame=0`／恢复 300 | 55.10／14.72 | 19.55／458.77 ms | 周期尾部基本消失并在恢复后复现，GPU P50 7.93／26.06 ms |
| Lumen 并行更新设为 0 | 15.29 | 457.97 ms | 无清晰收益 |
| Lumen 已有卡片刷新比例设为 0 | 14.83 | 455.42 ms | 无清晰收益 |

卡片预算探针仍保留一个约 447 ms 的采集边界长帧。临时归零在静止预热机位可以限制捕获，但会影响新视角、灯光变化和换层时的 Surface Cache 更新，无法用这组数据证明同画质，未保存为优化。正式复测前已验证调度为 1、卡片预算为 300、并行更新为 1、刷新比例为 0.125。 [原值读回](Saved/CastlePerformanceOptimizationV1_20261126/Runtime-Settings-Restored.json) · [卡片暂停诊断](Saved/CastlePerformanceOptimizationV1_20261126/RawCSV/CastlePerf-Diag-LumenCards0.csv) · [恢复采样](Saved/CastlePerformanceOptimizationV1_20261126/RawCSV/CastlePerf-Diag-LumenCardsRestored300.csv) · [Epic Lumen Surface Cache 说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine)

- **AI 与 Gameplay。** 固定机位 BT Tick P50 两轮均约 0.091 ms。FIX05 的常规路径重试和恢复探针维持原实现。HealthLabel 的每帧同值刷新存在可减少的渲染状态更新，宽口径帧尾 GT 更新 P50 约 0.36 ms，尚未分离标签占比。当前证据不足以把它列为首批生产改动。
- **PCG 与场景。** 首层 RUN_START 到区域 Ready 367 ms，导航报告 70 ms。成功上楼后探索、宝库、Boss 区分别约 291、526、1148 ms 达到 Ready，尾段导航报告约 120 ms。宝库 Ready 到 Boss Ready 约 622 ms，日志包含宫殿 860 个模板 Actor。此为已预热 Editor 场景，不是冷磁盘打包加载。第一次按键未触发切层，其等待时间单列于原始日志，未计入上述生成耗时。探索视觉已按网格／材质分批，未看到支持改碰撞壳或场景资产的证据。 [运行日志](Saved/CastlePerformanceOptimizationV1_20261126/Runtime.log) · [楼层 Trace](Saved/CastlePerformanceOptimizationV1_20261126/Floor2-Vault.utrace)
- **动画与物理。** 静止机位动画 GT P95 0.83–0.99 ms，物理 Worker P95 0.18 ms 左右。真实双尸体区间骨骼物理结束 Tick 数从 0→1→2→1→0，双尸体的物理 Worker P95 约 0.89 ms、GT EndPhysics 等待 P95 约 0.72 ms，区间内最大整帧约 57.6 ms。这是死亡过程诊断，两组区间的玩家和渲染状态随战斗变化。逐刚体自然休眠未测。近战 Notify/有效窗及停尸寿命未作为优化点。 [死亡区间原始 CSV](Saved/CastlePerformanceOptimizationV1_20261126/RawCSV/CastlePerf-Deaths.csv)
- **内存、UI 与生命周期。** HUD 为原生 Canvas。现有全列表候选扫描和文本格式化值得独立计量，但当前关键等待和它们无已证实的归因。GPU 本地预算未见耗尽，楼层清理和 GC 需要未来分段的长局内存验证。未进行 UI、生命周期或资源释放代码修改。

## Gameplay 冒烟

| 入口与结果 | 实际证据 |
| --- | --- |
| 正式生成 · PASS | 第一层 11 远程、4 近战、1 原生精英。第二层探索 10 远程、6 近战、1 原生精英；宝库 4 远程、1 近战；Boss 区 1 原生 Queen。保留近战 1/3。 |
| 近战、远程和伤害 · PASS（冒烟） | 正常交战扣除护盾；原生 `CastleBolt` 在混战边界读到。近战真实 NotifyBegin 命中玩家 Capsule 并进入 Recovery，收招日志 `Completed=1`；撤离后观察到一次无命中的完整攻击。正式图两次 3 秒活体补采 HP 均保持 100，盾分别 60→20／10。 |
| 两次原生击杀 · PASS | 实弹消耗 8 发，首次近战实测 85→63→41→19→死亡，远程也实际被击杀。击杀奖励 35→47→59，拾取物 `WeaponMod/RAPID FIRE` 在同世界中存在。 |
| Ragdoll 与清理 · PASS（运行状态） | 两个不同代理引用进入 `Ragdoll`，请求既有 `AM_Grunt_Death`，两者 LifeSpan 均为 10 秒，并在同一 Ready World 清至 0。当前轮以状态和 Tick 取证，近战无枪／远程尸体持枪及目视过渡沿用同 SHA 资产的既有验收。 |
| 正式上楼、宝库交互 · PASS | 通过正式玩家交互按键进入二层，并由宝库入口落到 `(0,10000,90.15)`，玩家保有正常生命和护盾。前置定位只修改当前 PIE 的玩家位置。 |

[混战冒烟](Saved/CastlePerformanceOptimizationV1_20261126/Combat-Smoke.json) · [原生远程弹体快照](Saved/CastlePerformanceOptimizationV1_20261126/Native-Mixed-B-counts.json) · [双击杀和清理](Saved/CastlePerformanceOptimizationV1_20261126/Deaths-Gameplay.json) · [原掉落](Saved/CastlePerformanceOptimizationV1_20261126/Deaths-Drop-Observed.json) · [二层及宝库生成分类](Saved/CastlePerformanceOptimizationV1_20261126/Floor2-Vault-Confirmed.json) · [实按宝库交互成功](Saved/CastlePerformanceOptimizationV1_20261126/Floor2-Vault-Input.json)

两次 24 秒混战与两次 8 秒补采均在样本内发生玩家死亡。临时保活的官方反射写入和 Editor SET 均被拒绝，未放宽该限制。原始 CSV 保留为混合状态诊断，只有两次短切片确认全段活体。20–30 秒可重复的全程活体混战、实际移动探索路线和独立 Development Game 仍未建立。代码与资产未变化，本轮未重跑 FIX05 围栏恢复、受击与死亡取消专项、长帧命中窗和目视枪械专项。 [历史混编验收](CASTLE_MELEE_FINAL_INTEGRATION_V1_RESULT.md) · [历史 FIX05](CASTLE_ENEMY_PURSUIT_FIX05_RESULT.md)

## 保护、回滚与剩余选择

生产文件无变更，未构建新 DLL。源码、配置、关键资产与 DLL 共 65 项前后 SHA256 相同。地图 SHA256 为 `74e9cbf1f9fc883f47fc84e30d5fd13df3a8202fe78eff3a78f5c47dbdfdcca5`，`CastleGameplay.cpp` 为 `09a8954fcb1e99d0e0e6fad4958d03b6ca9db9cd6c0d3a2f7abe3add59f6da7d`，运行 DLL 为 `215c9301c0ab1f679f8f7ad80d35a7a7967968bd64b6edfa2e1e7a17ccc4c165`。回滚只涉及会话探针，参数已恢复，重开项目也会重新读取原配置。施工前副本存于本轮 `Saved` 下的 `Baseline` 子目录。Git 分支 `codex/castle-melee-v1`，HEAD `eea95e5`。保留原本的地图、BlendSpace、近战报告未提交内容和附件。 [前后保护核查](Saved/CastlePerformanceOptimizationV1_20261126/Protection-Final.json)

若下一轮仍以原画质为硬约束，优先用同一入口视角在独立 Development Game 复现，再在 Insights 或 GPU 队列工具对照 graphics/compute/copy work 与 fence signal。若考虑光照更新速度取舍，可在隔离地图路线定量比较过门、转向和换层时的 GI 收敛与倒影，单独审批长期预算变化。当前证据只支持定位，不支持判断 RTX 2070 的最终帧率上限或宣称可达到 60、80 或 100 FPS。

正式 `testfordemo Preview` 已留在 Seed 24680 首层入口，玩家 100 HP／60 护盾，`MOVE_Walking`，正常渲染并保持可直接游玩。 [交接读回](Saved/CastlePerformanceOptimizationV1_20261126/Handoff.json)
