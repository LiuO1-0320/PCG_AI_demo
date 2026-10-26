# P03-E2-HF03-C — Integration Scope Review

日期：2026-10-26（Asia/Shanghai）。

工程：E:/Unreal Projects/PCG_AI_demo_1/testfordemo.uproject。

**状态：只读集成审查完成；发现新增受保护核心范围，等待明确授权。未开始正式集成。**

本轮唯一写入是本新增报告。未编辑源码、配置、FloorProfile、模板或地图；未编译、启动Editor、运行PIE、生成、保存资产或执行Git写操作。以下实现方案均是申请，不是已完成结果。

## 1. 结论与必须先解决的授权项

仅把两个布尔/枚举配置接到UI不能安全完成本任务，已确认以下阻塞：

1. FloorProfile、共享SeedLayoutSettings、预备模板目录和最终实例之间还没有完整的OpeningMode/Ceiling传播链。
2. Open Passage现合同要求门洞完整覆盖走廊内腔宽高，且两房RoomHeight与CorridorHeight一致。400cm Junction3与500cm Warehouse原样混用无法满足；即使两房同高，当前Profile的走廊高350、门200×250也不是Open Passage规格。
3. Editor Tree提交、Runtime C2注册以及Runtime Revalidate存在固定59项判断；已验收的Open/无顶物理合同无法直接通过这些调用方。
4. 当前Runtime测试Actor每次Generate都会调用ConfigureSource，用旧SourcePreset覆盖Settings；直接给它赋E2配置会再次被覆盖。
5. 显式导航Retry调用RevalidateRuntimeGeometry，旧路径最终包含RebuildCorridor。此历史风险不能当作本轮顺手修复，也不能绕开严格导航合同。
6. **实际安装和上一阶段原始日志是UE5.8.4，不是任务标注的5.8.3。**

请求批准第6节的17份既有源码修改上限、3份现有FloorProfile资产的受控更新，以及明确限定的新增适配/测试文件。另需确认第4节实例级高度策略、第5节新路径验证隔离和本机引擎版本。未获批准前停止源码和资产实施。

## 2. 实测范围、保护基线与版本差异

### 2.1 本轮实际只读检查

- 当前目录与用户指定工程一致；uproject的EngineAssociation为5.8，此值不能证明补丁版本。
- Git已有修改仅按tracked检查发现：Content/Demo/L_CastleDemo.umap、testfordemo.uproject。完整保留，不暂存、不清理、不提交。
- 已读取根AGENTS、插件AGENTS、unreal-workflow、相关架构/决策/执行状态、E2/HF02/HF03/HF03-B报告及下述调用链源码。
- 采集39个审查目标/保护对象的SHA256，见附录。它们不是覆盖式恢复许可。
- 对照HF03-B修改前baseline：差异仅为HF03-B获批的C1头/实现与CorridorAssembly三文件，其余本次比对对象一致。此比较不能冒充整个仓库校验。
- 当前未发现UnrealEditor进程。本轮没有启动Editor，也没有进行新的MCP initialize、工具发现或工程身份核验；后续真实Editor验证仍必须重新执行官方MCP preflight。
- 阶段验收以最新用户授权为准：HF03第一阶段、HF03-B均已技术及人工验收；历史报告中的pending文本保留，不改写。

### 2.2 实际引擎

只读证据：

- D:/UE_5.8/Engine/Build/Build.version：MajorVersion=5，MinorVersion=8，**PatchVersion=4**，Changelist=58902052。
- D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe：++UE5+Release-5.8-CL-58902052。
- HF03-B的Development/P03E2HF03B/Run-20261026-1cea86ca/editor-validation.log：
  - 第495行：Build ++UE5+Release-5.8-CL-58902052；
  - 第499行：Engine Version: **5.8.4-58902052+++UE5+Release-5.8**；
  - 第510行：Base Directory: D:/UE_5.8/Engine/Binaries/Win64/。
- 当前Build.version修改时间为2026-10-25 21:57:45 +08:00。

因此，上一报告“5.8.3编译”的版本文字与原始日志不一致；原日志保留。本轮没有重新编译，也不将既有记录改写成新的结果。需要确认使用实际5.8.4继续，或由用户指定另一个真实5.8.3安装位置；不降级、安装或修改引擎。

## 3. 当前真实配置传播图

### 3.1 已有链路

~~~mermaid
flowchart TD
  FP["三个FloorProfile / FRogueliteFloorDefinition"]
  RES["ResolvePreview / ResolveRun"]
  SH["ResolvedFloor.Source + Planning"]
  ED["Workbench Generate（Editor）"]
  PLAN["LoopAwarePlanner.Plan / PlanRuntime"]
  CAT["BuildInputs + PreparedTemplateCatalog"]
  EM["MaterializeEditor"]
  RM["FloorProfileResolver.GenerateRuntime"]
  RS["RuntimeSeedAssembler.SpawnPlannedActors"]
  RL["RuntimeLoopAssembler：Tree注册 + Loop生成"]
  C2["C2 Connections唯一预约 / Collision与Capsule验证"]
  NAV["原NavigationReadinessComponent"]
  OLD["旧RuntimeLoopAssembly.ConfigureSource → SourcePreset"]
  FP --> RES --> SH
  SH --> ED --> PLAN
  SH --> RM --> PLAN
  CAT --> PLAN
  PLAN --> EM --> C2
  PLAN --> RS --> RL --> C2
  C2 --> NAV
  OLD --> PLAN
~~~

这里存在“共享算法/Resolver”，但**不等于已有Runtime测试Actor自动使用当前Workbench Profile**。Workbench Generate/Validate/Clear明确限定EditorWorld；RuntimeGenerationValidation仍明确Unavailable，仅DeveloperValidation可以调用共享Runtime Resolver，不能把本任务扩展为正式开局/Gameplay。

### 3.2 精确缺口

| 位置 | 代码事实 | 接入影响 |
| --- | --- | --- |
| Public/RogueliteFloorProfile.h；Private/RogueliteFloorProfile.cpp::Resolve | CorridorWidth=500、Height=350、DoorWidth=200、DoorHeight=250；无入口模式/顶板字段。 | 需要新增显式配置，不能只改C1类默认值。 |
| Public/RogueliteSeedAssembly.h | FRogueliteSeedCorridorProfile无Ceiling；计划房间没有完整实例门口配置。 | 必须让规划产物保存有效配置，避免注册后补参数。 |
| Private/RogueliteSeedAssembly.cpp::BuildInputs | Editor探针读取原Blueprint；Prepared分支要求目录Dimensions与CDO相同；配置摘要手工列出旧字段。 | 实例高度适配会被视为目录失效，必须区分源数据与有效实例数据。 |
| Private/RoguelitePreparedTemplates.cpp::CaptureEditor/IsCurrent | 仅封存现有Option/Descriptor/Sockets/MemberSignature。 | 新模式、高度策略改变后不能复用陈旧目录；Runtime仍不得生成元数据探针。 |
| Private/RogueliteLoopAwarePlanning.cpp::MakeRoom/Branches/Cycle | 使用目录尺寸与真实Socket，筛选宽高一致候选。 | 有效描述符必须在此之前准备，不能事后修Actor。只做元数据传播，不改拓扑或回环算法。 |
| Private/RogueliteLoopAwareAssembly.cpp::MaterializeEditor | Deferred初始化设置尺寸和bEnabled；未传OpeningMode/完整有效Door配置/Ceiling；Tree提交固定Probes.Num()!=59。 | Editor新模式会漏参或被数量判断拒绝。 |
| Private/RogueliteRuntimeSeedAssembly.cpp::SpawnPlannedActors | 初始化设置尺寸和出口启闭；走廊只设置旧尺寸，无Ceiling。 | Runtime必须在首次注册前应用相同计划配置。 |
| Private/RogueliteLoopCommit.cpp::TryCommitFixedEndpointLoop | Editor Loop走廊单独初始化旧尺寸；门配置有事务恢复。 | 不能只覆盖Tree路径，Loop也要完整传播并保留回滚。 |
| Private/RogueliteRuntimeLoopAssembler.cpp | Runtime Loop另有初始化；旧Revalidate固定59。 | Tree/Loop都要采用同一有效配置及模式对应证据。 |
| Private/RogueliteRuntimeLoopAssembly.cpp::ConfigureSource/GenerateRuntime | 从SourcePreset重建Settings；不是读取Workbench Profile。 | 需显式的可选Profile来源，旧测试Preset路径仍原样保留。 |
| Private/RogueliteCorridorAssembly.cpp::CheckCorridorPassage/TryRegisterRuntimePlacedConnection | Open严格等宽等高；Runtime注册固定59。 | 新Profile需模式感知的完整验证入口，不可删除安全门禁。 |
| RoguelitePrototypeEditor/Private/RogueliteDesignerPanel.cpp | 基础区只有Preset/Seed/操作；RulesView在折叠Advanced内。 | 必须在基础区明确增加两个控件，不能只说Details里已有新字段。 |

已验收C1的bGenerateCeiling默认true并可独立序列化；无需再次修改C1几何。P02的FRogueliteDoorConfig.OpeningMode与FRogueliteDoorSocket继续复用，不另建Socket体系。

## 4. 400/500cm兼容：事实与待授权方案

### 4.1 实际坐标合同

- 房间XY中心为原点，地板完成面Z=0；RoomHeight为墙顶高度。原ParametricRoom本来没有房间顶板。
- Room Socket在外墙阈面、局部Z=0，不在门洞中心；ManualOffset仅横向作用。
- 走廊完成面Z=0；CorridorHeight为侧墙顶/顶板下表面。ON顶板占据高度区间[Height, Height+WallThickness]。
- 关闭Ceiling不改变名义Height、端点规格及端框参数。
- Legacy房间门楣不是走廊Ceiling。OFF仍可保留Legacy房间门楣。
- P02 Open要求DoorHeight==RoomHeight；C2另外要求两端DoorWidth==CorridorWidth，DoorHeight==各自RoomHeight==CorridorHeight。此检查不因OFF而取消。

因此原始400/500房间不能连接到一个同时满足上述等式的单高度C1走廊。当前350cm走廊与200×250门口也不能直接视为全宽通顶Open。

历史HF03原型证据证明的是等高400cm独立房间、1500×500×400走廊。混高不支持的原记录在Development/P03E2HF03/Geometry-20261025-143904/height-compatibility.txt及原报告；**本轮没有新建混高Editor/PIE测试，不能把源码矛盾分析写成新的实体失败实验。**

### 4.2 推荐的最小方案：显式实例级统一高度

申请新增一个仅新Profile启用的几何适配策略，初始验证使用**统一500cm有效高度**：

1. Warehouse源资产仍500，Junction3源资产仍400；不写CDO、不保存原Blueprint。
2. 只对新生成的Open实例，将Junction3参数化外壳提高到500；Warehouse保持500。两者的有效DoorHeight与CorridorHeight均为500，DoorWidth与CorridorWidth均为500。
3. 长宽、墙厚、地板厚、Socket身份/横向ManualOffset和模板自有组件Transform/Scale/资源引用保持。不是Actor Scale适配。
4. 适配在Editor有效目录捕获/纯空间规划阶段完成，并在Deferred Spawn第一次Static注册之前应用。
5. 一个房间被多个Tree/Loop连接共用，只能有一份一致的有效房高；不能按每条边分别修改同一个已注册房间。
6. 未连接的出口仍关闭；需要供后续Loop启用的原合法侧别保留完整配置。非法原出口不因适配自动获得新身份或容量。
7. 新门口更宽更高，会扩大P03-D入口禁占体积；必须实际ValidateTemplate/POI检查。发现内部结构、屋顶或POI侵入新通道即明确拒绝，不移动POI、不降级Legacy、不关闭碰撞。
8. 新开口、外壳和C1分别沿用原生几何生成；不残留Room顶部横梁。Ceiling ON时走廊屋顶按500cm净高存在；OFF时开放，侧墙与地板仍阻挡/支撑。
9. 生效值及源400/500高度差异要显示在诊断/报告中，不暗中覆盖用户配置。此方案不是“原400/500外壳不变的过渡模块”。

**尚未实测的风险**：Junction3提高墙顶后与内部固定结构的视觉关系；500宽通道是否触及Warehouse/Junction3内部POI；新完整宽度的侧边余量、异墙厚、非零Offset及多出口组合；新增空间约束对Seed成功率的影响。必须先用两模板的独立实例通过ON/OFF、碰撞和视觉验证，再扩大到正式楼层。

如果用户要求生成后外壳仍分别400/500，则需要另一种有明确两端高度数据的合法过渡设计，可能涉及C1/P02/C2新核心范围，**不属于上面最小方案**。本次不提前选择或开发该路线。

## 5. C2合同、Static与导航风险

### 5.1 不可把固定59直接改成“至少59”

HF03-B已验收独立单门夹具证据：

| 模式 | Ceiling ON | Ceiling OFF |
| --- | ---: | ---: |
| Legacy | 59 | 68 |
| Open Passage | 75 | 84 |

此表来自原始报告与physics-replay记录，不是本轮重新执行。Open检查还按当前未使用外墙数量追加封闭边界检查；多门房的实际数量会变化，不能把75/84又硬编码成所有房间的永久常数。

申请在C2提供**显式模式感知的注册与只验证入口**，由新Profile路径使用：

- 仍执行现有ValidateCorridorConnection、占用、阈面/反向法线、双向胶囊、地板、实体侧墙、外围封闭边界及顶部条件探针。
- 校验实际适用探针集合完整且逐项成功，不靠数量下限、不删除失败项。
- 保留Legacy+ON原59合同及旧调用路径；Open+ON保留相应Open探针；OFF继续包含HF03-B内部向上胶囊与无隐藏顶板检查。
- 唯一Connections、Socket抢占、批次所有权、事务发布和释放权威不变。
- 原型及旧地图回归继续使用原证据合同；不能修改Golden适配新结果。

这是对原Runtime59门禁适用范围的扩展，必须额外批准，不能声称“仅UI参数传递，C2完全未变”。

### 5.2 历史GameWorld Revalidate精确边界

源码链：

NavigationReadinessComponent::RetryNavigation
→ Rig()->RevalidateRuntimeGeometry()
→ RuntimeLoopAssembler::Revalidate
→ Registry->RevalidateConnection()
→ RogueliteRoomAssembly.cpp约第596行RebuildCorridor。

这是**显式Retry/Revalidate路径**，不等于每次正常Generate/Start必然执行该重建。旧Runtime Revalidate另有固定59门禁。

推荐申请：新Profile适配路径覆盖虚函数RevalidateRuntimeGeometry，调用C2的“不移动、不重建、实际重新Collision Query”的新入口；复用现有Epoch/Revision失效规则及严格导航流程。旧Preset/旧Revalidate函数保持，不修改RoomAssembly.cpp，不将历史问题标为已修复。

若发现新路径仍无法安全完成严格导航Retry，应停止该部分并提交证据；不修改NavigationReadinessComponent判定、减少路径/MoveTo、跳过NavReady前提或修改Static Mobility。若必须更改旧GameWorld函数，本授权清单也不包含它，仍需另批。

### 5.3 配置与确定性

- 默认兼容字段：通用/旧数据Legacy、Ceiling true；新正式3个Profile显式Open、Ceiling true。旧地图不自动重建。
- 新几何设置必须纳入Profile/配置/目录/计划新鲜度和几何摘要；关闭顶板不能沿用旧NavigationReady。
- 仅切换入口/顶板不应改变GraphSettings、算法版本、拓扑随机流或RoomTypeAssignment。
- ProfileVersion参与RunSeed→FloorSeed派生；不能为标记几何升级随意增加它导致拓扑Seed改变。采用独立几何策略版本/摘要；老Graph Golden保持。
- 多处使用StaticStruct ExportText参与摘要，新增默认字段也可能改变Legacy摘要。需版本化兼容编码，不能通过重录Golden掩盖变化；优先保持旧版默认路径的既有摘要。旧目录不能被当作新几何目录直接复用。
- 空间布局可能因新通道/POI合法性而真实失败或选择其他候选；保留全部失败及预算证据，不减MinLoops，不改拓扑救场。

## 6. 精确申请范围

下列是**待批准修改上限**，不是本轮实际修改清单。只改实现必需的部分；无法在这些文件内安全完成则再次停止。路径相对于Plugins/RoguelitePrototype/Source。

| # | 既有文件 | 限定目的 |
| --- | --- | --- |
| 1 | RoguelitePrototype/Public/RogueliteFloorProfile.h | 新Profile入口/顶板/显式几何适配配置；旧缺省兼容。 |
| 2 | RoguelitePrototype/Private/RogueliteFloorProfile.cpp | 唯一Resolver传播新配置与可读失败；保留1200–1500及Seed派生。 |
| 3 | RoguelitePrototype/Public/RogueliteSeedAssembly.h | 共享几何设置、Corridor Profile、计划/记录中的有效门口配置；不新增预约。 |
| 4 | RoguelitePrototype/Private/RogueliteSeedAssembly.cpp | BuildInputs应用有效实例描述符、源模板保护与配置签名；不改类型/随机规则。 |
| 5 | RoguelitePrototype/Public/RoguelitePreparedTemplates.h | 区分源描述符与有效实例描述符/几何策略身份。 |
| 6 | RoguelitePrototype/Private/RoguelitePreparedTemplates.cpp | Editor捕获、封存、旧格式兼容及新配置失效；Runtime不建探针。 |
| 7 | RoguelitePrototype/Private/RogueliteLoopAwarePlanning.cpp | 将有效门口/房高配置写入计划、签名；不改Tree/Loop搜索算法和预算规则。 |
| 8 | RoguelitePrototype/Private/RogueliteLoopAwareAssembly.cpp | Editor首次注册前传配置；使用模式对应的完整C2证据。 |
| 9 | RoguelitePrototype/Private/RogueliteRuntimeSeedAssembly.cpp | Tree Deferred实例化使用计划配置，保留原默认B3A路径。 |
| 10 | RoguelitePrototype/Private/RogueliteRuntimeLoopAssembler.cpp | Runtime Tree/Loop配置传播、证据消费与当前状态检查；旧危险Revalidate实现不顺改。 |
| 11 | RoguelitePrototype/Public/RogueliteRuntimeLoopAssembly.h | 可选Profile来源/新路径安全验证声明；默认仍旧Preset。 |
| 12 | RoguelitePrototype/Private/RogueliteRuntimeLoopAssembly.cpp | Profile路径复用Resolver，避免ConfigureSource覆盖；安全验证分流，复用原导航协议。 |
| 13 | RoguelitePrototype/Private/RogueliteLoopCommit.cpp | Editor Loop传播顶板/端口配置、失败恢复/新鲜度；预约发布规则不变。 |
| 14 | RoguelitePrototype/Private/RogueliteLoopPlanning.cpp | 仅必要的模式/配置签名兼容与一致性检查，不改候选评分/拓扑/空间算法。 |
| 15 | RoguelitePrototype/Public/RogueliteRoomAssembly.h | C2模式感知完整证据及只验证入口声明；不新建Registry。 |
| 16 | RoguelitePrototype/Private/RogueliteCorridorAssembly.cpp | 新Profile模式感知C2入口、无Static重建验证；不削弱几何/胶囊/预约。 |
| 17 | RoguelitePrototypeEditor/Private/RogueliteDesignerPanel.cpp | 基础区两个清晰控件和覆盖提示，不增加求解器参数或Runtime依赖Slate。 |

申请仅通过真实Editor/API更新下列三个现有DataAsset的新配置；先备份、只保存具名资产，不批量保存：

- Content/Roguelite/FloorProfiles/DA_Floor1_Intro.uasset
- Content/Roguelite/FloorProfiles/DA_Floor2_Expansion.uasset
- Content/Roguelite/FloorProfiles/DA_Floor3_Final.uasset

必要新增文件限定：

- Source/RoguelitePrototype/Private/RogueliteGenerationGeometry.h、.cpp：一个小型共享配置/描述符适配及兼容摘要helper，不另建拓扑/连接系统。
- Source/RoguelitePrototype/Private/Tests/RogueliteOpenPassageIntegrationTests.cpp。
- Source/RoguelitePrototype/Private/Tests/RogueliteOpenPassageIntegrationPIETests.cpp。
- Source/RoguelitePrototypeEditor/Private/RogueliteOpenPassageIntegrationEditorTests.cpp。
- Content/Roguelite/Maps/L_Roguelite_OpenPassageIntegration_Dev.umap：全新独立地图；拒绝覆盖已存在资产。
- Development/P03E2HF03C/：具名备份、SHA、日志、逐Seed/逐连接数据、截图及测试驱动。
- 最终新报告P03E2_HF03C_INTEGRATION_RESULT.md。本报告保留作为授权边界；状态归档仅陈述实际结果，不改历史报告。

**不申请修改**：ParametricRoom.h/.cpp、ParametricCorridor.h/.cpp、RoomTemplate源码、RoomAssembly.cpp、NavigationReadinessComponent、RoomGraph V1/V2、CompactCorridorContract、模板Blueprint、旧地图/Golden、CastleDemo/AI/Boss/PCG、Build.cs、uproject/uplugin、Config。也不申请更改HF03/HF03-B原型源码或历史报告。

## 7. 生成后手工顶板覆盖合同（待实施验证）

1. Stop PIE后，可在本批次Corridor Details改变Generate Corridor Ceiling，沿用现有安全Editor更新。
2. 保存当前地图会序列化Actor自己的布尔值；重新加载后读取它，不从Profile强制覆盖。
3. 显式Validate读取Actor当前顶板与真实碰撞，使用正确的ON/OFF探针，不把它重置成Profile默认。几何Revision/摘要更新，旧导航Ready撤销。
4. Profile或几何规则整体改变使旧批次失效时，明确提示重新生成，不用旧成功状态冒充新配置。
5. Clear仅处理所有者批次；Regenerate的新Actor重新使用Profile，旧手工覆盖不跨Seed迁移。
6. PIE期间不提供开关切换/重建；Runtime按生成前配置，不试图迁移Editor批次。
7. 顶板覆盖不改变C1名义尺寸、Socket、Transform、ConnectionId；若外部障碍导致OFF向上探测失败，明确失败。

目前仅独立HF03-B原型已验证保存/重启/四组合；正式批次中上述完整行为本轮未验证。

## 8. 批准后的最小实施/验收顺序

1. **基线与接口**：逐文件备份获批内容；固定旧Graph Golden/历史配置；添加共享配置和版本化有效描述符。先运行小型编译/原生默认值及摘要回归。
2. **高度与C2先行**：独立实例使用两种源模板，统一500有效高度；四组合、四方向、非零Offset、不同墙厚、关闭出口、POI签名及碰撞。若失败停止后续正式集成。保存同角度视觉对照供人工验收。
3. **Editor与Workbench**：同一计划配置驱动Tree和Loop，基础控件、Manual Ceiling覆盖、Validate、Clear、保存卸载重载；E1读取真实连接。
4. **Runtime与导航**：真实GameWorld首次注册前设参，C2完整证据与静态注册审计；动态导航、Tree/Loop完整路径及真实Character MoveTo。新Profile Retry使用安全查询入口，旧危险入口不修不调用。
5. **有界矩阵**：新正式Profile F1至少20 Seed（4/3/0），F2/F3各6 Seed；逐Seed记录Tree/Loop、每条原生Length/阈面距离、门口/顶板、C2、导航、MoveTo、预算、清理和失败。四组合采用明确测试清单统计，不将32个几何样本自动记成全部NavigationReady。相同配置重复生成比较有序摘要；不同视觉配置比较拓扑摘要保持。
6. **回归与交付**：旧P01/P02/C1/C2/P03-D、B2B/B3、E1/E2、HF01/HF02/HF03/HF03-B和Golden；不运行会覆盖旧图的authoring脚本。停止等待人工验收。

本轮没有执行上述步骤，没有逐Seed新数据、编译退出码、MCP/PIE截图或新导航成功率。不能引用HF02的旧成功率替代本阶段结果。

## 9. 安全、恢复与停止点

- **本轮恢复**：只有这一新报告可单独移出审查；不需要恢复任何源码或资产，不执行删除。
- 后续实施前备份17份授权上限中实际修改文件与3个具名Profile，保存原SHA及新建清单。已有用户/队友内容不纳入覆盖操作。
- 后续恢复必须以本次实际diff逐文件进行：先检查是否存在新增用户修改；未经针对性确认不覆盖恢复。新地图不被默认批量清除，批次由原Registry按所有权释放。
- 旧Binaries/Intermediate/Saved不删除；不Reset/Clean/Commit/Push；不改引擎、全局Config或默认地图。
- 若新增第四种连接几何、改变高度过渡接口、需要本清单以外核心文件、需要修改旧GameWorld Revalidate或NavReady判定，停止并再次申请。
- 根Docs/AGENTS目前只归档到HF02，较新HF03验收记录在用户授权与阶段报告中；这是状态文档滞后，不是人工验收失败。本只读审查不扩展为全局文档维护。
- 当前停止在授权门：**17文件/3Profile范围、500cm实例适配、C2新入口/新Profile安全验证分流，以及实际5.8.4或指定5.8.3构建环境**需用户明确选择/批准。

## 附录A：本轮39项SHA256基线

group仅表示本次申请/保护分类；全部均未修改。未对整个项目宣称全量SHA审计。引擎文件只读核验，不在恢复范围。

| 工程相对路径 | SHA256 | 分类 |
| --- | --- | --- |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Public/RogueliteFloorProfile.h | 4b61965155e0ce8b34a71eff5b367197f1404bed5c7c758f8a06c7d07ed4bb37 | proposed |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteFloorProfile.cpp | 7adc3f19c55fb013030adc19859a281e92b0190dba7c66e9628939a5c4926843 | proposed |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Public/RogueliteSeedAssembly.h | 731be063402f96208b2cea1bc675a9bc81f24c755ae296b16ba07ece777ce708 | proposed |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteSeedAssembly.cpp | 4581fb54a0d09ea14db2bd48b639b0969475eab221e7c47084a1e37a42e58807 | proposed |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Public/RoguelitePreparedTemplates.h | e18e5f50bb0340d52416347dc125a1e6897c3bdf3303229115874ec738b68fe0 | proposed |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RoguelitePreparedTemplates.cpp | 0cde513664ba5336999a88a0004a1a324e7629cdff5bb0660a4640d28c40db69 | proposed |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteLoopAwarePlanning.cpp | 2d0a64948ea3cbc34e7820e1ae4dcef39f454936a9a45869741795ac4fd0b536 | proposed |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteLoopAwareAssembly.cpp | 445cf79af250daa42556fc7eccf352a62e60d3bea21bdf24956973786fc8fa43 | proposed |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteRuntimeSeedAssembly.cpp | 6d6036ee0e77c1911d711e80a7f71a4358ea68ac3284cc184adda3e436ca9359 | proposed |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteRuntimeLoopAssembler.cpp | 56067c3b48dca629996f980f4a0fd9564b14e2d550518fc6877b35331d1d0191 | proposed |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Public/RogueliteRuntimeLoopAssembly.h | 7a141409e1ecd9535792ebec689053e0e6b3832f5b1f0d1a3f3d7f88312a6eb6 | proposed |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteRuntimeLoopAssembly.cpp | 61a5692caf8a3c07090ec82d834fb307e8b1faa2f54272f68becd37545d76a71 | proposed |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteLoopCommit.cpp | 54b5985d57cc839bb946aea2bb36d5faf532f48d4c2146f183ea10bbe2145fcf | proposed |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteLoopPlanning.cpp | c2eb0915e57288c8c84df889a1232fd8ad2bb6fc04224c0b38172847941d43de | proposed |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Public/RogueliteRoomAssembly.h | 166e820b6b19f9a1d2193f16ba18eaca64007e0530b728e779e22ffd73d704f8 | proposed |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteCorridorAssembly.cpp | 94a0e088d0c3ce317a7d649120bcd7c9e40578c7a37dfc35484d9942d57707cb | proposed |
| Plugins/RoguelitePrototype/Source/RoguelitePrototypeEditor/Private/RogueliteDesignerPanel.cpp | 2a2251e79e3b5aa31a1f3a8c63f02edbccf8f6e661d3498237d7367a560e3971 | proposed |
| Content/Roguelite/FloorProfiles/DA_Floor1_Intro.uasset | ad1ae6f498c93f9d2d825324d9f18617c2d1b341edc935dcf8d898c01b0c7385 | proposed_asset |
| Content/Roguelite/FloorProfiles/DA_Floor2_Expansion.uasset | 4f5a3c489953e769ca97fd0a55977078bebd2536907faec01f192bb186689b9e | proposed_asset |
| Content/Roguelite/FloorProfiles/DA_Floor3_Final.uasset | 04ecae63d864408edf4a37e9aafecd70d8b25ab0a39180a271c7a5356e6e6133 | proposed_asset |
| Content/Roguelite/RoomTemplates/BP_Room_Warehouse.uasset | a5812cfb9a02fb9016be87c3be7a1ce177de96523f5f8e21f9d00fbb4eeae563 | protected |
| Content/Roguelite/RoomTemplates/BP_Room_Junction3.uasset | c70e19c3966773b027b9c3330f2f466833a55ab62bbf60e5b62b0cb02b8d7bae | protected |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Public/RogueliteParametricRoom.h | e24785aeebab578272a5e5bd79ac9b629f21c424acadcb057fc36666ec053969 | protected |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteParametricRoom.cpp | 955a2644657bcc987dc15a76259aacc4cc299ada215a3d809b4bfb85f52ae1ab | protected |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Public/RogueliteParametricCorridor.h | 85c2a040f97edc663b21f1a3c28377b8c438df7cdcd884cdec7e415471089267 | protected |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteParametricCorridor.cpp | 1ab0b558ace50eac34dd1fdd6ffeba2eecee122d92688bddf5ab1c3f5eb49223 | protected |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteRoomAssembly.cpp | a94443bf7d6b43a6521ca9962ca0aa561bb61fe1d26ec99988b44b748f895f4e | protected |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteNavigationReadinessComponent.cpp | 3ee90403ea87a1c15af2bbb8c1ba5a27d92c11c045fd061b475fc404bde8559c | protected |
| Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteRoomGraph.cpp | 81d6c67a0cc7561055ead401e628547a1fcdc529164261cbf9471096bb548d6d | protected |
| Plugins/RoguelitePrototype/P03E2_HF03_OPEN_PASSAGE_PROTOTYPE_RESULT.md | 1bf625066288255245d869b9739bf5b1ab212f7bc2e2eb7caeaec304fe1ac323 | protected |
| Plugins/RoguelitePrototype/P03E2_HF03B_CEILING_TOGGLE_RESULT.md | 4f9beb3014a65354cafd862785e3ad97d24b1b1de13417b6c74b001c9376cc10 | protected |
| Plugins/RoguelitePrototype/RoguelitePrototype.uplugin | 0a8ec131168a5b6d136bffcbeb8e1fb36097693182542684a0f9c98dc2107353 | protected |
| Content/Demo/L_CastleDemo.umap | c3bee36c6454235eadd8be7ce67b02954aa740f454a49acc2ac5fa05907aaecd | protected |
| testfordemo.uproject | 27a3ab541173d04f6d11a8d1ac31f6dc5c3a5d2319dea6698b3f4eccfb3659e3 | protected |
| AGENTS.md | 7ce729c1720f3133aa06df757da404bb1880587e14ddb0cd22d08f83d3f64faf | protected |
| Plugins/RoguelitePrototype/AGENTS.md | 05e10e593c4f4e6a959321c354b67d20516c434b85f4c647dc598d56d9379246 | protected |
| Docs/02_TECHNICAL_ARCHITECTURE.md | 73dd93f4d2f1e524375c8cd719d0b08604a85607c9d3016ea51b9a3889d7cb5b | protected |
| Docs/03_DECISIONS_AND_OPEN_QUESTIONS.md | 0e0232a508f78cfaad6abe4e53026135dc4fe83ad85d5c7f544c1a63d8283405 | protected |
| Docs/06_EXECUTION_STATUS.md | 97cd67a4c1e14701efb7e258acc4a07a28d8492917949db6014873e49bd09c7f | protected |

