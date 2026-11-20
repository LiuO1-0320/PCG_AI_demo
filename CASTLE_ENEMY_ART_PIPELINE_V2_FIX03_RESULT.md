# CASTLE_ENEMY_ART_PIPELINE_V2-FIX03 验收报告

工程 `E:/Unreal Projects/PCG_AI_demo/testfordemo.uproject`，UE 5.8.4 / CL58902052。测试使用 `L_CastleGruntArt_FIX01`、Seed `24680`、官方 Unreal MCP。死亡专项和三类追击基线在独立 DebugGame Editor 中运行。用户原有编辑器保留。

项目内使用更新后的 `CASTLE_ENEMY_ART_PIPELINE_V2_FIX01_RESULT.md`，其中已有 Director 实际生成、攻击、伤害和旧死亡代理证据。未找到文件名带 `(1)` 的副本。

## A. 死亡系统 — PARTIAL

### 已实施并验证

- `AM_Grunt_Death` 使用 `DefaultSlot`，实际片段为 `0.000–1.100 s`、单次。`AM_Grunt_Hit` 为 `0.767 s`。两份 Montage 文件沿用现有版本。Art 的 `DeathMontage` 原来为空，本轮赋值为完整死亡 Montage。Art 的 `HitMontage` 仍为空。
- 原生 `ACastleEnemy::TakeDamage` 在致死时停止、解除控制并销毁 AI 控制器，调用原 `EnemyKilled`，广播一次 `BP_OnDeathVisual`，然后销毁原兵。Art 创建一个独立代理并传递世界变换、骑士 Mesh 和动画类。代理通过 `Play Montage` 的 `On Completed` 确认自然播完，下一帧保留 `MM_Death_Front_01` 的 `1.100 s` 末姿态（暂停、循环关闭），再调用 `SetLifeSpan(10)`。两份蓝图的播放配置均为 `AM_Grunt_Death`、倍率 `1.0`。要换死亡动作时，需要同步代理配置及末帧序列。
- 首次隔离测试中，直接在完成回调内切换单节点动画触发引擎 `PostAnimEvaluation already in progress`。修正为在回调后的下一帧切换末姿态，下面两次完整重测均结束正常。中断路径安排销毁。缺 Montage 或 Mesh 时终止代理。初始兜底是 15 秒，合法播放中设置为动画预计时长加 12 秒，进入末姿态后由独立的 10 秒寿命接管。
- 单兵与三兵使用真实 Director 生成的 `BP_CastleEnemy_Grunt_Art`，经 `ApplyDamage` 进入原生致死入口。单兵游戏时间 `3.101 s` 进入末姿态，采样在 `12.933–13.119 s` 之间观察到代理销毁。三兵进入末姿态的游戏时间为 `3.096 / 3.748 / 4.399 s`，对应销毁轮询区间约 `12.973–13.173 / 13.547–13.753 / 14.357–14.543 s`。区间是轮询墙钟；独立运行时探针在进入末姿态后约 0.4 秒读到剩余寿命 `9.599 s`。三兵最后无活代理，枪械和代理同生命周期。
- 运行时直接读取尸体枪械 Socket 为 `Weapon_R`，资产是 `SM_CastleRifle`，缩放约 `(2.4, 2.4, 2.4)`。尸体 Mesh 和枪械均为 `NoCollision`、`bSimulatePhysics=false`。玩家金币单兵从 `35` 到 `47`，三兵为 `35 → 47 → 59 → 71`。原兵对象在致死后消失。Art PIE 没有直接采样击杀计数内部值和补给实例；同一原生结算函数另由新 Development 模块中的旧 Grunt 回归测试验证 `Kill +1`、`Ammo +1`。
- 两份蓝图编译成功并已定向保存；Art 的 18 个 BeginPlay 节点及 33 个攻击事件节点对比均无变化。首次隔离编辑器保存遇到 Windows `ERROR_SHARING_VIOLATION (32)`，磁盘 SHA 保持原值。退出隔离进程后，在当前编辑器确认两份资产均无未保存改动，再复现已测试的改动并只保存这两份资产。新 Development 编辑器冷读两份 CDO，确认同一个完整 Montage、倍率 `1.0` 和代理兜底寿命 `15 s`。证据见 `B/Post/A-ArtColdReadback.json`。

### 性能采样

下表为同关卡整段 CSV，单位毫秒。每格为 P50 / P95。结果包含场景其余活动。

| 测试 | 帧样本约 | GameThread | GameThread Animation | Animation Parallel Evaluation Total | GameThread Physics |
| --- | ---: | ---: | ---: | ---: | ---: |
| 单兵 | 775 | 13.194 / 17.354 | 0.455 / 0.730 | 0.318 / 0.432 | 0.048 / 0.072 |
| 三兵 | 880 | 14.293 / 16.757 | 0.491 / 0.770 | 0.453 / 0.630 | 0.037 / 0.073 |

### 布娃娃阻断与最小补齐步骤

骑士 Mesh 的 `PhysicsAsset=None`。官方 MCP 在内存自动生成的候选有 16 个刚体，包括多根扭转骨。候选让小腿扭转直接约束到骨盆、前臂直接约束到 `Spine02`，没有形成所需的膝肘链。官方工具能添加刚体与约束及设置角度，缺少已验证的网格拟合、关节锚点初始化和相邻体禁碰操作。候选未保存、未挂接。当前交付停在动画末姿态，无布娃娃混合、静态地面接触和布娃娃 Chaos 成本证据。

在 PhAT 建一个隔离的 `PA_CastleGrunt_Ragdoll`。显示全部骨骼，按从近到远用 **Create Bodies / Constraints → Create Constraints → Create Bodies with Capsule** 补齐 `L/R_Thigh`、`L/R_Upperarm`、`L/R_Calf`，并核对已有 `L/R_Forearm`、手和脚重接结果。校对 `Pelvis → Thigh → Calf → Foot` 和 `Spine02 → Upperarm → Forearm → Hand`，保留 `Hip → Pelvis` 与 `Hip → Waist` 两支，校准髋部碰撞体和颈部关节。PhAT 独立模拟稳定后，再在代理的真实完成回调后接物理，验证地面阻挡、忽略 Pawn 和约 0.15–0.25 秒的姿态混合。

## B. 追击修复 — PARTIAL

### 已复现的基线和定位

三次 PIE 均由真实 Director 生成，使用同一测试关卡、种子和出生位置 `(2383.825, -268.878, 90.15)`。玩家由原生 `AddMovementInput` 执行同一条 12 阶段路线，覆盖直线、横移、墙角和进出射击距离。Art / 原生 / 旧 Grunt 分别记录 135 / 134 / 135 个样本。

| 版本 | 至少 60 cm/s 行进时 Actor 前向与速度夹角 P95 | 同口径最大夹角 | 隔墙空闲实例 |
| --- | ---: | ---: | --- |
| Art | 51.70° | 103.51° | 约 336–352 cm，14.43–14.84 s |
| 原生 | 52.15° | 64.49° | 约 356–367 cm，14.23–14.84 s |
| 旧 Grunt | 53.38° | 61.74° | 约 366–369 cm，14.51–14.71 s |

Art 在 `13.428 s` 绕墙行进 `67.08 cm/s`，模型 `+Y` 和 Actor 前向重合。Actor 朝玩家的误差仅 `0.15°`，与实际移动方向相差 `103.51°`。原 `UpdatePursuit` 每次调用强制朝向玩家，`MoveToActor` 默认允许侧移，控制器移动焦点也可指向玩家。三种版本均复现，Art 资源坐标和 Mesh `-90°` 配置沿用现状。

记录到的隔墙停步同时满足有效目标、决策范围内、Walking 模式、LOS 为假、速度和加速度为零、PathFollowing Idle、无下一路点。旧 `MoveToActor(P, 320, true)` 包含双方胶囊半径，等高时到达预判阈值约 `320 + 32 + 1.1×36 = 391.6 cm`，在路径求解前即可能把隔墙目标判为已到达。原代码也每 `0.45 s` 重新提交移动请求并可取消在途请求。可见目标在原射程内的正常停步单独记录。本轮基线路线没有复现永久停追，运行时采样未直接读取旧请求的 ID 或结束码。

### 源码修复

普通兵在 Pursuit 中由 CharacterMovement 按移动方向转身。原有 `可见且 ≤450 cm` 条件负责站定，通过 Gameplay 焦点使控制器朝向玩家。发出普通兵追击请求时使用 `20 cm` 到达精度、排除双方胶囊半径、关闭侧移，沿用控制器导航过滤器和部分路径语义。保留活请求及 UE 对移动目标的跟踪；空闲后按 `0.45 s` 节流，同一目标位置最多发三次。目标改变、移动至少 `100 cm` 或从合法射击位置重新追逐时可重新获得请求预算。Boss、精英分支继续用原追击逻辑。攻击结算、弹体生成、冷却和死亡逻辑没有改动。

### 构建及修复后同路线复验

完整 `testfordemoEditor Win64 Development` 两次停在 `Module.RoguelitePrototype.gen.cpp`，未产出新模块，已停止两次本轮构建。随后用同一个 Editor Target 定向构建 `CastleDemo`，临时禁用 Roguelite 插件并使用既有 Castle-only 构建选项。28 项编译及链接完成，`Result: Succeeded`，包含 `CastleGameplay.cpp` 和 `CastleEnemyPresentationTests.cpp`。新隔离进程实查已加载 `Binaries/Win64/UnrealEditor-CastleDemo.dll`，SHA-256 为 `E889EC4562DB39419FDB8F7276862A8E7407A3A1D185AFD18575BF70A6692561`。分别见 `DevelopmentEditorBuild.log`、`DevelopmentEditorBuild-Local.log`、`DevelopmentCastleOnlyBuild.log` 和 `B/DevelopmentLoadedModule.json`。

三类敌人均由真实 Director 生成并跑完路线，记录 135–139 条样本。下表角度继续使用速度至少 `60 cm/s` 的样本。隔墙空闲事件指目标有效、在决策范围内、不可见且敌人零速、PathFollowing 为 Idle 的连续采样段。

| 版本 | 夹角 P95 修复前 → 修复后 | 最大夹角修复前 → 修复后 | 隔墙空闲段修复前 → 修复后 |
| --- | ---: | ---: | ---: |
| Art | 51.70° → 20.98° | 103.51° → 55.45° | 4 → 0 |
| 原生 | 52.15° → 60.59° | 64.49° → 76.52° | 2 → 0 |
| 旧 Grunt | 53.38° → 46.97° | 61.74° → 67.46° | 3 → 0 |

- 可见验收场景的真实 Art 实例在追逐时读回 `bOrientRotationToMovement=true`、`bUseControllerDesiredRotation=false`、Yaw `RotationRate=360°/s`。此次保持引擎转向速度，保留起步和转弯的过渡。
- Visual Logger 直接记录 `BT_CastleGuard` 的活动节点 `5. Face and chase / stop`、可用 NavMesh 路径、普通兵新 `20 cm` MoveTo 请求及实际请求 ID。Art 在 `t=0.417 s` 接受 ID `1`，站定或失去视线后重新追击的已观察 ID 包括 `6 / 8 / 9 / 15`。跟踪的三名小兵分别接受 5 / 5 / 4 条完整路径，未记录活跃路径的 `NewRequest` 中断、`Blocked`、`OffPath`、`Invalid` 或 `AlreadyAtGoal`。观察到的活动路径中止为站定或 Hold 的 `MovementStop`。Art 仍能停步射击，例如一次站定约 `4.14 s`，之后恢复移动。旧 BT 节点说明仍显示原阈值，实际请求以运行时记录为准。
- Art 行进中的转向偏差下降，低速重启时仍抓到短暂大角度偏差。原生兵本次的 P95 反而上升，旧 Grunt 的最大偏差略升。不可达静止目标的三次预算已由源码核对，未做独立不可达场景的 runtime 计数。未做多人拥堵专项。本轮追击标记 `PARTIAL`。
- 真实炮弹按 Owner 和 Actor Age 记录。Art 观测连续弹体间隔为 `1.919 / 1.910 / 1.912 / 2.159 / 1.927 s`。原生和旧 Grunt 也有约 `1.9 s` 的连续发射。采样可能漏掉很快销毁的弹体，旧基线未记录相同口径的弹体时间戳。源码攻击间隔 `1.9 s`、弹体、伤害与死亡结算保持原逻辑。

三类复测数据见 `B/Post/RouteSummary.json` 和 `B/Post/Trial-*.log`。Art 单独分析见 `B/Post/RouteSummary-Expanded.json`。原生 Visual Logger 及 `*-CastleNavigationExtract.json` 位于 `B/Post/SandboxUser/Saved/Logs/`。

原生 `CastleDemo.EnemyPresentation.SpawnAndDamage` 在新 Development 模块中实际发现、执行并通过，测试数量为 1。旧 Grunt 基线记录一次攻击、一次受击、一次死亡、一个弹体、一次击杀结算及一个补给掉落，空死亡槽不产生代理。测试含原兵立即销毁、精英生成隔离和死亡代理不参与 Gameplay 碰撞的断言。Art 的实际完整动画和持枪死亡代理单独由 A 项运行验证。证据见 `Native-SpawnAndDamage.log`。

## C. 文件安全与回滚

| 修改文件（相对工程根目录） | 修改前 SHA-256 | 修改后 SHA-256 |
| --- | --- | --- |
| `Content/Art/Grunt01/IntegrationV2/BP_CastleEnemy_Grunt_Art.uasset` | `259C000FFFF9E21669056E7B439DEEE3A8A7098070DA563F7E2F87401578C433` | `D88241F934F5C3D7C331BE06BEA3A458ADC7621C1AC223F79AF10BA2B283DE5A` |
| `Content/Art/Grunt01/IntegrationV2/BP_CastleEnemy_DeathVisualProxy_Art.uasset` | `7BDCF80B9F9A8A3FAB6B1C0652E8E87E048115D86C9216619B87574DB2824D02` | `6D1E6DF29D437B741AA0FEB67FFC51E10B5962A1F2A988087799823E293519C0` |
| `Source/CastleDemo/CastleGameplay.cpp` | `798F5EAA8EBD620BA844058A9D835A72BA3FC65C0E2C51846FE66030097C349E` | `4AA3BC8BE057C1A066F2DF9B436BD56A6C4038E948E3D4C09B8C445E38B1E768` |
| `Source/CastleDemo/CastleDemo.h` | `07C71213E1DAC12AEDABE67E7C73FF824A5FDECECBEAA40A51D5DAD3ADD75AE4` | `2D378CA6B2662DED88E7ED44496F2A107FF08E2E057221C68FC0557A4B48119F` |

10 个受核对文件的 SHA-256 前后一致，包括测试地图、正式地图、项目描述、`CastleAI.cpp`、角色 Mesh / Skeleton、枪械、动画蓝图和两条 Montage。测试探针只存在于隔离编辑器内存。地图与 Director 的持久配置保持原版本。`git diff --check` 通过。

备份位于 `E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_FIX03_20261120/Backup/`。回滚前结束测试 PIE，检查当前文件是否仍为本表改后 SHA，并保留之后出现的用户改动。只回滚死亡表现时，卸载并替换两份 `.uasset`，然后重新打开资产。只回滚追击时，使用 `Backup/Source/CastleDemo/` 的两份原文件逐个替换，并重新构建 Editor Target。其他资产、地图或 Git 状态都无需参与回滚。

主要证据位于 `E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_FIX03_20261120/`。死亡证据包括 `A-Single.json`、`A-Three.json`、`A-SocketLoot.json`、`A-CsvSummary.json`、`A-GraphScope.json` 和 `PhysicsCandidate.json`。文件保护证据见 `AfterComparison.json`。追击基线包括 `B/RouteSummary-Expanded.json` 与三个 `B/Trial-*.log`。CSV 原件位于 `E:/Unreal Projects/PCG_AI_demo/Saved/Profiling/CSV/`，文件名 `Profile(20261120_170713).csv` 和 `Profile(20261120_170749).csv`。

## 人工验收

新开的 Development 编辑器已加载 `L_CastleGruntArt_FIX01` 并暂停在可玩 PIE。已确认玩家生命 `100`、护盾 `60`，以及正式骑士 Art 实例和完整死亡 Montage。原编辑器保持打开。点 **Resume / 继续** 可以从入口开始。先沿墙横移、绕过拐角，再进出射击范围观察追击和站定。击杀骑士后可观察完整倒地、右手持枪、末帧停留约 10 秒。近距离的低速转身和倒地衔接仍需人工美术确认。可见窗口的证据见 `Handoff-Visible.json`。
