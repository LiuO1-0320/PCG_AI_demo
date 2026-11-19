# CASTLE_ENEMY_PRESENTATION_V1 FIX01 补验结果

工程 `E:/Unreal Projects/PCG_AI_demo/testfordemo.uproject`，UE 5.8.4 / CL 58902052。记录日期 2026 年 11 月 20 日。

**结论：PCG 失败根因已确认，普通兵生成和表现事件已有运行证据，整体未通过。** Seed 1337 在 V1 前和当前版本均因三件砌体资源缺失而失败。另有独立的 Grunt 资产问题：Helmet 和 Visor 的隐藏设置未持久化。最终 PCG 回归仍失败，已按停止条件结束补验。

## 1 工程门禁与保护

已核对全局 `AGENTS.md`、项目既往 MCP 验收流程和 V1 施工报告。工程内未找到单独命名的自动化 SOP。官方 Unreal MCP 使用隔离 DebugGame 会话完成握手、工具发现和真实只读调用，确认目标工程、当前 `/Game/Demo/L_CastleDemo` 及 PIE 未运行。三个已保存蓝图均完成只读加载和回读。本轮未保存任何蓝图或地图，也未给正式 Director 写入持久类引用。用户原编辑器持续运行。

## 2 Seed 1337 种子回归归因

原 V1 二进制、加入只读探针后的当前版本、V1 前隔离版本和本轮最终回归均复现 `Exploration room is missing overhead masonry`。当前和前置对照首层均生成 10 个房间与 10 个点，内容哈希均为 `1430845164`，四向移动检查均通过，随后在同一个入口房首条屋顶探针失败。

| 字段 | V1 前与当前实测 |
|---|---|
| 运行条件 | Seed `1337`，Floor `1`，Region `0 / Exploration`，房间索引 `0`，类型 `Entrance`，中心 `(0,0,0)`，Doors `2` |
| 房间及世界身份 | 房间本身是 Director 的 `FCastleRoom` 数组元素，无单独房间 Actor。世界为 `/Game/Demo/L_CastleDemo.L_CastleDemo`，Director 全路径为 `/Game/Demo/L_CastleDemo.L_CastleDemo:PersistentLevel.CastleDirector_0`。未发现 CourtScenery Owner。 |
| 射线 | Origin `(0,0,612)`，End `(0,0,1332)`，Channel `ECC_Visibility`，`TraceComplex=0`。忽略 `/Game/Demo/L_CastleDemo.L_CastleDemo:PersistentLevel.CastlePlayer_0`。 |
| 命中结果 | `Hit=0`，`bBlockingHit=0`，`bStartPenetrating=0`，`Time=1.0`，`Distance=0.0` 为无命中时的结果值。 |
| 命中对象与设置 | `HitActor=None`，`HitComponent=None`，Actor 和 Component Tags 均为空。Collision Profile 为 `None`，Collision Enabled、Object Type 和 Visibility Response 均记录为 `-1`，表示无命中组件可读取。 |
| 预期砌体 | Court Owner、带 `CastleVaultMasonry` 标签的组件、其中已注册的组件和实例数均为 `0`。 |

**根因**：`/Game/Demo/CourtArt/Masonry/SM_Wall_00`、`SM_Ground` 和 `SM_Pillar` 三个包均不存在，加载对象均无效。`CastleCourtScenery.cpp` 的构造逻辑在必需资源缺失时返回，未创建后续带 `CastleVaultMasonry` 标签的上部壳体。`CastleGeneration.cpp` 只有在 `Court.Shell` 有效时才调用 `EncloseRoom`。因此原始入口房向上探针没有命中预期几何。这一链路在 V1 前的源码和相同缺失资源的内容快照中可复现。当前结论限定在本工程的这份内容状态。

**对照控制**：V1 前副本位于 `E:/Unreal Projects/PCG_AI_demo/Saved/FIX01_PCG_PreV1_235413`。四份施工前源文件来自 SHA 已核对且 Git blob 与 `0027121` 一致的备份。两边使用相同 `.uproject`、项目配置、Content 资源和地图，基线快照逐一核对了 434 个文件，其中四份为前置源文件。两边在相同 UE 版本、限定 DebugGame 编译参数及 `-CastleSmokeTest -CastleSeed=1337` 启动条件下运行同一份只读探针。原断言文件 `CastleSmoke.cpp` 和构造文件 `CastleCourtScenery.cpp` 的 SHA 分别在两份工程中一致。V1 的生成类与表现事件改动未参与砌体创建条件。

## 3 Director 真实生成和失败行为

在未保存的独立瞬时游戏世界，专项测试使用 Seed `24680` 调用原 `GenerateLayout` 和 `GenerateCave`，从 Director 实际登记的 Actor 中验类和计数。测试覆盖原函数内的两个生成入口，正式 `L_CastleDemo` 的生成类字段保持原状。

| Director 请求 | 探索战斗房普通兵 | 宝库普通守卫 | 探索精英 | Actor Owner |
|---|---|---|---|---|
| `BP_CastleEnemy_Grunt_C` | 15 个，均为 Grunt | 3 个，均为 Grunt | 1 个，原生 `ACastleEnemy` | `None` |
| 空类 | 15 个原生 | 3 个原生 | 1 个原生 | `None` |
| 显式 `ACastleEnemy` | 15 个原生 | 3 个原生 | 1 个原生 | `None` |
| 不兼容的原生 `AActor` 类 | 字段拒绝该类型，回退 15 个原生 | 回退 3 个原生 | 1 个原生 | `None` |

Grunt 的实际类路径为 `/Game/CastleDemo/Enemies/Presentation/BP_CastleEnemy_Grunt.BP_CastleEnemy_Grunt_C`。各组 Actor 均由其 Director 配置和登记，`Actor Owner` 和 Director 引用各自独立。Grunt 组的 Director 为 `/Temp/Untitled_4.Untitled:PersistentLevel.CastleDirector_0`。

独立坏例在允许的瞬时世界中使一个刚生成的 Grunt 在登记前失效。收到一次现有的 `CASTLE_ENEMY_SPAWN_FAILED`，15 个请求登记了 14 个 Grunt。失败位置未登记、未重试、未换成原生替补。同组其余生成继续完成。`CastleDemo.EnemyPresentation.SpawnFailure` 通过。

## 4 单兵表现事件和死亡代理

| 项目 | 运行证据 |
|---|---|
| 攻击 | 对真实 Director 生成的一名 Grunt 调用正式攻击提交路径。无视线时为 0 次通知，随后一次远程攻击与冷却内重试合计收到 1 次实际蓝图 Attack 通知，生成 1 个 Gameplay 弹体。 |
| 受击与死亡 | 无效和零伤害未发出 Hit。有效非致死伤害发出 1 次 Hit，致死发出 1 次 Death，Hit 总数仍为 1。击杀 `+1`，原补给掉落 `+1`，死亡通知后原敌人立即销毁。 |
| 空资源 | 在该实例的三个 Montage、三个 Sound 和衰减槽为空时，三个蓝图事件照常接收。未观察到蓝图运行异常，死亡代理生成数为 0。音频输出和渲染在自动化会话中关闭。 |
| 有效死亡 Montage | 在另一瞬时世界给单个 Grunt 实例赋值从现有 `MM_Idle` 和 `DefaultSlot` 创建的内存 Montage，确认死亡后只生成一个代理。原敌人立即销毁，代理接过原网格世界变换，实际开始播放内存 Montage。代理无碰撞、无重叠、无导航贡献、无 Pawn、Owner 或复制身份。 |
| 清理 | 代理实际设定寿命为 `7.717 s`，跨帧按测试世界计时器推进并在 `7.900 s` 采样到自动销毁。没有提前触发模板的 0.25 秒兜底清理，也没有由测试手动销毁。该结果覆盖内存测试资源的运行行为。 |

### 冷加载发现的资产问题

综合测试 `CastleDemo.EnemyPresentation.SpawnAndDamage` 的最终状态为 **Fail**，两条失败分别是 Grunt 的 Helmet 和 Visor 冷加载后未隐藏。第二次官方 MCP 会话从磁盘冷加载同一蓝图，确认两者均为 `bVisible=true`、`bHiddenInGame=false`。SentinelBody 则为 `bVisible=false`、`bHiddenInGame=true`。两条失败断言保留，资产保持原样。综合测试中成功获得的生成、事件及代理证据仍可独立查阅。

## 5 构建、发现与回归状态

| 验证 | 结果 |
|---|---|
| CastleDemo 编译链接 | 当前和 V1 前的限定 DebugGame Editor 构建均成功。当前最终构建实际编译了补验测试并链接 CastleDemo。构建以进程参数排除了 RoguelitePrototype，并使用 `-NoPCH` 和临时头文件参数，工程描述与配置文件未写入。 |
| 常规 Editor Target | 标准 Development 完整构建未执行。用户原编辑器仍加载目标工程的 `UnrealEditor-CastleDemo.dll`，本轮保留该会话和原输出。全插件完整构建仍无通过证据。 |
| 自动化发现与执行 | 测试框架发现且运行 2 项。`SpawnFailure` 带警告通过，`SpawnAndDamage` 失败（2 条可见性断言）。最终自动化进程退出码为 0，测试结果以框架和 `index.json` 的状态为准。 |
| 原 Castle AI 冒烟 | Seed `1337` 通过，退出码 `0`，出现 `CASTLE_AI_SMOKE_OK`。 |
| 最终原 Castle PCG | Seed `1337` 仍在同一顶部砌体断言失败，退出码 `1`。未删改断言、标签、碰撞或种子，完整回归按停止条件结束。 |

首次专项执行曾因寿命夹具的临时世界没有引擎 WorldContext 而断言退出。随后只修正测试世界的注册、释放和寿命计时驱动，保留首次日志。最终测试走完两项并产生了上述明确结果。

## 6 待人工作业和验收边界

- PCG 依赖的三个砌体资产仍缺失，原回归需要在具备这些依赖的内容状态下重跑。PCG 实现本轮未修改。
- Grunt 的 Helmet 和 Visor 需要修复持久化的隐藏设置并重新冷加载核对。本轮是诊断，三个蓝图均未写入。
- 攻击、受击、死亡的正式 Montage、Sound 和衰减配置仍为空。`MM_Idle` 仅作为隔离的寿命测试资源。Idle、Walk、Jog 的画面观感、脚底贴地、朝向、正式动画和空间听感仍需人工验收。
- 模块接入在独立测试世界验证，正式地图尚未使用 Grunt 进行完整战斗与流程验收。G1A/C2 和全工程构建未执行。未扩大 Boss、精英 AI、队友或其他敌人范围。

## 7 文件和恢复

| 文件 | 修改前 SHA-256 | 修改后 SHA-256 |
|---|---|---|
| `Source/CastleDemo/Tests/CastleEnemyPresentationTests.cpp` | `62F588BB00ADA1BA01E3996E04ACEF76BAE89CD4225E1790418B5FB33D4D91DA` | `E69D44D06A25C52A0E62311994A926B6CD9460B65D143BF598FC7A5BADAEFE8E` |
| `Source/CastleDemo/Tests/CastleEnemyPresentationFix01PcgDiagnostics.cpp` | 新增 | `0A1589C614843BDCC271E47AD5A5D12103E694024E5547B12E2D34E765E3641C` |
| `CASTLE_ENEMY_PRESENTATION_V1_FIX01_RESULT.md` | 新增 | 本报告 |

四份 V1 已有 C++ 改动、三个蓝图、正式地图及预先存在修改的 `testfordemo.uproject` 的 SHA 与本轮开始一致。地图 SHA-256 为 `C3BEE36C6454235EADD8BE7CE67B02954AA740F454A49ACC2AC5FA05907AAECD`。

恢复既有测试前，对照上表确认没有后续他人的修改，再从证据目录的 `CastleEnemyPresentationTests_before.cpp` 逐文件恢复。新诊断只在同时传入 `-CastleSmokeTest` 和 `-CastleEnemyFix01PcgDiagnostics` 时启用，可省略诊断参数停用。若需要从源树移出诊断文件，先核对 SHA 并保留副本。V1 前的工程副本只用于对照，不能整体覆盖目标项目。未执行 Git Reset、Clean、提交或推送。

## 8 原始证据

证据目录 `E:/Unreal Projects/PCG_AI_demo/Saved/Backups/CastleEnemyPresentationV1_FIX01_20261119_235413`。

- [当前诊断复现](<E:/Unreal Projects/PCG_AI_demo/Saved/Backups/CastleEnemyPresentationV1_FIX01_20261119_235413/PCG-FIX01-V1-diagnostic.log:1456>) · [V1 前诊断对照](<E:/Unreal Projects/PCG_AI_demo/Saved/Backups/CastleEnemyPresentationV1_FIX01_20261119_235413/PCG-FIX01-PreV1-diagnostic.log:1456>) · [基线副本 SHA 清单](<E:/Unreal Projects/PCG_AI_demo/Saved/Backups/CastleEnemyPresentationV1_FIX01_20261119_235413/BaselineSnapshotSHA256.txt>)
- [当前最终构建](<E:/Unreal Projects/PCG_AI_demo/Saved/Backups/CastleEnemyPresentationV1_FIX01_20261119_235413/UBT-FIX01-CastleOnly-world-context.log:109>) · [V1 前构建](<E:/Unreal Projects/PCG_AI_demo/Saved/Backups/CastleEnemyPresentationV1_FIX01_20261119_235413/UBT-FIX01-PreV1-CastleOnly.log:167>)
- [首次夹具断言](<E:/Unreal Projects/PCG_AI_demo/Saved/Backups/CastleEnemyPresentationV1_FIX01_20261119_235413/Automation-FIX01.log:2134>) · [最终专项状态](<E:/Unreal Projects/PCG_AI_demo/Saved/Backups/CastleEnemyPresentationV1_FIX01_20261119_235413/Automation-FIX01-world-context/index.json>) · [实际事件与代理日志](<E:/Unreal Projects/PCG_AI_demo/Saved/Backups/CastleEnemyPresentationV1_FIX01_20261119_235413/Automation-FIX01-world-context.log:2166>) · [Grunt 冷加载回读](<E:/Unreal Projects/PCG_AI_demo/Saved/Backups/CastleEnemyPresentationV1_FIX01_20261119_235413/MCP-ColdRead-Grunt.json>)
- [最终 Castle AI 冒烟](<E:/Unreal Projects/PCG_AI_demo/Saved/Backups/CastleEnemyPresentationV1_FIX01_20261119_235413/CastleAI-FIX01-Seed1337.log:1460>) · [最终原 PCG 失败](<E:/Unreal Projects/PCG_AI_demo/Saved/Backups/CastleEnemyPresentationV1_FIX01_20261119_235413/PCG-FIX01-Final-Seed1337.log:1462>)
