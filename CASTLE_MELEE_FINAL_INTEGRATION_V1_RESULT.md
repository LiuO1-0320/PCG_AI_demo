# CASTLE_MELEE_FINAL_INTEGRATION_V1_RESULT

**MELEE FINAL INTEGRATION PASS** · 2026-11-26

正式地图 `/Game/Demo/L_CastleDemo` 已保存近远程混编。新 Editor 进程从磁盘冷加载后，以 Seed 24680 在正常渲染的 Standalone PIE 中实测首层、二层、宝库与战斗。最后保留正式地图的暂停预览，玩家面前是本局实际生成的近远程混编敌群。解除暂停即可继续当前战斗。 [交接画面](Saved/CastleMeleeFinalIntegrationV1_20261126/Paused-Formal-Mixed.png) · [交接运行数据](Saved/CastleMeleeFinalIntegrationV1_20261126/Paused-Formal-Mixed.json)

## 生产核查与正式保存

- 实际工程 `E:/Unreal Projects/PCG_AI_demo/testfordemo.uproject`，引擎 UE 5.8.4 / CL 58902052，分支 `codex/castle-melee-v1`，HEAD `eea95e5`。工作区和附件中未定位到精确文件名 `CASTLE_MELEE_V1 (1).md`，本轮以实际工作树中的 [CASTLE_MELEE_V1.md](Docs/MultiAgent/CASTLE_MELEE_V1.md) 及已保存资产为基线。
- Epic 官方 MCP 完成握手、工具发现和实际只读检查。冷启动 Editor PID `12344` 加载本工程 `Binaries/Win64/UnrealEditor-CastleDemo.dll`，SHA256 `215c9301c0ab1f679f8f7ad80d35a7a7967968bd64b6edfa2e1e7a17ccc4c165`。沿用此前与当前 Castle 源码对应的模块，运行日志实测混编抽样、Melee Notify 与原生弹体。此轮未发起构建。 [施工前核查](Saved/CastleMeleeFinalIntegrationV1_20261126/Preflight.json) · [加载模块](Saved/CastleMeleeFinalIntegrationV1_20261126/Cold-Module.json)
- 唯一 Director 为 `/Game/Demo/L_CastleDemo.L_CastleDemo:PersistentLevel.CastleDirector_0`，随地图包持久化。修改前地图干净，定向保存后和冷加载后读回相同的三项配置。 [定向保存](Saved/CastleMeleeFinalIntegrationV1_20261126/Formal-Map-Save.json) · [冷加载读回](Saved/CastleMeleeFinalIntegrationV1_20261126/Formal-Cold-Gate.json)
- 读回近战兵 `bUseMeleeV1=true`，使用 `ABP_CastleGrunt_Melee` 和 `BS_CastleGrunt_Melee`。0 cm/s 是 `/Game/Characters/MM_Idle`，135 和 270 cm/s 均为 `AS_Grunt_Melee_Walk`，攻击为 `AS_Grunt_Melee_Strike_Montage`。死亡使用既有 `AM_Grunt_Death` 与 Ragdoll 代理。 [实际资产读回](Saved/CastleMeleeFinalIntegrationV1_20261126/Preflight.json)

| Director 反射属性 | 修改前 | 保存并冷加载后 |
| --- | --- | --- |
| `OrdinaryEnemyClass` | `BP_CastleEnemy_Grunt_Art_Ragdoll_C` | `BP_CastleEnemy_Grunt_Art_Ragdoll_C` |
| `OrdinaryMeleeEnemyClass` | `None` | `BP_CastleEnemy_Melee_C` |
| `OrdinaryMeleeChance` | `0` | `1/3`（浮点读回 `0.3333333432674408`） |

远程类路径为 `/Game/Art/Grunt01/IntegrationV2/Ragdoll/BP_CastleEnemy_Grunt_Art_Ragdoll`，近战类路径为 `/Game/Art/Grunt01/MeleeV1/BP_CastleEnemy_Melee`。概率逐生成槽位抽取。

| 唯一修改的 UE 资产 | SHA256 |
| --- | --- |
| `Content/Demo/L_CastleDemo.umap` 修改前 | `c05e20978eecdbc77015ef0ab90cf9b590d6bcf7ae59464671645f63d9487778` |
| 保存后及最终复核 | `74e9cbf1f9fc883f47fc84e30d5fd13df3a8202fe78eff3a78f5c47dbdfdcca5` |

## 正式地图出兵

| 区域（Seed 24680） | 远程 | 近战 | 普通兵合计 | 特殊敌人 |
| --- | ---: | ---: | ---: | --- |
| 第一层探索 | 11 | 4 | 15 | 1 原生 `ACastleEnemy` 精英 |
| 第二层探索 | 10 | 6 | 16 | 1 原生 `ACastleEnemy` 精英 |
| 第二层宝库 | 4 | 1 | 5 | 同层快照另有 1 原生 `ACastleQueen` Boss |

实际使用场景互动进入二层和宝库。测试定位后确认玩家处于 Walking 且碰撞有效，由实际预览窗口的 `F` 交互触发转换。36 个普通兵槽位按楼层、区域、房间和序号与既有同 Seed 的基线逐项比对，抽样结果、实际生成类和生成位置均一致，日志无槽位生成失败。 [首层 Actor](Saved/CastleMeleeFinalIntegrationV1_20261126/T01-Floor1-Actors.json) · [二层 Actor 与宝库守卫](Saved/CastleMeleeFinalIntegrationV1_20261126/T05-Floor2-Actors.json) · [上楼交互](Saved/CastleMeleeFinalIntegrationV1_20261126/T05-Ascend.json) · [宝库交互](Saved/CastleMeleeFinalIntegrationV1_20261126/T05-Cave-Entered.json) · [正式 PIE 日志](Saved/CastleMeleeFinalIntegrationV1_20261126/Formal-Cold-PIE.log) · [既有同种子对照](Saved/CastleMeleeV1/rendered-continuation-evidence.log)

## T01–T06 正常渲染 PIE

| 编号 | 结论 | 核心运行证据 |
| --- | --- | --- |
| T01 正式出兵 | **PASS** | 首层生成完成。实际运行类为新近战蓝图和既有远程蓝图，普通兵 15 名，原生精英 1 名。 |
| T02 近战表现与战斗 | **PASS** | 同一名近战兵空手待机 6.004 秒，7 次角色与动画速度读数均为 0。自然追击最高 270 cm/s，近身停步并完成前摇、挥击、命中、收招，日志也有合法挥空。拉开 492.06 cm 后同一敌人恢复追击、移动 192.06 cm。自动后退键没有产生位移，恢复追击补测通过 MCP 移动玩家，随后验证原生 Walking、碰撞和敌人自主追击。 |
| T03 远程回归 | **PASS** | 同场远程 `C_7` 可见持枪，自主移动至 270 cm/s 后停步瞄准。实际 `CastleBolt_0` 的 Owner 为远程 `C_3`。移动瞄准与原生射击分别由同场的两名远程兵取证。 |
| T04 近远程死亡 | **PASS** | 两名目标各消耗 4 发实弹并由 85 HP 降至死亡。各奖励 12 碎片且击杀数累计至 2，首次掉落 `RAPID FIRE`。每次只生成一个代理，从 `AM_Grunt_Death` 自然进入实际 Chaos 模拟。Montage 资产时长 1.1 秒，PIE 采样确认阶段顺序。画面确认近战尸体无枪、远程尸体仍持枪，运行时 Socket 字段未读出。代理均设定 10 秒，两个代理在同一个仍运行的 PIE 世界中先后消失。 |
| T05 楼层与宝库混编 | **PASS** | 正式交互进入二层与宝库，按实际生成记录确认两处均混编，精英和 Boss 维持原生类。 |
| T06 混合战斗与基础稳定性 | **PASS** | 两类敌人同场分别进行近战命中与原生射击，连续两次击杀的两个代理均完成清理。观察期间未见集体停追、攻击死锁或崩溃。结论覆盖本次运行的行为与生命周期，帧时间独立记录。 |

- T02–T03 [空手待机采样](Saved/CastleMeleeFinalIntegrationV1_20261126/T02-Idle.json) · [待机画面](Saved/CastleMeleeFinalIntegrationV1_20261126/T02-Idle-3.png) · [混合战斗数据](Saved/CastleMeleeFinalIntegrationV1_20261126/T02-T03-T06-MixedCombat.json) · [自主复追补测](Saved/CastleMeleeFinalIntegrationV1_20261126/T02-Recovery-Confirmed.json)
- T04 [近战击杀阶段](Saved/CastleMeleeFinalIntegrationV1_20261126/T04-Melee-V2-Kill-Phases.json) · [远程击杀阶段](Saved/CastleMeleeFinalIntegrationV1_20261126/T04-Ranged-V2-Kill-Phases.json) · [近战倒地画面](Saved/CastleMeleeFinalIntegrationV1_20261126/T04-Melee-V2-Ragdoll.png) · [远程持枪倒地画面](Saved/CastleMeleeFinalIntegrationV1_20261126/T04-Ranged-V2-Ragdoll.png) · [同局清理记录](Saved/CastleMeleeFinalIntegrationV1_20261126/T04-T06-SameWorld-Cleanup.json)

### 帧时间与后续覆盖

| 正常渲染 PIE 采样（193 帧，ms） | P50 | P95 | 最大 |
| --- | ---: | ---: | ---: |
| Frame | 32.99 | 216.05 | 495.75 |
| GameThread | 32.28 | 109.99 | 340.26 |
| GPU | 18.10 | 63.09 | 195.77 |

短样本含测试定位、控制台及采集操作，观察到长帧。此轮缺少同条件对照，无法归因到混编、动画或物理。历史长帧边界、墙体命中边界、ABBA 性能对照仍待覆盖。 [采样摘要](Saved/CastleMeleeFinalIntegrationV1_20261126/T06-FrameSummary.json)

## 工作树与回滚

- `codex/castle-melee-v1` 与 `origin/codex/castle-melee-v1` 在核查时对齐。本轮新增本报告与 `Saved` 下的本地证据，正式资产仅写入 `Content/Demo/L_CastleDemo.umap`。原有未提交 BlendSpace、`Docs/MultiAgent/CASTLE_MELEE_V1.md` 与未跟踪 `.codex-attachments/` 保留。16 个相关文件的前后哈希对比中只有正式地图不同。 [范围核对](Saved/CastleMeleeFinalIntegrationV1_20261126/Final-Protection.json)
- 最小回滚是在正式地图的 `CastleDirector_0` 将 `OrdinaryMeleeEnemyClass` 设回 `None`、`OrdinaryMeleeChance` 设回 `0`，保留正式远程类，仅保存地图并冷加载读回。此法保留团队随后对地图的其他修改。若必须逐字节恢复，只在确认正式地图仍是本轮保存 SHA、且相关 Editor 已正常关闭并无未保存改动时，使用备份替换地图文件，再核对恢复前 SHA。 [原图备份](Saved/CastleMeleeFinalIntegrationV1_20261126/L_CastleDemo.before.umap)
