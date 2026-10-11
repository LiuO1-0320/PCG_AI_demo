# RoguelitePrototype — P00.5 / P01 / P02 / P03-A / P03-C1

独立的 Roguelite 开发区，仍使用根目录的 `testfordemo.uproject` 和 UE 5.8.3。

## v2.1 当前设计入口（DOC-UPDATE-01，2026-10-12）

未来开发以根 `Docs/03_DECISIONS_AND_OPEN_QUESTIONS.md` 和 `Docs/01_FINAL_DESIGN_SPEC.md` 的 v2.1 为准。正式可通行房间连接必须包含参数化走廊，首版水平直线；长宽高独立编辑。P03-A Direct 原实现/地图/证据保留为底层与测试用途，**Superseded 的是正式默认连接规则，不是其验收成果**。

Fog Transition Corridor 替代单层 Fog Gate，局部雾的自由移动/渐进遮挡揭示与未就绪安全等待属于后续任务。5–10 秒是待测体验目标，雾参数/性能预算未定，不是计时或加载保证。正式入未清理战斗房、队友安全进入、封入口后才无限 Prep；仅正式入下一房才遗弃旧房 Downed，进入走廊不触发。

DOC-UPDATE-01 当轮仅更新文档，未实施走廊/雾/状态机；其“未找到 P03-C1 正文”是当时记录。随后用户正式批准 P03-C1，现已实施下方独立直线走廊 Actor，人工验收待做。该范围不包括迷雾、加载同步、战斗门控、随机地牢或 AI。后续可只读评估 CastleDemo 纯拓扑，仍不能引入其游戏依赖。下方及各 Pxx 报告是当轮真实记录；其未测试项不因设计更新变成通过。

## 目录与依赖

- C++：`Plugins/RoguelitePrototype/Source/RoguelitePrototype`。
- 资产：`Content/Roguelite`，对应 `/Game/Roguelite`。
- Runtime 公开依赖 `Core`、`CoreUObject`、`Engine`；P03-A 增加私有依赖 `NavigationSystem`，仅查询可用导航，不修改导航配置。
- Editor Target 额外私有依赖 `UnrealEd`，用于初始地图创建命令。
- 插件 `EnabledByDefault=true`，`CanContainContent=false`；资产使用项目内容目录，而非插件挂载点。
- 不依赖 CastleDemo、PCG、AI 或 Boss。现有项目默认地图和 GameMode 保持不变。

## 使用测试地图

1. 编译新插件后，正常重新打开当前 `.uproject`。
2. 在 Content Browser 打开 `/Game/Roguelite/Maps/L_Roguelite_Dev`。
3. World Settings 的 GameMode Override 应为 `RogueliteDevGameMode`。
4. 地图保留初始地板和灯光；P01 在 `(10000,0,0)` 增加了参数化房间和手工测试 POI。点击 Play 仍使用引擎自带 `SpectatorPawn`，没有武器或具体玩法。
5. Output Log 应出现 `ROGUELITE_DEV_START`，`PawnClass=/Script/Engine.SpectatorPawn`、`LegacyActorCount=0`。

地图由新插件自己的 `RogueliteDevMap` commandlet 通过 UE Editor API 创建。它只处理这个固定地图路径，遇到已有同名地图会拒绝覆盖。不要用旧 Demo 的重建脚本生成此地图。

## 验证与共享限制

证据保存在 `Development/P005`：Git 初始状态、37 个旧文件的 SHA-256 基线、原始 `.uproject` 副本、构建日志及地图创建日志。

2026-10-11 的实际结果：

- 本机 `Engine/Build/Build.version` 为 UE 5.8.3。已核对本机 `PluginManager` 对项目 Runtime 插件和 `EnabledByDefault` 的支持；无需在 `.uproject` 增加插件条目。
- `testfordemoEditor Win64 Development` 增量构建成功，退出码 0；新插件 DLL 已生成。未做全量清理式重编、Game Target 构建或打包。
- 临时 null-RHI 编辑器和用户正常重启后的编辑器均通过 Epic 官方 MCP 启动、检查并停止了真正 PIE。日志确认独立 GameMode、实际玩家为 `SpectatorPawn`、`LegacyActorCount=0`；地图未变脏。
- 临时实例已关闭。用户编辑器最后停在新测试地图且 PIE 已结束；画面、键鼠操作与人工游玩未验收。
- 新地图资产依赖为引擎 Cube、编辑器灯光图标、NavigationSystem 和本插件脚本，没有旧 Demo 资产或 CastleDemo 脚本依赖。37 个旧基线文件和 `.uproject` 均未改变。

两次 PIE 的 MCP 结果分别在 `mcp-verification.json` 和 `primary-editor-verification.json`；最终文件校验与新增清单在 `final-verification.json`。

现有项目启用的 GameFeatures 缺少 `GameFeatureData` Asset Manager 规则。该错误在 P00.5 之前的编辑器日志中已存在；地图创建命令实际保存地图后，引擎仍因这两条全局错误返回退出码 1。不要把该退出码报告为命令整体成功，也不要在本任务擅自修改全局 Asset Manager 配置。

插件隔离代码和资产职责，仍共享引擎插件、Editor Target、构建缓存、输入默认设置、渲染与导航配置。

临时并行编辑器出现旧 HTTP 服务端口 5353 占用错误；本次 MCP 使用仅绑定 `127.0.0.1` 的独立端口 8001，验证完成后随临时实例关闭。今后优先使用单个正常编辑器实例。

## 安全撤销

先正常关闭引用新模块的编辑器，并保存另处备份。将本轮新增的插件目录和 `Content/Roguelite` 移出项目即可停用；任何移动前必须核对目标路径与新增文件清单。不要回滚整个项目，也不要覆盖已有未提交的 `.uproject` 和旧 Demo 地图。构建产物可在下一次正常构建中更新，无需批量清理。

后续如果这里已有新的开发成果，不能再按 P00.5 的整目录撤销方式操作。

## P01 参数化房间

P01 交付时的 `ARogueliteParametricRoom` 使用引擎 Cube 和五个固定 Static Mesh Component，通过 Details 独立调整净长、净宽、墙高及两种厚度。`ManualPOIActors` 只检查手工内容的 Bounds，不修改它们。P02 扩展后的组件数量见下节；关闭四侧门洞时保留 P01 外壳行为。未新增模块依赖或修改 `.uproject`。

使用与人工补验见 `P01_ROOM_GUIDE.md`，交付结果见 `P01_RESULT.md`。`Development/P01` 保存真实编译日志、107 项 MCP 检查、用户重启后读取/截图证据和逐文件备份；仅撤销 P01 时不要移走整个插件或整个 `Content/Roguelite`。

本轮还按根目录约定更新了 `Docs/06_EXECUTION_STATUS.md` 的 P00.5 记录，其原始副本位于 `Development/P005/06_EXECUTION_STATUS.before-P005.md`。撤销该文档记录时先核对后续修改，不直接覆盖恢复。

## P02 可编辑门洞与 Door Socket

四侧门洞分别配置启用、自动居中、手工厘米偏移和净宽/净高；合法开口由左右墙段与横梁组成，非法配置保留原始值、警告并恢复完整墙。固定 13 个网格组件及 4 个编辑器箭头；已有 P01 五个网格组件保留复用，没有 Tick 或重复创建组件。查询接口提供门槛的局部/世界位置和朝外方向。

请打开专用 `/Game/Roguelite/Maps/L_Roguelite_DoorSocket_Dev`，选择 `P02_DoorSocketRoom`。原 P01 地图布局未修改。使用说明见 `P02_DOOR_GUIDE.md`；交付、限制与仅针对 P02 的恢复方法见 `P02_RESULT.md`。真实编译、358 项官方 MCP 检查、Details 输入补验和 PIE 证据位于 `Development/P02`。P02 人工体验已由用户在 P03-A 指令中确认通过；P02 本身没有实施拼接、PCG、AI 或战斗门控。不要再次按 P00.5 的整目录撤销方式操作。

## P03-A 两房直接拼接

新增 `ARogueliteRoomAssembly`，复用 P02 Socket，在 Details 指定两个房间/Socket 后点击 Align；固定 A，只移动及直角旋转无手工 POI 的 B。独立记录逻辑连接与预约，Direct 验证参数化占用、射线、Pawn 胶囊通行和门槛地面支撑，可明确重新检查或释放连接。其他走廊模式只预留数据定义。

专用地图 `/Game/Roguelite/Maps/L_Roguelite_RoomAssembly_Dev` 中 A 为 2000×2000，B 为 3000×2500，B 南门偏移 300 cm。P01/P02 原地图和源码保持不变。使用及接口见 `P03A_ROOM_ASSEMBLY_GUIDE.md`，真实结果与安全恢复见 `P03A_RESULT.md`。最终 Editor Target 编译成功；236 项主检查、13 项最终补验及真正 PIE 启停通过。用户在 P03-C1 正式启动指令中确认 P03-A Editor 人工验收通过；当轮 NavMesh 不可用，导航仍未验证，原报告/证据保持历史原件。未进入 P03-B、PCG 或 AI。

## P03-C1 参数化直线走廊

新增独立 `ARogueliteParametricCorridor`，Details 分别编辑长度、净宽、净高、墙厚、地板厚。沿局部 X，完成面 Z=0；两端开口规格可小于内腔，固定端框封住差额。十个固定 Static Mesh 加根/两个编辑器箭头共 13 个编辑器组件，不整体缩放 Actor、不 Tick。Start/End 端点内部直接复用 P02 `FRogueliteDoorSocket`，提供局部/世界门槛、朝外方向、开口尺寸与有效性；连接状态字段仅预留，C1 不建立预约。

新图 `/Game/Roguelite/Maps/L_Roguelite_Corridor_Dev` 仅在 X=90000 的独立区域增加走廊与可移开测试障碍，保留复制来的 P01 布局。最终走廊 2000×500×350，墙厚 25、地板厚 30、两端开口 200×250 cm。使用说明见 `P03C1_CORRIDOR_GUIDE.md`，实际交付和精确恢复范围见 `P03C1_RESULT.md`。

`testfordemoEditor Win64 Development` 实际成功，退出码 0；官方 MCP 333 项主检查、11 项 Details 实际输入补验通过，包含真实射线、Pawn 胶囊 Sweep/Overlap、保存卸载重载及真正 PIE。证据在 `Development/P03C1`。P03-A、P01/P02、旧 Demo/Boss/PCG、默认设置及 `.uproject` 未修改；本轮未增模块依赖。人工交互、Undo/Redo、NavMesh、真人角色与打包仍未验证。

P03-A 的 Direct 执行 API 只接受 Room，不能直接完成 Room→Corridor→Room。P03-C2 需独立走廊连接执行器、连接器引用/端点记录及与 Direct 共用的唯一预约权威；本轮未改 P03-A，未实施两端自动对接或迷雾。完成后停止等待人工验收。
