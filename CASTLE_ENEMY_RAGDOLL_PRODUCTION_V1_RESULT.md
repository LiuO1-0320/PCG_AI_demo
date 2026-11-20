# CASTLE_ENEMY_RAGDOLL_PRODUCTION_V1 验收报告

工程 `E:/Unreal Projects/PCG_AI_demo/testfordemo.uproject`，UE 5.8.4 / CL58902052。已核对 AGENTS.md、工程 SOP、Pilot 报告、原始单兵与三兵证据、最终代理 DSL 和姿势桥接节点。官方 MCP 完成握手、工具发现和资产、地图、PIE 状态的真实回读。

**Production Technical PASS（独立派生版）**。三个资产均已保存，新的编辑器进程冷加载后通过真实单兵与三兵 PIE。新版已经接入 `BP_CastleEnemy_Grunt_Art_Ragdoll`。现有地图磁盘上的生成配置仍选择原有类，随报告保留一个使用新版、已暂停的隔离测试窗口。

## 已保存的资产

三个新增包位于 `/Game/Art/Grunt01/IntegrationV2/Ragdoll/`。它们均通过编译和定向保存，并在独立、全新进程中从磁盘冷加载。

| 新资产 | 用途 | SHA-256 |
| --- | --- | --- |
| `ABP_CastleGrunt_DeathProxy` | 代理专用姿势桥接 | `1CA1CF4F646142DA0A24D7D8D4BEFC18743AA11FBAC66F13D303A547087BC74C` |
| `BP_CastleEnemy_DeathVisualProxy_Ragdoll` | 组件级物理覆盖、混合和尸体寿命 | `657FC3645C452E80B868694BDE5378A0954F79D95DF2B96BF38F75C1F5E1A705` |
| `BP_CastleEnemy_Grunt_Art_Ragdoll` | 继承原 Art，仅覆盖死亡表现 | `74689EEF11E5C06A6796A3DF070B8FE6A5DF6A365F503CE92B65CB6FB3CEA285` |

已保存的 AnimGraph 使用 `MM_Death_Front_01` 的非循环 Sequence Player，通过 `DefaultSlot` 输出。播放率 `1.0`、起始位置 `0`，Slot 始终更新基底姿势，供原 `AM_Grunt_Death` 的自然淡出与自然结束使用。三份新包的依赖中没有 `/Tests/RagdollPilot_Unsaved`。正常 Art 的 `ABP_CastleGrunt` 保留现状。

新代理只对 `VisualMesh` 设置现有 `PA_CastleGrunt_Ragdoll`。原 `1.100 s` Montage 的自然完成回调等待下一帧，评估并暂停末帧，然后开启 Chaos 和 `10 s` 寿命。混合权重 `0 / 0.15625 / 0.5 / 0.84375 / 1`，每步 `0.05 s`。代理物理碰撞为 `PhysicsOnly`，阻挡 `WorldStatic` 并忽略 Pawn、Visibility，枪械保持 `Weapon_R` 附着和 `NoCollision`。无资源或播放中断时代理清理，保留 `15 s` 初始兜底。无全局计时器。

## 普通兵接入与保护

施工前原用户编辑器的 `BP_CastleEnemy_Grunt_Art` 已读到未保存标记。保留原窗口、原 Art、旧死亡代理和其磁盘状态。新普通兵继承原 Art 的外观、武器、攻击及 C++ Gameplay 入口。它的死亡覆盖只有一个 SpawnActor，生成新的 Ragdoll 代理，附着枪械时保留原 Socket、相对变换及比例。覆盖没有调用父类静态代理链。

冷加载时测试地图的已保存 Director 仍指定原 `BP_CastleEnemy_Grunt_Art`。验收把 `OrdinaryEnemyClass` 临时设为 `BP_CastleEnemy_Grunt_Art_Ragdoll_C`，实际由 Director 生成和击杀。两张地图与 Director 配置都不写盘。正式 `L_CastleDemo` 的普通兵生成配置本轮保持原样，直接打开磁盘上的旧测试地图仍会使用原类。

## 冷加载 PIE 证据

在新进程 PID `36368`，用测试地图 `/Game/Art/Grunt01/IntegrationV2/Tests/L_CastleGruntArt_FIX01` 和 Seed `24680` 两次启动 PIE。新资产由磁盘加载。测试探针通过引擎 `ApplyDamage` 对 Director 实际生成的普通兵造成致死伤害，带真实玩家控制器作为伤害发起者。单兵场景剩余敌人由 15 降到 14，产生一个弹药补给，玩家记忆碎片由 35 升到 47。三兵场景由 15 降到 12，产生三个弹药补给，碎片由 35 升到 71。每次致死恰有一个新代理，旧静态代理计数始终为 0。原小兵从场景移除和掉落在 Ragdoll 倒计时之前已经发生。

按 Pilot 的相同口径，取模拟前最后七个采样与模拟首帧，计算骨盆相邻帧的最大三维位移。以下时间为 PIE 游戏时间。首次可见模拟均在代理首采样后约 1.12–1.13 秒，与 `1.100 s` 自然播放加下一帧交接一致。

| 试验 | 启动模拟时点（s） | 最后一帧存活时点（s） | 末帧剩余寿命（s） | 交接最大位移（cm） |
| --- | ---: | ---: | ---: | ---: |
| 单兵 | 3.11147 | 13.12315 | 0.00498 | 1.822 |
| 三兵 1 | 3.10102 | 13.11015 | 0.00780 | 1.857 |
| 三兵 2 | 3.76206 | 13.76777 | 0.01124 | 1.844 |
| 三兵 3 | 4.40858 | 14.42325 | 0.00200 | 2.139 |

Pilot 同口径的单兵最大值约 1.961 cm，三兵最大值约 2.185 cm。本次四具尸体的代理根位移均为 0，骨盆从约 92.3 cm 降至 15.9–17.2 cm。单兵逐帧日志还记录了右前臂、大腿和小腿的实际下落与摆动。右前臂从约 66.2 cm 开始，摆动后降至约 15.5 cm。左侧肢体提供了游戏视口画面，未单独做逐骨数值采样。

运行时读取和探针确认 `PhysicsOnly` 模拟，地面命中 `CastleDirector_0/InstancedStaticMeshComponent_9`，`WorldStatic`，Z=0、法线朝上。96 cm 的 Pawn 通行扫描使用玩家和敌人各自的碰撞配置，两条扫描均 `BlockingHit=false`。该项是实际 PIE 物理查询，玩家亲自走过尸体留给人工验收。

四个代理首次读到的寿命均为 10.000 秒，三具尸体按各自时钟依次清除。枪械整个模拟期间均挂在 `Weapon_R`，保持原世界缩放 `(2.4,2.4,2.4)`，枪械组件原点与 Socket 距离约 7.199–7.201 cm，枪械自身未开启物理。每次到期读取均确认代理和武器组件已销毁。测试 PIE 停止后也未在下一次运行中读到前次代理。播放失败和中断清理通过保存图表核对，未另行注入故障。

原始证据：[冷加载回读](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyRagdollProductionV1_20261120/Cold-Gate.json>) · [单兵](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyRagdollProductionV1_20261120/Cold-Single.json>) · [单兵逐帧日志](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyRagdollProductionV1_20261120/Cold-Single.PIE.log>) · [三兵](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyRagdollProductionV1_20261120/Cold-Three.json>) · [模块回读](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyRagdollProductionV1_20261120/LoadedModule.json>)。

## 真实游戏视口与性能

单独开启可见 PIE，从真实游戏视口取证。截图显示击杀后尸体落在地面且枪械随手保留。Montage 中段抓帧有墙体遮挡，因此交接判断以逐帧数据为主。近景可见枪身与甲片在部分角度重叠，需人工在连续播放中判断穿模程度，并检查手腕、膝踝、墙边落地和实际走过尸体的观感。

[落地实拍](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyRagdollProductionV1_20261120/Visual-03-Ground.png>) · [枪械和关节近景](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyRagdollProductionV1_20261120/Visual-04-Detail.png>)。

人工复测时切到保留的 `L_CastleGruntArt_FIX01` 隔离窗口，点 UE 工具栏的 **Resume**。本窗口的 Director 在内存里已生成 15 名 `BP_CastleEnemy_Grunt_Art_Ragdoll`，五个自动探针已经移除。击杀一名小兵后留在附近观察完整倒地、继续持枪、行走穿过尸体和约 10 秒消失，随后可再击杀三名看独立清理。结束后用 **Stop** 退出 PIE，关闭这个隔离窗口时选择不保存测试地图，磁盘上的生成配置就继续使用原 Art。视觉和窗口证据见 [人工验收会话](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyRagdollProductionV1_20261120/Manual-Handoff.json>)。

UE CSV 全运行区间统计，单位 ms，以 P50 / P95 表示。单兵约 863 帧，三兵约 874 帧，含同图其他 AI 与诊断探针。截图运行单独进行。

| 指标 | 单兵 P50 / P95 | 三兵 P50 / P95 |
| --- | ---: | ---: |
| 帧时间 | 16.67 / 20.01 | 16.89 / 19.97 |
| GameThread | 16.37 / 20.08 | 16.77 / 20.12 |
| GameThread Animation | 0.509 / 0.900 | 0.506 / 0.792 |
| GameThread Physics | 0.087 / 0.122 | 0.143 / 0.198 |
| 并行动画评估任务 | 0.323 / 0.502 | 0.445 / 0.640 |

没有同条件关闭 Ragdoll 的对照组，因此性能回退幅度未定。独立 Chaos 约束列只在单兵 CSV 出现，无法给出跨两组的单独 Chaos 成本对照。 [CSV 路径与统计](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyRagdollProductionV1_20261120/Perf.json>)。

## SHA、备份和恢复

原 `BP_CastleEnemy_Grunt_Art.uasset` 与 `BP_CastleEnemy_DeathVisualProxy_Art.uasset` 的本轮开始时备份保存在 `E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyRagdollProductionV1_20261120/Backup/`。备份哈希与本轮开始时的磁盘版本一致。复核的 16 个受保护文件与三份新增包在 PIE 后的 SHA-256 均与各自基线相同。原编辑器未进入 PIE，原 Art 未保存标记仍在。逐文件对照见同目录 `Before.json`、`After.json`，新增包清单为 `NewAssets.json`。

回到原死亡表现时，独立场景的 Director 继续用原 `BP_CastleEnemy_Grunt_Art_C` 即可。正式地图当前使用原配置，本轮无需从备份覆盖文件。保留未保存的用户版本。旧代理和三个新增包可以同时保留。

官方 UE MCP 完成三份资产图表施工、定向保存、冷加载与测试，无阻断生产资产的 MCP 步骤。保留现有 Development 模块，未执行 C++ 构建。独立进程中的临时插件选项没有写入项目设置。没有修改 PA、Skeleton、原动画 / Montage、原 AI、地图、持久 Director 配置或其他敌人系统。
