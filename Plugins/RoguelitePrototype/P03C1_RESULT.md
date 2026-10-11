# P03-C1 参数化直线走廊交付记录

日期：2026-10-12。用户已确认 P03-A Editor 人工验收通过并正式授权 C1。工程 `E:/Unreal Projects/PCG_AI_demo_1/testfordemo.uproject`，实际 UE5.8.3。状态：**AUTO-PIE，等待用户人工验收**。未进入 P03-C2/P03-B。

## 本轮任务 / 目标

独立 Runtime Actor `ARogueliteParametricCorridor`：可分别编辑长度、净宽、净高、墙厚、地板厚，自动更新地板/左右墙/天花板与两个端部开口，输出兼容 P02 的 Start/End Socket。仅水平直线；保留 P03-A Direct，未改其实现，也没有实现两端自动房间连接。

未来正式通行连接必须包含走廊。C1 不实施迷雾、Niagara、加载/准备同步、门控、POI/PCG装饰、随机拓扑或 AI；长度未绑定5–10秒体验目标。

## 检查发现 / 相关现有资产

- 实际引擎 `D:/UE_5.8/Engine/Build/Build.version`：5.8.3，Changelist58210709。MSVC14.44.35228，工具链目录14.44.35207，Windows SDK10.0.22621.0。
- 查阅真实 AGENTS/工程 Skill、v2.1规范/决策/进度、P03-A报告、Room/Door/Assembly接口。`WORKFLOW_AGENT.md` 未在工程找到，未创建替代文件。P03-A原报告及原不可用NavMesh证据保留，人工通过来自本次用户确认。
- 原 Room 的 `FRogueliteDoorSocket` 已有局部/世界门槛位置、朝外方向、开口宽高、墙厚、有效性与原因，直接复用；未另造房间Socket系统。
- P03-A具有 Direct/StraightCorridor/CornerCorridor/CustomConnector模式定义，但执行器只支持Direct，参数限于Room。C1无需修改它，下一阶段不能把Corridor强转成Room。
- 实施前记录Git状态。原有 `Content/Demo/L_CastleDemo.umap` 与 `.uproject` 的未提交修改保留；其他大量未跟踪文件也保留，没有暂存或提交。

## 实际改动（文件 / 资产路径）

| 类型 | 实际路径 | 内容 |
|---|---|---|
| 新C++ | `Plugins/RoguelitePrototype/Source/RoguelitePrototype/Public/RogueliteParametricCorridor.h` | Actor、端点类型/数据、编辑参数、查询与显式碰撞诊断入口 |
| 新C++ | `Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteParametricCorridor.cpp` | 固定组件/自动更新/校验/端框/Socket/原生Sweep实现 |
| 新地图 | `Content/Roguelite/Maps/L_Roguelite_Corridor_Dev.umap` | 官方MCP复制并仅保存新路径；独立C1测试区 |
| 新文档 | `Plugins/RoguelitePrototype/P03C1_CORRIDOR_GUIDE.md`、本报告 | 编辑器说明、真实结果、限制与恢复范围 |
| 既有文档增量 | 根 `AGENTS.md`、`prompts/P01_TO_P13_TASKS.md`、`Docs/03_DECISIONS_AND_OPEN_QUESTIONS.md` | 登记本次正式批准范围与P03-A人工确认，替代此前仅计划状态 |
| 既有文档增量 | `Docs/06_EXECUTION_STATUS.md`、插件 `README.md` | 当前C1结果、证据/人工待验收、C2接口障碍；历史记录保留 |
| 新验证脚本 | `Development/P03C1/corridor_mcp.py`、`editor_session.py`、`setup_map.py`、`validate_corridor.py`、`verify_details.py`、`capture_final.py`、`audit_final.py` | 独立C1工具/测试，拒绝覆盖已有证据或操作旧测试图 |
| 新证据/备份 | `Plugins/RoguelitePrototype/Development/P03C1/` | 基线、5份文档原件、构建日志、MCP JSON、截图、关闭记录、最终逐文件清单 |

**没有修改** Build.cs、插件配置、P03-A/P01/P02源码和地图、CastleDemo/Boss/PCG、默认地图/全局配置或 `.uproject`。C1未增加模块依赖，复用同一Runtime模块内P02数据与UE原生组件。

独立地图复制了干净 `L_Roguelite_Dev`，复制来的P01房间/POI、地板与灯光保留，没有调用旧P01/P02验证脚本重置用户布局。新 `P03C1_Corridor` 位于 `(90000,0,0)`；胶囊负例障碍 `P03C1_Blocker` 最后移到 `(100000,5000,100)`，不会挡住主走廊。原三张验收地图没有改写。

### 参数与几何

| 参数 | 新建默认cm | 新图保存值cm | 合法要求 |
|---|---:|---:|---|
| CorridorLength | 1000 | 2000 | 100..100000，且大于2×墙厚 |
| CorridorWidth | 400 | 500 | 内腔净宽100..100000 |
| CorridorHeight | 300 | 350 | 内腔净高100..100000 |
| WallThickness | 20 | 25 | 1..100000，同时用于天花板/端框 |
| FloorThickness | 20 | 30 | 1..100000，向地面下方延伸 |
| DoorWidth / DoorHeight | 200 / 250 | 200 / 250 | 两端统一，至少1，不超过内腔宽高 |

XY中心、完成面Z=0、长度沿X。外部占用Box为 `[-L/2,+L/2] × [-W/2-T,+W/2+T] × [-F,H+T]`。Actor Scale须为1、Pitch/Roll0、Yaw为90°整数倍；原Transform不会被校验器擅自修正。

固定10个Static Mesh组件：4个外壳、两端各左右封板/横梁；加根及两个Editor-only箭头，共13个编辑器组件，重复修改引用/数量不变。构造/属性通知更新已有组件，无Tick。端框厚度从阈值外表面向内延伸；开口小于内腔时填补左右/顶部差额，等大时停用零尺寸端框碰撞。预计非编辑器构建移除箭头，但该构建/组件计数未验证。

合法实体采用 `BlockAll + QueryAndPhysics`，开口没有堵路Mesh，箭头无碰撞。非法外壳保留作者原值，停用全部外壳显示/碰撞并使Socket无效；合法外壳中的非法开口恢复两端整面封板。`ValidationWarnings`与Output Log `P03C1_CORRIDOR_WARNING`含Actor路径/属性/原因，箭头转红；没有弹出通知。

### Socket接口

`FRogueliteCorridorSocket` = `Endpoint(Start/End)` + 原P02 `FRogueliteDoorSocket` + `bConnected`/`ConnectionId`预留字段。C1不创建连接，后两者始终false/空GUID，也不提供持久化预约。

- `GetCorridorSockets()` / `TryGetCorridorSocket(Endpoint,OutSocket)`：每次计算当前局部/世界阈值、朝外方向、开口规格/有效性。Start局部 `(-L/2,0,0)`朝-X，End局部`(+L/2,0,0)`朝+X。Door.Side的South/North只是局部侧映射，稳定标识应使用Endpoint。
- `IsRoomSocketCompatible(...)`：合法性与相同开口宽高（0.1cm容差），无需内腔宽高等于开口。只检查规格，不承诺实际对齐、接缝、占用或导航通过。
- `GetLocalOccupancyBounds()` / `GetWorldOccupancyBounds()`：参数占用Box，不受箭头/视觉Bounds干扰；非法外壳返回无效Box。
- `RebuildCorridor()` / `CheckPassage()`：Details显式按钮。后者使用真实Pawn通道胶囊Sweep与起终Overlap，不忽略走廊；修改/重载后旧结果失效。

以上已通过UHT/编译，实际MCP读取了自动重建的Socket数据并调用两个编辑器按钮；BlueprintPure查询节点/规格检查方法的直接Blueprint调用尚未验证。

最终保存的Start世界位置 `(89000,0,0)` 朝 `(-1,0,0)`，End `(91000,0,0)` 朝 `(1,0,0)`，开口均200×250、合法未连接。

## 验证证据（编译 / MCP / PIE：已运行或未运行）

### 真实构建

UBT目标 `testfordemoEditor Win64 Development`，`-WaitMutex -NoHotReloadFromIDE -NoUBA -MaxParallelActions=1`：

| 构建 | 结果 | 退出码 | 时间 | 证据 |
|---|---|---:|---:|---|
| 修改前基线 | Succeeded | 0 | 3.05s | `Development/P03C1/build-baseline.log` |
| 新走廊实现 | Succeeded | 0 | 389.21s | `Development/P03C1/build-p03c1.log` |

后者完成UHT、新源文件/模块生成代码编译、DLL/lib及元数据构建。未执行Clean或Game Target/打包。模块后来已被真实Editor加载并实际生成原生Actor，不仅是文件存在检查。

### 官方MCP与实际编辑器

当前Codex客户端没有原生Unreal工具入口；实际使用Epic官方ModelContextProtocol插件的 `127.0.0.1:8001/mcp`，先真实`list_toolsets`/`describe_toolset`，读取当前Scene/Actor/Object/Asset/EditorApp/SlateInspector/Logs schema，随后严格顺序调用。没有Nwiro、并发游戏线程调用或外网监听。

- `editor-session.json`：真实Toolsets/schema、启动/项目/PIE/源图clean预检。
- `map-setup.json`：只复制至新C1图、只保存新路径、加载、生成新走廊。
- `validation.json`：**333/333断言，19个证据记录组，456工具调用**。断言包括重复状态读取，不能理解为333种独立玩法验收。
- `details-validation.json`：**11/11补验，25工具调用**，实际点击数字框/键入Length2000→2200→2000。无Rebuild点击即可自动更新端点、移除旧端框碰撞，恢复后保存新图。
- `final-editor-readback.json`：恢复之后重新执行原生Check Passage，另4次实际Sweep通过；保存值/Socket/警告及截图元数据。
- `unlit-captures.json`、`details-screenshot.json`：真实截图/相机。已查看 `corridor-details-editor.png`、`corridor-opening-unlit.png`、`corridor-overview-unlit.png`。最初Lit内腔偏暗；另一个`corridor-details-ui.png`实际只截到ViewMode弹出菜单，保留原记录，但不作为Details截图证据。

| 测试 | 实际结果 |
|---|---|
| Length1000→2000单独修改 | Socket到±1000；地板/墙/顶更新；旧端框消失；缩回旧长壳碰撞消失 |
| Width400→600单独修改 | 内腔/端框/地板/顶更新；阈值不漂移；旧宽墙/地板/顶碰撞消失 |
| Height300→450单独修改 | 侧墙/顶/横梁更新；门槛Z=0；旧高/低顶碰撞消失 |
| Wall/Floor独立修改 | 35/40测试通过，恢复20无残留 |
| 内腔大于开口/完全等大 | 400×300内腔与200×250开口合法收口；200×250等大停用端框且通行 |
| 重复编辑 | 同13个组件引用，Actor Scale1，无重建重复组件 |
| 非法外壳 | Length=-20、Width=0、Height=80、Wall=0、Floor=-3、Wall=600保留值并报警、端点无效、碰撞停用；恢复正确 |
| 非法开口 | Width=500/0、Height=350/-5保留值并报警；两端封板实际命中，恢复后无旧封板 |
| 直角旋转 | Yaw90/180/270位置/方向/胶囊检查通过；MCP改Transform后显式Rebuild，未据此冒充人手视口拖动验证 |
| 保存/卸载/重载 | 真正切至Engine模板图再载回C1；尺寸/规格/派生端点正确，13组件，旧胶囊结果失效，再次检查通过 |

### 碰撞证据

主验证实际177次 `SceneTools.trace_world`，不是视觉估计。最终内腔500×350时：局部Z100向下100cm命中地面；向左右墙在Y±250命中；向上250cm命中Z350天花板底面；两端开口内Z=1/125/249射线无命中，左右实体封板/顶部横梁命中端面100cm。每轮缩放还检测旧几何曾占区域为空；Details实际输入补验另有2条端框射线。

原生 `CheckPassage` 默认胶囊半径34、半高88、净距2cm，Pawn通道。每组前向偏移-64/0/+64及中心反向4条Sweep，另各有起终Overlap；主验证36条实际Sweep记录，最终补验另4条。合法默认、等大开口、直角旋转和重载通过；合法DoorWidth60仍因胶囊放不下明确拒绝。

负例：在路径偏心20cm放10cm方块，中心Visibility射线无命中，中心胶囊Sweep却在500.488cm命中该障碍StaticMeshComponent；正/反向均失败，移开后恢复四条通过。证明胶囊测试确实使用实体体积，并未忽略自壳或把Spectator移动作为依据。

**没有测试实际Room↔Corridor接缝，也没有NavMesh连通验收**；C1无自动对接功能。当前扫掠通过仅表示所测几何/碰撞通行。

### PIE与保护

实际 `StartPIE → IsPIERunning=true → StopPIE → false`。`p03c1-editor.log`：

```text
ROGUELITE_DEV_START Map=UEDPIE_0_L_Roguelite_Corridor_Dev GameMode=/Script/RoguelitePrototype.RogueliteDevGameMode PawnClass=/Script/Engine.SpectatorPawn LegacyActorCount=0
```

停止前 `is_dirty=false`，专用PID4460已受控结束、端口8001停止监听；仅结束本轮自建进程。`editor-closed.json`退出码1来自受控进程终止，不能当作正常UE退出验收，也不能将其混为UBT退出码。未关闭其他编辑器/程序。

`baseline-files.json`覆盖253个既有非生成文件；5份文档是允许改动，另248份保护文件SHA256不变、无丢失。包括P01/P02/P03-A源码/地图/证据、旧Demo/Boss/PCG、配置、Targets和已有脏`.uproject`。`final-verification.json`列出具体差异/新增路径及备份校验。未覆盖原验收证据、未暂存/提交/Reset/Clean。

## 人工 PIE 操作步骤与期望结果

1. 正常打开工程，再打开新 `L_Roguelite_Corridor_Dev`；Outliner选 `P03C1_Corridor`，清空Details搜索、按F聚焦。Lit偏暗时视口Lit菜单选择Unlit。展开Corridor Dimensions，各参数可单独编辑，Scale保持1。
2. 分别改长1000/2000、宽400/600、高300/450和两厚度；观察外壳及两端绿色箭头，无重复网格。End Openings设200×250，较大内腔两端应仍有封板/横梁。
3. Validation点击 `Check Passage / 检查胶囊通行`，Passage Check查看结果和四条证据。非法开口应保留输入、红箭头/警告与封板；非法外壳停用，恢复后再次检查通过。不要用Spectator穿墙判断碰撞。
4. 保存并切至其他图再返回，只操作C1图；参数保留，旧通行结果变为未测，再次点Check Passage。补验撤销/重做、鼠标拖动编辑及警告可见性。
5. Play观察后Stop，独立DevGameMode、无旧地牢生成。真人碰撞角色、NavMesh、Blueprint查询节点、Map Check UI、BeginPlay后运行时重建与Game Target/打包均仍待专门验证。

## 问题 / 风险 / 回滚

### C2兼容性与最小扩展

**现有P03-A接口不足以直接执行Room A→Corridor→Room B，但没有阻止C1Actor成立的架构障碍。**

可复用：P02阈值/方向/开口规范、P03-A连接模式/逻辑端点数据，以及C1 Start/End与参数化占用。C1内腔独立于开口、端部向内封框，为连接平面收口提供几何基础。

缺口：P03-A `TryAlignRoomToSocket`只接受 `ARogueliteParametricRoom*`且拒绝非Direct；`FRogueliteRoomConnection`只有两个Room/Side，没有Corridor引用或端点映射；`ValidateEndpoints`/`IsOccupied`/`CheckOccupancy`为私有，C1也未管理预约。

下一阶段最小方案：增加一个StraightCorridor编辑器执行入口；记录Corridor引用与Start/End映射；逐端根据实际Socket对齐位置/相反法线，失败回滚完整Transform；统一Direct与走廊的Socket预约权威；验证房间/走廊占用、接缝支撑/封边、开口Trace、角色Sweep与必要导航；保存重载后复验。C2可复用现有Door数据而不重造，但共用预约需要经批准的少量P03-A公开接口/连接记录扩展，不能保证完全零改动。Direct几何限制仍只属于Direct，不能套到所有模式。

C1没有提前创建连接器管理器、走廊网络、L型模块或大量空类，也没有伪称StraightCorridor已执行。Static组件的BeginPlay后移动/修改尚未验证；C2运行时生成宜先明确deferred spawn/注册前定Transform与尺寸的时机。

### 已知环境与未测试

- 既有GameFeatureData Asset Manager全局错误仍有两条，未扩大范围改全局配置。新图PIE仍实际启动/停止成功。
- 复制来的P01房间在加载/PIE时有 `P01_POI_NO_BOUNDS` 诊断；本轮未修旧Room或重置手工POI。这不是C1端点或碰撞通过的证明；原图/源码保持不变，也未重跑P01/P02全套回归。
- 官方MCP有部分中文文本序列化乱码，实际主窗口截图中文正常。部分可选结构需按当前schema传全字段；Slate截图空ref曾只截到菜单，改为真实主窗口ref后取得完整截图，原证据保留。
- 用户人工画面/交互、UndoRedo、Map Check UI、真人角色、NavMesh、Blueprint查询、非90°非法Transform/NaN/Inf、打包和运行时改尺寸未测试。查询函数编译与Socket属性读取不等于这些交互通过。

### 仅本轮安全恢复

以下是恢复方案，**未执行任何撤销**。先保存自己内容并正常关闭工程，再保留本轮新增文件/新图的独立备份，核对 `final-verification.json` 精确清单。

只归档本轮两个Corridor源文件、新C1地图、C1说明/报告与Development/P03C1证据；地图引用原生类，移走类之前必须先处理该新图。然后按本轮Backups与当前内容逐段比较，只撤销5份文档中本轮差异，不能覆盖其后续编辑。未来移出代码后正常重编Editor Target更新生成产物，不能把此前DLL当撤销成功证据。

不要移走整个插件或Content/Roguelite；不要回滚P01/P02/P03-A、旧Demo或脏`.uproject`，不要Reset/Clean/批量恢复。移动/删除需另行批准明确路径，当前只交付方案。

## 建议下一项任务（不要自动执行）

先由用户完成P03-C1人工验收。通过且明确批准后，再细化P03-C2两端房间连接/共享预约方案。当前停止，未自动开始P03-C2、P03-B或其他系统。
