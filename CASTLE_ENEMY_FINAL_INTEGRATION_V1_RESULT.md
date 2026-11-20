# CASTLE_ENEMY_FINAL_INTEGRATION_V1_RESULT

**FINAL INTEGRATION PASS。** 正式地图已保存新版普通兵配置。新的生产 Editor 从磁盘冷加载后，实测普通兵生成、原生敌人隔离、追击、战斗及三兵死亡清理通过。美术目视验收另见下文。

## 生产 C++ 模块门禁

工程为 `E:/Unreal Projects/PCG_AI_demo/testfordemo.uproject`，引擎为 UE 5.8.4 / CL 58902052。官方 Unreal MCP 已完成握手、工具发现及地图、Director、PIE 状态的只读调用。生产源码与 FIX05 验收版本逐文件匹配。原用户编辑器 PID 36184 正常退出，退出后复核地图及受保护资产的磁盘 SHA-256 与预检一致。[MCP 门禁](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Copy-MCP-Gate.json>) · [生产源码对照](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Preflight-InputCompare.json>) · [正常退出日志](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Production-BuildInputs.json>) · [退出后哈希](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/AfterUserClose-SHA.json>)

| 文件 | 生产源码 SHA-256 |
| --- | --- |
| `Source/CastleDemo/CastleGameplay.cpp` | `FCF4F40F25709F796E9C7F8261875389FD2680964468CAD172819D7F8E9BF8A8` |
| `Source/CastleDemo/CastleDemo.h` | `7ED5E01CB35C3E282253EF1F3F65E4883445E6941904E4CF68252A954733DED7` |

以普通用户为当前生产工程构建完整 `testfordemoEditor Win64 Development` Target。UBT 实际编译 CastleDemo、链接 DLL 并写出 Target 凭据，结果为 `Succeeded`。构建沿用生产工程当时缺少项目 `Plugins` 目录的状态。旧 DLL SHA-256 为 `E889EC4562DB39419FDB8F7276862A8E7407A3A1D185AFD18575BF70A6692561`。新 DLL 为 `E:/Unreal Projects/PCG_AI_demo/Binaries/Win64/UnrealEditor-CastleDemo.dll`，SHA-256 为 `9FD3542E3E2DD98DB7FF2FBAC2CA8A6453CF152AB908DE4D7E65865A7752DC34`，Build ID 为 `55116800`。通过 Windows 进程模块清单核对，运行原 FIX05 门禁的 PID 36584 和运行正式地图副本的 PID 29580 实际加载了同一路径及 SHA-256 的新 DLL。[UBT 构建日志](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Production-Development-UBT.log>) · [构建产出](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Production-BuildResult.json>) · [门禁进程模块](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Production-Editor-FIX05Gate-LoadedModule.json>) · [副本进程模块](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/FormalAuthor-LoadedModule.json>)

原 FIX05 地图 `L_CastleGruntArt_FIX01` 的 Art / Reopen / 1 门禁在生产 DLL 上实际执行。玩家位移 0.000 cm，本次捕获 `RecastNavMesh-Default` 更新后完整路径查询耗时 0.0308 ms，`MoveTo` ID 6 被接受。测试到 27.003 s 正常结束。FIX05 报告的三种小兵、三次请求上限及多兵专项仍对应其隔离对照；本轮上述运行证实生产模块包含并执行了恢复逻辑。[本轮门禁日志](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Production-FIX05Gate-Art-Reopen-1.log>) · [FIX05 专项报告](<E:/Unreal Projects/PCG_AI_demo/CASTLE_ENEMY_PURSUIT_FIX05_RESULT.md>)

## 正式地图副本的追击恢复

在 `/Game/Art/Grunt01/IntegrationV2/Tests/L_CastleDemo_FinalIntegrationV1`，唯一的 `CastleDirector_0` 将 `OrdinaryEnemyClass` 从 `None` 改为 `BP_CastleEnemy_Grunt_Art_Ragdoll_C` 并定向保存。副本首次追击探针调用超时，复核后重新执行。在 Seed 24680 的 PIE 中，目标固定在 `(530, -510, 90.15)`，位移 0 cm。新版普通兵 `C_1` 在 15.900919 s 处于 Idle 且查询为 Partial。围栏于 16.000921 s 移除，16.035 s 收到 `RecastNavMesh-Default` 通知并取得完整路径，查询耗时 0.0177 ms，`MoveTo` ID 8 被接受，距移除围栏约 34.1 ms。16.105770 s 观察到该敌人以 103.52 cm/s 追击，路径已是完整路径。本次副本只对观察到完整恢复链的这名敌人作出通过判定，常规请求次数未采到。[副本保存记录](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/FormalCopy-Class-Saved.json>) · [首次超时](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Copy-Reopen.json>) · [复测结果](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Copy-Reopen-Retry.json>) · [复测日志](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Copy-Reopen-Retry.log>)

## 正式地图副本的三兵死亡与物理

Seed 24680、Floor 1 的副本 PIE 中，Director 实际生成 15 名新版普通兵。探针由真实玩家控制器发起三次原生 `ApplyDamage` 致死，存活数降为 12，出现三个 `Ammo/SUPPLIES` 掉落，玩家金币由 35 增至 71。三次致死各产生一个新布娃娃代理。Ragdoll Production 报告保留了独立派生版的单兵、三兵冷加载及完整 1.100 秒 Montage 的证据。本轮副本从观测到代理至模拟接管分别为 1.134、1.123、1.125 s。[本轮副本测试](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Copy-Death-Three.json>) · [Ragdoll Production 验收](<E:/Unreal Projects/PCG_AI_demo/CASTLE_ENEMY_RAGDOLL_PRODUCTION_V1_RESULT.md>)

| 代理尾号 | 首次物理时间（s） | 切入物理的骨盆单帧位移（cm） | 观测物理寿命（s） | 清理前骨盆 Z（cm） |
| --- | ---: | ---: | ---: | ---: |
| 0 | 9.150219 | 0.000 | 9.988954 | 12.270 |
| 1 | 9.801260 | 0.000 | 10.000168 | 15.573 |
| 2 | 10.469269 | 0.000 | 9.994379 | 14.523 |

在 22.014312 s 结束采样时，场上代理数为 0。共 1,671 条逐代理记录，全部显示枪械附着于 `Weapon_R`、枪械物理关闭、比例 `(2.4, 2.4, 2.4)`，枪械原点距 Socket 约 7.20 cm，符合配置偏移。地面实测为 `WorldStatic`、Z=0、法线朝上；玩家和敌人的胶囊扫掠均无阻挡。整套死亡测试由探针触发，游戏视口中的关节穿模、过渡观感和实际走过尸体留待目视验收。[逐帧日志](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Copy-Death-Three.markers.log>)

三兵测试 CSV 含 1,087 帧、22.382 s。用日志帧号匹配三具尸体同时模拟的 441 帧，GameThread p50/p95/p99 为 19.15/24.97/28.10 ms，游戏线程物理为 0.151/0.198/0.226 ms，物理工作线程为 0.796/0.937/1.098 ms，游戏线程动画为 0.542/0.880/1.015 ms。同一次 PIE 内开杀前预热阶段的 GameThread p50/p95/p99 为 15.36/20.63/22.46 ms。CSV 的 Physics 列覆盖全场物理，Chaos 专属耗时及独立对照组缺失，测量值包含探针和场景负载。[原始性能 CSV](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/FormalAuthorUser/Saved/Profiling/CSV/Profile(20261120_235015).csv>)

## Seed 1337 PCG 基线及退出异常

正式切换前的独立预检使用原生普通兵及 `/Game/Demo/L_CastleDemo`。当时资产注册表可以找到 `SM_Wall_00`、`SM_Ground`、`SM_Pillar`，实际地图日志确认墙体与地面加载。Seed 1337 的 Floor 1 顶部砌体探针共 30 项并输出 `PASS`。同一次运行完成三层探索、宝库与首领区域，原冒烟逻辑打印 `CASTLE_SMOKE_OK`。收到退出请求之后，独立进程在清理阶段出现 `WorldSubsystem.cpp:118` 的 `!bInitialized` Ensure，随后出现 `EXCEPTION_ACCESS_VIOLATION`。本次旧砌体缺失未复现，冒烟逻辑完成，编辑器干净退出仍有异常。该证据来自切换前原生普通兵基线，退出异常的根因尚未定位。[PCG 预检门禁](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Copy-PCG-MCP-Gate.json>) · [Seed 1337 全程日志](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Copy-PCG-Seed1337.log>)

## 副本出兵、战斗和动画

在 Seed 24680 的正常 PIE 中，等第一层完成生成后，将测试玩家临时移到真实的 ASCEND TO FLOOR 2 前，使用官方 MCP 发送 E。第二层三条区域完成日志及 bReady=true 均已验证。直接对 23 个实际 Actor 读取类及区域标签，探索新版普通兵 16，宝库新版普通守卫 5，原生 ACastleEnemy 精英 1，原生 ACastleQueen Boss 1。23/23 实际类符合预期，区域标签互斥。[传送门测试记录](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Copy-Ascend-Position.json>) · [二层类快照](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Copy-Floor2-Classes.json>)

实际运行的 ABP_CastleGrunt 收到静止和约 270 cm/s 的追击输入。另在副本独立 PIE 中，仅将一名刚生成的小兵的运行时移动上限临时设为 135 cm/s，24/24 个实时样本的 AnimBP Speed 同步到 135 cm/s，对应 Blend Space 的 Walk 点，随后恢复 270 cm/s。动画输入验证通过，人体观感仍待目视验收。[实时战斗数据](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Copy-Combat-Retry.json>) · [Walk 速度取样](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Copy-Visual-Walk-Band-EarlyPIE.json>)

两名小兵停步攻击时均采到 AM_Grunt_Attack 实际播放，主观察兵识别出 13 个归属明确的原生 CastleBolt，连续可见弹体间隔通常为 1.905–1.918 s，有一次 2.243 s。玩家护盾 60 耗尽，生命 100 降至 40。一点非致死伤害在前后独立帧样本将目标生命从 85 降到 84。探针在同一日志行重复求值的 before/after 均显示 84，使用独立帧读数佐证。当前未观察到受击 Montage 播放。自动路线在入口侧通道未走完整圈，绕障后重新追击由独立围栏测试验证。[逐帧战斗日志](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Copy-Combat-Retry.log>)

尝试使用 CastleBossPreview 时，Director 到达第二层但 bReady 持续为 false。该测试已停止。使用实际传送门的独立流程完成了全部二层区域。预览停滞根因尚未确认，工程 PCG 和导航代码保持原样。[预览诊断](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Copy-Preview-Floor2-Diagnostic.json>)

## 正式配置及冷加载

正式地图唯一 Director 为 /Game/Demo/L_CastleDemo.L_CastleDemo:PersistentLevel.CastleDirector_0。关卡不使用 External Actors。OrdinaryEnemyClass 持久归属为 Content/Demo/L_CastleDemo.umap。保存前值为 None，原生普通兵回退。保存后为 /Game/Art/Grunt01/IntegrationV2/Ragdoll/BP_CastleEnemy_Grunt_Art_Ragdoll.BP_CastleEnemy_Grunt_Art_Ragdoll_C。

| 资产 | 保存前 SHA-256 | 保存后 SHA-256 |
| --- | --- | --- |
| 正式 L_CastleDemo.umap | C3BEE36C6454235EADD8BE7CE67B02954AA740F454A49ACC2AC5FA05907AAECD | C05E20978EECDBC77015EF0AB90CF9B590D6BCF7AE59464671645F63D9487778 |
| 验收副本 L_CastleDemo_FinalIntegrationV1.umap | 本轮新建 | DDA06ACCA95FF25FAD00A4A0C0B372EE5E83F982CF21A17F84E860868B9F3550 |

只通过官方 Save Asset 接口保存正式地图包。保存后原 Art、旧死亡代理、新版普通兵及物理资产均显示 clean。正式写入会话关闭后，新的 Editor PID 38940 从实际项目路径加载正式地图，读回新版类，正式地图处于 clean。该进程实载生产 DLL，SHA-256 为 9FD3542E3E2DD98DB7FF2FBAC2CA8A6453CF152AB908DE4D7E65865A7752DC34。[正式定向保存](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Formal-Write-Result.json>) · [存盘哈希](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Formal-Write-SHA.json>) · [冷加载闸门](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Formal-Cold-Gate.json>) · [冷加载实际模块](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Formal-Cold-LoadedModule.json>)

## 正式地图实际出兵与 Gameplay

使用冷加载后的 /Game/Demo/L_CastleDemo 与 Seed 24680。从内存中核对正式 Director 的持久配置，按正常传送门交互进入第二层。测试仅在 PIE 中将玩家移到门前，以 E 触发原生交互，三条区域完成日志、bReady=true 和实例读取一致。第一层普通兵 15 名使用新版。第二层逐实例检查结果如下。[正式传送门](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Formal-Ascend-Position.json>) · [到达二层确认](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Formal-Ascend-Confirmed.json>) · [23 个实际 Actor](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Formal-Floor2-Classes.json>)

| 入口 | 数量 | 实际生成类 |
| --- | ---: | --- |
| 探索普通兵 | 16 | BP_CastleEnemy_Grunt_Art_Ragdoll_C |
| 宝库普通守卫 | 5 | BP_CastleEnemy_Grunt_Art_Ragdoll_C |
| 探索精英 | 1 | /Script/CastleDemo.CastleEnemy |
| Boss | 1 | /Script/CastleDemo.CastleQueen |

分别观察到三名普通兵停步并实际播放攻击 Montage。三名小兵分别识别到 4、3、5 个自身拥有的原生 CastleBolt，去重以 Actor 名称与 Owner 为依据。连续可见弹体的最短间隔为 1.905 s，与原生 1.9 s 冷却一致，较长间隔包含离开射程或弹体漏采。将测试玩家移出接战位置后，三名小兵在 9.196–9.826 s 的取样中相继恢复接近 270 cm/s 的移动。此项通过实际敌人、玩家和弹体执行，玩家站位由探针安排。[正式三兵作战](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Formal-Combat-Group.json>) · [停步攻击及续追](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Formal-Combat-Group-Checks.json>)

正式进程用官方 Automation 工具执行 CastleDemo.EnemyPresentation.SpawnAndDamage 和 CastleDemo.EnemyPresentation.SpawnFailure，各 1 项通过，共 2/2，0 错误。继承路径验证覆盖攻击、有效受伤、击杀计数、掉落、不可重复死亡、新类失败不回退和精英隔离。SpawnAndDamage 有一条临时 Untitled World 清理告警，既有测试使用技术版 Grunt，不代表正式美术蓝图的运行时检查。[原生测试结果](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Formal-Native-Regression.json>)

## 正式地图死亡及性能

正式三兵原生致死测试 8/8 检查通过。15 名实际生成的新版兵中，3 名通过真实玩家控制器发起的 ApplyDamage 先后受致死伤，存活数变为 12。金币增加 36，出现三份 Ammo/SUPPLIES。每次实际死亡分别生成一个新代理。正式 Director 的 Kills 内部字段未直接读取，计数由原生测试独立断言。代理从出生到 Chaos 接管为 1.121–1.135 s，对应已验证的 1.100 s 自然死亡 Montage 加帧对齐和混合开始。首次物理帧的骨盆单帧位移均为 0.000 cm。[正式死亡实测](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Formal-Death-Three.json>)

| 实际代理 | 观察到的物理寿命（s） | 清理前骨盆 Z（cm） | Weapon_R |
| --- | ---: | ---: | --- |
| C_0 | 9.994 | 17.373 | 持续附着 |
| C_1 | 9.984 | 15.833 | 持续附着 |
| C_2 | 9.988 | 15.736 | 持续附着 |

地面探针命中 WorldStatic，地板 Z=0。玩家和敌人的 Pawn 胶囊扫掠都未被尸体阻挡。1,848 条代理采样均保持 Weapon_R、枪械物理关闭、缩放 2.4。约 22.001 s 时场内已无任何代理。视口对倒地关节、枪械穿模和玩家实际走过尸体的人工验收仍待完成。

正式三兵 CSV 为 1,185 帧、22.393 s。三具尸体同时模拟的 482 帧中，GameThread p50/p95/p99 为 17.17/21.80/24.36 ms，游戏线程物理为 0.133/0.169/0.204 ms，物理工作线程为 0.743/0.913/1.009 ms，游戏线程动画为 0.479/0.681/0.864 ms。同一次 PIE 杀敌前已预热的 267 帧，GameThread 为 15.01/17.84/20.65 ms。采样包含场景和探针开销，缺少独立性能对照与 Chaos 独占耗时。[正式性能 CSV](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Formal-ColdUser/Saved/Profiling/CSV/Profile(20261121_001643).csv>)

## 正式地图的 FIX05 追击恢复

玩家保持在 (530, −510, 90.15)，全程位移 0 cm。新版普通兵 C_1 在 15.950 s 已停步，对同一目标的受限查询返回 Partial。16.016 s 移除测试围栏。约 33.7 ms 后 RecastNavMesh-Default 更新，实际完整路径查询耗时 0.0175 ms，MoveTo ID 16 被接受。16.150 s 实际沿完整路径移动，速度 157.22 cm/s。此次正式地图重验采到一名小兵的完整恢复链。请求预算上限来自 FIX05 专项报告。临时围栏和探针已清理。[正式追击结果](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Formal-Reopen.json>) · [原始追击日志](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Formal-Reopen.log>)

## 保护核查与剩余验收

冷加载与测试结束后，正式地图的 SHA-256 与定向保存时完全一致。14 个保护对象中只有正式地图发生预期变化，副本哈希亦与首次定向保存一致。诊断 Actor 及蓝图仅用于各自测试进程，磁盘 Content 中没有留下探针包。关闭了本轮自建的测试进程。[退出前状态](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Formal-Cold-ExitGate.json>) · [最终文件与 SHA-256](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Final-SHA.json>)

- 正常上楼流程的 Seed 24680 三个区域均完成生成。本轮新版配置的 Seed 1337 全三层冒烟未另跑。原生旧配置的 Seed 1337 全流程功能断言通过，退出异常有独立日志。
- 人工美术验收待做。已保留正式地图 /Game/Demo/L_CastleDemo 的 testfordemo Preview 可见窗口，Seed 24680，第一层已实际生成 15 名新版普通兵。在探索战斗房观察站定射击与持枪，再击杀三名小兵，确认倒地、枪械随手、走过尸体和约 10 秒清理。[人工验收窗口](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Formal-Playtest-Active.json>)
- 当前未观察到受击 Montage，独立受击和掉落结算已验证。未修改动画、物理和音效资产。
- 截至本轮，少量敌人的追击、同场射击及连续死亡通过。性能来自定向单次采样，独立基线对照及更大规模压力测试未做。

## 最小回滚

在没有用户未保存正式地图改动的会话中，用正式地图中的同一 Director 将 OrdinaryEnemyClass 改回 None，只保存 /Game/Demo/L_CastleDemo，再冷加载读回。这样保留后续地图编辑和 FIX05 模块。如需字节级恢复，先确认正式地图自本轮后没有其他编辑，关闭持有它的 Editor，使用关闭原用户编辑器后 SHA 已核对的地图备份。新 DLL 保留用于维持 FIX05，独立副本也可保留供后续测试。[正式地图备份](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/Backup/L_CastleDemo.after-user-close.umap>) · [写盘前保护文件对照](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyFinalIntegrationV1_20261120/PreFormalSave-SHA.json>)
