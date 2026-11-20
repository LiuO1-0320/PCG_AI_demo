# CASTLE_ENEMY_ART_PIPELINE_V2-FIX04 验收报告

工程 `E:/Unreal Projects/PCG_AI_demo/testfordemo.uproject`，UE 5.8.4 / CL58902052。预检读取了 FIX03 报告、AGENTS.md 和工程 SOP。官方 Epic MCP 完成 `initialize`、工具发现和真实属性、地图、PIE 状态回读。测试使用新开的隔离 Development Editor 与测试地图 `/Game/Art/Grunt01/IntegrationV2/Tests/L_CastleGruntArt_FIX01`，追击种子 `24680`。

## A. Physics Asset 核查 — BLOCKED

`/Game/Art/Grunt01/PA_CastleGrunt_Ragdoll` 确实存在，Solver 为 `World`。资产预览指向 `SK_CastleGrunt_Corrected`。实际 Art 小兵的 `CharacterMesh0` 使用该 Skeletal Mesh，并且 `PhysicsAssetOverride=None`。原编辑器和隔离冷启动编辑器均读到 Mesh 的 `PhysicsAsset=None`，专用 `get_physics_asset` 同样返回未分配。Asset Registry 只记录 PA 对 Mesh 的预览引用，Mesh 的依赖没有这份 PA。

| 校验项 | 专用物理工具读回 |
| --- | --- |
| 骨架及枪械 Socket | 41 根骨骼，`Weapon_R` 绑定 `R_Hand` |
| 物理刚体 | 15 个均为 `Default`，每体一个胶囊 |
| 主要关节骨链 | 两侧 `Thigh`、`Calf`、`Upperarm` 主骨缺少刚体。两侧小腿扭转骨还直接与骨盆约束，两侧前臂还直接与 `Spine02` 约束 |
| 错误配对 | 19 条约束中存在 `Head → R_ThighTwist01`，头部原有颈部约束仍存在。部分肢体刚体同时有直接跨关节约束和额外肘膝约束，形成多余闭环 |
| 髋部形状尺寸 | `Hip` 胶囊半径与柱段均为 `0.505 cm`，相邻 `Pelvis` 胶囊半径 `10.605 cm` |

隔离 PhAT 窗口已显示真实骑士、15 个胶囊和 19 条约束。官方窗口操作后 `Simulate` 仍读回未开启，因此当前物理稳定性、自碰撞和地板接触为 `NOT_TESTED`。现存截图记录骨架和刚体预览。原始数据见 `A-PhysicsColdRead.json`，窗口状态及截图见 `A-PhAT-Preview.json`、`A-PhAT-01-Reference.png`。

关联与关节配置均未通过 Phase A。后续修正应在 PhAT 核对真实上臂、前臂、大腿和小腿关节，移除头部到右大腿的异常配对，校核髋部形状和相邻刚体碰撞。在 Mesh 的 **Asset Details → Physics → Physics Asset** 保存绑定后，重新冷加载并复查 World Solver 的实际模拟。

## B. 死亡动画转 Ragdoll — BLOCKED

按照 Phase A 门禁保留现有代理。官方 MCP 核对 `OnCompleted → DelayUntilNextTick → MM_Death_Front_01 末姿态 → SetLifeSpan(10.0)`，死亡 Montage 为 `AM_Grunt_Death`、长 `1.100 s`、倍率 `1.0`，兜底寿命 `15 s`。用户当前编辑器内的死亡代理有未保存改动，原样保留。磁盘代理 SHA 与 FIX03 相同。

Phase C 的 Ragdoll 接管、地面接触、Pawn 通行、枪械在物理骨骼上的跟随、三具物理尸体清理和 GameThread / Chaos / Animation 成本均为 `NOT_TESTED`。FIX03 的末帧降级行为继续生效；其持枪、单兵与三兵约 10 秒清理和性能数据属于上轮基线。

## D. 追击专项回归 — PARTIAL

三类普通兵均由真实 Director 生成。标准路线复用同一地图、种子与输入。围栏试验在隔离 PIE 中放置临时静态障碍，玩家用正常移动穿出，无传送或强制 Actor 转向。Visual Logger 记录实际 MoveTo ID、结束结果、目标距离及 NavMesh 结果，轨迹探针记录速度和 Actor 前向。

| 专项 | Art | 原生普通兵 | 旧 Grunt |
| --- | ---: | ---: | ---: |
| 静止玩家可达时，跟踪兵从首次采样起的移动距离 | 约 1,375 cm | 约 1,424 cm | 约 1,403 cm |
| 速度至少 60 cm/s 时，前向与速度夹角 P95 | 29.84° | 64.10° | 64.44° |
| 三兵同时移动的采样数 | 114 | 120 | 129 |
| 三兵移动时最小胶囊中心间距 | 72.004 cm | 72.144 cm | 72.005 cm |
| 固定不可达时的前三次部分路径 MoveTo ID | `111 / 112 / 113` | `142 / 143 / 144` | `179 / 180 / 181` |
| 玩家达较大位移后的首个新 MoveTo ID | `116`，16.523 s | `147`，16.495 s | `184`，16.497 s |
| 玩家正常走出围栏后，首条完整路径时间 | 28.260 s | 27.938 s | 27.984 s |

- **静止可达和射击距离通过。** 玩家保持 `(530, -510, 90.15)` 直到约 `6.62–6.78 s`，三类跟踪兵均以完整路径逼近，跟踪距离约由 `1,869 cm` 降至 `501–503 cm`。三类均观察到近距可见时停步、玩家离开后恢复移动。标准路线举例，Art 在约 `408 cm` 站定，首次再次满足追击条件的采样 `9.709 s` 已恢复移动。采样只能确定恢复发生在相邻采样区间内。
- **固定不可达和目标移动恢复通过。** 三类小兵各完成三条部分路径，PathFollowing 记录 `Success[Success]`，到达的是部分路径终点。玩家小幅移动 `37.60–38.85 cm` 后约 `2.8 s` 的停留中没有第四次请求。后续较大移动阶段最终位移为 `157.38–157.74 cm`，三类都重新发起导航，并在玩家走出临时围栏后拿到完整路径。
- **三兵拥堵和绕障已观察。** 三类同类小队各自跑过同一周边障碍，贴近至约两个 `36 cm` 胶囊半径的距离，之后继续移动。角度仍有短暂大偏差，原生和旧 Grunt 的夹角 P95 均超过 `60°`。本轮只做回归，转向代码没有变更。
- **静止目标的路径重新开放失败。** 独立 Art 测例复现，详见下面的时间线。原生与旧 Grunt 本轮完成固定障碍及移动恢复对照，路径重开而玩家原地不动的负例仅在 Art 复现。

核心数据在 `D/Decision-D-Evidence.json`、`D/Route-D-Summary.json` 和 `D/Native-D-Summary.json`。逐试验日志、原生 `.bvlog` 和临时围栏参数由 `D/Artifacts-D.json` 索引。

### 已复现的新停追缺陷

Art 测例维持玩家位置 `(530, -510, 90.15)`。普通兵 `_C_1` 依次接受部分路径 ID `106 / 107 / 108`，提交时间约 `0.325 / 6.874 / 7.333 s`。三条请求均以 `Success[Success]` 结束于部分路径终点，属于寻路请求的成功完成。玩家和小兵相距仍约 `249.23 cm`。

在 `15.984711 s`，真实导航查询仍为 `Valid=true, Partial=true`。测试探针约在 `16 s` 只移走自建围栏的西侧墙。`16.203746 s`，同一位置的真实查询变为 `Valid=true, Partial=false`。随后玩家仍静止，黑板目标有效、`LOS=false`，小兵处于 `Walking`，速度和加速度为零，PathFollowing 为 `Idle`。到 `25.729367 s` 未出现新的 MoveTo，继续停在 `(779.204, -506.525, 90.15)`。

原因定位在 `ACastleEnemy::UpdatePursuit`。现有分支只在更换目标、累计位移达 `100 cm` 或进入可见射击位置后重新离开时取得新的预算。目标不动且仍无视线时，导航从部分路径变为完整路径也无法越过 `PursuitRequestsRemaining <= 0`。证据包含真实的 `FindPath`、Visual Logger 和三次历史请求完成记录。复现使用临时测试墙，未调整正式导航或视线规则。[CastleGameplay.cpp](<E:/Unreal Projects/PCG_AI_demo/Source/CastleDemo/CastleGameplay.cpp:508>)

候选修复范围只包括 `Source/CastleDemo/CastleGameplay.cpp` 中的普通兵追击分支，以及 `Source/CastleDemo/CastleDemo.h` 中用于记录该兵导航重建状态的少量字段。以实际使用的 NavData 更新为触发，在预算已耗尽时做一次受限的完整路径确认，确认可达后再补发请求。同一不可达状态仍保留三次上限。Boss、精英、攻击、伤害、死亡结算与碰撞配置继续使用现有实现。本轮已提交定位和范围，C++ 保持现有版本。

## E. 构建、文件保护和回滚

- 完整 `testfordemoEditor Win64 Development` 仍为 **BLOCKED（既有）**。FIX03 两次全工程尝试停在 `Module.RoguelitePrototype.gen.cpp`。本轮没有 C++ 变更，保留原始失败过程日志，未重复全量构建。追击使用 FIX03 定向构建产物，独立进程实际加载 `UnrealEditor-CastleDemo.dll`，SHA-256 `E889EC4562DB39419FDB8F7276862A8E7407A3A1D185AFD18575BF70A6692561`。
- 构建日志位于 `E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_FIX03_20261120/DevelopmentEditorBuild.log`、`DevelopmentEditorBuild-Local.log`。Castle-only 成功记录为 `DevelopmentCastleOnlyBuild.log`。隔离进程的临时插件选项不写入 `.uproject`、正式插件配置或 Roguelite 源码。
- 本轮工程资产和 C++ 无变更。14 个保护文件按本轮开始时的 SHA 核对，包含物理资产、用户 Mesh、两份表现蓝图、Montage、Skeleton、枪械、正式及测试地图、`.uproject` 和相关源码。基线、运行证据和最终文件对照保存在 `E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_FIX04_20261120/`。
- 不需要回滚工程文件。测试中使用独立进程和不保存的测试对象。若需复查现场状态，按证据目录的 `Before.json` 和 `After.json` 对照；遇到后续用户新改动时保留当前文件。FIX03 的旧代理备份不用于覆盖此次用户未保存的内容。

| 关键保护文件 | 修改前后相同的 SHA-256 |
| --- | --- |
| `PA_CastleGrunt_Ragdoll.uasset` | `26BB52495A346956E5912659016984B5E43D72969D9B320B29CA174AF9840636` |
| `SK_CastleGrunt_Corrected.uasset` | `66CD9E9F71A503B51843120CDC9DE4EADCAEE476083BF1EBC85198F9D2AF8128` |
| `BP_CastleEnemy_DeathVisualProxy_Art.uasset` | `6D1E6DF29D437B741AA0FEB67FFC51E10B5962A1F2A988087799823E293519C0` |
| `CastleGameplay.cpp` | `4AA3BC8BE057C1A066F2DF9B436BD56A6C4038E948E3D4C09B8C445E38B1E768` |
