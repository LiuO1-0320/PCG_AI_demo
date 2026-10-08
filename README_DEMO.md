# 古堡射击 Demo — The Hollow Citadel

这是 UE 5.8.2 的单人第一人称 Roguelite 原型。使用引擎基础网格、自制纯色材质和程序化场景，不依赖付费素材。场景采用可读性优先的占位美术；角色目前是几何体骑士，武器是占位卡宾枪。

## 启动

打开 `testfordemo.uproject`，地图为 `/Game/Demo/L_CastleDemo`，点击 Play。也可以右键 `Scripts/PlayDemo.ps1` → 使用 PowerShell 运行，启动独立游戏窗口。

地图已保存一份可见的编辑器预览。选中 `Castle PCG Director`，在 PCG 组件上点 Generate 可重新预览；运行时会清理预览并通过 PCG 重新生成。

如果编辑器提示编译模块，允许编译。第一次编译需要 Visual Studio C++ 工具链和 Windows SDK。本机已检测到 UE 5.8.2 和 MSVC 14.44。

## 操作和游玩流程

| 操作 | 按键 |
| --- | --- |
| 移动 / 视角 | WASD / 鼠标 |
| 射击（按住连发） | 鼠标左键 |
| 换弹 | R |
| 交互（靠近并瞄准物体） | E |
| 跳跃 | 空格 |
| 帮助 / 暂停 | F1 / Esc |
| 死亡或通关后重开 | Enter |

每层从入口进入，探索九个连通房间。小地图显示 IN 入口、S 商店、F 敌人、C 宝箱、E 事件、V 藏宝洞和 B Boss 入口。房间类型、通道、掩体和奖励随种子变化。

- 商店：花金币升级枪械（每次 +8 伤害、+4 弹匣），补弹并回复生命和护盾，价格逐次增加。
- 宝箱：金币、备用弹药和一个藏品，只能领取一次。
- 武器：宝箱房间旁有可拾取的强化卡宾枪（+10 伤害、+4 弹匣），只能领取一次。目前采用一种基础枪械及其强化版本。
- 事件：用 15 点生命换一个藏品，生命不足时不能使用。
- 藏品：随机增加每发伤害、护盾上限或生命上限；属性跨楼层保留。
- 藏宝洞：额外守卫和三个藏品宝箱，返回传送阵回到原探索位置。可以重复往返，奖励不会刷新。
- 敌人：AIController 导航追击、视线检测、可躲避弹体和近距离攻击；击败后直接给金币并掉落补给。
- Boss：三层生命和伤害逐步提升，扇形弹幕、地面攻击预警，半血后加快攻击并增加弹幕数量。
- Boss 入口：进入后锁定在竞技场，击败 Boss 后中央出现上楼传送阵。第三层传送阵结算通关。
- 护盾优先吸收伤害，连续 5 秒未受伤后回复。换层补少量生命、补弹并回满护盾。

## PCG 与 AI 实现

PCG 插件已开启。`/Game/Demo/PCG_Castle` 包含原生自定义 **Castle Layout** 节点，运行时由 `Castle PCG Director` 的 `UPCGComponent` 调用。布局生成、关卡实体和掩体都在该 PCG 节点执行阶段创建，并输出房间点数据：

| 属性 | 含义 |
| --- | --- |
| RoomType | 0 入口、1 商店、2 战斗、3 宝箱、4 事件、5 藏宝洞入口；特殊区域为 -1 |
| Region | 0 探索、1 Boss、2 藏宝洞 |
| DoorMask | 东、北、西、南四方向的位掩码 |

随机生成树确保探索区必定连通，随后增加环路。Boss 门位于图距离最远的房间。Boss 竞技场和藏宝洞是同一地图上的独立区域，通过传送进入。三层按顺序重新生成，清理上一层敌人、弹体、交互物、灯光和实例网格。

在 PCG 图里选中 Castle Layout 节点，可调整 `ExtraConnections` 和 `ArenaCoverCount`。在地图的 Director 里设置 `RunSeed` 可以复现同一布局；设为 0 则每次随机。命令行 `-CastleSeed=12345` 也可复现。游戏重开会使用新种子。

小怪和 Boss 使用原生 `AAIController`、动态 Recast 导航和 `MoveToActor`，逻辑在 `CastleGameplay.cpp` 中；当前没有依赖行为树、外部 LLM 或在线服务。

PCG 组件在运行时执行图的方式符合 [Epic PCG 概述](https://dev.epicgames.com/documentation/en-us/unreal-engine/procedural-content-generation-overview)。本 Demo 的拓扑算法封装在 C++ 节点中，后续可把点输出接到标准 PCG 节点扩展装饰。

## 文件与扩展

| 文件 | 用途 |
| --- | --- |
| Source/CastleDemo/CastleDemo.h | 玩家、AI、交互物、PCG 设置等可暴露给蓝图的类 |
| CastleGameplay.cpp | 射击、属性、奖励、AI 和弹体 |
| CastleGeneration.cpp | PCG 节点、连通房间、洞穴、Boss 区和换层 |
| CastleHUD.cpp | 第一人称 HUD、血盾条、Boss 血条、小地图 |
| CastleSetup.cpp | 在编辑器命令行生成地图、材质和 PCG 资产 |
| Scripts/BuildDemo.ps1 | 使用引擎自带 .NET 编译 |
| Scripts/ValidateDemo.ps1 | 无界面验证完整运行流程 |

地图和 PCG 图可以直接在编辑器修改。`BuildDemo.ps1 -RebuildAssets` 会重建 Demo 地图、PCG 图和材质，因此仅在明确需要重建时使用，先备份编辑过的资产。

后续美术可替换 `AddBox` 的实例网格、敌人 Body/Helmet/Visor、玩家 Carbine 和交互物 Pedestal。正式版建议增加骨骼动画、音效、UMG 中文界面、存档、更多武器和 Boss 行为。目前 HUD 用英文确保引擎默认字体可正确显示。

## 已执行的验证

2026-10-08 在本机 UE 5.8.2 上编译 Editor Development 模块成功。以种子 1337 和 2026 各运行了三层自动验证，均出现 `CASTLE_SMOKE_OK`：

- PCG 在真实游戏世界中触发，九个房间都连通，动态导航可到达每个房间及 Boss。
- AIController 正确接管 NPC；实际第一人称射线消耗弹药并命中敌人。
- 护盾先吸收伤害、商店余额判断、升级、藏品单次领取、事件生命限制、换弹、死亡状态正确。
- 藏宝洞进入和返回、击败 Boss 解锁上楼、跨层保留属性、第三层通关正确。

运行日志位于 `Saved/Logs/CastleSmoke.log` 和 `CastleSmoke2026.log`。实际渲染截图位于 `Saved/Screenshots/Demo/`。自动验证用于检查逻辑，战斗手感和难度仍需要实际游玩调优。

## WASD 移动修复

关卡生成时暂停角色移动、关闭碰撞；生成完成及传送后明确恢复 `MOVE_Walking`，避免角色停留在 `MOVE_None`。玩家控制器和角色分别固定使用 `UPlayerInput` 与 `UInputComponent`，使编辑器自动升级全局 Enhanced Input 设置后，Demo 的传统绑定仍然有效。

新增运行验证通过玩家控制器发送 W/S/D/A 按下、松开事件，等待真实游戏帧并检查四个方向的角色位移。日志为 `Saved/Logs/CastleInputSmoke.log`。更新原生模块后需重新打开编辑器再 Play；在嵌入视口中点击一下游戏画面可捕获键盘和鼠标。
