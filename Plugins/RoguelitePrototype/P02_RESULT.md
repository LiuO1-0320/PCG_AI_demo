# P02 — 可编辑门洞与 Door Socket 交付记录

日期：2026-10-11（Asia/Shanghai）。状态：**实际编译、官方 MCP 自动验证、Details 偏移编辑和自动 PIE 通过；用户本人 P02 人工体验待验收。** 用户暂停重启后已授权继续；本次续接补齐文档与文件校验，没有重复构建或启动编辑器。

后续人工反馈：用户缩房后未在屏幕看到 POI 警告。已通过当前用户编辑器的官方 MCP 和本次日志只读核对：越界记录确实存在，实际打开 Output Log 并筛选后可见 `P02_ManualPOI` 的黄色 Warning。警告数组属于房间 Actor，当前房间恢复 3000×2500 后为空。现有实现没有 POI 视口红框/浮动通知，此项不能视为已经实现。此次只补正使用说明与记录，不改源码、地图或保存用户未提交布局；未重新编译。补验截图和文档修改前备份见 `Development/P02/Feedback_POI/`。P02 人工体验仍不记 PASS。

## 检查与边界

工程 `E:/Unreal Projects/PCG_AI_demo_1/testfordemo.uproject`，本机 `D:/UE_5.8` 的 Build.version 为 5.8.3。用户在 P02 开始前确认 P01 人工检查未发现异常，并正常关闭编辑器允许常规编译。

复用现有独立 `ARogueliteParametricRoom`，保留 P01 参数/地板/外墙/POI 检查。没有修改 CastleDemo、Boss、AI、旧 PCG、全局默认地图或 GameMode，没有 P03、门体或战斗门控。用户当前 P02 指令不含旧任务表中的两房拼接；未静默提前实施。

原 P01 地图已有用户调整后的 3000×3000×500 房间，与历史交付的 3000×2500 不同；本次保留现状，不恢复历史布局。

## 实际文件与资产

| 类型 | 路径 | 内容 |
|---|---|---|
| 修改现有 C++ | `Plugins/RoguelitePrototype/Source/RoguelitePrototype/Public/RogueliteParametricRoom.h` | 四侧配置、Socket 结构/查询、固定组件与编辑器更新声明 |
| 修改现有 C++ | `Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteParametricRoom.cpp` | 分段开口/碰撞、安全检查、可视箭头、重载更新；保留 P01 POI 保护 |
| 修改说明 | `Plugins/RoguelitePrototype/README.md` | P02 入口及与 P01 组件数量的历史区分 |
| 修改状态 | `Docs/06_EXECUTION_STATUS.md` | P01 用户人工确认、P02 实测与待验收状态 |
| 新说明 | `Plugins/RoguelitePrototype/P02_DOOR_GUIDE.md` | Details 使用、坐标、安全范围、五步人工验收 |
| 新报告 | `Plugins/RoguelitePrototype/P02_RESULT.md` | 本交付记录 |
| 新地图 | `Content/Roguelite/Maps/L_Roguelite_DoorSocket_Dev.umap` | 原 P01 图的独立复制及 P02 房间/POI；仅此图有资产写入 |
| 新验证脚本 | `Plugins/RoguelitePrototype/Development/P02/validate_door_sockets.py` | 顺序调用官方 MCP 的本轮专用测试；不运行/导入 P01 脚本 |
| 新补验脚本 | `Plugins/RoguelitePrototype/Development/P02/supplement_client.py` | 受限于专用 P02 地图的单次官方 MCP 调用、证据及图片保存 |
| 新证据/备份 | `Plugins/RoguelitePrototype/Development/P02/` | 四份 before-P02 备份、基线、构建/验证成功与失败记录、截图、暂停记录及最终清单 |

编译还正常更新插件 DLL/PDB、Intermediate 等生成文件和构建日志；不把生成文件当成新的游戏源码。逐文件 SHA256 及目录清单见本轮 `final-verification.json`，其自身哈希不自包含。

Build.cs、插件描述、Targets 和 `.uproject` 本轮未修改。模块依赖仍为 Runtime 的 Core/CoreUObject/Engine；Editor 构建额外私有 UnrealEd。没有 CastleDemo、PCG 自定义节点或第三方依赖。

## 实现与数据接口

四侧配置 `FRogueliteDoorConfig`：Enabled、Auto Center、ManualOffset、DoorWidth、DoorHeight。新房间默认门关闭；自动居中不改写保存的手工偏移。门宽高测试默认 200/250 cm，安全侧墙/横梁默认 50/20 cm，均可调。

每侧原 P01 墙组件作为完整墙或负轴侧墙段，另加两个固定 Default Subobject 作为正轴侧墙段与横梁。共 13 个 StaticMeshComponent；含根与 4 个编辑器 Arrow 为 18 个编辑器组件。合法门洞的正中不保留完整墙碰撞；关闭/非法时隐藏额外段并设 NoCollision，整墙恢复。无 Tick、动态重复创建或 Actor 整体缩放。

OnConstruction、编辑器属性/嵌套属性通知、编辑器移动及组件注册负责刷新；注册后刷新可重建 Transient 诊断，保存数据仍来自设计配置。手工 POI 不移动、缩放、删除、附加或写入组件配置。

`FRogueliteDoorSocket`：Side、bValid、LocalPosition、WorldPosition、LocalOutwardDirection、WorldOutwardDirection、DoorWidth、DoorHeight、WallThickness、AppliedOffset、ValidationMessage。门槛在墙体外表面，局部 Z=0。北/南 ±X，东/西 ±Y；北/南偏移正值 +Y，东/西正值 +X。

BlueprintPure `GetDoorSockets()` 现场计算启用侧并包含非法项；`TryGetDoorSocket(Side, OutSocket)` 仅启用且合法返回 true。只读 `DoorSockets` 数组是自动刷新的诊断。直接 Blueprint 节点调用尚未单独实测，C++/UHT 编译与实际诊断数据已验证。没有永久入口/出口、连接占用或战斗状态。

非法尺寸、余量或偏移**保留原始数据**，显示 `P02_INVALID_DOOR` 及具体 Room/Side/原因，标红、bValid=false并恢复整墙。合法范围见使用说明；缩房超限不会偷偷移动门。大非法偏移的红箭头可能在视口外，Details/日志仍可定位。

## 真实编译

基线和实现均实际构建 `testfordemoEditor Win64 Development`；最终日志 `build-p02-final.log` 为 Result: Succeeded、退出码 0、49.82 秒。实际使用 UE bundled dotnet/UBT、MSVC 14.44.35228、SDK 10.0.22621.0；没有清理式全量重编、Game Target 或打包。

源码最终修改时间早于成功生成 DLL 和最终日志；重启续接已再次核对。当前 SHA256、时间与 DLL 记录写入最终清单。未因文档变化重复编译。

## 官方 MCP 和编辑器证据

完整成功记录：[validation-transform.json](<E:/Unreal Projects/PCG_AI_demo_1/Plugins/RoguelitePrototype/Development/P02/validation-transform.json>)，**358 个检查全部通过**。报告含 initialize、真实 toolsets/schema、逐次调用与结果；临时实例只绑定 127.0.0.1:8001，编辑器调用按顺序执行。

真实使用 SceneTools 查图/放 Actor/加载地图/trace_world；ActorTools 读组件、Bounds、完整 transform；ObjectTools 发现属性 schema 并读写房间配置；AssetTools 复制原图到独立新路径且仅 save_assets([P02地图])；EditorAppToolset 选 Actor/截图/启动与停止 PIE；LogsToolset 读实际日志。SlateInspectorToolset 用于 Details 补验。

| 检查 | 实际结果 |
|---|---|
| 2000×2000 四门居中 | 四侧开口、局部/世界位置/方向正确；Socket 外表面距中心 1020 cm |
| 每侧独立启用/关闭、单独仅开一侧 | 对应开口/整墙与碰撞正确，其余侧维持关闭 |
| 北 +300、东 -250，调整到 3000×2500 | 保留偏移，随边界更新；北局部 (1520,300,0)，东 (-250,1270,0) |
| Actor 90/180/270° 旋转 | 各 Socket 世界位置与朝外方向正确 |
| 非法宽度 0/-200/2100、高度 500、偏移 9999 | 原值保留、无效 Socket、明确警告、整墙碰撞恢复 |
| 侧墙/横梁安全值为 0 | 原值保留并明确警告 |
| 门宽 1900、门高 380（墙高 400） | 精确合法边界通过，20 cm 横梁碰撞存在 |
| 偏移 ±850 与缩房 offset=600 | 合法端点通过；缩房失效不改值，再扩大恢复原开口 |
| 五次反复改尺寸与开关 | 同一组 18 个组件引用，无重复墙段或旧碰撞；POI 全配置不变 |
| 保存、卸载、重载 | 参数/POI 引用保留、Socket 诊断重建、几何与碰撞一致 |
| 真正 PIE | Start/IsPIERunning/Stop 通过；日志为独立 GameMode、SpectatorPawn、LegacyActorCount=0 |

### 碰撞证据

实际 `trace_world` 是原生 Visibility 复杂射线，不是通过 SpectatorPawn 的移动作判断。以下为四侧重复实测的共同结果，具体起终点在完整 JSON 的 checks.detail：

| 射线路径 | 预期 | 实测 |
|---|---|---|
| 门中心 Z=1/125/249 cm，跨墙 | 无墙命中 | null |
| 门中心两侧 ±130 cm，Z=100，跨墙 | 实体墙命中 | 200 cm |
| 门中部上方 Z=280，跨墙 | 顶部横梁命中 | 200 cm |
| 禁用或非法门配置后跨原开口 | 完整墙命中 | 200 cm |
| 地板由 Z=100 向下 | 完成面命中 | 100 cm |

### P01 回归与手工 POI

关闭全部门洞后实测原外壳：2000×2000 四侧内部射线 1000 cm；3000×2500 北/南 1500、东/西 1250 cm；地板顶面 100、由下往上底面 80 cm。墙高 700→400 后 Z=600 不再命中旧墙；墙/地板厚 50/40→20/20 后旧厚度区域无命中。原最小尺寸处理继续通过。

POI Location=(30900,600,100)、Yaw=30、Scale=(3,2,2)，模型、材质、BodyInstance、Mobility、相对组件配置、Bounds 在五次重建及重载前后相同。缩房到 2000×2000 时原点局部 X=900 仍在内，实际 Bounds 最大 X=1079.904，警告正确指出 `P02_ManualPOI`，未调整 POI。

### Details 输入补验

通过真实数字框输入北门 350 cm：`ui-edit-offset-350.json`；`ui-offset-350-properties.json` 确认 ManualOffset=350、Socket 局部 Y=350。此时 Y=425/Z=100 射线无命中，Y=225/Z=100 命中 200 cm，证明编辑器通知也更新几何和碰撞。

随后通过相同输入框恢复 300，`ui-offset-300-restored.json` 确认同步；只保存新图，`ui-final-map-clean.json` 为 is_dirty=false。恢复之后没有再重复卸载重载；完整自动验证此前已对相同最终配置重载。

![实际编辑器门洞与标记](<E:/Unreal Projects/PCG_AI_demo_1/Plugins/RoguelitePrototype/Development/P02/ui-selected-room-markers.png>)

Details 四侧参数截图另存 `ui-door-details-image.png`。完整检查也保存 2000×2000 和重载最终场景截图。

## 问题、当前未测试与风险

- 已解决的执行问题：沙箱下 UBT trace/DDC 写入受阻，正常权限构建/编辑器运行通过；新复制图未保存时加载工具拒绝，改为先仅保存新图；MCP partial transform 重置未指定位置，测试改为传完整 transform 后重新通过。失败日志/报告保留，没有手动改引擎或全局配置。
- Slate 搜索框首次 Type=false，选择自有窗口并点击聚焦后成功。补验等待自然结束后临时进程退出；之后 Unobserve 连接被拒绝，不重启、不重试，观察器随进程消失。
- 暂停前临时编辑器已经退出，报告 temporary_editor_stopped=true；续接时未发现运行的 UnrealEditor。当前 Codex 工具目录没有 Unreal MCP 原生入口，本次续接没有新的 MCP/PIE 调用；本报告实测记录均来自同一 P02 任务暂停前。
- P02 用户本人画面/键鼠体验、Undo/Redo、Map Check UI、Collision Pawn/Sweep、直接 Blueprint getter 调用、Game Target/打包未验证。不提供视口拖拽控制点。
- 旧 GameFeatures 缺少 GameFeatureData Asset Manager 规则的错误仍在编辑器日志中；未修改共享设置。旧模块/全局插件仍属于共享编译和运行环境，插件不是完全隔离的第二个工程。
- 箭头/隐藏墙段影响 Actor 视觉 Bounds；未来拼接应使用尺寸规则与 Socket，并实测空间碰撞，不能直接套用任意 Actor 视觉 Bounds。
- 非 90° 旋转下 POI 世界 AABB 转局部检查可能偏保守；POI 移动后需 Rebuild Room。POI 保持独立 Actor，不附加到可缩放的墙段。
- 本轮未新增 POI 与门洞占据空间的自动相交检测、NavMesh/角色步行净距、成对 Socket 兼容或两房连通测试；这些仍需后续经授权的拼接/通行阶段处理，不能把单房射线通过当作拼接已完成。

## 保护与仅本轮恢复

51 个保护文件与本轮前 baseline-files.json 的 SHA256 全部一致，包括 CastleDemo/Boss/Demo 图、原 P01 图、配置/Targets/`.uproject`、P01 脚本与报告。原有未提交 `Content/Demo/L_CastleDemo.umap` 和 `testfordemo.uproject` 已保留，没有暂存或提交。四份备份 SHA256 仍有效；最终记录见 final-verification.json。

若以后要撤销 P02，先关闭引用插件的编辑器、保留新改动，再对比 Backups 中的 room h/cpp 原件，只恢复这两份文件的本轮改动并常规重编；恢复前不应覆盖后续工作。逐项撤销 README/状态中的 P02 记录，保留其他阶段记录。通过编辑器或核对后的单文件归档处理专用 P02 地图及 P02 文档/脚本；P02 备份和证据先保留。

不能删除整个 RoguelitePrototype、Content/Roguelite、P01 地图，不能覆盖 `.uproject`、旧 Demo 或执行全仓 Git 恢复/清理。没有执行任何恢复。精确备份路径见 `Development/P02/backup-manifest.json`。

## 下一项

按照 [使用说明](<E:/Unreal Projects/PCG_AI_demo_1/Plugins/RoguelitePrototype/P02_DOOR_GUIDE.md>) 的五步完成策划人工验收，记录异常或确认。停在 P02；没有自动执行 P03、PCG 或 AI。
