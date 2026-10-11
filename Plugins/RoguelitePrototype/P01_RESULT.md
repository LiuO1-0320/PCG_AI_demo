# P01 交付报告 — 2026-10-11

P01 房间外壳已实现，实际 Editor 编译成功，107 项官方 MCP 自动检查通过，用户正常重启后再次核对保存结果并取得视口截图。当前状态为 AUTO-PIE；人工编辑交互与游玩验收仍待进行。停止在 P01，不执行下一阶段。

## 文件与依赖

新增的实现文件：

- `Source/RoguelitePrototype/Public/RogueliteParametricRoom.h`
- `Source/RoguelitePrototype/Private/RogueliteParametricRoom.cpp`
- `P01_ROOM_GUIDE.md`、本报告。
- `Development/P01/validate_room.py` 及同目录的基线、备份、构建日志、MCP 原始报告和截图。脚本会启动自己的临时编辑器、修改并保存固定测试地图，仅用于明确需要重新验收时；不要在手工调整地图后随意运行。

修改的既有文件：

- `Content/Roguelite/Maps/L_Roguelite_Dev.umap`：通过官方 Editor/MCP 增加一个房间和一个手工 POI 测试 Actor；保留初始场景 Actor。
- 插件 `README.md`：补充 P01 使用入口与范围。
- `Docs/06_EXECUTION_STATUS.md`：只更新 P01 状态与真实证据。

构建还更新了插件正常的 Binaries/Intermediate 产物。未修改 Build.cs、插件描述符、根/作用域 AGENTS、`.uproject`、默认地图或全局配置。Runtime 依赖仍为 Core/CoreUObject/Engine；既有 Editor 私有依赖 UnrealEd 用于编辑器功能，无 CastleDemo、PCG、AI、Boss 或第三方依赖。没有第二个 `.uproject`。

## 参数与使用

在测试地图选中 `P01_ParametricRoom`，按 F 对焦。Details → Roguelite → Room Dimensions 中调整 RoomLength/RoomWidth/RoomHeight/WallThickness/FloorThickness。单位厘米；默认值分别 2000/2000/400/20/20，最小值分别 100/100/100/1/1。非法值会限制到最小合法值。

采用 Actor XY 中心、地板完成面 Z=0；北/南沿 ±X，东/西沿 ±Y。长宽是室内净尺寸，墙向外延伸，地板向下延伸。保持 Actor Scale 为 1。构造和编辑器属性通知更新已有组件，不依赖 PIE 或 Tick。

最终已保存房间位于 `(10000,0,0)`，净尺寸 `3000×2500×500`，墙厚和地板厚度均 20；共有 RoomRoot 与五个外壳 Static Mesh Component。仅预留局部室内 Bounds 计算结构，未实现门洞、Door Socket、拼接或 PCG。

## 实际编译与 MCP 操作

真实编译命令目标为 `testfordemoEditor Win64 Development`，使用本机 UE 5.8.3 的 UnrealBuildTool、MSVC 14.44.35228 和 Windows SDK 10.0.22621.0。基线构建退出码 0；实现后构建退出码 0，日志 `Result: Succeeded`，耗时 400.33 秒，新模块 DLL 已链接。未执行 Clean、Game Target 构建或打包。

官方 MCP 实际执行了 toolset/schema 发现、Actor 放置、Details 对应属性读写、组件/变换/Bounds 读取、世界射线检测、只保存该测试地图、卸载并重新加载地图、真正 PIE 的启动/读取状态/停止。没有调用旧地图生成器或 PCG Graph 写入。

临时正常渲染编辑器的 107 项检查全部通过，并已关闭。用户重新打开的正常编辑器再次返回尺寸 3000/2500/500/20/20、ManualPOI 引用、六个组件、无越界警告、地图 `is_dirty=false`、`IsPIERunning=false`；已选择测试房间并设置观察视角。

## 碰撞与尺寸证据

`trace_world` 实际使用 UE 原生 Visibility 复杂射线。下表为房间中心向墙/地板发射的真实命中距离，不是 SpectatorPawn 的穿墙结果。

| 测试净尺寸 | 北/南内墙 | 东/西内墙 | 从 Z=100 向下到地板顶面 |
|---|---:|---:|---:|
| 2000×2000×400 | 各 1000 cm | 各 1000 cm | 100 cm |
| 3000×2500×400 | 各 1500 cm | 各 1250 cm | 100 cm |
| 保存重载 3000×2500×500 | 各 1500 cm | 各 1250 cm | 100 cm |

另测北墙外表面和地板底面。单独把高度 400→700，Z=600 的射线命中墙；恢复 400 后该射线无命中，长宽保持不变。墙厚 20→50、地板厚 20→40 后命中面按厚度更新，恢复后旧厚度区域无命中。

五次来回改变尺寸后，组件引用与数量不变；扩大、缩小后的旧墙位置射线均无命中。保存/卸载/重载后参数、组件、ManualPOI 引用和射线结果均保持。所有外壳组件的实际 BodyInstance 为 BlockAll、QueryAndPhysics。

这些结果证明已采样位置的世界碰撞查询有效；未执行 Capsule Sweep、真实行走角色或物理物体落地试验，不把配置读取当作这些动态测试已经完成。

## 手工 POI 与越界

`P01_ManualPOI` 位于 `(10900,600,100)`，Yaw=30°、Scale=(3,2,2)。五次尺寸修改前后及保存重载后，其位置、旋转、Scale、实际 Bounds、StaticMesh、OverrideMaterials、BodyInstance、Mobility 与组件相对变换均一致。

在 2000×2000 的房间中，该 POI 原点局部 X=900 仍在室内；实际 Bounds 最大 X=1079.904 已超过内边界 X=1000。ValidationWarnings、OutOfBoundsManualPOIActors 和真实 Output Log 均出现 `P01_POI_OUT_OF_BOUNDS`，含 Actor 名称、路径和 Bounds；扩大后警告消失。房间不会自动移动、缩放、删除或重配这些 Actor。

## PIE 与人工待验收

官方 MCP 的真正 PIE 已启动并停止，日志为独立 `RogueliteDevGameMode`、SpectatorPawn、`LegacyActorCount=0`。没有自动启动旧地牢系统。地图在结束后未变脏。

人工五步补验见 `P01_ROOM_GUIDE.md`：选中房间对焦、用 Details 改两组长宽及单独墙高、检查手工 POI/固定组件、缩小查看越界警告、保存重开并进入 PIE 观察。人工点击 Details、Undo/Redo、Map Check UI、键鼠游玩与碰撞 Pawn 行走目前未验证；未验证非 90° 房间旋转、大量房间性能、打包。

## 错误、风险与旧文件保护

首个验证脚本按 C++ 大写名称判断 MCP 属性 schema，断言失败，未保存地图；修正为发现真实属性名后，第二次全套通过。原失败证据保留。自动截图先因适配器要求显式 annotations 参数失败，随后在用户编辑器补传零覆盖层参数，截图成功；修正后的脚本截图参数没有重跑整套检查，原完整测试报告中的截图失败记录由补验报告补充。

`WORKFLOW_AGENT.md` 未在本工作区找到，未创建替代规范。已有全局 GameFeatures/GameFeatureData 配置错误和共享 Editor Target、构建缓存、渲染/输入配置依然存在，本轮没有扩大范围修复它们。

Bounds 检查使用世界 AABB 的八角点转换，任意斜向房间旋转可能保守报警。移动手工 POI 后，点击 Rebuild Room 或改尺寸刷新检查；不要将 POI 附加到会移动/缩放的外壳网格组件。房间整体 Scale 非 1 会警告，不会强制改动。

47 个受保护既有文件 SHA-256 均与本轮开始一致，覆盖旧 Demo/Boss 资产、CastleDemo 源码、配置、Targets、`.uproject` 及 P00.5 既有实现。开始时已存在的 `.uproject` 和旧 Demo 地图未提交修改保留；无 Git 提交、暂存、Reset/Clean。最终细目见 `Development/P01/final-verification.json`。

## 仅撤销 P01 的方法

先正常关闭本工程，并另外保留 P01 之后的新修改。对照 `Development/P01/backup-manifest.json`，逐项核对后恢复测试地图的修改前副本；README 与执行状态文档只撤销 P01 段落，若没有后续修改才使用其备份恢复。备份文件的 SHA-256 已核对。

仅将本轮两份房间源文件、P01 说明/报告与验证文件归档到用户选择的位置，再正常重编 Editor Target 更新 DLL；保留 P00.5 插件模块、GameMode、地图创建命令、插件描述符、初始测试地图及 `.uproject`。不要删除整个插件或 Content/Roguelite，不使用全项目回滚、Clean 或覆盖旧 Demo 文件。恢复动作需再次核对当前状态，本轮没有执行恢复。
