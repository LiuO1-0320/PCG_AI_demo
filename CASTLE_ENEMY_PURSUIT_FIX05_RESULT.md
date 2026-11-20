# CASTLE_ENEMY_PURSUIT_FIX05_RESULT

**FIX05 PASS。** Art、旧 Grunt、原生普通兵在固定围栏内各完成三次部分路径。移除围栏后，玩家位移保持 0 cm，三种小兵都在约 33–34 ms 后提交了一条完整路径。不可达阶段保持三次常规请求上限。位移恢复、战斗、三兵追击和原生 Gameplay 回归均已验证。

工程为 E:/Unreal Projects/PCG_AI_demo/testfordemo.uproject，UE 5.8.4，CL 58902052。隔离 PIE 使用 /Game/Art/Grunt01/IntegrationV2/Tests/L_CastleGruntArt_FIX01、Seed 24680。官方 MCP 完成握手、工具发现和真实地图、NavData、PIE 状态查询。通过 Windows 进程模块列表和 SHA-256 确认每套试验实际加载的 DLL。[MCP 门禁](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyPursuitFix05_20261120/Baseline-MCP-Gate.json>) · [修复前 DLL](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyPursuitFix05_20261120/Before-LoadedDll.json>) · [修复后 DLL](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyPursuitFix05_20261120/After-LoadedDll.json>) · [补验 DLL](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyPursuitFix05_20261120/Replay-LoadedDll.json>)。

## 根因与改动

原追击分支在普通兵用尽三次请求后保持 Idle。只有目标切换、相对重试起点移动至少 100 cm，或进入合法射击位置后再离开，才会重启常规预算。FIX04 中玩家原地不动，导航约在 16.204 s 返回完整路径，行为树到 25.729 s 仍没有提交 MoveTo。本轮用合并后的当前源码重建基线，三类兵分别复现同一机制。[FIX04 历史对照](<E:/Unreal Projects/PCG_AI_demo/CASTLE_ENEMY_ART_PIPELINE_V2_FIX04_RESULT.md>)。

地图使用动态 Recast 的 RecastNavMesh-Default。UE 5.8.4 的动态脏瓦片批次完成时，UNavigationSystemV1::OnNavigationGenerationFinishedDelegate 会广播具体 ANavigationData。普通兵在第三次常规请求前订阅。相关 NavData 更新后只记录一个待检查标记。第三次部分路径仍活动时，标记会保留到 PathFollowing 回到 Idle。重复通知合并。

现有追击任务在目标有效且空闲时，按同一个控制器的 NavData、目标和过滤器做同步确认。只有合法完整路径会提交，直接复用已查到的路径。部分或无效结果消耗标记，常规预算维持为零。沿用单兵至少 0.45 s 的请求间隔。同一 World 最快每 0.10 s 执行一次恢复查询。

目标切换、重试起点更新、进入合法射击位置、解除控制和 EndPlay 都会清除监听与待处理标记。死亡入口解除控制时走同一清理。普通兵保留原追击转向、20 cm 到达精度、原胶囊到达判定及关闭侧移。Boss 和精英继续使用原有追击分支。[生产代码](<E:/Unreal Projects/PCG_AI_demo/Source/CastleDemo/CastleGameplay.cpp:491>)。

## A／B　不可达上限和静止目标恢复

前后两份新构建的 Development DLL 使用同一地图副本、同一种子、同一围栏及同版探针。诊断兵以实际角色类、控制器和行为树运行。环境小兵只在隔离 PIE 中暂停行为及碰撞以隔离样本，结束时恢复。各试验末尾均回读到 Director 类原值。表中时间为探针启动后的游戏时间，真实请求号和部分路径状态来自原生 PathFollowing 的 Visual Logger。

| 小兵 | 修复前的三条部分路径 ID | 修复前独立查到完整路径 | 修复后的三条部分路径 ID | 修复后导航通知及完整请求 | 移障至请求 | 同步查询 |
| --- | --- | --- | --- | --- | ---: | ---: |
| Art | 3 / 4 / 5 | 16.162 s，至 27.313 s 无新请求 | 3 / 4 / 5 | 约 16.050 s，同帧 ID 6 | 33.7 ms | 0.0358 ms |
| 旧 Grunt | 8 / 9 / 10 | 16.203 s，至 27.009 s 无新请求 | 9 / 10 / 11 | 约 16.046 s，同帧 ID 12 | 33.7 ms | 0.0338 ms |
| 原生 | 13 / 14 / 15 | 16.167 s，至 27.010 s 无新请求 | 15 / 16 / 17 | 约 16.038 s，同帧 ID 18 | 33.3 ms | 0.0289 ms |

围栏封闭时，三类修复后的小兵均止于三次请求。开门后每类增加一条完整请求，随后靠近并站定射击。玩家在整个试验中固定于 (530, -510, 90.15) cm。第三次请求可能在提交当帧结束，特以原生日志核对了路径类型及 ID。[修复前原始对照](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyPursuitFix05_20261120/Before-Summary.json>) · [通知、查询、修复后 ID](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyPursuitFix05_20261120/After-Reopen-Summary.json>)。

## C／D　目标移动及正常战斗

首轮探针每 0.05 s 注入一次玩家输入，实际位移约 94 cm。补验改为逐帧注入正常角色移动输入。Reopen 场景继续使用原探针节奏。围栏始终封闭，故下表位移恢复时的路径仍允许为部分路径。

| 小兵 | 首次跨过 100 cm 的位移 | 时间 | 同帧新 MoveTo | 最终位移 |
| --- | ---: | ---: | ---: | ---: |
| Art | 103.964 cm | 16.369 s | 6 | 169.001 cm |
| 旧 Grunt | 101.280 cm | 16.366 s | 14 | 167.713 cm |
| 原生 | 103.965 cm | 16.378 s | 22 | 169.002 cm |

三类单兵均完成六个正常移动路线点。小兵绕墙后，在合法射击距离停步，并在玩家退出射程后继续追击。每个单兵场景均观察到 15 枚真实 CastleBolt，最短发射间隔均为 1.900 s，玩家护盾减少 60、生命减少 50。三类各自的三兵路线同时覆盖自然遮挡和视线恢复，九名受测兵均记录到先看到玩家、短时失去视线和再次看到玩家，三组均出现约 0.75 s 的短暂失去视线片段。[位移、战斗及遮挡证据](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyPursuitFix05_20261120/Replay-Summary.json>)。

## E　多兵、拥堵和查询成本

| 三兵正常路线 | 三人同时有活动路径且速度至少 60 cm/s 的采样数 | NewRequest 替换旧路径 | 末帧样本 | 帧间隔 P50 / P95 |
| --- | ---: | ---: | --- | ---: |
| Art | 32 | 0 | 两名可见站射，一名仍追击 | 16.667 / 16.679 ms |
| 旧 Grunt | 32 | 0 | 两名可见站射，一名仍追击 | 16.667 / 16.668 ms |
| 原生 | 32 | 0 | 两名可见站射，一名仍追击 | 16.667 / 16.744 ms |

三组都有一名兵累计三次 Blocked 结束，随后在同次路线中继续移动并重获视线。没有因新请求反复取消在途路径。另在静止玩家的窄门压力场景，第一名 Art 兵耗尽预算后恢复完整请求 ID 28，查询耗时 0.0197 ms。第二名的在途路径随导航更新并继续追击。第三名尚有常规预算，获得完整请求 ID 29 并绕到门口，26.814 s 以 Blocked 结束，在这一摆位的剩余试验时间仍受物理拥堵。此处保留原胶囊与拥堵行为。正常三兵移动路线实测通过。

四次生产恢复查询耗时 0.0197–0.0358 ms。同一三兵开门用例帧间隔 P50 / P95 为修复前 16.667 / 19.968 ms，修复后 16.667 / 16.668 ms。这些是带探针的 D3D11 离屏 PIE 整帧采样，包含单次运行波动及预热开销。三兵开门场景只有一名兵耗尽预算并实际进入新查询，因此未覆盖满队列并发压力。代码上限为每 World 约 10 次恢复查询／游戏秒。[开门统计](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyPursuitFix05_20261120/After-Reopen-Summary.json>) · [三兵路线统计](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyPursuitFix05_20261120/Replay-Summary.json>)。

## F　原 Gameplay 回归

官方 MCP 实际发现并执行了 CastleDemo.EnemyPresentation.SpawnAndDamage。结果 **1/1 通过、0 错误**，耗时约 3.14 s。断言覆盖探索及宝库普通兵、原生回退、精英隔离、攻击与实际 CastleBolt、有效受击、单次死亡通知、立即销毁、击杀和弹药掉落。临时 Untitled World 的 CleanupWorld／EndPlay 警告在本轮及 FIX03 旧日志均出现。原回归测试源码哈希匹配。[本轮原生测试](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyPursuitFix05_20261120/SpawnAndDamage-Official-MCP.json>)。

## 构建、文件保护和恢复

在 Saved/CastleEnemyPursuitFix05_20261120/Project 的完整独立副本中，以普通用户构建 testfordemoEditor Win64 Development。原工程预检时 Plugins 目录已不存在，副本沿用这一实际状态。完整 Target 对当前源码实际编译与链接通过。原项目配置保持原值。初始两次构建因 2.57 GB 共享 PCH 持续磁盘换页而停止，随后采用 Unity、NoPCH 等临时命令行参数。曾有一次 UBT 报告 0 actions，产出仍是基线 DLL，未投入运行验证。刷新隔离源码时间戳并重算构建图后，真实完成 UHT、编译和链接。补验只调整独立探针，两份生产源码 SHA 一致。[实际基线构建](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyPursuitFix05_20261120/Baseline-NoPCH-UBT.log>) · [实际修复版构建](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyPursuitFix05_20261120/Candidate-Rebuilt-UBT.log>) · [补验构建](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyPursuitFix05_20261120/Replay-Development-UBT.log>)。

| 文件 | 修改前 SHA-256 | 修改后 SHA-256 |
| --- | --- | --- |
| Source/CastleDemo/CastleGameplay.cpp | 71134FF3F58D3A774A1D0A61A80A95712C2F0162C8C4FF16ACCD7D6B343C7292 | FCF4F40F25709F796E9C7F8261875389FD2680964468CAD172819D7F8E9BF8A8 |
| Source/CastleDemo/CastleDemo.h | A1F694A3D867FCE6CB6B26DEEA392EC2F3EAE40E6ED6849F204FDB1E13D30FEA | 7ED5E01CB35C3E282253EF1F3F65E4883445E6941904E4CF68252A954733DED7 |
| Source/CastleDemo/Tests/CastleEnemyPursuitFix05Tests.cpp | 本轮新增 | E1BB06A0EFCD7F922B89EA41A7266A72D8C2FBC46FCBC418CBDA9168A6DC71C6 |

生产源码修改前的两个原件已逐个校验，位于 Saved/CastleEnemyPursuitFix05_20261120/Backup。回滚时先核对当前哈希，确认两文件没有后续编辑，再从备份分别恢复两个同名文件，并移除本轮新增的独立探针源码，随后重新编译。正式地图、测试地图、原回归、导航配置、uproject、原 DLL、受核对的美术及 Ragdoll 资产均与预检 SHA 一致。隔离实例已停 PIE 并退出，临时探针没有写盘。用户原编辑器保持原会话，仍加载进入本轮前的项目 DLL，验收在重新构建的独立实例完成。[SHA 对照](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyPursuitFix05_20261120/Protected-After.json>) · [备份](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyPursuitFix05_20261120/Backup>) · [最终工作区](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyPursuitFix05_20261120/Final-Workspace-State.json>)。
