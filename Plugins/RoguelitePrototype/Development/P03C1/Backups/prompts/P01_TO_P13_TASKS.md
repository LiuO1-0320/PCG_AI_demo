# Codex Work 分段实施提示词 P01–P13｜v2.1 开发计划

**使用规则：** 不要一次性把下面全部内容作为一个任务发送。完成上一阶段、完成真实编译/PIE 验收和 Git 基线后，复制**一个已批准范围**的任务正文给 Codex。遵循根 `AGENTS.md` 与 `Docs/03_DECISIONS_AND_OPEN_QUESTIONS.md` 的 Locked v2.1，再读取 `Docs/01_FINAL_DESIGN_SPEC.md`。本文件于 2026-10-12 由 DOC-UPDATE-01 增量更新，仅为计划，不授权开始开发；历史已完成任务不重复执行，不覆盖 P01/P02/P03-A 用户地图。

**Superseded：** 旧 P02 将 Direct 拼接混在门洞任务内、旧 P03 将无走廊直接拼接作为正式地牢默认、旧 P07 将 Prep 入口开放直到首枪集结/封门，均不再指导未来开发。P02 实际只完成 Socket，Direct 实际交付属于 P03-A，其代码/地图/验收保持不变。

## 通用首行（每次任务可加）

> 请执行当前获授权任务；先检查 RoguelitePrototype 与 `/Game/Roguelite` 的相关实现，优先复用其已验证能力及 UE 原生功能，不依赖 CastleDemo、不更改 Boss/旧 Demo/默认设置。采用项目 Skill `unreal-workflow`，先给最小方案后实施；不要引入任务范围外功能。给出实际修改列表、编译/MCP/PIE 的真实证据及人工步骤，结束后停下。文档维护任务不编译、不调用编辑器写入或运行验证。

---

## P01｜参数化房间（第一项实际开发）

**给 Codex：**

```text
任务 P01：仅实现可在 UE 5.8.3 Editor Details 修改长宽高的参数化矩形房间（建议 BP_ParametricRoom；若项目已有 Room Actor 请复用）。修改 RoomLength/RoomWidth/RoomHeight 时自动重建地板、外墙和碰撞边界，不能仅把整个 Room Actor 等比/非等比缩放。手工放在房间中的 POI、墙壁、凹槽、预制结构保持原模型尺寸、不被自动删除或强制移动；缩小房间让其越界时显示警告。参数可编辑，施工与运行逻辑最小化。本轮不做地牢拼接、不做 PCG 图、不做 AI。若 MCP 不支持所需资产配置，请给明确人工步骤。真正编译（若本机可用）并提供在新建灰盒地图中从 20x20m 改到 30x25m 的可视化 PIE/Editor 测试。
```

**必须过关：**房间尺寸实时/明确触发更新，POI 不拉伸，墙体碰撞合理；未支持动作被清楚标记。

## P02｜可编辑门洞与 Door Socket（已交付；仅作历史范围参考）

```text
任务 P02：在 P01 房间上实现四侧启停门洞、自动居中/ManualOffset、净宽净高、真实开口和碰撞；每侧首版一个门，非法配置保留设计值并警告。提供局部/世界门槛位置和朝外方向，支持直角旋转及属性修改后更新。保留 POI 与固定组件，不做两房拼接、门体/战斗门控、走廊、PCG 或 AI。
```

**历史验收边界：** 门洞偏移、逐侧启停、开口/实体墙碰撞、保存重载及 P01 回归；结果见原 P02 报告。拼接属于 P03-A；正式地牢未用门封堵属于后续集成，不补记已实现。

## P03-A｜Socket Direct 两房拼接（保留已交付成果）

使用原 `ARogueliteRoomAssembly`、P02 Socket、连接模式/预约/有效状态及原专用测试地图。历史编译/MCP/胶囊/PIE 证据见 `Plugins/RoguelitePrototype/P03A_RESULT.md`，导航不可用及人工待验收项保持原记录。Direct 保留为底层和测试模式，不删除、不重写，不代表正式走廊连接已完成。

## P03-C1｜参数化水平直线走廊基础（范围护栏，非新增执行指令）

用户指定当前 P03-C1，但本地没有正式任务正文或执行记录。这里只约束基础走廊方向：独立可编辑长/宽/高、水平直线外壳/碰撞、复用已有 Socket 与连接数据、两端合法性及基础通行检查。具体类名、接口、测试地图与完整验收须按另行批准正文确认，不以此替代或扩大已有授权。

不得在 P03-C1 加入 Fog Transition 视觉、异步房间准备/安全等待、战斗入口封锁/Encounter、完整模板 POI 刚性装配、随机拓扑、PCG、AI 或 Boss；不得借新全局规范执行这些后续模块。本轮 DOC-UPDATE-01 未实现或测试走廊。

**后续依赖顺序（任务 ID/范围待独立批准）：** 基础直走廊 → 连接模式集成及通行/导航验收 → 随机图与布局 → PCG/敌军内容就绪合同 → 迷雾过渡与安全等待 → P07/P10/P11 的正式入房/队友/门控集成。可以按批准任务调整顺序，不把整条链当作一次实现授权。

## P03｜有主路线与分支的地牢生成

```text
任务 P03：在经验证的 P02 Socket、连接数据与水平直线走廊基础上实现独立 DungeonDirector 最小随机生成器。先生成连通逻辑图（起点→普通战斗房→Boss终点，可选商店/奖励/战斗支线），再选参数化房间/方向及走廊连接；所有正式可通行房间边都有走廊。限水平 0/90/180/270 旋转，按实际端点和走廊长度布置，不要求两房端点重合。每次放置验证房间/走廊占用、碰撞、胶囊与必要导航；有限重试/回退、明确失败与安全备用路线。Seed 可复现，未用开口封堵。可先另开只读任务评估 CastleDemo 纯拓扑逻辑，但不得引入旧房间生成/AI/PCG/Boss 依赖，也不照搬旧玩法规则；复用结论需记录。仅灰盒随机图/布局，不实现迷雾或 Encounter，不接管 Boss。提供 20 Seed 检查证据，不能以 Direct 测试通过代替走廊验证。
```

**必须过关：**主线可达、支线合法、没有非法重叠/死循环、Seed 可复现。

## P04｜PCG_RoomCover 掩体 Graph

```text
任务 P04：仅构建/修改 PCG_RoomCover，读取已放置房间的 Bounds、OpenZone、CoverZone、手工 POI/内墙 Exclusion、关键通路与有效门洞排除区。开阔区稀疏生成低掩体，密集区允许高/低/预制组合掩体；可配置资产列表、权重、密度、碰撞间距，且关键门路通畅。通过官方 Unreal MCP 已发现的 PCGToolset 编辑；加载 Epic Skill_PCGGraphGeneration，先查看相近的 PCG 示例 Graph 并尽可能复用。不要编造 MCP 工具名称，不许直接改 .uasset 二进制。本轮仅改 Cover Graph 和用于验证的灰盒房，保留手工 POI。显示 PCG 采样/空间排除等课程技术证据。用至少 2 个 Seed 检查门口、POI 与 NavMesh；导航失败不能悄悄忽略。
```

**必须过关：**形态随机但通路安全，Graph + 实际生成物均可复查。

## P05｜PCG_EnemySpawn（候选点 + Metadata）

```text
任务 P05：在 P04 已验证的生成空间内制作 PCG_EnemySpawn，仅输出可供 C++ 消费的敌人合法出生候选点及可检查 Metadata，不重复 Spawn AI Actor。混合分散点与 1–2 个小群组；当前玩家入口、手工 POI、掩体、门口关键路径禁止出生；检测点位间距、碰撞和导航可达性；按 EncounterProfile 与可用面积限制总数和近战/远程类型权重。比率和数量是可调建议值，不可写死。PCGToolset + Epic 专用 Skill + 示例 Graph 前置检查；生成失败只返回较少合法点或有上限重试，不在墙里补刷。输出两组 Seed 下的点属性 Data View 或其他实证。
```

**必须过关：**候选点全部合法、入口不刷、分散和小群组可见、无重复 AI Spawn。

## P06｜PCG_RoomProps + 数据驱动物资与奖励

```text
任务 P06：给每个房间模板关联可被非程序员在 Editor 调节的 Room Loot Profile（优先重用现有物品/掉落资产）：场景物资、敌人掉落、清关奖励三类分离；RoomType 默认→模板覆盖→本局难度覆盖，参数包括数量、权重、固定/随机、保底。PCG_RoomProps 只生成可拾取的合法物资点与奖励位置标记，不能决定永久最终奖励本体；奖励由 RoomEncounterDirector 的 Cleared 事件解锁，同局不可重复领取。物资不堵门、不埋 POI，随机种子可复现。只做简易灰盒拾取演示，不扩大背包/经济功能；缺少物品类型先用测试占位并说明。完成后提供“关卡策划修改某一房间 Loot Profile 不影响其他模板”的 PIE 测试。
```

**必须过关：**关卡策划不用代码即可调奖励，普通物资/清关奖励时机正确。

## P07｜生成同步 + 房间战斗状态机 + 全连接门控

```text
任务 P07：实现独立 RoomEncounterDirector 的 Generating→Preparation→Combat→Cleared。先等待外壳、手工 POI、P04/P05/P06 内容、Spawn/必要导航全部验证，C++ 一次性生成并注册所有休眠普通敌军，得到内容 Ready。复用经批准的过渡入口合同：玩家正式入下一未清理战斗房→可行动存活队友安全进入/Defend 转 Follow→安全封入口→无限 Preparation；推进出口仍锁。内容或安全条件不足不得提前进 Prep；不能把 5–10 秒或雾当就绪/碰撞证明，迷雾视觉另属独立任务。Prep 玩家/队友可移动布防，敌军不移动索敌攻击、不运行首次 EQS，队友禁火。仅玩家首次命中当前 Prep 房 Enemy 才幂等 TryStartCombat，锁全部有效门且物理关闭不夹人，激活普通敌军/队友火力；Combat 不回退 Prep。全部有效敌军清除后 Cleared，解锁全部合法门与奖励；首枪秒杀、感染者 Downed、多门和重访正确。提供实际入房/集结/封入口/Prep/首枪/清场事件日志，不改 Boss。
```

**必须过关：** 内容/导航就绪和队友安全入房先于封入口/Prep；不会因首枪秒杀、重复触发、队友门外或准备失败卡死。走廊进入不误报正式入房。

## P08｜AI Faction / Health / Target Lock

```text
任务 P08：只实现统一 Enemy/Friendly/Neutral 阵营、Alive/Downed/Dead、TargetActor 有效性、房间注册的 Friendly 集合与事件驱动重索敌。Enemy 在当前房首次 Combat 激活时锁最近 Alive Friendly（玩家或队友）；后来更近的 Friendly 不得改变锁定；只有目标失效/死亡/Downed/阵营变化/持续不可达才能重选。远程 LOS 消失不清空目标。优先复用已有 AIController、Health 和 Team ID；不要每帧全图 GetAllActorsOfClass；提供黑板/Debug 证据。必要时先用 Test Pawn 灰盒验证，不抢先实现 BT/EQS。
```

**必须过关：**人眼可观察目标锁定及失效重选。

## P09｜普通近战 BT + 远程 BT/EQS 和武器

```text
任务 P09：在已有阵营/房间战斗许可基础上分别实现普通近战 BT（追击锁定目标、近战前摇+单次命中+冷却；不救援、不使用 EQS）、远程 BT（近距有 LOS 停住自动射击，远距/LOS 丢失用 EQS 找 Nav 可达且对原锁定目标能射击的掩体侧位，移到位再射击；视线丢失不换目标）。远程首次 EQS 只能在 Combat 激活后执行，后续事件触发与冷却控制；有限弹匣、无限备用弹药、自动 Reload，组件将来可被 Friendly 复用。与 PCG 提供的 firing candidate points 对接，PCG 点不足时可用受验证的 EQS 场景候选替代。所有数值数据驱动，MCP Graph/BT/EQS 工具必须先探测；不修改 Boss BT。需提供 BT+EQS 可视化和实际射击测试。
```

**必须过关：**近战攻击一次伤害、远程有真实 EQS、换弹不无限重置、LOS 不换目标。

## P10｜远程感染者转队友 + Follow/Defend/Rally

```text
任务 P10：只允许远程感染者击倒后可解救成 Friendly；同一 Actor 状态事务性切换：敌军计数退出、停攻击/移动、旧 Enemy 目标/感知结果清除、阵营 Team ID 更改、注册到 SquadManager、启动 Companion BT 的 Follow。近战感染者不可解救。感染者救援时长/满员规则在 Docs/03 属未决事项；先检查最新决定，以可配置原型占位，不套用 Friendly 35s/2s。Follow 使用侧后方 Formation Slots+RVO，Defend 为合法点位、站位预约和有限活动区；Prep 禁火，跨房取消 Defend 恢复 Follow。正式进入战斗房时，存活可行动队友安全进入后才封入口/进 Prep，不能等首枪才集结，也不能将 Downed 当 Alive 搬入。友军子弹穿友军、不穿墙，敌军子弹正常伤友军。先做 2 队友闭环再考虑 6/10；不在本任务重做迷雾视觉或走廊外壳。
```

**必须过关：**转换身份无旧敌我目标、交互提示正确、房门不会留 Alive 队友在外。

## P11｜Friendly 倒地、救援、遗弃、单局重置

```text
任务 P11：Friendlies HP<=0 -> Downed，停移动射击且退出敌军有效目标；可配置基线 35s 救援期/长按 E 2s，超时本局永久损失，不减容量。清场后仍可救；仅玩家正式进入下一房间的幂等事件才遗弃上一房未救 Downed，仅进入连接走廊/安全等待不遗弃，救援自然计时仍继续。可加警告 UI，具体入房边界与过渡合同须先确认。救援后 Follow；敌军目标失效重选。队友/扩编/奖励仅当前 Run，死亡/退出/重开重置，绝不 SaveGame 继承；感染者解救另行处理。验证走廊停留、正式入房、重复事件、计时边界、取消交互与重开归零。
```

**必须过关：**35s/2s 可调；走廊不提前遗弃、正式入下一房才遗弃上一房 Downed；无跨局遗留。

## P12｜Boss/商人接口与奖励互通

```text
任务 P12：先只读审查 Boss 团队已有实现与主项目对应接口，提出单向接入 RoomEncounterDirector 的小型适配事件：Boss 房单独开战触发可覆写、OnBossDefeated -> 当前房清场与开门；不能改 Boss-owned .uasset、技能、行为树或重命名其接口，未经许可只提供协作清单。商人用交互 Actor 与商品配置/Data Asset，不需要完整 BT。验证商店不会误进入普通 Combat 状态；清关奖励来自 RoomLootProfile 且只能领取一次。
```

**必须过关：**不破坏他人 Boss 资产，Boss/商店与房间系统接口清晰。

## P13｜100 Seeds、打包和课程证明材料

```text
任务 P13：在实际 UE 5.8.3 项目中运行/指导运行 100 次不同 Seed 关卡生成，记录模板 ID、门连接/重叠、Boss 可达、NavMesh、Spawn legality、普通 PCG 遮挡、生成超时、AI 清怪卡死，以及失败 Seed。按实际测试机器记录 2+8、4+20、6+30、10+50（友方+敌方）等压力场景的 FPS / CPU Game Thread / AI/EQS 代价；若性能不足缩减同时活跃敌人数或分波展示，不说“优化完成”但不给证据。收集 AI Behavior Tree + EQS 截图、PCG 空间采样/差集/Metadata 属性流程的 Graph 截图和两 Seed 对比、最终一局完整流程视频。将未通过问题按 P0/P1 修复建议排序，不额外扩展玩法，不擅自更改课程要求。
```

**必须过关：**全部实际测试数据可复现，未运行与失败透明披露。

---

## 每项任务后汇报要求

按 `templates/CODEX_HANDOFF_REPORT.md` 汇报，并把准确结果更新到 `docs/06_EXECUTION_STATUS.md`。用户验收完成前，不要开始下一 Pxx。
