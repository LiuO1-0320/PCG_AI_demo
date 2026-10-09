# 古堡射击 Demo — The Hollow Citadel

这是已在 UE 5.8.3 上编译和验证的单人第一人称 Roguelite 原型。使用引擎基础网格、自制纯色材质和程序化场景，不依赖付费素材。场景采用可读性优先的占位美术；角色目前是几何体骑士，武器是占位卡宾枪。第一层地图 PCG 的设计依据见 [`output/pdf/first_floor_pcg_design.pdf`](output/pdf/first_floor_pcg_design.pdf)。

## 启动

打开 `testfordemo.uproject`，地图为 `/Game/Demo/L_CastleDemo`，点击 Play。也可以右键 `Scripts/PlayDemo.ps1` → 使用 PowerShell 运行，启动独立游戏窗口。游戏先显示主界面：输入 1–2147483647 的数字种子后点击 **START GAME**；留空开始则自动选择随机种子，也可点 **RANDOM SEED** 先查看随机数。启动参数 `-CastleSeed=12345` 可跳过菜单，便于自动验证。

地图已保存一份可见的编辑器预览。选中 `Castle PCG Director`，在 PCG 组件上点 Generate 可重新预览；运行时会清理预览并通过 PCG 重新生成。

如果编辑器提示编译模块，允许编译。第一次编译需要 Visual Studio C++ 工具链和 Windows SDK。本机已检测到 UE 5.8.3 和 MSVC 14.44。脚本自动查找本机安装路径，也可设置 `CASTLE_UE_ROOT` 指向引擎目录。

## 操作和游玩流程

| 操作 | 按键 |
| --- | --- |
| 移动 / 视角 | WASD / 鼠标 |
| 射击（按住连发） | 鼠标左键 |
| 换弹 | R |
| 交互（靠近并瞄准物体） | E |
| 跳跃 | 空格 |
| 帮助 / 暂停 | F1 / Esc |
| 死亡或通关后随机新局 / 同种子重试 | Enter / K |

第一层在 4×4 候选格中生成 10–12 个实际房间，至少有两条入口到出口的走法。保证路线依次为入口、战斗、战斗、商店、战斗、休息、出口；其余节点提供宝箱、额外战斗和 1–2 间随机的事件、精英或机关房。第一层没有 Boss 房、藏宝洞或提前跳层房。小地图显示全部节点与道路；入口和邻近节点的类型已知，其余以 `?` 表示，进入房间后揭示相邻房间。

- 商店：花金币升级枪械（每次 +8 伤害、+4 弹匣），补弹并回复生命和护盾，价格逐次增加。
- 宝箱：金币、备用弹药和一个藏品，只能领取一次。
- 武器：宝箱房间旁有可拾取的强化卡宾枪（+10 伤害、+4 弹匣），只能领取一次。目前采用一种基础枪械及其强化版本。
- 事件：用 15 点生命换一个藏品，生命不足时不能使用。
- 休息：每层一次，回复 35 生命、30 护盾与 48 备用弹药。
- 精英：强化现有守卫的生命和伤害，击败后获得 30 金币并出现奖励宝箱。
- 机关：两处一次性伤害区域，各造成 15 伤害，可绕行；房内有奖励宝箱。
- 藏品：随机增加每发伤害、护盾上限或生命上限；属性跨楼层保留。
- 敌人：AIController 导航追击、视线检测、可躲避弹体和近距离攻击；击败后直接给金币并掉落补给。
- 第一层出口：可直接进入第二层，不要求击败 Boss。
- 第二、三层暂沿用原型的随机房间、藏宝洞和 Boss 流程：藏宝洞有额外守卫及三个藏品宝箱，可返回原位置；击败 Boss 后解锁上楼传送阵，第三层结算通关。
- Boss：目前仅第二、三层出现；生命和伤害随层数提升，使用扇形弹幕与地面攻击预警，半血后加快攻击并增加弹幕数量。
- 护盾优先吸收伤害，连续 5 秒未受伤后回复。换层补少量生命、补弹并回满护盾。

## PCG 与 AI 实现

PCG 插件已开启。`/Game/Demo/PCG_Castle` 包含原生自定义 **Castle Layout** 节点，运行时由 `Castle PCG Director` 的 `UPCGComponent` 调用。布局生成、关卡实体和掩体都在该 PCG 节点执行阶段创建，并输出房间点数据：

| 属性 | 含义 |
| --- | --- |
| RoomType | 0 入口、1 商店、2 战斗、3 宝箱、4 事件、5 藏宝洞入口、6 休息、7 精英、8 机关、9 出口；特殊区域为 -1 |
| Region | 0 探索、1 Boss、2 藏宝洞 |
| DoorMask | 东、北、西、南四方向的位掩码 |
| RouteOrder | 第一层主路线中的顺序 0–6；支路为 -1 |

第一层先按种子生成七节点保证路线，再扩展 3–5 个节点，并增加可选连接，形成至少两条入口到出口的路线。生成器检查连通性与替代路线；若候选不合格则继续用同一种子尝试，最终有确定性回退图。地图拓扑、房间类型、初始奖励使用独立随机流，以保持同种子复现。第二、三层暂保留原有随机生成树、环路、藏宝洞和 Boss 区。换层会清理上一层的敌人、弹体、交互物、灯光和实例网格。

在 PCG 图里选中 Castle Layout 节点，可调整第二、三层使用的 `ExtraConnections` 和 `ArenaCoverCount`。第一层的种子由主界面确定；命令行 `-CastleSeed=12345` 可直接复现。Enter 开始随机新局，K 使用同一颗种子重试。

小怪和 Boss 使用原生 `AAIController`、动态 Recast 导航和 `MoveToActor`，逻辑在 `CastleGameplay.cpp` 中；当前没有依赖行为树、外部 LLM 或在线服务。

PCG 组件在运行时执行图的方式符合 [Epic PCG 概述](https://dev.epicgames.com/documentation/en-us/unreal-engine/procedural-content-generation-overview)。本 Demo 的拓扑算法封装在 C++ 节点中，后续可把点输出接到标准 PCG 节点扩展装饰。

## 文件与扩展

| 文件 | 用途 |
| --- | --- |
| Source/CastleDemo/CastleDemo.h | 玩家、AI、交互物、PCG 设置等可暴露给蓝图的类 |
| CastleGameplay.cpp | 射击、属性、奖励、AI 和弹体 |
| CastleNodeLayout.cpp | 第一层节点图、替代路线与房间类型生成 |
| CastleMenu.cpp | 种子输入主界面与校验 |
| CastleGeneration.cpp | PCG 点输出、真实房间生成、地图揭示与换层 |
| CastleHUD.cpp | 第一人称 HUD、血盾条、Boss 血条、小地图 |
| CastleSetup.cpp | 在编辑器命令行生成地图、材质和 PCG 资产 |
| Scripts/BuildDemo.ps1 | 使用引擎自带 .NET 编译 |
| Scripts/ValidateDemo.ps1 | 无界面验证完整运行流程 |
| Docs/Design/build_first_floor_design.py | 生成中文策划 PDF 的源文件 |

地图和 PCG 图可以直接在编辑器修改。`BuildDemo.ps1 -RebuildAssets` 会重建 Demo 地图、PCG 图和材质，因此仅在明确需要重建时使用，先备份编辑过的资产。

后续美术可替换 `AddBox` 的实例网格、敌人 Body/Helmet/Visor、玩家 Carbine 和交互物 Pedestal。正式版建议增加骨骼动画、音效、UMG 中文界面、存档、更多武器和 Boss 行为。目前 HUD 用英文确保引擎默认字体可正确显示。

## 已执行的验证

2026-10-09 在本机 UE 5.8.3 上编译 Editor Development 模块成功。新版节点图以固定种子 4、42、1337、2026、24680 及随机种子运行三层自动验证，均出现 `CASTLE_SMOKE_OK`：

- PCG 在真实游戏世界中触发；第一层生成 10–12 个房间，七房保证路线、房间配额、双向连接和替代路线正确，动态导航可到达全部实际房间，普通出口可进入第二层。
- 小地图初始揭示入口及相邻节点，进入下一房间后继续揭示邻近节点；没有行动力消耗。
- 实际菜单的随机按钮产生有效数字，非法种子 0 被拦截，24680 成功开始；菜单画面经过实际渲染检查。
- 同种子重试运行两次完整三层流程；24680 两次地图摘要均为 208365018，初始奖励摘要均为 2489863569。最大允许种子 2147483647 也完成了生成和三层换层验证。
- AIController 正确接管 NPC；实际第一人称射线消耗弹药并命中敌人。
- 护盾先吸收伤害、商店余额判断、升级、藏品单次领取、事件生命限制、换弹、死亡状态正确。
- 第二、三层的藏宝洞进入与返回、击败 Boss 解锁上楼、跨层保留属性、第三层通关正确。

本轮菜单与重试日志为 `Saved/Logs/CastleNodeMenuRetryFinal.log`，种子边界日志为 `Saved/Logs/CastleNodeMaxSeedFinal.log`；其他固定种子日志为 `CastleNodeFinal4.log`、`CastleNodeFinal42.log`、`CastleNodeFinal1337.log` 和 `CastleNodeFinal2026.log`，随机种子日志为 `CastleNodeRandom.log`。实际菜单截图为 `Saved/Screenshots/Demo/CastleSeedMenu.png`。自动验证用于检查逻辑，战斗手感和难度仍需要实际游玩调优。

普通 `UnrealEditor.exe` 游戏入口已加载新模块并显示菜单，日志为 `Saved/Logs/CastleEditorMenuFinal.log`。新版第一层和四乘四小地图经过实际渲染检查，截图为 `Saved/Screenshots/Demo/Exploration.png`，日志为 `Saved/Logs/CastleNodeRenderFinal.log`。

## WASD 移动修复

关卡生成时暂停角色移动、关闭碰撞；生成完成及传送后明确恢复 `MOVE_Walking`，避免角色停留在 `MOVE_None`。玩家控制器和角色分别固定使用 `UPlayerInput` 与 `UInputComponent`，使编辑器自动升级全局 Enhanced Input 设置后，Demo 的传统绑定仍然有效。

新增运行验证通过玩家控制器发送 W/S/D/A 按下、松开事件，等待真实游戏帧并检查四个方向的角色位移。日志为 `Saved/Logs/CastleInputSmoke.log`。更新原生模块后需重新打开编辑器再 Play；在嵌入视口中点击一下游戏画面可捕获键盘和鼠标。
