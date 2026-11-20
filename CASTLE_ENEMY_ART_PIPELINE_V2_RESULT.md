# CASTLE_ENEMY_ART_PIPELINE_V2 验收报告

## 交付状态

`/Game/Art/Grunt01/IntegrationV2/BP_CastleEnemy_Grunt_Art` 已编译、保存并通过磁盘冷加载。可在独立测试关卡运行，检查骑士模型、持枪待机、右手枪械挂载及原角色碰撞。当前角色移动时仍使用持枪待机姿势。

编辑器中实测的 Epic 官方 MCP 能新建和编辑普通 Blueprint，也能复制及编辑现有 Animation Sequence。实际工具列表、源码与调用结果未提供可安全创建 AnimBP、Blend Space、Montage 或编辑 AnimGraph 的入口。上述资产未创建。完整移动动画、战斗 Montage、持枪死亡代理、音效和多兵战斗性能尚未验收。原 Grunt、正式地图和 Director 配置继续使用各自的原始资产。

工程为 `E:/Unreal Projects/PCG_AI_demo/testfordemo.uproject`，引擎为 UE 5.8.4 / CL58902052。查阅了项目工作约定、自动化 SOP、ART_PIPELINE_V1 与 PRESENTATION_V1-FIX02 报告。通过活跃编辑器的 `127.0.0.1:8000/mcp` 完成 MCP 初始化、工具发现及实际只读调用，核对了项目身份、`/Engine/Maps/Entry` 关卡和非 PIE 状态。施工与测试使用独立编辑器进程。用户原编辑器保留。

## 已核实的模型、骨骼和重定向资源

| 用途 | 当前 UE 包路径 | 核验结果 |
| --- | --- | --- |
| 骑士 Skeletal Mesh | `/Game/Art/Grunt01/SK_CastleGrunt_Corrected` | 运行时使用此模型。参考模型高度约 176.00 cm，共 41 根骨骼 |
| 骑士 Skeleton | `/Game/Art/Grunt01/SK_CastleGrunt_Corrected_Skeleton` | 用户下列六条导出序列均绑定此骨架 |
| 源 IK Rig | `/Game/Characters/R_UE5` | 其预览网格为 `/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple` |
| 目标 IK Rig | `/Game/IK_NewIKRig1` | 预览网格为最新骑士 |
| IK Retargeter | `/Game/RTG_NewRetargeter` | 依赖上述源、目标 IK Rig，沿用用户配置 |
| 模板独立 IK Rig | `/Game/Characters/UE5_Mannequins/Rigs/IK_UE5_Mannequin_Retarget` | 资源存在。当前 Retargeter 使用 `R_UE5` |
| 枪械和材质 | `/Game/Art/Grunt01/SM_CastleRifle`，`/Game/Art/Grunt01/M_CastleGrunt_Atlas` | 枪械约 78.125 cm，使用原网格和尺寸 |

骑士参考姿势局部前向为 +Y。Art 的角色 Mesh 使用相对位置 `(0, 0, -88)` cm、Yaw `-90°`、Scale `(1, 1, 1)`，将资源 +Y 对齐 Actor +X。参考姿势双脚水平中心与原点的差量约在 `0.00001 cm` 内，脚底 Z 偏差约 `-0.00001 cm`。原 Capsule 半径 `36 cm`、半高 `88 cm`，`MaxWalkSpeed` 为 `270 cm/s`。`SentinelBody`、`Helmet` 和 `Visor` 在子类模板及模拟实例均隐藏。

| 用途 | 用户已导出序列 | 核验和本轮处理 |
| --- | --- | --- |
| 持枪待机 | `/Game/Characters/MF_Rifle_Idle_ADS` | 8.000 秒，直接用于 Art 的循环待机 |
| 持枪走 | `/Game/Characters/MF_Rifle_Walk_Fwd` | 1.533 秒。源资产启用 Root Motion，另存副本供原地运动 |
| 持枪跑 | `/Game/Characters/MF_Rifle_Jog_Fwd` | 1.533 秒。源资产启用 Root Motion，另存副本供原地运动 |
| 射击 | `/Game/Characters/MM_Rifle_Fire` | 0.533 秒。源资产为 Mesh Space Rotation Offset Additive，已准备全姿势副本 |
| 正面受击 | `/Game/Characters/MM_HitReact_Front_Med_01` | 0.767 秒。源资产为 Local Space Additive，已准备全姿势副本 |
| 正面死亡 | `/Game/Characters/MM_Death_Front_01` | 1.100 秒，完整姿势。可直接作为死亡 Montage 原料 |

动画已离线导出，本轮无需调用重定向器再次导出。IK Rig、IK Retargeter、Retarget Pose、原序列及原模型的磁盘校验通过，见下方保护清单。

## Art 蓝图和枪械

- `BP_CastleEnemy_Grunt_Art` 继承 `/Game/CastleDemo/Enemies/Presentation/BP_CastleEnemy_Grunt`。Mesh 当前为 `Use Animation Asset`，循环播放 `MF_Rifle_Idle_ADS`，`Anim Class` 为空。
- `RifleVisual` 是角色 Mesh 下的新 `StaticMeshComponent`。已有 `Weapon_R` 位于骨骼 `R_Hand`，Socket 本身的相对位置与旋转均为零，缩放为 `(1, 1, 1)`。Art `BeginPlay` 通过 `Attach Component To Component` 使用 `Weapon_R`。挂接成功后应用枪组件偏移并显示，模板默认隐藏。挂接规则为位置和旋转 `Snap to Target`，尺度 `Keep World`，关闭 Weld。
- 枪组件在 Socket 下的位置为 `X=-0.4591, Y=-2.7856, Z=1.0148 cm`，旋转为 `Pitch=19.7705°, Yaw=-99.3579°, Roll=98.9200°`。这是按持枪待机双手位置校准的初始值。运行中右手挂接和枪口前向已验，完整动画中的左手支撑、手腕和穿模需人工验收。
- 枪组件为 `NoCollision`，关闭 `Generate Overlap Events` 和 `Can Ever Affect Navigation`。角色视觉 Mesh 也为 `NoCollision`，原 Capsule 继续承担角色碰撞。父类配色会作用到静态网格，Art 在挂接后经过 `Delay Until Next Tick` 为枪组件恢复 Atlas 材质。三个已放置的模拟实例均读回预期材质。正式 Director 生成 Art 后的同帧配色路径未测。
- 攻击、受击、死亡通知仍由原 C++ 发出。Art 继承现有表现事件与资源槽。旧死亡代理只有角色视觉 Mesh；现有死亡事件使用其固定蓝图类，且要求死亡 Montage 和当前 Anim Instance。当前 Art 的死亡 Montage 为空，因此尚无 Art 的持枪死亡代理。保留现有 C++ 结算与 Destroy 时序。
- 本轮项目资源查询未发现可直接用于这三种事件的 `SoundWave`、`SoundCue`、`MetaSoundSource` 或现成衰减资产。`Attack Sound`、`Hit Sound`、`Death Sound`、`Sound Attenuation` 均保持空槽，音效和空间衰减未验收。

## 保存的动画准备资产

在 `/Game/Art/Grunt01/IntegrationV2/Prepared/` 下保存四个 `Animation Sequence`。它们均与最新骑士 Skeleton 兼容，已在第二个编辑器进程从磁盘冷加载并读回属性。

| 新序列 | 处理与验证 |
| --- | --- |
| `AS_Grunt_RifleWalk_InPlace` | 关闭 `Enable Root Motion`，保留 `Force Root Lock` 与 `Ref Pose`。独立角色 Mesh 单独试播时 Actor XY 位移为零 |
| `AS_Grunt_RifleJog_InPlace` | 同上。副本保留源序列的脚步 Notify，接 AnimBP 时检查是否有需要另行配置的音效引用 |
| `AS_Grunt_Attack_FullPose` | 从射击序列复制，将 Additive 类型设为 `No Additive`。已单独试播目标骨骼的射击姿势 |
| `AS_Grunt_Hit_FullPose` | 从受击序列复制，将 Additive 类型设为 `No Additive`。已单独试播目标骨骼的受击姿势 |

计划的 `ABP_CastleGrunt`、`BS_CastleGrunt_Rifle`、`AM_Grunt_Attack`、`AM_Grunt_Hit`、`AM_Grunt_Death` 均未创建。`Attack Montage`、`Hit Montage`、`Death Montage` 为空。`DefaultGroup.DefaultSlot` 是拟用 Slot，需在目标 Skeleton 的 Anim Slot Manager 确认。当前 MCP 拒绝了对 `SlotGroups` 的读取。

## 运行证据与回归

| 检查 | 本轮结果 |
| --- | --- |
| 独立单兵 SIE | 真实 Art 实例使用骑士、预期脚底基准和朝向，持枪待机有动画，枪跟随右手，原三件静态占位件隐藏。空 Montage、空声音的 Art 实例可初始化并运行 |
| 三兵 SIE | 3 个真实 Art 实例分别挂枪。独立 CharacterMovement 激励采样得到 `+3.54`、`+3.62`、`+4.65 cm` 的 X 位移。最终可见性复验中，三把枪各自在挂接成功后显示，复拍画面未见原复验画面里的落地枪形。此场景使用待机动画，移动混合、转向脚滑和战斗负载尚未验收 |
| Art 受伤及死亡探针 | 在未保存的 SIE 测试子类调用 `ApplyDamage(1)` 与 `ApplyDamage(10000)`。非致死记录一次 `BP_OnHitVisual`，致死记录一次 `BP_OnDeathVisual`，随后敌人从世界消失。探针只记录事件，绕开了继承的表现处理。无代理实例，尚未验证配置死亡 Montage 后的代理及枪械清理 |
| 原生 `CastleDemo.EnemyPresentation.SpawnAndDamage` | 保持旧测试与 Manny 版 Grunt 原样，用既有 DebugGame Editor 构建运行。1 个测试成功，27 条警告、0 条错误，退出码 `0`。Seed 24680 的探索普通兵 15 个、洞穴守卫 3 个使用旧 Grunt，精英 1 个用原生 `CastleEnemy`。攻击通知、弹体、非致死 Hit、死亡通知、击杀计数和弹药掉落的增量均为 1。空槽场景代理数 0；独立旧代理夹具在 7.717 秒设寿命，7.900 秒观察到自然消失 |
| Art 完整战斗、性能及正式生成 | Art 的自主攻击、Montage 分发、带枪死亡代理、音效和空间衰减未验证。三兵仅作场景运行检查，未采集游戏线程、动画或代理压力指标。正式 Director 未接 Art。未启动正式地图 PIE，未重新构建 DLL |

原生测试警告包含已有砌体与房间美术缺失、测试世界清理提示和网络检查超时。本轮未运行完整 PCG Seed1337 回归。

证据目录为 `E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_20261120/`。

- [Art 持枪侧视](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_20261120/Art-Calibrated-Side.png>)、[最终三兵画面](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_20261120/Art-Three-Final.png>)、[挂接可见性读回](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_20261120/VisibilityVerification.json>)、[磁盘冷加载读回](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_20261120/VisibilityColdRead.json>)。
- [三兵移动采样](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_20261120/ArtThreeSIE.json>)、[四条准备动画试播](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_20261120/PreparedClipsSIE.json>)、[独立伤害探针](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_20261120/ArtDamageFixture.json>)及[日志事件行](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_20261120/V2-Cold-RHI-MCP.log:2393>)。
- [原生自动测试结果](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_20261120/NativeBaseline/index.json>)与[生成、事件和代理日志](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_20261120/V2-Native-Baseline.log:2127>)。

## 人工收尾与验收

1. **先看成品现状。** 在内容浏览器打开 `/Game/Art/Grunt01/IntegrationV2/BP_CastleEnemy_Grunt_Art`，拖入空白测试关卡，放在平地上。用 Simulate 或 Play 查看。枪在运行时挂到手上才显示。转到角色侧面看枪口、右手握把和左手支撑。需要调枪时，编辑 Art 的 `BeginPlay` 中 `Set Relative Location And Rotation` 节点。该节点在运行时应用偏移。
2. **建速度混合。** 在 `IntegrationV2` 中新建 `Blend Space 1D`，选择 `SK_CastleGrunt_Corrected_Skeleton`，命名 `BS_CastleGrunt_Rifle`。横轴设为 `Speed`，范围 `0–270 cm/s`。先用 `0` 对应 `MF_Rifle_Idle_ADS`，`135` 对应 `AS_Grunt_RifleWalk_InPlace`，`270` 对应 `AS_Grunt_RifleJog_InPlace`。`135 cm/s` 是初始混合点，预览实际脚步后再调。
3. **建动画蓝图。** 用同一 Skeleton 在该目录建立 `ABP_CastleGrunt`。Event Graph 中从 `Blueprint Update Animation` 取 `Try Get Pawn Owner`，经 `Is Valid` 后取 `Get Velocity → Vector Length XY → Set Speed`。无有效 Pawn 时将速度设为 `0`，供独立视觉代理初始化。AnimGraph 连 `BS_CastleGrunt_Rifle` 播放器的 Pose 输出 → `Slot` → `Output Pose`，给播放器接上 `Speed`。在目标 Skeleton 的 Anim Slot Manager 核对 `DefaultGroup.DefaultSlot`，让 `Slot` 节点和三条 Montage 使用相同 Slot。Class Defaults 的 `Root Motion Mode` 选 `Ignore Root Motion`，由原 CharacterMovement 完成位移。 [Epic 动画蓝图编辑器文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-blueprint-editor-in-unreal-engine)
4. **建三个 Montage。** 依次从 `AS_Grunt_Attack_FullPose`、`AS_Grunt_Hit_FullPose`、`MM_Death_Front_01` 创建 `AM_Grunt_Attack`、`AM_Grunt_Hit`、`AM_Grunt_Death`。在 Montage 的 Slot Track 选择 `DefaultGroup.DefaultSlot`。先检查播放、过渡和中断。Notify 只用于表现，实际射击与伤害继续由 C++ 处理。
5. **填 Art 的槽。** 在 Art 的 `Mesh` 将 `Animation Mode` 改成 `Use Animation Blueprint`，`Anim Class` 选择 `ABP_CastleGrunt`。在 `Class Defaults` 搜索 `Attack Montage`、`Hit Montage`、`Death Montage` 并分别指定对应资源。导入合适且有使用权的音效后再填 `Attack Sound`、`Hit Sound`、`Death Sound` 和 `Sound Attenuation`，用近远两个位置测试空间衰减。
6. **补持枪死亡代理。** 在 `IntegrationV2` 中从现有 `BP_CastleEnemy_DeathVisualProxy` 派生 `BP_CastleEnemy_DeathVisualProxy_Art`。保留父级初始化，添加无碰撞、无重叠、无导航影响的 `RifleVisual`。在代理设置好 `VisualMesh` 的骑士网格及 Anim Class 后调用代理内的挂枪函数，将枪附到 `VisualMesh → Weapon_R`，使用与 Art 相同的枪械偏移与材质。单独在 Art 覆写 `BP_OnDeathVisual`，照现有事件保留 3D 声音和资源守卫，把固定类生成替换为一次 `SpawnActor BP_CastleEnemy_DeathVisualProxy_Art`。使用该次生成的返回值配置网格、动画类及枪械并播放 Death Montage。播放成功按返回时长加 `0.15 s` 设置 `Set Life Span`，播放失败清理代理。此覆盖应只执行一套死亡表现，避免另调用会生成原代理的父事件。 [Epic Skeletal Mesh Socket 文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/skeletal-mesh-sockets-in-unreal-engine)
7. **分两次实际试玩。** 在空白测试关卡放一名 Art 检查外观。要验自主攻击，在独立的 Castle 测试关卡副本中，手动把测试 Director 的 `Ordinary Enemy Class` 设为 `BP_CastleEnemy_Grunt_Art`，启动关卡并单独引出一名普通兵。Director 的配置流程会启动原生行为树。检查待机、走跑、转向、脚底、双手及枪口。观察一次成功攻击是否对应单次弹体和伤害，再分别施加非致死、致死伤害，看 Hit、Death、枪械保留和清理。随后引出三名重复，并用 Unreal Insights 记录 Game Thread、动画和死亡代理数量。精英和 Director 的类路由也在该测试副本中验收。

## SHA-256、备份和回滚

此次实际新增的 UE 包在 `Content/Art/Grunt01/IntegrationV2/`。`BP_CastleEnemy_Grunt_Art_beforeVisibility.uasset` 是新 Art 蓝图在最后一次挂接可见性调整前的快照，SHA-256 为 `5013A9502A6C02BF5EDA8B39205B31ECBFF5C72B0D5DFDA3AF640D1E59B6B60C`。

| 新增包文件 | 最终 SHA-256 |
| --- | --- |
| `BP_CastleEnemy_Grunt_Art.uasset` | `F890C2FD49B364F66C821966AA567E441D632BA99611264C419FF7EF8B0D9ED7` |
| `Prepared/AS_Grunt_Attack_FullPose.uasset` | `BF1D8BB29F6972C41C0466B7B1D076B93DBC0F659655F792D84D5C5947D219EA` |
| `Prepared/AS_Grunt_Hit_FullPose.uasset` | `9B9C5CF88D1E96D039672FF7010EDB2FBC9A084B7CCCFD8C7D3B3E331B8F4DD2` |
| `Prepared/AS_Grunt_RifleJog_InPlace.uasset` | `7DF5848E39A07C629D642E45BAAB8F3E5B434CA4292740BD046EE1E37F6D1319` |
| `Prepared/AS_Grunt_RifleWalk_InPlace.uasset` | `414B8C450437D2D3772DEB645F6D13B89655A204D280C28D5376DF5E6D20470F` |

原三份表现蓝图已逐文件备份至 `Saved/CastleEnemyArtPipelineV2_20261120/Backup/`，文件名与原包相同。施工后复核了 57 个受保护文件，差异为 0，涵盖原蓝图、用户骨架及动画、相关源码、项目配置和正式地图。完整磁盘位置与 SHA 见 [保护清单](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_20261120/ProtectedBeforeSHA256.txt>) 和 [新增包清单](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_20261120/NewPackagesSHA256.txt>)。

日常回退时，在临时关卡继续使用原 `BP_CastleEnemy_Grunt` 即可。需要回退 Art 最后一次显示修改时，先保留当前 Art 包，结束相关 PIE 并关闭加载该包的编辑器，从 `Backup/BP_CastleEnemy_Grunt_Art_beforeVisibility.uasset` 复制到原 Art 包的路径与文件名，按对应 SHA 核对后重新打开 UE。原三份表现蓝图只有发生独立损坏时才需要从同名备份逐一恢复。若日后决定移出 V2 资产，先用 Reference Viewer 检查新引用，随后归档 `IntegrationV2` 中本轮的五个包。原工程资产无需随上述操作回退。
