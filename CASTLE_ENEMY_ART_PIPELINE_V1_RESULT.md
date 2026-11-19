# CASTLE_ENEMY_ART_PIPELINE_V1 验收报告

2026-11-20 · `E:/Unreal Projects/PCG_AI_demo/testfordemo.uproject` · UE 5.8.4 / CL 58902052 · Blender 5.2.3 LTS

**角色、枪械和贴图已整理并导入。** 五个 Grunt01 新资产均通过磁盘冷加载，角色蒙皮和原骨骼层级保留，右手已建立 `Weapon_R`。当前官方 Unreal MCP 缺少可验证的骨链、Retarget Pose 和离线动画重定向编辑能力。**已重定向动作数为 0，正式 Grunt 仍使用 FIX02 的 Manny 和 ABP_Unarmed。**

| 状态 | 本轮结果 |
|---|---|
| 已实际完成 | Blender 工作副本、独立 FBX、共享贴图、五个 UE 资产及右手 Mesh Socket |
| 已自动测试通过 | Blender 蒙皮响应抽测、UE 骨链与尺寸冷加载、原 Grunt 的临时世界模拟及 `SpawnAndDamage` 基线 |
| 需要人工美术验收 | 转制姿势、脚底与 Capsule 比例、肩肘膝和手腕变形、双手握持、材质和声音 |
| 未验证 | 新外观 Idle、Walk、Jog、持枪与战斗的实际画面，以及多兵 GameThread／动画／代理性能 |
| 阻断 | IK Rig、IK Retargeter、目标动画和 AnimBP 自动施工。新外观依赖这些资产，因此未写入正式 Grunt |

## 1. Blender 源件与工作副本

Blender MCP 协议 16 已握手，实际读取 `C:/Users/12854/Desktop/Untitled.blend` 并检查了材质预览。对象原名为 `tripo_node_fe781818`（骑士）和 `tripo_node_fe781818.001`（枪）。两者原本就是独立 Object，均以 `Armature` 为父对象，有启用的 Armature 和 Decimate Modifier。预览确认枪是旁边竖放的独立网格，枪口沿源场景 +Z，角色未摆成双手持枪姿势。保留场景的四个隐藏相机。

- **骑士** `CastleGrunt` 有 5,124 个顶点、5,528 个原始面、1 张 `UVMap` 和 30 个有效权重组。全部顶点有 1–4 个有效影响，权重和为 1。工作副本保留全部原网格、权重、材质、UV、修改器和绑定姿势。临时转动两侧前臂、两侧小腿及 Waist 时均检测到顶点变形，抽测后重新加载工作副本。
- **骨架** 保留 41 根骨骼。层级从 `Root → Hip` 开始，`Pelvis` 支配双腿，`Waist → Spine01 → Spine02` 支配上身。存在双侧四肢扭转骨和颈部扭转骨，无独立手指骨、单独衣物骨、约束、Action 或 NLA。静置姿势中手臂下垂，Root 有非零姿势平移。部分主关节由其扭转子骨承载权重，这些主关节和子骨全数保留。
- **枪** `CastleRifle` 有 728 个顶点和 785 个原始面。源权重混有左手、左前臂和左小腿骨。仅在工作副本解除枪的 Armature Modifier、父对象和权重组，用测得的刚性位移保留原摆放，残差约 1 微米。原始网格、UV、共享材质均保留。
- **尺寸与纹理** 源文件为米制、单位倍率 1，角色约 1 米高，脚底 Z 约为 0。骑士与枪共用一张 1024×1024 的 sRGB Base Color JPEG。源材质粗糙度为 0.9、金属度为 0。工作副本指向工作目录内的纹理副本，原件仍使用原纹理引用。

| 文件 | SHA-256 |
|---|---|
| 原始 `Untitled.blend`，前后相同 | `8701B625EA9F8F6595DF3863E7AB302BD437EEB090F1A141CD7BA415BD1D7394` |
| 整理后的 `CastleEnemy_UE_Work.blend` | `90D76F6F2DD2F96101D3BEC887BD777440239C9231DDFB9D6B3E51ABFA8DF2E0` |
| 原 JPEG 及工作目录 JPEG 副本 | `8F17BEC4C14B755F3B1525413297DFF1E7FAC2F5BFEB06D4F74F10F249A261D3` |

[Blender 工作副本](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_20261120/CastleEnemy_UE_Work.blend>) · [Blender 和 UE 只读盘点](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_20261120/Preflight.json>) · [Blender 导出和蒙皮探针数据](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_20261120/BlenderExportAndReadback.json>)

## 2. 最终 FBX 与 Unreal 资产

交付使用 `FBX_UEcm`。首轮试导入将 FBX 头中的单位换算遗漏，模型尺寸小 100 倍且细小面损失，四个试导入包均留在该临时会话内。重新导出时把厘米换算写进 FBX 数值，复读 UE 包围盒后才保存。工作目录另有早期 `FBX` 试导产物，供追溯。

| 最终导出 | SHA-256 |
|---|---|
| [SK_CastleGrunt.fbx](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_20261120/FBX_UEcm/SK_CastleGrunt.fbx>) | `126A707192C72D79A6B8434066830B6D1781C9C163650F984E76E3B39256E03D` |
| [SM_CastleRifle.fbx](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_20261120/FBX_UEcm/SM_CastleRifle.fbx>) | `7F5CEEA0DFA07EF511F3558B10AA9F57000B5E40DC720A8D12FCE2691464AC90` |
| [T_CastleGrunt_BaseColor.jpg](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_20261120/Textures/T_CastleGrunt_BaseColor.jpg>) | `8F17BEC4C14B755F3B1525413297DFF1E7FAC2F5BFEB06D4F74F10F249A261D3` |

通过官方 Unreal MCP 完成协议 `2025-11-25` 握手、工具发现及真实只读调用，确认工程路径、初始地图 `/Game/Demo/L_CastleDemo` 和 PIE 未启动。只保存以下新包。随后在独立进程中冷加载并回读。

| UE 资产完整对象路径 | 冷加载结果 |
|---|---|
| `/Game/CastleDemo/Enemies/Art/Grunt01/SK_CastleGrunt.SK_CastleGrunt` | Skeletal Mesh，41 根骨骼的父子关系与 Blender 一致，参考姿势高 99.95 cm，脚底 Z 约 0，无 Physics Asset |
| `/Game/CastleDemo/Enemies/Art/Grunt01/SK_CastleGrunt_Skeleton.SK_CastleGrunt_Skeleton` | 独立目标 Skeleton |
| `/Game/CastleDemo/Enemies/Art/Grunt01/SM_CastleRifle.SM_CastleRifle` | Static Mesh，沿局部 +X 长约 78.13 cm、LOD0 1,351 三角面。自动生成的简单碰撞已清除 |
| `/Game/CastleDemo/Enemies/Art/Grunt01/M_CastleGrunt_Atlas.M_CastleGrunt_Atlas` | 两个网格均关联此材质，Base Color 使用贴图 RGB，Roughness 为 0.9 |
| `/Game/CastleDemo/Enemies/Art/Grunt01/T_CastleGrunt_BaseColor.T_CastleGrunt_BaseColor` | Texture2D，1024×1024 |

`Weapon_R` 是新 Skeletal Mesh 上的 Mesh Socket，挂在 `R_Hand`，其父骨为 `R_Forearm`。局部偏移为零、旋转为零、缩放为 1，冷加载一致。枪的 FBX 局部原点为根据源网格选取的**初始握把附近**，源坐标约 `(0, 0.195, 0.327) m`。枪口已转为枪网格局部 +X。这些只是对位起点，实际附着、左手接触、旋转和穿模均待持枪姿势预览。

[官方 MCP 保存和资产读回](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_20261120/UEImportAndSave.json>) · [冷加载与基线模拟证据](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_20261120/UEColdAndBaselineSIE.json>) · [五个新包 SHA](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_20261120/NewUEAssetsSHA256.txt>)

## 3. 动画资源与阻断

以下资源均在当前 UE 编辑器中读取到实际类型及 Skeleton。它们共用 `/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin`。表中路径相对于 `/Game/Characters/Mannequins/Anims/`。

| 用途 | 已确认的源资产 | 实际类型 |
|---|---|---|
| 无枪待机／走／Jog | `Unarmed/MM_Idle`、`Unarmed/Walk/MF_Unarmed_Walk_Fwd`、`Unarmed/Jog/MF_Unarmed_Jog_Fwd` | AnimSequence |
| 持枪待机／走／Jog | `Rifle/MF_Rifle_Idle_ADS`、`Rifle/Walk/MF_Rifle_Walk_Fwd`、`Rifle/Jog/MF_Rifle_Jog_Fwd` | AnimSequence |
| 攻击／前方受击／死亡 | `Rifle/MM_Rifle_Fire`、`Rifle/HitReact/MM_HitReact_Front_Med_01`、`Death/MM_Death_Front_01` | AnimSequence，尚无 Montage Slot |
| 手枪开火示例 | `Pistol/MM_Pistol_Fire_Montage` | AnimMontage，实际 Slot 为 `Arms` |
| 原动画蓝图 | `Unarmed/ABP_Unarmed`、`Unarmed/BS_Idle_Walk_Run` | AnimBP 与无枪 BlendSpace，目标为 Manny 共用 Skeleton |

官方 MCP 的已加载资产注册表中未检索到 IK Rig 或 IK Retargeter。加载并检查了 EditorToolset 与 AnimationAssistantToolset；能实际导入网格、编辑网格 Socket、创建材质和读取动画数据，动画编辑入口限于 Control Rig、Sequencer 等已注册接口。缺少用于创建和配置 IK Rig 骨链、校准 Retarget Pose、批量离线导出到目标 Skeleton，以及为新 Skeleton 创建并验证 AnimBP 的工作链。本轮停在这些步骤，IK Rig、IK Retargeter、专用 AnimBP 和重定向动画清单均为空。现有 `ABP_Unarmed` 的目标仍是 `SK_Mannequin`，新独立 Skeleton 需要自己的兼容 AnimBP。部分走、Jog 和全部三份持枪运动序列使用 `SKM_Quinn_Simple` 为 Retarget Source，可在人工预览时与 Manny 源比例对照。

## 4. Grunt 保护与实际回归

正式 Grunt 的修改前、修改后、独立备份 SHA-256 一致，均为 `2B44897835A5C5BCBF977D4D0AFACBA9480208C825360D3DB391614AA7F2978A`。仍使用 Manny、ABP_Unarmed，Mesh 相对偏移和原表现事件框架保留。攻击、受击、死亡的三个 Montage、三个 Sound 和 `Sound Attenuation` 槽均为空。未把无关动作赋到任何槽。

- **单兵基线 SIE** 在未保存的 Engine Entry 临时世界运行实际原 Grunt。SentinelBody、Helmet、Visor 三个占位部件均不可见，Capsule 36／88 cm、`QueryAndPhysics`，移动速度 270 cm/s。会话无渲染和音频。
- **本轮原 `CastleDemo.EnemyPresentation.SpawnAndDamage`** 使用已有 CastleDemo DebugGame 模块原样运行，发现并通过 1 项，26 条警告、0 条错误，进程退出码 0。Director 实际生成 15 个探索房 Grunt、3 个宝库 Grunt、1 个原生精英。攻击、有效受击、死亡通知各 1 次，同时记录 1 枚真实弹体、击杀 +1、弹药掉落 +1。攻击立即重试未增加次数，致死无额外 Hit，原 Actor 立即销毁。空资源无死亡代理，临时寿命测试 Montage 生成的代理无 Gameplay 碰撞，计划 7.717 秒，模拟至 7.900 秒时自动清理。临时 Montage 只用于现有基线断言。
- **警告** 20 条房间装饰资源缺失提示、5 条砌体或烛台静态网格加载提示、1 条临时世界 EndPlay 清理提示。PCG 保持此前的 `Blocked_MissingAssets`，本轮没有执行完整 PCG。
- **待测** 新角色与枪尚未接入正式 Grunt，未执行换模后的攻击、伤害、冷却、动态死亡观察或多兵性能测试。现有自动测试硬编码 Manny 和旧 AnimBP，结果适用于 FIX02 外观基线。换模后的动作、静态法线观感、脚滑、转向、双手接触、伤害／冷却以及 GameThread、动画和代理数量需要在独立世界另行测量。现有死亡代理只复制角色视觉网格，独立枪在死亡视觉阶段的去向仍待验收。

[基线自动化 JSON](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_20261120/ArtPipeline-Baseline-SpawnAndDamage/index.json>) · [事件、生成和死亡代理日志](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_20261120/ArtPipeline-Baseline-SpawnAndDamage.log:2094>)

## 5. 最小人工接续

1. 在内容浏览器选中一份上表的 Manny Animation Sequence，右键 **Retarget Animations**。在预览窗口设置源 `SKM_Manny_Simple`、目标 `SK_CastleGrunt`。可先使用 Auto Generate Retargeter，并将生成资产保存到 Grunt01。如果目标骨架未被模板识别，创建两份 IK Rig 后分别使用 **Auto Create Retarget Chains**，再人工补缺。 [Epic 自动重定向文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/auto-retargeting-in-unreal-engine)
2. 核对目标的 `Root`、`Hip`、`Waist → Spine02`、`NeckTwist01 → Head`、两侧 `Upperarm → Hand`、`Thigh → Foot` 和脚趾。从 `Hip` 作为目标骨盆运动参考开始预览，它同时统领 `Waist` 与 `Pelvis` 两支。自动对齐后手动核对肩、肘、膝、腕和着地。先看无枪 Idle、Walk、Jog，再看持枪三件套。达标后离线导出到 Grunt01 的独立动画目录。 [Epic IK Rig 重定向文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/ik-rig-animation-retargeting-in-unreal-engine)
3. 在独立测试子类使用目标 Skeleton 的 AnimBP，由速度驱动新的 Idle／Walk／Jog BlendSpace，并在输出前加全身 `DefaultSlot`。用有效的目标序列分别制作 Attack、Hit、Death Montage，之后从 **Class Defaults → Animation / Audio** 配置这些 Montage、声音和衰减。实际攻击触发继续由现有 C++ 控制。
4. 在测试子类的 **Components** 新增 Static Mesh 组件，挂在角色 Mesh 下，将 **Parent Socket** 设为 `Weapon_R`，选择 `SM_CastleRifle`，将组件设为 **NoCollision**，关闭重叠事件和导航影响。先调 Socket 旋转与右手握把，再检查左手支撑点。新角色当前素材高约 100 cm，先在原 Capsule 下预览比例；如需调身高、中心偏移和脚底，只调视觉 Mesh 的相对变换，保持 Capsule 和移动参数。用有渲染、声音的独立世界完成单兵，再做少量多兵对照。 [Epic Mesh Socket 文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/skeletal-mesh-sockets-in-unreal-engine)

## 6. 工程保护与回滚

施工前后对照的 148 项文件 SHA 全部一致，覆盖 Blender 原件和原纹理、正式 Grunt、另两个表现蓝图、正式地图、相关 C++ 和测试、配置与 Manny 资产。五个 Grunt01 新包及工作目录是本轮交付。用户原编辑器仍运行，未保存正式地图，未写入 Director 类配置，未构建、提交、推送或清理缓存。

- **Blender** 原件直接重开即可。工作副本需要回到初始拆分前时，保留当前工作文件，再以 `CastleEnemy_UE_Work.blend1` 为源另存一份。若移动恢复文件，在材质 Image Texture 节点重指向本次提供的 JPEG，避免原件的相对路径在移动后失效。
- **UE** 正式 Grunt 当前无需回滚。若以后已接入新外观，恢复前先保留当前 Grunt 并核对编辑器未保存数据，只用本次备份定向恢复此蓝图，再核对 FIX02 SHA。移除本轮导入时，在内容浏览器使用 Reference Viewer 核对后续引用，确认仅涉及五个新包，再移除两个网格，随后处理目标 Skeleton、共享材质和贴图。保留 `FBX_UEcm` 和纹理便于重导。无需恢复团队资产和正式地图。

[原 Grunt 单资产备份](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_20261120/BP_CastleEnemy_Grunt_before.uasset>) · [保护 SHA 清单](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_20261120/ProtectedBeforeSHA256.txt>) · [FIX02 技术基线](<E:/Unreal Projects/PCG_AI_demo/CASTLE_ENEMY_PRESENTATION_V1_FIX02_RESULT.md>)