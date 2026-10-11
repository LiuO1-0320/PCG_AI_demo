# P03-A 交付记录 — 两房 Socket Direct 拼接

## 本轮任务 / 目标

用户已在 P03-A 指令中确认 P02 人工验收通过。本轮只增加两间房的直接拼接，不开发随机地牢、走廊、PCG、AI、Boss 或门控。工作区为 `E:/Unreal Projects/PCG_AI_demo_1/testfordemo.uproject`，UE 5.8.3。

## 检查发现 / 相关现有资产

完整复用 P02 `ARogueliteParametricRoom::TryGetDoorSocket`，没有改写房间外壳、门洞或 POI 检查。门槛在墙体外表面，局部 Z=0；北/南/东/西分别沿 +X/−X/+Y/−Y。存在隐藏网格和箭头，不能把 Actor 视觉 Bounds 当作占用。

开发前记录 Git 状态并备份三个既有目标文件。原 `.uproject` 和旧 Demo 地图已有未提交修改；本轮保留，没有暂存或提交。新地图经 Epic 官方 MCP 复制 P01 测试图到独立路径，原图仅读取；复制的原始地板、灯光、P01 房间/POI 留在新图原位置，P03-A 测试区位于 X=50000 cm 远处。

## 实际改动（文件 / 资产路径）

| 对象 | 本轮变化 |
|---|---|
| `Source/RoguelitePrototype/Public/RogueliteRoomAssembly.h` | 新增 Actor、连接模式/记录、验证结果与查询证据结构 |
| `Source/RoguelitePrototype/Private/RogueliteRoomAssembly.cpp` | 新增 Direct 对齐、占用预约/检测、失败恢复、碰撞/胶囊/导航检查及三个编辑器按钮 |
| `Source/RoguelitePrototype/RoguelitePrototype.Build.cs` | 最小增加私有 `NavigationSystem` 依赖 |
| `/Game/Roguelite/Maps/L_Roguelite_RoomAssembly_Dev` | 新地图；只在新图放置 P03-A 房间及测试辅助 Actor |
| `P03A_ROOM_ASSEMBLY_GUIDE.md`、本报告 | 新增使用、接口、验收及恢复说明 |
| `README.md` | 更新依赖、P02 人工确认及 P03-A 入口 |
| 根目录 `Docs/06_EXECUTION_STATUS.md` | 最小记录 P02 用户确认和本轮真实结果 |
| `Development/P03A/` | 新增 Git/文件基线、备份、构建日志、专用 MCP 验证脚本、JSON 证据与截图；构建也更新本插件生成产物 |

表中未以根目录开头的文件均位于 `Plugins/RoguelitePrototype/`。资产的实际文件为 `Content/Roguelite/Maps/L_Roguelite_RoomAssembly_Dev.umap`。精确新增/修改清单及 SHA-256 见 `Development/P03A/final-verification.json`。

### 接口与扩展数据

- `ARogueliteRoomAssembly` 在 Details 配置 RoomA/SocketA、RoomB/SocketB、ConnectionMode；按钮 Align / Revalidate / Disconnect。
- `TryAlignRoomToSocket(FixedRoom, FixedSocket, TargetRoom, TargetSocket, Mode)` 返回 `FRogueliteAssemblyResult`，可在 Blueprint 调用。A 固定，B Scale=1；失败不新增记录，B 保留或恢复原 Transform。
- `FRogueliteRoomConnection` 保存 GUID、两个 Room/Socket、模式、活动占用、上次几何/胶囊有效状态、导航状态和原因。重复对齐同一对端点复用 ID；其他连接的预约必须先释放。
- `FRogueliteAssemblyResult` 保存建议 Transform、参数化占用、门槛距离/法线、胶囊尺寸、恢复结果及每条真实查询的起终点/命中组件。`RevalidateConnections` 只检查；`ReleaseConnection` 只释放，不移动或封门。
- Direct 与通用端点、门规格、预约检查分开。Direct 才要求外墙直接邻接并移动 B；StraightCorridor、CornerCorridor、CustomConnector 仅为序列化模式定义，实际调用明确拒绝，未生成走廊。
- 未来可复用连接数据及 P02 Socket，增加连接器实体/生成和对应占用、胶囊、导航验证，再扩展模式分派。可能追加路径/连接器引用和设置；固定两端的走廊可能另增创建接口，当前移动 B 的接口不是已完成的通用走廊系统。

## 验证证据（编译 / MCP / PIE：已运行或未运行）

### Built

实际命令使用 `D:/UE_5.8/.../UnrealBuildTool.dll`，目标 `testfordemoEditor Win64 Development`，MSVC 14.44.35228 / Windows SDK 10.0.22621.0。

| 构建 | 日志 | 结果 |
|---|---|---|
| 修改前基线 | `build-baseline.log` | Succeeded，退出码 0，2.53 秒 |
| 新增模块/依赖后的构建 | `build-p03a.log` | Succeeded，退出码 0，559.02 秒 |
| 门槛地面支撑补充后的最终构建 | `build-p03a-ground.log` | Succeeded，退出码 0，32.23 秒；最终 DLL 已在新临时编辑器加载并测试 |

没有 Clean、全量重编、Game Target 或打包。三个日志位于 `Development/P03A/`。

### Editor-configured / MCP

本轮 Codex 原生工具列表没有 Unreal 调用入口，实际使用 UE 官方 ModelContextProtocol 的 localhost:8001 HTTP JSON-RPC。两个本轮自建临时编辑器都执行真实 `list_toolsets` / `describe_toolset` 或加载后工具发现，调用按游戏线程顺序执行；只绑定本机，未安装其他 MCP 替代品。

实际调用包括 SceneTools 放置/查找 Actor 和 Trace，ObjectTools 读取实际 schema 并修改属性，ActorTools 获取 Transform/组件，AssetTools 复制并仅保存新地图、读取依赖，EditorAppToolset 启停 PIE/截图，Slate Inspector 真实点击 CallInEditor 按钮。没有调用旧 P01/P02 验证脚本。

最终代码的 `validation-ground.json`：**236 项主检查全部通过，29 个场景，908 次记录的官方工具调用**。`final-editor-ground.json`：**13 项补验全部通过**。计数包含设置/只读验证，不能把 249 项检查当作 249 次碰撞测试。

| 测试 | 真实结果 |
|---|---|
| A 2000×2000、B 3000×2500，居中 North/South | 成功，门槛距离 0 cm、法线 dot=−1，A Transform 不变、B Scale=1 |
| A 偏移 +100、B 偏移 −250 | B 世界 Y=350 cm，按门槛而非中心对齐，原始配置保留 |
| A Yaw 90/180/270；North/South、North/North、East/West、North/East | 成功，B 得到合法直角旋转；B 门偏移 +300 保留 |
| 不同墙厚、地板厚与墙高 | A/B 地面 Z=0 对齐，几何/碰撞通过 |
| 第三房间占据 B 候选空间 | 拒绝，未移动 A/B，未新增连接 |
| 门关闭、非法偏移/宽度、宽高不兼容、胶囊不适配、Scale2、Yaw45、无效 B | 明确拒绝，具体 Room/Socket 原因在本机 UTF-8 日志；Transform/记录保留 |
| 其他 Assembly 占用同一端点 | 拒绝；同一连接再次对齐不产生重复记录，ID 不变 |
| B 带 ManualPOIActors 引用 | 拒绝；缩小后实际 Bounds 越界提示存在，POI 位置/旋转/Scale 不变 |
| 修改 A 长度后 Revalidate | 标记连接无效，B 不动；明确 Align 后恢复 |
| 保存、卸载、重载 | 尺寸/偏移/Transform/连接 ID/预约保留；旧验证状态变成未检查；再次实际 Revalidate 通过 |
| 不存在的 Socket 名称 | MCP 枚举转换拒绝，配置/Transform/连接不变；没有伪称调用过非法枚举字节的 C++ 分支 |

最后保存状态：A `(50000,0,0)`、2000×2000×400；B `(52540,−300,0)`、3000×2500×500；Yaw=0、Scale=1。A 北门与 B 南门均 200×250，B 南门 ManualOffset=300；门槛均 `(51020,0,0)`。两房间 ManualPOIActors 为空。两个占用矩形在 X=51020 接触；没有实体占用交叉。

### 碰撞 / Capsule / 导航

每次成功 Direct 验证记录 **24 个原生查询**：9 条开口射线均无阻挡；4 条侧墙、2 条横梁、2 条地板射线正确命中；4 次水平 Pawn 胶囊 Sweep 加起终点 Overlap 均通行；3 次门槛向下胶囊 Sweep 命中地板。默认胶囊半径 34、半高 88 cm，边缘偏移 ±64，底部净距 2 cm。

门槛三次向下 Sweep：从 `(51020,−64/0/+64,90)` 到同 XY 的 Z=86，均在 **2 cm** 后命中 B 的 `Floor`，且法线向上。这是地面支撑证据。

10 cm 小障碍放在开口侧向 Y=20、Z=100：9 条开口射线全部畅通，中央胶囊仍被阻挡。系统拒绝连接，并恢复 B 的完整原 Transform；障碍移回本轮辅助区后成功。这证明没有用 Visibility 射线替代角色通行检查。

复杂射线恰好在公共地板边界 X=51020 曾漏检；X±0.001 cm 均命中 100 cm。保留失败和诊断证据，并补充上述真实胶囊地面支撑。未声称精确共边复杂射线通过；容差默认 0.1 cm。

**Navigation=Unavailable：当前没有覆盖门两侧且适配胶囊的可用 NavMesh；导航未验证。** 代码具备数据可用时投影和完整路径查询，实际可用导航通过/失败分支未测试。没有创建 NavMesh 或修改全局导航设置。

### P01 / P02 回归、PIE

回归全部在新地图辅助房间执行：2000×2000、3000×2500、单独墙高变化及恢复时四墙/地板射线正确；扩大后旧碰撞移除；每侧门逐一关闭时墙体命中 80 cm，重新打开无旧墙碰撞；固定组件集合不变，每房间 18 个编辑器组件。手工 POI Transform 不变、真实 Bounds 越界诊断通过。P01/P02 源码和原地图未修改。

最终新地图真实 StartPIE，IsPIERunning=true，然后 StopPIE=false；日志 `ROGUELITE_DEV_START` 确认 `RogueliteDevGameMode`、引擎 SpectatorPawn、`LegacyActorCount=0`，没有 CastleDirector。地图依赖仅 NavigationSystem、引擎 Cube/材质/灯光图标和 RoguelitePrototype，没有 CastleDemo 或 `/Game/Demo`。

已实际获取并查看 `supplement-ground-overview.png` 和 `supplement-ground-door-seam.png`。最终地图 is_dirty=false。两个本轮临时进程已关闭，没有结束其他进程。截图与 JSON/原生日志均在 `Development/P03A/`。

## 人工 PIE 操作步骤与期望结果

重新打开主工程，先打开新拼接图，选择 `P03A_Assembly`。

1. 检查 A/B 尺寸和 B 南门偏移 300，点击 Align；两门衔接，A 不动、B Scale1。
2. 释放连接并改变 A 的直角旋转/所选 Socket，再拼接；观察门方向及房间中心可以错开。
3. 改尺寸或门偏移，Revalidate 应报无效且不移动 B；点击 Align 才重新对齐。
4. 给 B 添加独立 POI 引用，拼接应拒绝；缩小尺寸时 POI 不动，Details Validation/Output Log 可定位越界对象。
5. 保存、切换地图、返回后重新检查；进入 PIE 观察几何和独立 GameMode，再停止。真人键鼠体验、角色走门、Undo/Redo 和 NavMesh 仍待人工验收。

完整操作见 `P03A_ROOM_ASSEMBLY_GUIDE.md`。本轮没有声称已运行真人控制的碰撞 Pawn；胶囊证据来自原生 Sweep。

## 问题 / 风险 / 回滚

- 当前官方 MCP 会损坏部分 UObject 中文字符串；本地 UTF-8 原生日志及编辑器 UI 用于定位具体原因。Slate 点击成功不保证操作已发生；已使用 Hover/编辑器 tick 和状态读取验证，不盲目重试。早期脚本/UI 同步失败和捕获可选参数错误均保留，正确调用补验已通过。
- 连接有效状态是“上次检查”；修改参数或 Undo 后须明确 Revalidate，重载不会继续展示旧碰撞验证为当前通过。失效连接继续占用端点，需修复或释放。
- 当前 P01 房间组件是 Static。本轮验证编辑器拼接及保存场景进入 PIE，未验证 BeginPlay 后移动已注册 Static 房间或实际 Blueprint 调用拼接节点。未来运行时生成应考虑延迟生成/注册时机；不为本轮改写 P01 Mobility。
- 其他斜转房间的 AABB 占用可能保守拒绝；本轮支持直角、Scale1。附加 Actor 拒绝分支、非法枚举字节的原生调用、可用 NavMesh 路径分支、Game Target/打包未实测。
- 全局共享 Editor Target、NavigationSystem、插件和 UE 缓存；已有 GameFeatures 缺少 GameFeatureData 扫描规则的错误仍在日志，没有擅自修改项目全局配置。
- 旧文件保护校验与三份既有文件备份在 `baseline-files.json` / `backup-manifest.json` / `Backups/`；本轮仅改变备份列出的既有目标，旧 Demo/Boss/PCG、`.uproject`、默认地图及 P01/P02 原资产保持本轮前内容。

**仅撤销 P03-A，尚未执行：**正常关闭编辑器，先备份本轮清单；归档新 `L_Roguelite_RoomAssembly_Dev.umap` 和两个 RoomAssembly 源文件，再单独比较撤销 Build.cs 的 NavigationSystem 一行及 README/进度文档本轮记录。备份不应覆盖之后的修改；新说明/验证目录可单独归档。重新编译 Editor Target 更新生成产物。不要移动整个插件/整个 Roguelite Content，不执行 Reset/Clean，不覆盖已有 `.uproject` 或旧地图。

## 建议下一项任务（不要自动执行）

等待 P03-A 人工编辑器验收。未开始 P03-B 或任何走廊、PCG、AI。
