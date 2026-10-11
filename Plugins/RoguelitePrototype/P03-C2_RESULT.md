# P03-C2_RESULT — Room–Corridor–Room Assembly

日期：2026-10-12。UE 5.8.3（CL 58210709），主工程 `E:/Unreal Projects/PCG_AI_demo_1/testfordemo.uproject`，基于 Git `2b36a47`，未暂存/提交。

**结果：C2 技术自动验证完成，等待用户 Editor 人工验收。** Room A→水平直走廊→Room B、原子回滚/唯一预约、真实碰撞与最小 PIE Deferred Spawn 已实测。局部导航构建后完整路径通过；重载/重启受默认 Agent 高度144影响，必须显式局部重建，未证明持久或运行时导航。不启动 P03-B/PCG/AI/Fog。

## 1. 实际修改文件与资产

既有 C++ 最小修改：

| 文件（插件 Source/RoguelitePrototype 下） | 内容 |
|---|---|
| Public/RogueliteRoomAssembly.h | StraightCorridor 入口、计算/查询/复查 API、扩展既有连接记录与结果 |
| Private/RogueliteRoomAssembly.cpp | 模式分派、共用复查/释放与预约、目标子图保护；Direct几何/胶囊算法保留 |
| Public/RogueliteParametricCorridor.h | 预约缓存说明；外壳参数、端口接口不重写 |
| Private/RogueliteParametricCorridor.cpp | 端点 Connected/ConnectionId 从 Assembly 记录查询；无新的预约容器 |

新增 C++：

- `Private/RogueliteCorridorAssembly.cpp`：现有 Assembly 的直走廊计算/原子拼接/参数化占用/实际碰撞与导航。
- `Public/RogueliteAssemblyTestRig.h`、`Private/RogueliteAssemblyTestRig.cpp`：仅 C2 地图的显式局部导航测试、可关闭的 PIE 原生 API 探针，无 Tick。
- `Public/RogueliteCorridorAssemblyMapCommandlet.h`、`Private/RogueliteCorridorAssemblyMapCommandlet.cpp`：只创建固定路径的新空地图，发现已存在即拒绝覆盖。

唯一新游戏资产：`Content/Roguelite/Maps/L_Roguelite_CorridorAssembly_Dev.umap`。从 NewBlankMap 创建全新测试 Actor，不复制旧地图/POI。局部 GameMode 为既有 RogueliteDevGameMode；主测试、回归 Actor、局部 NavBounds/Recast、灯光/PlayerStart 均在本图。

新增本报告、`P03C2_CORRIDOR_ASSEMBLY_GUIDE.md` 和 `Development/P03C2/` 的脚本/备份/日志/JSON/截图。既有文档仅增量修改六份：根 AGENTS（当前C2保护范围）、Docs/02（真实接口/限制）、Docs/03（本轮授权与Agent未决）、Docs/06（验证状态）、prompts/P01_TO_P13_TASKS（批准C2范围）、插件README（当前使用入口）。

**未修改** `.uproject`、`.uplugin`、Build.cs、任何全局 Config、P01/P02 Room源码、旧 Demo/Boss/PCG/所有旧验收地图与报告。最终逐文件核查详见 `Development/P03C2/final-audit.json`。既有未提交 `Content/Demo/L_CastleDemo.umap` 和 `.uproject` 保留；无 Reset/Clean/add/commit/push。

## 2. 接口、连接数据与唯一预约

Runtime C++/Blueprint 可调用，Editor按钮不是唯一执行途径：

```cpp
ComputeCorridorTransforms(RoomA, SocketA, Corridor, EndpointFacingA,
                         RoomB, SocketB, OutCorridor, OutRoomB, OutReason);
TryAlignCorridorConnection(RoomA, SocketA, Corridor, EndpointFacingA,
                          RoomB, SocketB);
RevalidateConnection(ConnectionId);
ReleaseConnection(ConnectionId);
GetConnectionRecords();
GetRoomSocketConnectionId(Room, Side);
GetCorridorConnectionId(Corridor);
```

`ComputeCorridorTransforms` 仅预检规格并计算，不移动、不登记，**不声称碰撞/导航通过**。真正 Align 才进行占用、物理检测和登记。Start或End都可映射到A，另一端自动映射到B。

沿用 `FRogueliteRoomConnection.Id` 作为 ConnectionId，保留 RoomA/B、SocketA/B、Mode、bActive、几何/导航及诊断；追加 Corridor、EndpointA/B、ValidationStatus（NotChecked/Passed/Failed）、ValidationRevision。结果增加两处门槛距离/法线、两目标Transform、走廊参数占用、恢复标志及实际路径点/Partial。复用 P02 `FRogueliteDoorSocket`；C1端点仍内嵌该结构。

所有 Registry Actor 的 `Connections` 是唯一预约数据；跨 Registry 查询检查冲突。Corridor端点的 bConnected/ConnectionId只是由该数据计算的缓存。两处物理配对属于一条逻辑连接。重复同一 Room/Socket/Corridor连接复用GUID；反向Start/End映射可以明确更新同一记录；改变Room Socket须先释放原连接。整个走廊为该单条连接预约。

Disconnect/Release幂等，不移动/删除Actor，不关闭/重配门洞。验证失效不会自动释放预约；显式 Disconnect 后才可供其他连接使用。B承担其他连接时拒绝移动，未开发整子图刚性装配。

## 3. Direct 兼容与模式分离

Direct 的计算及原24项物理探针保留；`Align Selected Rooms` 按模式分派，StraightCorridor调用独立实现，其他模式仍拒绝。StraightCorridor只要求 A↔C、C↔B 两处门槛对齐/法线反向，不要求两个Room直接贴墙。

Direct 与 StraightCorridor复用规格/旋转/POI约束、预约、房间参数占用、复查/释放。对Direct的最小新增限制是拒绝移动已有其他连接的目标B，以及考虑新走廊占用，避免破坏当前子图。

本轮在全新图成功建立 B北门→另一测试Room南门的Direct分支，真实几何/胶囊通过；尝试以Direct抢占主连接的A北门被拒绝；释放Direct分支后C2可继续Align。P03-A原图/历史报告未改。

CornerCorridor、CustomConnector仍仅枚举预留；未来Fog可复用当前Corridor引用/端口/状态，但未实现走廊路径规划、Fog、加载、门控或随机拓扑。

## 4. 真实编译、MCP与创建过程

- 基线 `testfordemoEditor Win64 Development`：Succeeded、退出0、2.59秒。
- 首次主实现构建：Succeeded、退出0、311.61秒。
- 最终模块增量构建：`Development/P03C2/build-final.log`，Succeeded、退出0、43.04秒。UE5.8.3实际UHT/UBT；MSVC14.44.35228、Windows SDK10.0.22621.0；串行编译，无Clean/打包。中间诊断/创建器构建日志保留。
- 地图创建器第一次在命令行新空世界检测墙/地板全部无命中，拒绝并未保存；失败原日志 `map-create.log`、`map-diagnostic.log` 保留。改为只计算初始Transform、无预约，然后在完整Editor真实验证。最终 `map-create-final.log` 实际保存新图；既有GameFeatureData Asset Manager两条全局错误仍使命令整体退出1，不能称创建命令整体成功。
- 实际官方Epic MCP localhost8001（绑定127.0.0.1）成功 initialize/list_toolsets/describe_toolset；真实发现并串行使用 Scene/Actor/Object/Asset、EditorApp、Slate、Logs。没有Nwiro、PCG或虚构工具。
- 本轮五份主验证JSON记录 **306个检查均为真**、1125个工具调用记录（含UI观察/点击等，不是1125种测试）。`validation.json` 在186项后因C1按钮处于Details可见区域下方而中断，保留真实异常；只读核对并折叠分类后 `validation-tail.json` 接续24项，未重置旧图/重复创建测试Actor。另有回归44、导航/重载/PIE21、最终模块31。

## 5. 成功、失败与原子恢复证据

主保存配置：A 2000×2000×400、N门offset250/墙厚40；B 3000×2500×500、S门offset-300/墙厚35/地板厚35；C 2000×500×350、墙厚25/地板厚30，开口均200×250 cm。

A固定 `(0,0,0)`；C计算为 `(2040,250,0)`；B为 `(4575,550,0)`，Yaw0、Scale1。门槛位于 `(1040,250,0)` 和 `(3040,250,0)`；两处距离均0、法线dot均-1。还实测初始居中、C长度2500再2000、A yaw90/180/270配合不同B侧门、End→A反向端点、内腔恰等开口200×250后恢复500×350。

| 案例 | 实际结果 |
|---|---|
| 重复Align/Disconnect两次/重连 | 单条GUID复用；释放无移动，端点缓存空闲；重连单记录 |
| 改长度后Revalidate | 标记Failed、诊断门槛不对齐，不自动移动或释放；Align修复 |
| Room Socket关闭/宽度不兼容/非法offset9999 | 拒绝；原offset配置保留，不静默覆盖 |
| Corridor开口宽600>内腔500 | 拒绝，原配置保留；恢复200后可重连 |
| B整体Scale2、C yaw45、A yaw45 | 拒绝、不改变输入/其他Actor；手动恢复合法值后可连 |
| 另一参数房间占据预期走廊 | 参数化矩形占用拒绝；预期边界接触正常允许 |
| 跨Registry抢占Room Socket/同一Corridor | 拒绝；主预约不变 |
| B还有Direct分支/ManualPOIActors | 拒绝移动，不改变分支或手工POI |
| 偏离中心射线20cm的10cm障碍 | 中心Visibility射线未命中，真实Pawn胶囊命中，C/B移动后原子恢复 |
| 原生空目标/未知Endpoint/未知RoomSide/未知ConnectionId | 最终PIE API逐项拒绝，具体原因不为空，无崩溃/额外登记 |

失败预检不移动；碰撞阶段失败恢复C/B的原始Transform、无新预约/GUID。原连接的Room/Socket/Corridor/模式/GUID预约拓扑不改变，但最近诊断/有效性更新为Failed，避免保留旧成功。这里“恢复记录”指预约/连接关系，不把失效诊断恢复成历史成功。失败探针记录阻挡组件路径与Sweep距离。

## 6. 碰撞、Capsule与导航

每次成功完整物理验证 **59项**：38条Visibility射线（两处开口网格采样、双方实体侧墙/横梁/内部地板、走廊墙/顶/底）+21个 `ECC_Pawn` Capsule Sweep，另检查起终点Overlap。胶囊半径34、半高88、净距2 cm；包括两个接缝各正/反/边缘通行和三点向下地板支撑、内部及横跨A→C→B全路径的正/反/边缘Sweep。没有忽略房间/走廊本体，没有用SpectatorPawn穿墙代替通行证据。非参数化阻挡物在移动目标的占用区域内也需通过物理占用检查。

代表证据：`final-proof.json → final_connection.LastResult.Probes`，全部通过；门槛地板支撑Sweep在移动2 cm时命中Floor组件，法线向上；偏心Cube失败见 `validation.json → cases[post-move collision rollback]`。

局部NavBounds仅本图，20000×20000×2000 cm；TestRig调用真实 `UNavigationSystemV1::Build`，仅Recast实例设35×180，不修改SupportedAgents/全局Config/类默认值。实际 `ProjectPointToNavigation` + `FindPathSync`，禁止部分路径，完整4点：

```text
(0,0,10) → (1007,190,10) → (3021,304,10) → (4575,550,10)
PathSuccessful=1 Partial=0 Points=4
```

**Navigation限制实际存在**：保存卸载重载和最终Editor重启后，默认Recast Agent为35×144，不足以代表高176的胶囊；即使等待后仍返回Unavailable，原始读回在 `nav-delayed-readback.json`。最终诊断明确Data/Agent/Required/BuildOrLocked。显式Build Local Navigation后两次真实路径通过；日志还出现AgentAlreadySupported/AgentNotValid及NavData替换/移除警告，不能把该试验NavData当持久或Runtime有效数据。正式Agent与运行时导航准备需后续单独决定。没有擅自全局改配置。

## 7. 最小 Runtime、重载与回归

两次真实PIE启动/运行状态检查/停止。最终 `p03c2-editor-final.log`：

```text
Computed=1 RoomAStatic=1 CorridorStatic=1 RoomBStatic=1 Begun=1/1/1
Runtime native API success=1 Geometry=1 Capsule=1 Probes=59 Connections=1 AUnchanged=1
PostBeginPlayStaticMoveRejected=1
InvalidNativeInputsRejected=1
P03C2_RUNTIME_RESULT Passed=1
ROGUELITE_DEV_START ... SpectatorPawn LegacyActorCount=0
```

Runtime探针在PIE远处预配置Deferred Actor参数/Offset，计算Transform后FinishSpawning；登记使用同一NativeAPI与真实碰撞。改变End映射需要移动已BeginPlay的Static外壳，明确拒绝且保持两Transform/记录。探针不测试Runtime导航、不代替完整生成器、玩家Character运动或动态子图；Stop后不写入保存地图。

保存→卸载到引擎Template_Default→重载真实执行。主参数/Transform/GUID/映射/预约保留；PostLoad将旧验证置NotChecked、几何缓存false，重新Revalidate后几何/胶囊通过。导航重载限制见上节。再次编辑器重启也读取同一记录；地图最终保存且clean，PIE已停，本轮两个自有临时Editor均已关闭。

P01：全新回归房间2000×2000、3000×2500、独立墙高650；固定18组件、墙/地板碰撞、缩回后旧碰撞消失；POI位置/旋转/Scale不变，缩小时实际Bounds越界警告带Room/POI路径。P02：四侧逐一开/闭，开口无碰撞、两侧/横梁命中、关闭恢复，组件仍18。C1：新测试走廊独立长1500/宽600/高450、13组件/端点/原生胶囊与墙顶底射线通过。Direct回归见第3节。所有测试只保存本轮新图。

## 8. 人工 Editor 验收

按 `P03C2_CORRIDOR_ASSEMBLY_GUIDE.md` 的六步操作：选择P03C2_Assembly，在Details的Roguelite Assembly Actions找到三个按钮；确认Setup引用/模式；改变长度/Offset/Yaw并显式Align；放阻挡物观察拒绝/恢复；显式局部Nav构建与重新验证；保存重开、PIE/Stop、Disconnect两次再连。

实际截图已采集并查看：`Development/P03C2/assembly-overview.png`、`seam-A-C.png`、`corridor-to-B.png`、`assembly-details.png`。视口为Unlit并显示局部导航，截图不替代物理探针或真人输入验收。

**待用户验收**：按钮/Details实际操作、手动观察接缝、Undo/Redo与键鼠游玩；真人Character步行/台阶体验。本轮没有真人游玩、打包或Game Target验证。

## 9. 已发现风险与后续前置问题

1. 导航全局Agent144与本轮胶囊176不匹配；局部180测试NavData不能保证重载/PIE注册，后续生成器前需明确Agent与导航重建/等待合同。本轮只保持真实Unavailable/Passed证据。
2. Static外壳可以Deferred预定位生成，不能据此声称已BeginPlay后可自由移动/变形；重新布局/动态导航尚未验证。编辑器当轮成功不能替代运行时结果。
3. B有手工POI或其他连接会拒绝自动移动；模板内部结构/子图整体刚性移动不在本轮。修改Room尺寸仍不移动或变形POI。
4. 连接改尺寸后需显式Revalidate/Align，缓存不是自动实时安全证明；未用门洞封堵、门控、Fog/内容Ready、多人或随机拓扑均未做。
5. 共享GameFeatures全局错误仍在；插件共享EditorTarget/引擎/导航/渲染环境，不代表完全隔离。部分Revalidate日志沿用P03-A的Room上下文前缀，实际Mode/记录为StraightCorridor。

## 10. 仅本轮安全撤销

**未执行恢复。** 修改前10份既有文件已逐份备份于 `Development/P03C2/Backups/`；`baseline-files.json` 为301份既有非生成文件SHA256基线，`backup-manifest.json`列确切10个目标与新地图路径，`final-audit.json`列实际变更/新增与备份校验。

若需撤销：先正常保存个人内容并关闭引用此模块的Editor；额外保存当前C2图/源文件，核对没有C2之后的新改动，再经用户针对性确认逐文件恢复**4份既有源码+6份文档**。仅将本轮5个新增源码、本轮新地图、2个新增报告/指南及C2证据移至授权备份位置；保留可恢复备份，不永久删除。新图含TestRig等C2类，撤销代码时应同时退出/退存这张图，避免缺类加载。

按正常EditorTarget重编译匹配DLL，不批量清理缓存、不覆盖历史图或整插件。不执行全仓Reset/Clean，不恢复用户原有`.uproject`/Demo地图，不按P00.5整目录撤销。恢复前须逐文件比较，不能盲目覆盖后续工作。

完成后停止，等待C2人工验收及下一阶段明确指令。
