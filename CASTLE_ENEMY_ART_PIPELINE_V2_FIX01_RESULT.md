# CASTLE_ENEMY_ART_PIPELINE_V2-FIX01 验收报告

## 结论

**已接入移动与攻击，独立战斗地图已通过真实出兵、射击、伤害、掉落与首轮性能采样。** 持枪死亡代理在临时短片段测试中成功播放并清理。完整受击和死亡仍受 Montage 截短阻断，正式 Art 的两个槽保持空白。

官方 Unreal MCP 已恢复，在现有 UE 5.8.4 / CL58902052 编辑器完成实时调用、蓝图编译及定向保存。工程为 `E:/Unreal Projects/PCG_AI_demo/testfordemo.uproject`。最后读回显示当前关卡为 `/Game/Demo/L_CastleDemo`，PIE 关闭，正式地图、Art 和新代理均无待保存修改。隔离测试实例已退出。期间检测到原 Development 实例正常退出及同工程新实例，已向当前实例恢复 8000 官方服务并读回原地图及非 PIE 状态。 [官方服务启动日志](<E:/Unreal Projects/PCG_AI_demo/Saved/Logs/testfordemo.log:6511>)

## 用户已有的五个动画资产

以下包均已保存，均引用 `/Game/Art/Grunt01/SK_CastleGrunt_Corrected_Skeleton`。本轮通过官方 MCP 核对资产类型、Skeleton、图表节点和连接，未改这些包。

| 实际包路径 | 实时核对结果 |
| --- | --- |
| `/Game/Art/Grunt01/IntegrationV2/BS_CastleGrunt_Rifle` | X 轴 Speed 为 `0–270 cm/s`，Idle 在 0，原地 Walk 在 135，原地 Jog 在 270，三个采样倍率均为 1 |
| `/Game/Art/Grunt01/IntegrationV2/ABP_CastleGrunt` | 有可读的已保存生成类。Event Graph 取 Pawn 的 `Get Velocity → Vector Length XY → Speed`，无有效 Pawn 时将 Speed 设为 0。AnimGraph 实际连线为 `Speed → Blend Space X` 和 `Blend Space Pose → DefaultSlot → Output Pose`。CDO 的 Root Motion Mode 为 `IgnoreRootMotion` |
| `/Game/Art/Grunt01/IntegrationV2/AM_Grunt_Attack` | `DefaultSlot`，使用 `AS_Grunt_Attack_FullPose`。片段和 Montage 都是 **0.533 秒**，覆盖完整源序列。已接入 Art，真实 Director 战斗中采到两轮播放 |
| `/Game/Art/Grunt01/IntegrationV2/AM_Grunt_Hit` | `DefaultSlot`，使用 `AS_Grunt_Hit_FullPose`。源长 **0.767 秒**，片段和 Montage 都在 **0.533 秒**结束，截掉约 **0.233 秒**。完整受击标为 `AnimationAssetBlocked` |
| `/Game/Art/Grunt01/IntegrationV2/AM_Grunt_Death` | `DefaultSlot`，使用 `/Game/Characters/MM_Death_Front_01`。源长 **1.100 秒**，片段和 Montage 都在 **0.533 秒**结束，截掉约 **0.567 秒**。完整死亡标为 `AnimationAssetBlocked` |

已核对 Walk、Jog 仍使用关闭 Root Motion 并强制根锁的 Prepared 副本，Attack、Hit 使用非 Additive 的 FullPose 副本。真实行走脚步、枪械与人体姿势仍要看运行轨迹和动画。 [动画资源与图表读回](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_FIX01_20261120/AnimationReadback.json>)

## Art 子蓝图与持枪代理

| 对象 | 已保存且读回的状态 |
| --- | --- |
| `/Game/Art/Grunt01/IntegrationV2/BP_CastleEnemy_Grunt_Art` | 仍继承原 Grunt。Mesh 为 `SK_CastleGrunt_Corrected`，Animation Mode 为 `Use Animation Blueprint`，Anim Class 为 `ABP_CastleGrunt_C`。相对位置 `(0,0,-88) cm`、Yaw `-90°`、Scale `(1,1,1)`、NoCollision。Capsule 半径 36 cm、半高 88 cm，移动速度 270 cm/s。三个静态占位件保持隐藏 |
| Art Montage 槽 | `Attack Montage = AM_Grunt_Attack`。`Hit Montage` 和 `Death Montage` 因片段截尾留空。三个既有播放倍率为 1 |
| Art 原持枪 | 保留单一 `RifleVisual`，通过现有 BeginPlay 挂 `Weapon_R`，挂接成功再显示，随后恢复 Atlas。偏移位置 `(-0.4591,-2.7856,1.0148) cm`，旋转 `Pitch 19.7705° / Yaw -99.3579° / Roll 98.9200°` |
| `/Game/Art/Grunt01/IntegrationV2/BP_CastleEnemy_DeathVisualProxy_Art` | 已新建，继承原死亡代理。新增一把使用原枪械网格及 Atlas 的枪。代理的视觉 Mesh 和枪均为 NoCollision，关闭 Overlap 与导航贡献。8 秒为兜底寿命 |
| Art 死亡覆写 | 静态图中恰有一个代理 Spawn 和一个 Montage_Play。保留原位置音效参数，验证死亡 Montage 和源 AnimInstance，再给新代理设置骑士 Mesh 与 Anim Class。检查 `Weapon_R`，挂枪后使用同一偏移。以代理自己的 AnimInstance 播放，成功时设置实际播放时长加 0.15 秒的寿命，失败时清理代理。当前空死亡槽会在生成前退出 |
| 音效 | 项目 `/Game` 中的 SoundWave、SoundCue、SoundAttenuation、MetaSoundSource 查询结果均为空。Attack、Hit、Death 和 Attenuation 保持空槽，听觉验收 Pending |

两份蓝图均通过官方编辑器编译并分别保存。持枪代理的短片段运行证据见下节。完整死亡终帧仍受资产截尾阻断。安装版蓝图节点发现工具在第一次缓存初始化时临时添加并移除了候选宏节点，曾把 Art 标记为 Dirty。施工前对照了上一轮挂枪图、当前节点及组件、Art 原文件 SHA，之后只保存本轮授权的两个蓝图。 [Art 配置读回](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_FIX01_20261120/ArtPhase1.json>) · [死亡图和碰撞读回](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_FIX01_20261120/DeathGraphSaved.json>)

## 游戏运行与性能

在独立 DebugGame 编辑器加载已保存的 Art 与新代理，运行 UE 5.8.4 / CL58902052、D3D11 离屏渲染。该实例使用项目已有的 DebugGame CastleDemo DLL。用户要求取消固定内存门槛后完成了本阶段 PIE，未重建 C++。原 Development 实例缺少 `OrdinaryEnemyClass` 及原生表现事件的结论仍成立。

| 检查 | 实际结果 |
| --- | --- |
| 独立单兵短片段测试 | 在未保存的独立世界放置继承 Art 的临时测试子类，只在该子类启用现有短 Montage。真实 `ApplyDamage(1)` 返回 1。Art 的受击 Montage 实际播放，采到位置 0.096 秒。致死调用返回 10000，原敌人随后消失。未直接计数原生 Hit/Death 函数调用 |
| 持枪死亡代理 | 致死后实际找到 Art 代理和 `ABP_CastleGrunt_C` AnimInstance。死亡 Montage 位置从 **0.105 秒**前进到 **0.288 秒**。枪组件读到 `Weapon_R`，两次世界变换随动画改变且保持单位缩放。采样时剩余寿命 **0.633 秒**，后续查询已无代理。此结果覆盖当前 **0.533 秒短片段**的播放和清理 |
| 实际 Director 生成 | 独立测试地图 `/Game/Art/Grunt01/IntegrationV2/Tests/L_CastleGruntArt_FIX01` 保存了 Art 普通兵类。Seed 24680 的探索层运行实际生成 **15 名 Art 和 1 名原生精英 `/Script/CastleDemo.CastleEnemy`**。未加载宝库作本轮额外验证 |
| 原生运动和攻击 | 实际 Art 的水平速度读到 **270 cm/s** 并回落至 **0 cm/s**。选中敌人 `BP_CastleEnemy_Grunt_Art_C_0` 有两个攻击 Montage 播放窗口，位置从零重启后继续增加。弹体归属补测分别记录两轮同房间三名 Art 各拥有一个原生 CastleBolt。两轮分别采样，逐次原生事件总数未挂钩统计 |
| 伤害与掉落 | 实际 Art 血量 **85 → 84**，再受致死伤害 Destroy。玩家金币 **35 → 47**，交互物数量 **7 → 8**，新增一件 **`Ammo / SUPPLIES`**。全场敌人数 **16 → 15**。正式 Art 的 Hit 和 Death 槽仍为空，击杀未用短片段来冒充正式配置 |
| 三兵独立性 | 同一战斗房间的三个真正生成的 Art 都使用正确骑士 Mesh 和 `ABP_CastleGrunt_C`。每把可见枪分别以本角色 Mesh 为父组件，NoCollision、关闭 Overlap 和导航贡献，偏移读回一致。三人同时受击和死亡的表现尚未做压力测试 |
| 性能实采 | 同一原生生成设置、全活动场景、带观察器的 144 帧。GameThread 平均 **15.00 ms**、P95 **19.13 ms**。GameThread 动画均值 **0.858 ms**、P95 **1.270 ms**。动画工作线程统计均值 **0.608 ms**、P95 **0.992 ms**。这是一段场景采样，未以无 Art 的对照组测量性能差值 |
| 人工美术与基线 | Idle 与 Jog 驱动速度区间有运行采样，Walk 中段、连续转向、脚底、双手握持、穿模、完整受击与死亡终帧仍需人工逐段查看。Manny 版 `SpawnAndDamage` 源和断言保留，本轮未重跑 |

两轮归属观察器曾因测试图引用已销毁弹体而出现一次测试工具错误。随后仅在单独采样图中修正引用和有效性判断，第二轮重新实测三枚弹体及三名实际主人，无该错误。所有观察器、短片段子类及临时地板均未保存到工程。测试地图磁盘包检查未发现观察器引用。正式地图与正式 Director 配置保持原状。

- [单兵短片段与代理实测](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_FIX01_20261120/SingleTestLiveEvidence.txt>) · [测试地图保存读回](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_FIX01_20261120/DebugGameTestMap.json>) · [生成类记录](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_FIX01_20261120/DirectorLiveSpawn.json>)
- [三兵组件与攻击采样](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_FIX01_20261120/DirectorCombatReadback.json>) · [第一轮弹体与掉落记录](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_FIX01_20261120/ProjectileOwnershipAndLoot-Confirmed.json>) · [第二轮弹体归属复验](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_FIX01_20261120/ProjectileOwnership-WaveB.txt>)
- [采样统计](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_FIX01_20261120/CombatPerfSummary.json>) · [引擎原始 CSV](<E:/Unreal Projects/PCG_AI_demo/Saved/Profiling/CSV/Profile(20261120_151640).csv>)

## 定向收尾和人工验收

1. 打开 `AM_Grunt_Hit` 的 Montage 时间轴，右键实际动画片段，执行 `Reset Segment Length`，确认起点 0，终点和 Montage 总时长均约 0.767 秒。对 `AM_Grunt_Death` 做同样检查，完整时长 1.100 秒。保留 `DefaultSlot`。官方 MCP 当前读取了真实段落，但其通用属性工具无法可靠走 Montage 时间轴重算。用户原资产本轮保留。
2. 两条完整 Montage 可用后，在 `BP_CastleEnemy_Grunt_Art → Class Defaults` 把 `Hit Montage` 设为 `AM_Grunt_Hit`，把 `Death Montage` 设为 `AM_Grunt_Death`。枪偏移可在 Art 挂接成功分支和死亡事件中对照，两处当前一致。
3. 用项目现有 DebugGame 模块打开已保存的独立地图 `L_CastleGruntArt_FIX01`，在启动菜单用 Seed 24680 开始。该副本的 Director 已指向 Art。先验证真实普通兵和原生精英类，再记录单次开火的弹体与 Gameplay 事件、三条 Montage 的播放状态与位置、致死不额外触发 Hit、击杀掉落及代理枪械自动清理。单兵通过后用三名记录 GameThread、动画和代理数量。人工看 Idle、走跑、转向、双手持枪、受击及死亡终帧的穿模情况。

## SHA-256 与回滚

| 变更文件 | 修改前 SHA-256 | 修改后 SHA-256 |
| --- | --- | --- |
| `Content/Art/Grunt01/IntegrationV2/BP_CastleEnemy_Grunt_Art.uasset` | `F890C2FD49B364F66C821966AA567E441D632BA99611264C419FF7EF8B0D9ED7` | `FC4374DAD53D458192A73D71F6E6FC83C78EFAF93AAD78796880BA1A836FF658` |
| `Content/Art/Grunt01/IntegrationV2/BP_CastleEnemy_DeathVisualProxy_Art.uasset` | 新建 | `0CBDCFA96C7FE973EE44FFD5E98D414F029014FAE5DBD4F4F1E95DA690CE77D8` |
| `Content/Art/Grunt01/IntegrationV2/Tests/L_CastleGruntArt_FIX01.umap` | 新建 | `AD97AC2B1ACF932849C5E1F347FC2879B26AA27E567FD69322B7464F7275F8CE` |

本阶段前后检查的 66 个受保护文件 SHA 差异为 0。覆盖了五个手工动画包、用户最新 IK/Retargeter、原网格、原三个表现蓝图、相关原生源码、项目配置和正式地图。 [保护清单](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_FIX01_20261120/ProtectedBeforeSHA256.txt>) · [最终校验](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_FIX01_20261120/FinalSHA256.txt>)

Art 原包备份位于 [Backup/BP_CastleEnemy_Grunt_Art.uasset](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV2_FIX01_20261120/Backup/BP_CastleEnemy_Grunt_Art.uasset>)。需要恢复时，结束相关 PIE 并正常关闭加载该包的编辑器，留存当前 Art，再将备份按原名放回其原目录，并核对修改前 SHA。恢复 Art 后，用 Reference Viewer 检查新代理和测试地图是否仍有引用，再决定是否归档这两个新包。正式地图和原表现蓝图无需回滚。
