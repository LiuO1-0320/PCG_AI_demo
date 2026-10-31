# P03-F0 — Floor-Specific Room Template Weights Scope Review

日期：2026-10-31（Asia/Shanghai）  
**状态：MCP_READY / F0只读范围审查完成；未实施，等待具体范围批准。**

## 0. 结论、证据等级与本轮边界

**已有数据能够按FloorProfile分别保存模板名单和Weight；完整需求不能只靠改配置交付。** 原因是正式Loop-Aware路径虽然调用了加权候选排序，但随后存在全候选均匀打乱/均匀选姿态，不能将Weight当作最终可靠刷新比例；跨楼层矩阵界面和Start安全屋模板资格也未实现。

本报告使用三种标记：

- **代码实证**：实际读取当前工程的类型、函数及调用方所得，未通过新生成实验验证。
- **Editor只读实证**：本轮官方MCP返回的进程/工程/资产字段。
- **推导/建议/待测试**：基于代码的后果、拟议接口或未来测试，不写成已实现/已测。

本轮仅更新本报告。源码、FloorProfile、Blueprint、地图、PCG、配置、AGENTS及执行状态文档均不修改；没有编译、测试执行、PIE、Spawn、Generate、Save或Clear。用户声明的P03最终人工验收与功能冻结作为本次保护基线，不改写旧报告。用户最新决定采用完整手工RoomTemplate，取消独立大型POI随机摆放、动态建筑据点组合和特殊地形运行时随机改造；本报告以此为设计约束，没有修改其他技术文档或启动PCG/AI/Gameplay。

## 1. 官方MCP恢复与工程身份实证

- 编辑器PID **10484**，可执行文件为 `D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe`。
- 本轮启动日志确认 **5.8.4-58902052**，启动工程为 `E:/Unreal Projects/PCG_AI_demo_1/testfordemo.uproject`。
- `netstat`显示 **127.0.0.1:8000** 由PID10484监听；其后确实完成 `initialize`，协议 `2025-11-25`，并读取 `tools/list`、`list_toolsets`、`describe_toolset`，并非只凭TCP判定。
- 实际调用 `PluginToolset.GetPluginInfo(RoguelitePrototype)`，返回插件目录 `E:/Unreal Projects/PCG_AI_demo_1/Plugins/RoguelitePrototype`。
- 实际 `get_current_level` 返回 `/Game/Demo/L_CastleDemo`，`IsPIERunning=false`。本轮始终没有切换或保存该地图。
- 通过已发现Schema的 `ObjectTools.list_properties/get_properties` 读取三个FloorProfile.Rules、两模板CDO；通过AssetTools查询资产及package dirty。三Profile、两模板、当前Demo地图最终均 `is_dirty=false`。
- 未改MCP配置，未启停服务，未降级为Unreal Python或Computer Use施工。HTTP客户端仅传输官方MCP的只读工具调用。

此前BLOCKED_MCP记录保留在文末历史附录，已被本次真实恢复结果替代。

## 2. A–F问题的直接回答

|问题|结论|证据与限制|
|---|---|---|
|A：Weight是否真正参与Editor/Runtime选择？|**参与底层加权候选顺序，但正式求解路径不能保证按Weight选模板。**|两端共用TemplateOrder；Branches随后均匀打乱全部空间候选，Cycle随后对模板×合法朝向均匀选一项。不能仅凭字段进入计算就声称最终刷新权重合同成立。|
|B：同一TemplateClass能否在不同Profile设置不同Weight，无需源码？|**可以表达并保存为独立配置。**|每个Profile有独立AllowedTemplates数组，包含相同Class但独立Weight；UPROPERTY可编辑，已有Save Floor Profile入口。本轮只读，未试改/保存验证新权重效果；仍受A限制。|
|C：是否已有按Floor禁用机制？|**有Profile局部硬名单；没有模板自身的AllowedFloors字段。**|从该FloorProfile.AllowedTemplates移除模板，该次生成就没有此候选；不会因空间失败自动补回。Weight=0不是禁用，当前会被拒绝。原始无Profile的旧测试API没有楼层概念，不能据此宣称全局Class级楼层限制已存在。|
|D：Start为Combat时如何防止普通Combat模板成为安全屋？|**目前没有这种隔离。**|StartType=Combat，TemplateOrder只按AllowedTypes与Degree过滤；StartRolePolicy是Reserved Gameplay元数据。不能把当前Start生成结果称为安全屋。未来需显式角色资格或指定Start模板。|
|E：权重与Socket/尺寸/回退是否造成偏差？|**会，且除合法性筛选外还存在候选展开和重新均匀抽选造成的权重语义问题。**|3连接节点排除两门Warehouse，回环端点需额外端口；不同模板产生不同数量的合法姿态/长度候选，几何和有限重试又改变幸存分布。本轮未做分布统计，不能给出实际偏差百分比。|
|F：最小增量改数据、配置还是UI？|**数据结构优先不增；配置可先表达名单/独立权重，UI可汇总三Profile；要让正式权重选择可靠，还必须获批修改受保护求解器。**|仅UI或仅调Weight不能解决A；不重建RoomGraph、Catalog、C2或HF03-C。建议白名单见第8节。|

## 3. 已有Template Weight与TemplateCatalog能力

### 3.1 实际字段与合法范围（代码实证）

[RogueliteSeedAssembly.h:37](<E:/Unreal Projects/PCG_AI_demo_1/Plugins/RoguelitePrototype/Source/RoguelitePrototype/Public/RogueliteSeedAssembly.h:37>) 定义的是：

`Id + TemplateClass + AllowedTypes + Weight`

用户所称TemplateId/AllowedRoomTypes对应这里的 `Id/AllowedTypes`；不能误建同名第二套字段。Weight的编辑最小值是1；BuildInputs最终要求 **1..10000**。每个设置目录限 **1..16个模板**，Id在该目录内非空且唯一。模板权重不等于RoomTypeAssignment中的房型权重，二者是不同阶段。

现有“Catalog”包括：

1. FloorProfile.AllowedTemplates及传入Settings.Templates：作者配置来源；
2. 内部 `CatalogEntry`：同一选项配合实际尺寸、端口、验证签名形成的求解输入；
3. `FRoguelitePreparedTemplateCatalog`：Editor准备、Runtime只读消费的已封存描述符与内容签名。

第3项不是另一个模板概率配置源；本方案不再建立新的权重Catalog。

### 3.2 加权顺序、过滤与Seed

[RogueliteSeedAssembly.cpp:102](<E:/Unreal Projects/PCG_AI_demo_1/Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteSeedAssembly.cpp:102>) 的TemplateOrder先保留：

`Capacity >= NodeDegree && Option.AllowedTypes.Contains(RoomType)`

再以 `DeriveSeed(Seed, TemplateSelection, AlgorithmVersion, ConfigVersion, NodeId)` 创建专用FRandomStream，进行**按Weight不放回抽取**，输出全部合法模板的排列。某个正权重模板不会因为权重低就被移出合法池。

BuildInputs先按Id字符串排序目录；端口按Side排序；TemplateSelection用途常量为 `0x6c8e9cf5`。Seed派生由固定Mix32规则组合版本、稳定NodeId、Attempt，不使用GUID或Actor创建顺序作为该选择的随机来源。BatchId虽然使用GUID，但用于身份/所有权，不是模板选择Seed。

旧B2直接生成和旧B3A空间计划会依照TemplateOrder逐个尝试，失败后换后续候选，因而即使旧路径也只能说“加权尝试优先级”，不是保证最终占比。

### 3.3 已确认的正式路径缺口

[RogueliteLoopAwarePlanning.cpp:97](<E:/Unreal Projects/PCG_AI_demo_1/Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteLoopAwarePlanning.cpp:97>)：

- **Branches，第110–115行**：先按TemplateOrder展开 `父端口 × 模板 × 子端口 × CorridorProfile × Length`，随后第113行对整个Choices执行均匀洗牌，再截取MaxCandidatesPerEdge。加权模板顺序不能保持为最终优先级。
- **Cycle，第145–152行**：按TemplateOrder枚举模板和四种Yaw，收集合格In/Out姿态后，使用 `Options[Random.RandRange(0,Options.Num()-1)]` 均匀选一个姿态；没有把各模板Weight用于这一步的选择概率。
- **根节点，第233–241行**：无环重试按加权排列的 `Attempt % Order.Num()` 轮转；有回环时Start可能先由Cycle决定，未在闭环路径上的节点再进入Branches。不能只检查根节点就判断整个地牢权重已正确。
- **配置/几何筛选**：World障碍、保留回环包络、门口尺寸方向、1200–1500cm闭合、候选/回溯预算都会影响最终幸存模板。

**推导**：改变Weight可能改变固定Seed的具体结果，但“结果变了”不能证明期望刷新比已经实现。重新均匀抽选还可能让合法姿态更多的模板更常出现。本轮没有执行统计实验，不声称Weight完全无效或给出虚构比例。

## 4. FloorProfile实读与楼层准入

[RogueliteFloorProfile.h:48](<E:/Unreal Projects/PCG_AI_demo_1/Plugins/RoguelitePrototype/Source/RoguelitePrototype/Public/RogueliteFloorProfile.h:48>) 已有 `AllowedTemplates`；[RogueliteFloorProfile.cpp:43](<E:/Unreal Projects/PCG_AI_demo_1/Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteFloorProfile.cpp:43>) 第52–61行验证配置并把它直接赋给 `R.Source.Templates`，没有空间失败后回退到全模板目录的逻辑。

三个实际资产均经MCP读取：

|Profile|FloorIndex|Junction3.Weight|Warehouse.Weight|房型资格|
|---|---:|---:|---:|---|
|DA_Floor1_Intro|1|1|1|两模板均允许Combat/Challenge/SupplyShop/Final/Event|
|DA_Floor2_Expansion|2|1|1|同上|
|DA_Floor3_Final|3|1|1|同上|

资产路径均在 `/Game/Roguelite/FloorProfiles/`。Floor1仍由RoomType配额禁止Challenge/SupplyShop/Event实际分配；模板声明支持这些类型不等于Floor1会生成这些房型。

三个实读Profile同时保持：

- OpeningMode=OpenPassage；GenerateCorridorCeiling=true；有效实例高度500cm；
- 走廊长度1200–1500cm；
- StartType=Combat；StartRolePolicy=TopologyStart_SafeHouseReserved；
- Floor1/2 FinalRolePolicy=FloorExit，Floor3=BossFinal，均不是已实现Gameplay；
- **资产ProfileVersion=1**。源码Defaults目前设置ProfileVersion=3，二者不能混同，F0不改任一版本。ResolveRun将ProfileVersion参与FloorSeed派生，不能为了模板权重UI顺手增加它，导致旧拓扑Seed变化。

按某Floor禁用模板可以通过该Profile名单缺席表达。若名单为空，Resolver返回TemplateCatalogInvalid；若RoomType、Degree或回环容量没有候选，BuildInputs/求解器明确失败，不从其他Profile借模板。RoomType配额可能因此不可满足，也不能悄悄取消最低配额或MinLoops。

目前没有每个TemplateOption的 `AllowedFloors/Floor1Weight/Floor2Weight/Floor3Weight` 字段，也没有Class自身全局楼层黑名单。硬名单约束只对明确采用该Profile的路径成立，旧SourcePreset测试入口不使用FloorProfile。

## 5. Editor/Runtime共享路径、摘要与HF03-C

### Editor

`Dungeon Generator → FDesignerWorkbench::Generate → Profile.ResolvePreview
→ Resolver.Source/Planning → LoopAwarePlanner::Plan
→ PlanWithInputs/BuildInputs/TemplateOrder → MaterializeEditor → 真实C2验证`

证据：[RogueliteDesignerWorkbench.cpp:30](<E:/Unreal Projects/PCG_AI_demo_1/Plugins/RoguelitePrototype/Source/RoguelitePrototypeEditor/Private/RogueliteDesignerWorkbench.cpp:30>) 第30–48行；LoopAwarePlanning.cpp第201–254行。

### Runtime

`RuntimeLoopAssembly.FloorProfile → ConfigureSource/ResolvePreview
→ FloorProfileResolver::GenerateRuntime → RuntimeLoopAssembler::Generate
→ LoopAwarePlanner::PlanRuntime → 同一PlanWithInputs
→ Prepared Catalog → Deferred Spawn → 原C2/严格导航`

证据：[RogueliteRuntimeLoopAssembly.cpp:14](<E:/Unreal Projects/PCG_AI_demo_1/Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteRuntimeLoopAssembly.cpp:14>) 第14–51行；FloorProfile.cpp第77–84行；RuntimeLoopAssembler.cpp第121–153行。无FloorProfile时走旧SourcePreset兼容分支，该分支不能冒充楼层规则执行。

两端共用模板候选和空间算法，但Editor现场获取模板描述符、Runtime使用Editor已封存数据；相同配置比较必须包含相同模板描述符、初始障碍环境和版本，不能只比较整数Seed。

[RoguelitePreparedTemplates.cpp:18](<E:/Unreal Projects/PCG_AI_demo_1/Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RoguelitePreparedTemplates.cpp:18>) 的OptionDigest覆盖完整TemplateOption，IsCurrent会检查Id/Class/OptionDigest、目录版本和几何摘要。改Weight、AllowedTypes或名单后，旧Prepared目录会失效；正确行为是安全Editor阶段重新Prepare，不能为了继续Runtime跳过新鲜度校验。

ProfileDigest/ConfigurationDigest记录配置；已生成Workbench批次会因规则变化失效。Actor级独立Ceiling覆盖、C2唯一预约、Static注册次序和导航合同与权重表是不同职责，不应在此重构。

HF03-C适配仍由GenerationGeometry.cpp负责：源模板有效门口先验证，再将OpenPassage新实例适配500cm、保留POI局部内容。F方案不改这一实现或原资产。

## 6. Start/Final、稳定Id与容量风险

### Start/Final

[RogueliteRoomTypeAssignment.cpp:20](<E:/Unreal Projects/PCG_AI_demo_1/Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteRoomTypeAssignment.cpp:20>) 第46–51行固定Start为StartType、Final为Final类型，再结合配额/兼容性分配；**模板候选没有独立Start/Final角色资格过滤**。

因此：只给安全屋高Weight、把普通模板设很低Weight、或依赖TemplatePurpose=Combat，均不能保证普通Combat不出现在Start。Final类型可排除未声明Final的模板，但普通楼层出口与Boss房目前同属Final，FinalRolePolicy只验证元数据一致性，不筛选两种视觉模板。

建议在真正安全屋/出口模板任务前设计显式 `AllowedTopologyRoles` 或 `StartTemplateId/FinalTemplateId` 硬过滤，并在候选顺序之前应用。不能以新RoomType枚举、Weight=0或Geometry失败后的回退代替。**本次建议的最小F1白名单不包含这一角色功能；未批准前，仍不得宣称安全屋或Boss模板隔离已解决。**

### 当前模板实读与Id别名

|模板|目录Option.Id|蓝图作者TemplateId|原始净尺寸cm|配置启用端口|
|---|---|---|---|---|
|BP_Room_Junction3|Junction3|Junction3|2000×2000×400|北/南/东|
|BP_Room_Warehouse|Warehouse|Warehouse_V1|3000×2500×500|北/南|

这是CDO属性读取，不是F0重新生成/验证几何。实际合法容量仍由既有模板验证/Prepared描述符确定；三向节点不能仅因Warehouse权重高就使用它，回环端点还可能需Degree+1。

**已存在的Id别名不能自动改名。** 生成记录使用Option.Id；BuildInputs将作者TemplateId也纳入签名，但只检查非空，不强制二者相等。未来矩阵以现有目录Id为稳定键、Class作一致性检查，作者Id只读展示；不能把Warehouse自动改成Warehouse_V1破坏旧引用和摘要。

同Id在不同Profile指向不同Class、同Class有多个Id别名的跨Profile一致性，目前未见全局强制检查。矩阵应先诊断冲突，拒绝猜测合并，不自动重写旧数据。

## 7. 最小数据/UI方案与尚缺能力

### 优先方案：复用三Profile，不加第二套持久化目录

|策划矩阵字段|实际存储/查询来源|
|---|---|
|TemplateId|现有TemplateOption.Id|
|TemplateClass|现有TemplateClass|
|RoomType|现有AllowedTypes；跨Profile不一致显示Mixed/展开每Floor，不静默合并|
|Allowed Floors|该Id出现在对应Profile.AllowedTemplates的FloorIndex集合|
|Floor1/2/3 Weight|对应Profile中该Id的现有Weight；不准入的列显示“禁用/不适用”|

这是对现有数据关系的视图，不新增权重权威或三个模板Blueprint。名单和有效Weight已有UPROPERTY与资产序列化来源，Advanced已有RulesView和Save Floor Profile，可保存并供后续Generate读取；本轮未实际修改/保存重载权重。

矩阵应放在Advanced，基础界面保持简洁。启用某Floor时创建该Profile的合法Option；禁用时仅从该Profile移除。保存前逐Profile验证非空、每Floor最多16、Id/Class/类型/权重合法；只保存具名Profile。改动使旧批次/Prepared目录过期，需明确提示Clear/Prepare/Regenerate，不自动移动已注册房间。

**这一路线的界限需要用户接受：**

1. Allowed Floors以三个Profile名单为唯一作者权威，不是新增Class默认值中的、独立于Profile的不可绕过全局限制。
2. 禁用列不声称仍保存此前被移除的Weight；若要求禁用后仍永久记住该列原值、或单一模板资产集中约束所有未来Floor，就需要新的持久化策略字段/来源，先另审，不能在UI里隐藏第二份配置。
3. 当前只讨论Floor1/2/3，不静默把它扩展到任意FloorIndex。
4. 完整可靠权重仍须第8节求解器增量；单纯矩阵只能整理现有配置，不能修正选择语义。

若用户要求显式持久化AllowedFloors与三Weight字段，本报告不直接定稿字段存放位置。应先决定唯一数据权威；不要同时在Blueprint、每FloorProfile和第三份表保存互相冲突的概率。现阶段推荐不增运行时数据字段，优先做上述派生视图。

## 8. 建议的最小实施白名单（尚未获实施批准）

为保持P03冻结结果，建议**显式新空间选择版本、旧版本2原逻辑保留**。可评估使用已有SpatialPlanningVersion=3作为新权重选择策略的选择器；当前Resolver/Planner只接受2，所以这只是未来提案，不是可立即填入3就能工作的功能。

|拟修改文件（相对Source）|最小职责|保护要求|
|---|---|---|
|RoguelitePrototype/Private/RogueliteLoopAwarePlanning.cpp|仅新选择版本：按模板Weight确定模板组优先级，组内才随机合法端口/姿态/走廊；避免Branches跨模板全洗牌与Cycle姿态数替代模板权重；识别新版本|**受保护B4-B2B核心，必须单独批准。** 不改拓扑、坐标求解、占用、预算、MinLoops或Static行为；版本2保持|
|RoguelitePrototype/Private/RogueliteFloorProfile.cpp|明确接受并传播选择版本；保持Graph版本/RoomType/12–15米/HF03-C参数不动；必要的友好配置错误|受保护共享Resolver，必须批准；不自动修改Defaults或现有Profile版本|
|RoguelitePrototypeEditor/Private/RogueliteDesignerPanel.cpp|Advanced跨三Profile矩阵、冲突/禁用诊断、具名保存；复用现有ProfilePaths/SaveProfile|Editor-only；不新建窗口、Catalog、运行时依赖或自动Generate|

实施时的配置范围至多是现有三个具名FloorProfile资产的Approved权重/名单/显式选择版本；F0没有改动它们。先在独立测试配置证明后，再经批准应用正式Profile，不批量迁移旧地图。

允许另行批准新增独立测试源码，例如 `Private/Tests/RogueliteFloorTemplateWeightTests.cpp`、Editor对应的新测试文件；这些是**建议新文件名，当前未创建**。不需要更改C2、RoomGraph、RoomTypeAssignment、RoomTemplate/ParametricRoom、C1、B4-B3导航或HF03-C几何源码。

现有Editor/Runtime实例化摘要包含固定“Spatial2”家族标记，但完整源配置及StablePlanDigest也参与摘要，新空间版本已在其中；最小方案不为改标签扩大到Materialize或Runtime核心。若后续要求改命名空间，必须追加精确审批，不能越过本白名单。

上述三文件是“楼层名单/权重矩阵＋正式选择语义”的建议最小范围，**不包含第6节Start/Final资格新功能**。若要求同期处理安全屋隔离，应另审SeedTemplateOption角色字段、TemplateOrder接口/所有调用方及默认字段对旧签名的影响，不将未知额外文件自动纳入批准。

### 选择策略合同建议

- 硬过滤先于权重：Floor名单 → RoomType/角色（若未来实现）→ Capacity/合法Socket → 当前可用几何候选。
- 再对合法模板按Weight做稳定不放回优先级；同模板内部的姿态/长度候选使用独立空间流。候选失败只试同一合法集合中的后续模板，不放宽Floor/类型/端口条件。
- 保留有限重试、完整几何与C2检查；不能为贴近统计比例接受非法布局。
- Weight定义为合法模板的抽签/尝试优先级，不承诺生成完成后严格配额；若策划要求“每层必有某模板”或精确比例，那是另一种配额约束，不能伪装成Weight。

## 9. 测试方案及旧系统回归范围（全部待执行）

1. **纯选择统计**：至少10000个固定Seed、相同容量/类型/几何资格的两个合成模板，以1:9、9:1、1:1检查第一选择的合理变化；分开统计抽签结果与最终装配，允许统计误差，不要求每层比例精确。
2. **确定性**：同Seed/Profile/目录/环境重复，候选顺序、TemplateId、空间摘要一致；目录输入重排不变；加入无关Actor/GUID但环境几何相同不改变选择。
3. **楼层硬限制**：三个Floor分别允许/禁用同Class；禁用模板0次入选。高权重禁用模板、候选全禁用、强制失败重试、可选回环回退都不能越过名单。
4. **配置非法**：空名单、重复Id、跨Profile Id/Class冲突、Weight=0/负数/>10000、>16/每Floor、类型最低配额不可满足应拒绝，并指出Floor/Id/原因。
5. **容量与几何**：Degree3排除两门模板，Loop端点计额外连接；门洞方向/非零Offset/不同尺寸造成的拒绝记录齐全，不牺牲C2/通行。
6. **Editor/Runtime一致性**：先Editor准备对应目录，再同Profile/Seed比较实际NodeId→TemplateId与Transform；变Weight/名单后旧目录失效，不能带旧成功状态继续。
7. **配置持久化**：经官方MCP保存、卸载/重载指定测试Profile，矩阵与原数组一致；Clear/Regenerate不丢有效权重；禁用列的“不记住被删旧值”若采用本方案必须明示。
8. **空间样本**：至少Floor1 20 Seeds、Floor2/3各6 Seeds；按节点Degree/RoomType/Tree/Loop记录模板初选、候选数量、拒绝原因、最终模板、重试及失败分母。不是仅报告生成成功总数。
9. **历史回归**：旧Spatial2同Seed结果、V1/V2图与Golden、旧模板/地图SHA、C2唯一预约/真实碰撞/双向胶囊、HF03-C四组合/500cm/POI、12–15米Tree与Loop、B4-B3严格导航/Character MoveTo、清理与Static注册。
10. **角色扩展若另获准**：Start普通Combat禁止/安全屋缺失时明确拒绝；普通Final出口与最终Boss资格区分。未实现前只记录风险，不声称安全屋验收。

F0没有运行上述任何测试或编译，没有把旧阶段通过记录当成新的权重验证。

## 10. 安全、恢复和待批准事项

- 原模板、Warehouse/Junction3、NPC1/AI_Controller、Boss、CastleDemo、旧PCG、旧地图和用户未提交修改保持保护。本轮只写本报告；旧阻断全文保留在附录，未额外创建备份/证据文件。
- 审查开始/结束计算路径与文件SHA的聚合摘要，以下组均相同；不是用Git状态代替忽略文件的检查。摘要算法：每组按路径排序，拼接“相对路径|文件SHA256”，再计算SHA256。

|保护组|文件数|开始/结束共同摘要|
|---|---:|---|
|Plugins/RoguelitePrototype/Source|133|`683c9a99ec4502d30243c0140489656cbb5e9aa7c5e965722b4159215b002377`|
|Content/Roguelite|27|`8ca3972881993232fa81aaf412865772085ec8ff7527edda0784e31543e3a51b`|
|Content/Demo|17|`4a35abda4d154bb2168215389c86eb0dfdae0df8833a92bef0f2b4c7b272dd28`|
|Source/CastleDemo|13|`3daaff1e95051b19f8a8de36a8c0df32a721c5060b700d857049cfa190b99dd7`|
|Config|4|`0002f0890435c77270fe0b6c524dacc564e99e7171c281f6df8722444bd83ac1`|
|Docs|80|`26989bd8750854f622d6364d2ccb2cca127c9d9b208c869c86a1fbb5bade8c4a`|

`AGENTS.md`、`testfordemo.uproject`、`Content/NPC1.uasset`、`Content/AI_Controller.uasset`也分别校验未变。Git仍只有原有跟踪修改：Demo地图及uproject，未触碰。六个已读资产package dirty=false；当前地图/非PIE状态未变。

未来三文件审批基线SHA（审批/施工前须再核对，防止用户后续修改）：

- `E:/Unreal Projects/PCG_AI_demo_1/Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteLoopAwarePlanning.cpp`：`3512b2d5d79bd8f1a9317039621060f9382ae70bb22986d7ec18ae8592ce36d8`
- `E:/Unreal Projects/PCG_AI_demo_1/Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteFloorProfile.cpp`：`e22a8fb11d7c0bf143c0bf08c299b4354cfc59e4fadf15b2f526f41590cb5c16`
- `E:/Unreal Projects/PCG_AI_demo_1/Plugins/RoguelitePrototype/Source/RoguelitePrototypeEditor/Private/RogueliteDesignerPanel.cpp`：`b70709106ec07ca527a5ae086eeb9fd8326e39580465c3410159fb0c33460649`

**本轮恢复**：仅本报告有更新；旧阻断全文在附录可查，游戏内容没有回滚需求。未来实施必须先逐文件备份、记录具名Profile原字节和Schema；撤销仅比较恢复获批差异，保留后续用户改动，不整仓Reset/Clean，不删除缓存，不提交/推送。

**待用户批准/决定：**

1. 是否接受“既有三Profile名单与Weight为唯一作者数据，矩阵为派生视图”方案；禁用列不额外保存被删的旧Weight。
2. 是否批准第8节三份源码，尤其受保护Loop-Aware选择逻辑的新版本增量及对应独立测试/限定资产范围。
3. Start/Final角色资格是否另开任务；在它被验证前，不将Start=Combat宣传为安全屋模板保障。
4. 若需要全局Class级AllowedFloors、禁用权重历史保留或超过三层，需先定唯一数据权威并重新评估白名单，不能在本方案下静默扩展。

**停止点：F0范围评估完成，等待具体实施批准。不启动P03-F1、P04-A、P04-B或Gameplay。**

## 附录：恢复前的BLOCKED_MCP原报告（历史，已被上述结论替代）

<details>
<summary>展开查看原始阻断与待审记录；不是当前状态</summary>

# P03-F0 — Floor-Specific Room Template Weights Scope Review

日期：2026-10-31（Asia/Shanghai）

**状态：BLOCKED_MCP — 只读审查尚未完成，不构成实施批准。**

## 连接门禁与实际操作

- 目标工程目录已核对：`E:/Unreal Projects/PCG_AI_demo_1`，目标工程为`testfordemo.uproject`。
- 本机`D:/UE_5.8/Engine/Build/Build.version`读取为5.8.4 / CL58902052；这仅证明安装版本，不证明编辑器当前运行。
- `Get-Process -Name UnrealEditor`未找到运行中的编辑器。
- 对`http://127.0.0.1:8000/mcp`和`http://127.0.0.1:8001/mcp`分别尝试MCP initialize，两者均返回`URLError / WinError 10061`（连接被拒绝）。没有成功握手、工具发现、真实Editor工具调用或运行中工程身份核验。
- 因用户明确要求MCP不可用时立即停止，本轮未继续读取模板选择实现、未调用资产写入工具，没有切换Unreal Python或Computer Use。
- 本轮只新增本报告。未修改源码、FloorProfile、Blueprint、地图、PCG、配置或执行状态文档；未编译、PIE、生成或保存游戏资产。
- Git既有跟踪修改为`Content/Demo/L_CastleDemo.umap`、`testfordemo.uproject`，本轮未处理这些修改。未Reset/Clean/Commit/Push或删除缓存。

## 待完成的11项审查

|交付项|当前结论及恢复后核查内容|
|---|---|
|1. 已实现Template Weight能力|未核实。需追踪FRogueliteSeedTemplateOption.Weight是否真正进入候选排序/加权选择，分别回答Editor与Runtime；不能凭字段存在判定有效。|
|2. 已实现FloorProfile配置能力|未核实。需读取实际FloorProfile定义及当前资产配置，确认每Profile是否拥有独立模板选项、权重、启停与资格。|
|3. Editor/Runtime选择路径|未核实。需沿FloorProfile→共享GenerationSettings→TemplateCatalog→空间规划→实例化追踪，并核对Template Selection随机流。|
|4. 尚未实现的楼层专属配置|未核实。需区分现有逐Profile配置与用户要求的Allowed Floors、Floor1/2/3权重矩阵；不能先认定需要新增系统。|
|5. Start/Final选择风险|未核实。需区分拓扑角色与RoomType，并核对实际角色资格过滤；Start=Combat不能自动证明未来安全屋模板隔离。|
|6. 建议字段/结构|待证据后提出。优先复用TemplateId、AllowedRoomTypes、TemplateClass、Weight；本轮没有批准或实现任何新增结构。|
|7. 最小修改白名单|尚未形成。需要实际调用关系和缺口证据后才能列出，不预先批准修改冻结模块。|
|8. 是否涉及受保护核心|未判断。C2、RoomGraph、B4-B3安全合同、HF03-C几何及既有模板继续保护；如确需修改，另列精确范围等待批准。|
|9. 能否仅靠配置实现|未判断。重点回答同TemplateClass跨Profile权重差异、按Floor禁用、硬性准入以及保存重载能力。|
|10. 测试及回归范围|尚未执行。后续方案应包含固定Seed复现、非法/空目录拒绝、Floor硬限制不放宽、RoomType/角色/Socket过滤、空间回退分布偏差及新旧Editor/Runtime回归；F0不运行构建或测试。|
|11. 安全恢复|本轮仅新增本报告，无游戏改动需要恢复。后续若批准实施，应先建立具体文件/资产SHA基线与逐文件备份，保留用户修改，禁止全仓回退。|

## A–F回答状态

A（Weight实际消费）、B（不同Profile不同权重）、C（楼层禁用）、D（Start安全屋隔离）、E（空间筛选/回退分布偏差）、F（最小增量位置）均为**待源码与实际配置审查**。不能将本连接阻断记录作为这些问题的技术答案。

## 最新设计要求的性质

用户本轮提出以手工完整RoomTemplate替代独立大型POI随机摆放、动态大型据点组合和特殊地形运行时改造。这是本次F0的审查约束，尚未核对原文档冲突，也未修改技术规范或实现代码。

## 恢复条件与停止点

正常打开目标UE5.8.4工程并启动Epic官方Unreal MCP后，重新执行initialize、工具发现、至少一次真实只读调用和工程身份核验。全部通过才恢复F0只读调查，再将本报告补充为证据支持的完整范围评估。

当前停止于BLOCKED_MCP。不启动P03-F实施、P04-A施工、P04-B AI或Gameplay。

</details>
