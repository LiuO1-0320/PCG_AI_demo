# 技术架构、模块职责及接口合同 v2.1

> 更新：2026-10-12，DOC-UPDATE-01。除明确标注 P01–P03-A 已交付的类型外，本文 C++ 类/资产及事件名均为**建议命名/接口语义**，不是实现声明。新系统仅在 RoguelitePrototype 和 `/Game/Roguelite` 内建设，优先复用该独立系统，不依赖 CastleDemo 游戏类。旧“必须复用旧项目游戏基础设施”规则已 Superseded。

## A. 分层与单一职责

```text
RunSession (run seed / room graph / per-run state)
   |
   +-- DungeonDirector (logical graph + mode dispatch + overlap/backtracking)
         |
         +-- Connection records (Room A/B + Socket A/B + mode + validity)
         |     |-- Direct assembly (P03-A: retained low-level/test capability)
         |     '-- Parametric corridor (planned: straight first; formal traversal)
         |           '-- Fog transition / readiness wait (planned: later task)
         |
         +-- RoomInstance / ParametricRoom (shell / doors / hand POIs / zone volumes)
               |
               +-- RoomPCGCoordinator
               |     |-- PCG_RoomCover   -> physical cover + candidate firing points
               |     |-- PCG_EnemySpawn  -> validated spawn point + faction/type metadata
               |     '-- PCG_RoomProps   -> eligible props / reward position markers
               |
               +-- RoomEncounterDirector (phase / doors / registration / clear / rally)
                      |-- AIController + BT/BB/EQS
                      |-- Weapon / Health / Faction
                      '-- SquadManager (follow / defend / downed / revive)

RoomLootResolver (profiles / weighted picks / claims) <--> RoomEncounterDirector
Boss-adapter events only                         <--> RoomEncounterDirector
```

图中为目标职责划分，不要求每个节点新建一个 Manager/空类。迷雾视觉、房间内容准备和正式入房门控分别承担职责，不把雾当碰撞或战斗许可。

### A0. 已交付连接数据与走廊扩展边界

- 已有 `ARogueliteParametricRoom` 与 P02 Socket 查询，提供门槛局部/世界位置、朝外方向、净宽/净高及合法状态；继续复用，不重做 Socket。
- 已有 `ARogueliteRoomAssembly` / `FRogueliteRoomConnection` 记录两个 Room、各自 Socket、连接 GUID、模式、预约状态、几何有效状态、导航结果与诊断。预约与有效性分开，保存/重载后的旧检查结果不等于当前有效；详见 P03-A 原始报告。
- `ERogueliteConnectionMode` 已含 Direct、StraightCorridor、CornerCorridor、CustomConnector；**目前只有 Direct 实现**。P03-A 的 Direct 对齐要求端点重合、法线相反、两房外墙直接邻接，仅限该模式。
- 正式可通行地牢连接须通过参数化走廊；未来 StraightCorridor 使用端点和门规格，而不要求两房端点重合。可为连接追加连接器引用/参数；固定两端的连接器创建入口与现有“移动目标房间”的 Direct API 区分，不能声称已有 API 已完成走廊。
- P03-C1 只记录基础水平直线走廊范围护栏；本地正式任务正文未找到。迷雾视觉、异步准备/等待、入房战斗门控、完整模板 POI 刚性装配均不得借文档更新塞入该任务。其他模式只保留扩展类型，不建立大量空类。

### A1. 数据资产（建议）

`DA_RoomDefinition`：RoomType、外壳参数默认值、Door Socket 模板、可用 POI/PCG 区域、可用门数量与生成权重。  
`DA_EncounterProfile`：预算、敌军构成、区域/簇权重、难度、随机种子偏移。  
`DA_RoomLootProfile`：场景物资、敌人掉落、清关奖励、权重与领取条件。  
`DA_AICombatTuning`：攻击距离/速度、弹匣、冷却/EQS 间隔、Follow 半径、倒地计时。  
`FRuntimeRoomState`：本局特定实例的 ID、PCG Seed、RoomPhase、已生成敌军名单、奖励领取状态、门锁状态。

后续走廊参数：独立长/宽/高、两端 Room/Socket 引用和必要净距；Fog Transition 配置：局部雾强度、遮挡/揭示距离与曲线、等待反馈。穿行 5–10 秒仅为待测试体验目标，雾与性能参数未定值。数据可组合使用，不新增虚假的已实现类型。

**注意：** Blueprint/DataAsset 可编辑与保存，需要 UE Editor 或可用 MCP 能力；不能仅写一个 struct 就声称“关卡策划可以直接使用”。

### A2. 旧随机拓扑的复用评估

后续可只读评估 CastleDemo 的 Seed、节点/边、连通性和有限回退纯逻辑，记录依赖及解耦成本。可复用性尚未验证，迁移需独立授权；不得在新模块 include/link 旧房间生成、AI、PCG 或 Boss。旧地图的格子尺度、楼层、房型分布和传送流程不自动成为本游戏规则。

## B. 事件合同（语义先于函数名）

| 发布方 → 接收方 | 事件名（建议） | 数据/要求 |
|---|---|---|
| Dungeon → Room | `OnRoomPlaced` | RoomInstanceId / door pairs / transform / seed |
| Room → PCG | `RequestRoomGeneration` | Bounds / cover zones / exclude / current entry socket |
| PCG → Room | `OnGenerationDataReady` | candidate points / cover generation status / seed / warnings |
| Nav validator → Room | `OnNavigationValidated` | 必要房间/走廊路径可达；失败含具体原因 |
| Room generation → transition | `OnRoomContentReady` | 外壳、POI、掩体、候选点、普通敌军及必要导航全部就绪；不能仅看 PCG 完成或秒数 |
| Player → transition | `OnPlayerEnteredConnection` | 进入走廊/安全等待；不正式切房、不触发旧房 Downed 遗弃 |
| Room entry → Encounter | `OnRoomEntrySecured` | 正式入房、可行动队友安全进入、入口已安全封锁 |
| Room → Encounter | `OnPreparationReady` | 内容就绪且入房安全条件完成；敌军已生成/注册/休眠，队友 Follow/Defend 禁火 |
| Player weapon → Encounter | `OnPlayerHitCurrentRoomHostile` | shooter / hit target / room ID; idempotency guard |
| Encounter → AI + door + Squad | `OnCombatStarted` | 当前 Preparation 房首枪命中；锁全部有效门、检查物理封门安全、AI/队友火力启用；入口已在 Prep 前封锁 |
| Enemy Health → Encounter | `OnHostileNeutralized` | once per enemy ID, LifeState and faction validated |
| Encounter → Loot + doors | `OnRoomCleared` | one-shot clear; reward claim eligibility |
| Room transition → Squad | `OnPlayerEnteredNextRoom` | 正式切当前房：取消旧 Defend 恢复 Follow，遗弃上一房未救 Downed；随后完成可行动队友入房，再封入口/进 Prep |
| Boss adapter → Encounter | `OnBossDefeated` | event acknowledgment, no direct Boss BP edits |
| Run lifecycle → systems | `OnRunEnded` | reset in-memory squads, rewards, route; no SaveGame |

**幂等保证：** 一个攻击命中多次通知不会重复 StartCombat；一个敌人多次 OnDeath/OnDowned 不会让敌军数为负；战利品只生成/领取一次；重复进入房间不会重跑 PCG。

正式入房与走廊进入须为两个独立事实；精确触发体积/边界尚待后续任务确认。上表新增事件均为规划合同，尚无新增 C++ 实现。入房/遗弃重复通知也须幂等，已 Cleared 重访不重启首次战斗流程。

## C. 两阶段生成与运行准备

**Phase G1 — 物理内容：** 房间/走廊变尺寸外壳完成 → Socket 接口和 POI → PCG 掩体 → 碰撞/主要通路检查 → 失败回退。  
**Phase G2 — 战斗与玩法：** PCG 敌人候选点 → Nav/Spawn 合法性核查 → C++ 实例化并注册全部休眠敌人 → PCG 物资与 Reward markers → 等待并验证必要导航 → 内容 Ready。  
**入房安全阶段（与生成完成分离）：** Ready → 玩家正式入房/旧房 Downed 遗弃 → 可行动队友安全入房、Defend 转 Follow → 安全封锁入口 → 无限 Preparation。

技术事件应代表“确实发生的事实”：PCG 图运行结束 ≠ 导航可用；导航数据已更新 ≠ 所有军种可在指定空间找到合法位置。PCG 管理者必须能等待与超时、记录失败原因、使用安全 fallback。不得让单个空出生点导致房间永远不加载。

### C1. 迷雾缓冲与安全等待（后续任务，未实现）

- 迷雾走廊允许自由观察/移动，按位置渐进遮挡前后房间、接近出口渐进揭示；约 5–10 秒为待测体验目标，不修改玩家速度、没有固定倒计时，不证明加载足够快。
- 内容未 Ready 时保持安全区域和遮挡，由独立准入逻辑/碰撞拦住未准备出口；等待/超时/失败与恢复须显式诊断。队友安全条件未完成时不能进入 Prep 或关门夹人；具体安全位置/回退/UI 待定。
- 局部雾只负责视觉；攻击许可、碰撞响应、射线/弹体命中检查分别验证，不能用雾材质充当墙体或伤害规则。走廊内不允许提前触发下一房战斗/遗弃。
- 雾强度/范围/渐隐曲线与 CPU/GPU/帧时/内存预算待目标机器实测。单层 Fog Gate 为 **Superseded**，不再作为待实现默认方案。

**Superseded：** 旧 G2 直接进入 Prep、Prep 入口保持开放及首枪时才安全集结的合同，替换为内容 Ready 与入房安全两道门槛；Combat 不能回退 Prep。

## D. 参数化门墙生成

- 从中心与 RoomLength/Width/Height 计算四面墙分段；在 Door Socket 附近将墙段拆分成左右两段与门楣（若适用），避免整墙仅使用一个巨大缩放 Mesh。
- 门洞属于墙的局部坐标与外法线；手动偏移相对墙中心；计算尺寸变化后的有效区间，避免角落墙短于最小支撑。
- 当前房间整体旋转后，Socket 世界坐标/法线也必须旋转；同一个门口不可连接两次。
- 封堵未使用门洞应兼顾碰撞、可见性与 NavMesh。已连接门洞不应被“封堵模块”误盖住。
- 碰撞检查排除共享门缝的合法边界接触；要检查墙体实体阻挡，而不仅是 Bounds。

### D1. 水平直线走廊基础合同（规划）

厘米参数长/宽/高独立编辑，合法范围和端点规格检查，不以 Actor 整体 Scale 拉伸。用参数化占用检查房间/走廊重叠，实际检测两端开口、两侧墙、顶部、地面接缝与胶囊通行；必要导航另验。属性改变不重复创建组件或遗留旧碰撞，不移动作者 POI。未来完整模板装配的局部坐标刚性移动单独开发；目前 P03-A 拒绝移动携带手工 POI 的目标房间。

## E. PCG 输出契约

| Graph | 最小输入 | 输出 / 使用者 | 验收前置 |
|---|---|---|---|
| `PCG_RoomCover` | 房间 Bounds、Open/Cover Zones、POI/通道排除、Seed、掩体库 | 掩体实体、有效射击站位候选；供 AI/EQS 使用 | 出入口通路完整，不侵占手工 POI |
| `PCG_EnemySpawn` | 同房间 Bounds、入口敌军禁止区、Cover 结果、EncounterProfile | `SpawnPoint` 含 `EnemyArchetype`、`ClusterId` 等属性；供 C++ 使用 | 候选在 NavMesh/合法碰撞区内，间距满足 |
| `PCG_RoomProps` | LootProfile、Open/Cover/Exclude Zones、固定奖励锚点、Seed | `PropPoint/RewardPoint`；由 LootResolver/Spawner 使用 | 道具可拿到，不埋墙，不堵路 |

**重要：** 只有一条生成路径可以真正 Spawn Enemy Actor；PCG 与 C++ 不能同时 Spawn 同一逻辑敌军。PCG Meta 属性名需要在工作开始后根据 Graph/engine API 实证确定。

## F. AI 模块（建议资产）

- `BB_Combat`: TargetActor、CombatEnabled、CurrentOrder、FiringPoint（具体键类型在编辑器中校验）。
- `BT_MeleeEnemy`: NotCombat -> Idle/Stop；Target invalid -> Acquire；近距 -> Attack；否则 MoveTo。
- `BT_RangedEnemy`: NotCombat -> Idle；Target invalid -> Acquire；MagazineEmpty -> Reload；Near + LOS -> Fire；Far/NoLOS -> EQS when allowed -> MoveTo -> Fire。
- `BT_Companion`: Downed -> Stop；transition -> Rally；Preparation -> Follow/Defend no fire；Combat -> Defend/Follow and autoattack。
- `EQS_FindFiringPosition`: 候选 Nav 可达、目标线性视野、最佳攻击范围与移动成本，检查遮挡/点占用；不能在 Dormant 状态第一次查询。
- 伤害层验证阵营，再由射线过滤相同 Friendly，避免不同 Weapon/Projectile 分支产生不一致友伤结果。

## G. 编辑器资产所有权与并行开发

| 资产类别 | 建议 Owner | 需要避免的冲突 |
|---|---|---|
| C++ 房间接口、Door / DungeonDirector | PCG/基础设施负责人 | 修改 AI/Boss 功能 |
| 三套 PCG Graph 与灰盒房 | PCG/关卡负责人 | 自动覆盖已手工设计的 POI |
| 近战/远程 BT、Squad 和 RVO | AI 负责人 | 重复 Health、Weapon、Controller |
| Boss BT/BP/关卡 | Boss 负责人 | Codex 无授权写入 |
| 美术、UI、奖励表 | 设计/美术协作 | 未同步字段和资源命名 |

首次提交必须建立 Git 基线，任何 `.uasset` 修改先经 Editor/MCP 安全确认保存。不同组员最好独立工作分支或按地图/资产互斥编辑，避免二进制资产合并冲突。

## H. 性能、异常与降级

- 使用分房/当前房间目标注册表；EQS 与导航请求按事件及最小间隔执行；批量 AI 进入 Combat 时同步状态，错峰高开销查询。
- 先保证 2–6 友军与约 4–15 普通敌军的灰盒体验，再测 10 与 50 的压力目标；“50 同屏”不是保证指标。
- 若密集空间 Follow 队友阻塞，先调 Formation Slots + RVO，再考虑仅玩家/友军间的选择性物理穿行。
- 不因卡路而静默删除敌军；受控恢复、重新找点、报告调试状态；清怪零数必须真实。
- 记录 Seed / RoomID / Graph Version / Template ID / Source Commit / 平均和峰值 Frame Time。
- 雾与准备过程追加走廊/连接 ID、Ready/等待/失败事件时刻及 CPU/GPU/内存成本；具体预算待测试，超出体验目标不能越过安全门槛。
