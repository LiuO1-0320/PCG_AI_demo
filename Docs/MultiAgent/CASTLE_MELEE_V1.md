# CASTLE_MELEE_V1

## 工作基线与当前阶段

- 工程：`E:/Unreal Projects/PCG_AI_demo/testfordemo.uproject`
- 已核对引擎：UE 5.8.4，CL 58902052。目标为 `testfordemoEditor Win64 Development`。
- 用户重新拉取后的基线：`1d5d469f1f34507c42749a38fdd7ff44da88263c`。候选分支：`codex/castle-melee-v1`。本地 `main` 和已合入的楼层守卫数值、武器、奖励与宝库楔形布防仍在该基线。
- EXECUTE 范围来自用户审批及 [专项计划](<C:/Users/12854/Desktop/CASTLE_MELEE_V1_MULTI_AGENT_PLAN.md>)。
- 历史基础版：UE 5.8.4 构建通过。官方 MCP 原生测试 6/6 通过。独立图已冷加载，动作接入后取得正常渲染 PIE 证据。详见历史分项。
- R1 构建：两次实际构建通过。含补充用例的最终 DLL 为 `215C9301…`，已在本轮渲染 Editor 进程中确认加载。
- 已通过：最终 DLL `215C9301…` 的既有原生 6/6、渲染 PIE 定向 10/11、FIX05 静止目标恢复与死亡代理清理。本轮新增同一守卫连续两次被实弹打断后第三次起手的运行证据，以及有碰撞的横向、高差位置注入证据。
- 夹具条件未满足：`HitchOutside` 的两份原 FAIL 保留。均有自然同帧挥空及零伤害，尚缺人工停顿触发。
- 尚未验收：实体墙尝试未见墙体成为首个阻挡物，实际 A/D 横移与自然高差操作仍待试玩。四轮 ABBA 已采集，玩家生死、预热与页读入不匹配，性能结论限于诊断。
- 等待人工手感验收：唯一入口 `L_CastleMelee_V1_Dev`，Seed 24680。任务内存只记录警告，原释放 DLL 与 2 GiB 停止记录均为历史。
- 最新证据、性能统计和交接方式见「R1 收尾」。
- 启动前、就绪后或采样中的资源状态以当次测量为准，见 `Saved/CastleMeleeV1/prebuild-environment.jsonl`。

## 模块与责任

| 负责人 | 文件 | 接口与行为 |
|---|---|---|
| O，主协调 | `Source/CastleDemo/CastleDemo.h`、`CastleGameplay.cpp` | 新类默认关闭的 `bUseMeleeV1`，唯一注册组件，近战分流，追击与停步、受击和死亡取消，Director 的类与比例字段。原持枪兵、精英和 Boss 保留既有路径。 |
| A，近战开发 | `Source/CastleDemo/Melee/CastleMeleeCombatComponent.h`、`.cpp` | 实例标记、Windup/Active/Recovery、Montage Notify Window、真实扫掠、单次伤害及取消。 |
| B，出兵开发 | `Source/CastleDemo/CastleGeneration.cpp`、`CastleRegions.cpp`、`CastleOrdinaryEnemyMix.h`、`.cpp` | 探索与宝库各接入原普通兵入口。用版本 1 独立槽位抽样。探索两次 XY 抽样和宝库原楔形坐标按原顺序计算。 |
| T，测试开发 | `Source/CastleDemo/Tests/CastleMeleeV1Tests.cpp` | 独立原生自动化与只在验收图 Standalone PIE 生效的固定编组夹具。正式回归用例及其断言沿用既有版本。 |
| R1、R2，独立审核 | 只读 | R1 审查实际源码和引擎接口。R2 核对测试对需求的覆盖、夹具和待实跑项。 |

近战只在与动画实例匹配的 `CastleMeleeHitWindow` 内检测。要求当前姿势对应网格骨架与 `DefaultSlot` 播放，Sweep 先阻挡于当前玩家胶囊才进入原伤害入口。同一攻击最多提交一次伤害。原持枪兵的 `AttackInterval`、FIX05 路径预算和伤害分支沿用最新基线。近战兵实际损失生命时取消当前挥击，再交还原有受击或死亡表现。启动歧义拒绝时由近战重新追击。默认比例为 0，兼容已有地图。

出兵配置中正比例但空类使用旧普通兵并给出配置提示，具体非法类直接拒绝。合法类实际出生失败记录完整槽位，既不重新抽签也不改坐标。两条普通兵入口继续共用原 `AddEnemy` 的实际 Spawn、Configure 和 TrackGenerated。

## 审核与待运行证据

- CAM-01：起手同步回调可改变实际播放实例。已加入起手事件和同资源活动实例检查，再核对唯一新增且活动的实例。取消仅可停止已确认归属的播放。额外拒绝 UE 的旧根运动标记，避免停止其他播放后触发同步回调。需要对实际 Montage 运行时回调和中断补证。
- CAM-02：缺实际骨架和活动 Slot 检查可能造成看不到挥击却结算伤害。现有源码验证当前网格骨架、专用 Slot，命中前核对本次播放实例与实际 Slot 权重。同槽有其他活动或淡出动作时暂缓起手。需在实际动画 Blueprint 中验证可见动作和命中节奏。
- 原生测试已完成，新套件 4/4 与旧回归 2/2。新套件包括 `CastleDemo.MeleeV1.Selection`、`DirectorRoutes`、`SpawnFailure`、`FailClosed`。覆盖真实生成入口、固定种子下房间与出生坐标和奖励、无动作的拒绝及旧即时伤害对照。临时世界不代表实际可玩 PIE。旧回归包括 `CastleDemo.EnemyPresentation.SpawnAndDamage` 与 `SpawnFailure`。原始结果见 `Saved/CastleMeleeV1/automation-progress.json`。FIX05 静止目标恢复另需专项验证。
- 上一阶段的实际 PIE 待测清单已由文末的运行记录更新。

## 构建与接续

1. 复核工作树、源码 SHA、当前物理内存和任务进程。只管理本任务创建的 Editor。构建脚本为 `Saved/CastleMeleeV1/build.ps1`，启动脚本为 `Saved/CastleMeleeV1/launch-editor.ps1`。构建串行执行，保留 UBT 原文和退出码。已有磁盘 DLL 只是施工前备份，不能用来验证当前源码。
2. 构建完成后核对实际加载 DLL 的路径、SHA 和 Engine Build Id，再对同实例做官方 MCP initialize、工具发现、实际只读查询、地图／PIE／Dirty 核对。读回并运行新、旧自动化。连接或工具缺口按实际状态记录。
3. 主协调在获批新目录制作 `BP_CastleEnemy_Melee`，优先复用真实合格动作。若需补一条，复用现有 Retargeter 导出原地序列，只在新 Montage 配置完整 `DefaultSlot` 和命名窗口 `CastleMeleeHitWindow`。不动旧骨架、动画、网格、物理资产和蓝图。若官方工具缺这一步，提供精确人工补口并保留美术／运行待验收状态。
4. 唯一计划验收图为 `/Game/Art/Grunt01/MeleeV1/Tests/L_CastleMelee_V1_Dev`，从既有集成测试图复制。等实际地图保存和冷加载通过后验证正常生成。固定 6 持枪与固定 4 持枪加 2 近战的专项对照与真实概率出兵分开记录。夹具入口为 `CastleMeleeV1.Cohort Ranged6|Mixed42|Single|Pair|Stop`，需要有效的新近战蓝图和当前房间导航。各组使用新 PIE，匹配 Seed、房间、站位、路线和渲染条件。满足技术验收后留下可玩入口，等待用户手感验收。

## 保护与恢复

- `Saved/CastleMeleeV1/baseline.json` 包含基线及逐文件 SHA，`Baseline/` 保留四个既有源码快照及旧构建产物。旧 DLL 不与新的 Team 源码混用。
- `protected-check.json` 复核 101 项保护文件，初次检查未发现漂移。原正式地图、旧测试地图、旧 Grunt 资产、Boss/AI 受保护文件、Config 和项目清单均在范围内。
- 恢复前检查用户在此之后的修改，只逐文件撤回本轮差异。新增包转移前检查引用。没有提交、推送、全仓恢复或清缓存。


## 续接构建记录

- 已确认候选文件 SHA 与内存停止检查点一致。续接前可用物理内存约 9.555 GiB，未见占用 DLL 的 Editor。
- 首轮 UBT 生成反射文件后在新增测试的 UPackage 前置声明上报告 C2664、C2665、C2027，退出 1。初次失败原始记录见 `Saved/CastleMeleeV1/build-20261125-204915-434-ubt.log`。
- 测试负责人只补 `UObject/Package.h`，原断言保持。增量构建退出 0，`Result: Succeeded`，执行 73.70 秒。日志为 `Saved/CastleMeleeV1/build-20261125-205132-319-ubt.log`。磁盘 DLL SHA-256 为 `BAB8A9498509AAFA03B30ED48531E6AEF2F310A23A74CCD125BEEB53B5C5E7E0`。

## 续接 MCP、自动化与地图证据

- 正常渲染的任务 Editor PID 5288 在首批 PCD3D_SM6 着色器编译阶段等待约 9 分 52 秒，主窗口与 MCP listener 均未出现。核实任务进程归属且尚未进入地图后，先尝试关闭主窗口，因无窗口返回 false，随后仅结束该启动实例。日志与状态见 `Saved/CastleMeleeV1/rendered-startup-stop.json`。这是启动阶段限制，未采集渲染、视觉或帧率证据。
- 用同一构建启动 `-NullRHI` 的任务 Editor PID 9404。监听归属为 127.0.0.1:8000，握手、工具集发现、当前地图及 PIE 状态实读成功。核对实际加载模块 SHA 与构建一致。证据见 `mcp-gate.json`、`loaded-module.json`、`mcp-traffic.jsonl`。
- 新测试 **4/4 Success**。Selection、DirectorRoutes、SpawnFailure、FailClosed 均已实际执行。FailClosed 如预期记录缺注册组件的告警。其余新测试无警告。
- 旧普通兵回归 **2/2 Success**。SpawnAndDamage保留临时世界 CleanupWorld 警告，SpawnFailure 无警告。结果见 `automation-progress.json`。FIX05 专用追击 PIE、实际近战挥击与真机性能仍待验证。
- 官方 MCP 创建并编译 `/Game/Art/Grunt01/MeleeV1/BP_CastleEnemy_Melee`。其父类是已有 Ragdoll BP，已添加唯一 `CastleMeleeCombat` 组件。保存并冷读 `bUseMeleeV1=false`、`StrikeMontage=None`。证据见 `asset-setup-progress.json`。
- 官方 MCP 复制、保存并冷加载 `/Game/Art/Grunt01/MeleeV1/Tests/L_CastleMelee_V1_Dev`。Director 保留 Ragdoll 普通兵类，预接新近战类，近战比例仍为 **0**。证据见 `director-staging.json`。
- 第二个独立 `-NullRHI` Editor PID 24980 从磁盘打开新图并实际加载同 SHA DLL。Seed 24680、Floor 1 的真实 PIE 世界达到 bReady，存在 1 名玩家、15 名原持枪 Ragdoll 普通兵、0 名近战。停止 PIE 后新旧地图 Dirty 均为 false。证据见 `staged-map-smoke.json`。这验证保存配置及关闭模式的实际出兵。新兵对抗与手感待动画后验证。
- 两次无渲染任务 Editor 均使用官方 Slate 工具关闭，并在各自日志确认 LogExit。未保留活动 PIE 或任务 Editor。

## 历史动画准备与放行边界

实时 MCP 从 Grunt 目录只读找到 Attack、Hit、Death Montage。现有 Attack 为枪械射击。Manny 的 `MM_Attack_01/02/03` 和 `MM_ChargedAttack` 已发现，适合在现有 `/Game/RTG_NewRetargeter` 中预览重定向。已检查的官方资产、动画工具 schema 未发现直接导出 IK Retarget 结果并创建带 Montage Notify Window 的合格 Montage 的操作。证据见 `asset-preflight.json`、`asset-tool-schemas.json`。

上一阶段的人工补口记录。

1. 在现有 `RTG_NewRetargeter` 中预览一条挥击，确认 Grunt 骨架、持枪姿势和脚底。将合格原地序列导出至 `/Game/Art/Grunt01/MeleeV1/Animation/AS_Grunt_Melee_Strike`。
2. 用该序列新建同目录 `AM_Grunt_Melee_Strike`。确认骨架为 `SK_CastleGrunt_Corrected_Skeleton`、无根运动、一个 `DefaultSlot`。在实际挥击接触区间加入一个内置 Montage Notify Window，命名 `CastleMeleeHitWindow`，两侧保留前摇和收招。只保存这两个新包。
3. 资产确认后由主协调接入 `CastleMeleeCombat.StrikeMontage`、开启该子蓝图的 `bUseMeleeV1`，仅在新地图设置混合比例。随后执行正向攻击、取消、探索及宝库混编、追击、固定编组和渲染验收，等待用户手感验收。保持原审批范围。

构建及原生回归通过，独立图冷加载和零近战配置 PIE 通过。当时处于 `BLOCKED_MELEE_ANIMATION`，已由下节更新。 101 项受保护文件复核零漂移，仅新增两个白名单资产包。SHA 与实际变更见 `protected-after-stage.json`。候选分支没有自动提交或推送。

## 动作接入与渲染验证（2026-11-25）

- 绑定了实际保存的 `/Game/Art/Grunt01/MeleeV1/Animation/AS_Grunt_Melee_Strike_Montage`。编辑器历史显示最先创建的 `AM_Grunt_Melee_Strike` 已被替换，本轮保留当前资源名。
- Sequence 和 Montage 使用 `SK_CastleGrunt_Corrected_Skeleton`。`DefaultSlot` 以 1× 播放完整 0–1.833333 秒、56 采样键的非 Additive 动作，现有 `ABP_CastleGrunt` 使用同一 Slot。两项旧 Montage 根运动标志未设置。新增 Sequence 原先开启根运动，会被近战组件拒播。已关闭提取并启用 RefPose 根锁。
- `CastleMeleeHitWindow` 是原生 `AnimNotify_PlayMontageNotifyWindow`。官方读取核对了类型与名称，保存包内区间为 1.132997–1.229083 秒，约 96 ms，经两名审核者独立核对。
- 已编译并保存 `BP_CastleEnemy_Melee`，绑定 `CastleMeleeCombat.StrikeMontage`，启用 `bUseMeleeV1`。只有 `L_CastleMelee_V1_Dev` 保存近战比例 1/3。运行模块 SHA 与已有 UBT 成功构建一致，本轮未改 C++。101 项受保护文件与团队基线一致。读回见 `Saved/CastleMeleeV1/melee-asset-integration.json`，备份目录为 `Saved/CastleMeleeV1/ContinuationBefore_20261125_214325_602`。

### 正常渲染 PIE

| 用例 | 运行结果 |
|---|---|
| 单兵站定及连续攻击 | D3D SM6。连续五次各有一次 Start→Active→Hit→Recovery→Finish。首击起手后约 1.133 秒结算。前摇中护盾保持 60，两次有效结算后变为 50、40，生命仍为 100。见 `rendered-Single-Observe.json`。 |
| 真实探索及宝库生成 | 种子 24680。探索 Region 0 生成 11 远程＋4 近战。以测试定位抵达首层出口，再用真实 F 升二层。宝库 Region 2 的四间房生成 4 远程＋1 近战。二层宝库门提示出现，玩家在交互完成前被守卫击倒。 |
| 固定编组 | `Ranged6` 和 `Mixed42` 各用新 PIE。前台采样为同种子、Room 0、站位 (-350,-350,90.15)、相同的六个槽位和环境敌人计数 16。两组静止采样，玩家均在采样期间存活。 |
| 画面 | `rendered-Single-Sustain-Foreground-fire.png` 可见近战守卫贴近并举武器。此处文件名为测试意图，截图不代表开火已生效。伤害时序由实际窗口回调与护盾变化确认。 |
| 未通过的边界验证 | 两次持续射击自动输入均未消耗弹药，目标生命保持 85，无法判断持续命中的打断节奏。移动挥空、实体遮挡、近战受击和死亡取消、死亡代理与 FIX05 静止目标恢复均待有效用例。 |
| 低帧风险 | 后台 3 FPS 曾实际出现同帧 Active→Recovery 且无 Hit。长帧可能跨过约 96 ms 的窗口，导致角色 Tick 没有在窗口内执行 Sweep。这个现象与主动侧撤导致的挥空分开判定。 |

### 前台短帧时间对照

| 指标 | 6 远程 P50 / P95 | 4 远程＋2 近战 P50 / P95 |
|---|---:|---:|
| Frame（ms） | 24.91 / 96.46 | 38.29 / 128.83 |
| Game（ms） | 24.26 / 31.81 | 37.25 / 51.76 |
| GPU（ms） | 12.59 / 114.27 | 12.54 / 115.46 |
| 动画（Game，ms） | 1.00 / 1.39 | 1.41 / 2.14 |
| 物理（Game，ms） | 0.11 / 0.14 | 0.13 / 0.19 |
| CSV 样本 | 151 帧 / 5.47 秒 | 105 帧 / 5.52 秒 |

P95 用线性插值。原始 CSV 为 `Saved/Profiling/CSV/Profile(20261125_220207).csv` 与 `Profile(20261125_220218).csv`。日志节选和统计在 `Saved/CastleMeleeV1/rendered-continuation-evidence.log` 与 `rendered-frame-comparison.json`。这是同站位的短对照，首轮后台样本已排除。独立技术审核复查了动画、模块、读回与保护范围。独立功能审核复查了生成、攻击时序、截图、CSV 及未验收项。

### 人工试玩

1. 打开 `/Game/Art/Grunt01/MeleeV1/Tests/L_CastleMelee_V1_Dev`，使用 Play 或 New Editor Window (PIE)，开始菜单输入 `24680`。
2. 要隔离单兵，在入口房靠近中心且没有可交战的环境敌人时，从游戏控制台执行 `CastleMeleeV1.Cohort Single`。如使用 `BugItGo -350 -350 90.15 0 14 0` 做夹具定位，紧接执行 `Walk` 并确认已正常落地、恢复碰撞后再测。默认出生点距中心约 735 cm，夹具要求 600 cm 内。切换 `Pair`、`Ranged6` 或 `Mixed42` 前重开 PIE。进入对抗前关闭控制台，恢复游戏焦点。
3. 先站定等一次挥击，确认护盾只扣一次。再用 A/D 侧移、S 后退和门墙遮挡。卡宾枪用左键分别点射和持续命中，观察掉血时的打断、起手与收招。击杀后观察死亡代理、清理和通行。
4. 沿探索路线按 F 通过首层出口，再到二层宝箱房找到 TREASURE CAVE 门，清掉附近守卫后按 F。`Log LogCastleMelee Verbose` 可对照攻击 ID。`CASTLE_ORDINARY_MELEE_SPAWN` 日志中的 Region 0／2 对应探索与宝库。


## 定向修复 R1（2026-11-25）

### 长帧执行顺序与修复

- 原渲染日志按敌人实例、攻击 ID、Montage 实例 ID 配对。严格匹配到 14 次同日志时刻、同已记录帧开关窗并正常完成的攻击，14 次均无 `Hit`。例如敌人 `BP_CastleEnemy_Melee_C_7`，攻击 15、实例 202、帧 217。旧日志缺少单次 Sweep、目标位置、阻挡与取消细节，无法事后判断这些挥击若执行检测是否会命中。原日志和短测 CSV 保留。
- UE 5.8.4 的 Montage Advance 会在同一次更新中同步派发 Branching Point Begin 和 End。旧代码只有组件处于 Active 的 Tick 才会采样，因此一次动画更新完整跨窗时，两次回调之间可以没有有效组件 Tick。这个源码缺口独立于个别攻击的真实位置与遮挡。
- 近战开发 A 将首个检测机会放到已通过身份校验的真实 Begin 回调。匹配原生 Notify、当前攻击、播放实例、播放位置和活动上下文。Begin 使用当前 Montage 实例权重及实际 Slot 节点权重，避免读取上一轮动画图里的 Montage 混合权重。Active 内仍可按现有 Tick 继续采样。End 或 Recovery 无补伤害。
- 保留真实攻击体积、首个物理阻挡组件及目标合法性。成功伤害前先设置本次命中标记。死亡、失控、受击中断、旧播放和错误位置继续受原校验约束。新增窗口采样、范围外拒绝、实际 Sweep、遮挡、挥空、命中和同帧开关窗的计数。独立技术审核复核了动画图更新顺序和终止回调路径，已将权重发现反馈到最终差异。该审核为源码证据。
- 历史配对与本轮保护文件记录在 `Saved/CastleMeleeV1/TargetedR1/historical-window-evidence.json`、`baseline.json` 和 `protection-r1.json`。

### 短测诊断与复测口径

独立只读分析 B 复算原始 A/B。6 远程为 151 帧／5.467 秒，4 远程＋2 近战为 105 帧／5.524 秒。Frame P50 为 24.906／38.294 ms，Game P50 为 24.255／37.254 ms。两组还有 16 名环境敌人。结束时玩家生命／护盾分别为 50／0、100／20，弹体 Tick 和总 Actor 范围不同。原样本包含运行中的 MCP 查询。部分渲染线程采样接近零，从中无法归因 Draw 差异。UI 统计项是较大的差异项，现有证据不足以定位到 HUD 或其他具体函数。

新 A–B–B–A 每组使用独立 PIE，并在导航及生成完成后采 20–30 秒。核对实际 D3D12、输出、画质、窗口前台、同种子同槽位、真实相机与路径、玩家始终存活，以及编组和环境活动。用计数或定向 Trace 区分寻路请求、攻击、弹体、死亡代理，和近战、AI、动画、日志及采样本身的成本。原短测只作为待复核信号，本轮没有基于该短测修改性能路径。


### 专项夹具与独立审核

| 用例 | 夹具判据与当前状态 |
|---|---|
| 正常站定及连续攻击 | 原构建已有运行证据。本轮须对新 DLL 按完整攻击 ID 检查一次伤害、完整窗口及收招。 |
| 前台长帧 | `CastleMeleeV1.Input Hitch` 使用实际 Montage 播放位置安排一次约 220 ms 的测试停顿。只在实际观察到同攻击同帧开关窗、一次 Sweep、一次命中与一次扣血时通过。该诱发停顿仅供功能测试。待运行。 |
| 射击与前摇取消 | `Cancel` 和 `Death` 经本地 PlayerController 正常输入路径发射，检查弹药、真实弹体、伤害和原攻击窗截止后的状态。测试记录弹体发出时的阶段，结论需与实际命中时刻区分。待运行。 |
| 后撤与遮挡 | `Outside` 使用 S 的普通输入并核对实际位移、范围拒绝或挥空。`Blocked` 使用独立 PIE 中的临时实体 Box，核对原 Visibility 查询及 Sweep 阻挡。需同时核对日志阻挡组件确为该 Box。真实地图门墙、高差、横移仍需实测。 |
| 连续命中与死亡 | 持续开火造成的打断节奏、有效段内死亡、新近战死亡代理／布娃娃生命周期、旧播放通知在下一击期间迟到，均待验证。 |
| FIX05 | 静止目标与路径重新开放恢复仍待使用对应验收图实测。 |
| 真实混编 | 原构建的探索与宝库生成记录保留。R1 未改出兵源码，未在新 DLL 重跑。 |

T 只改 `CastleMeleeV1Tests.cpp`。功能审核独立复查了实弹证明、原窗口截止、临时碰撞、原渲染数据与样本限制。审核指出的取消观察提前结束风险已补入夹具，仍待编译与运行。

### 历史：当时的构建及 Editor 占用检查（已解除）

- 本轮 Epic 官方 MCP 完成握手、工具发现及真实只读查询。确认工程为 `PCG_AI_demo/testfordemo.uproject`、UE 5.8.4／CL58902052，当前验收图正确，PIE=false。验收图、近战蓝图和现有 Montage 的 Dirty 查询均为 false。查询结果见 `Saved/CastleMeleeV1/TargetedR1/prebuild-editor-state.jsonl`。
- 当时已打开的 Editor PID 22580 正加载 `UnrealEditor-CastleDemo.dll`，未能确认该会话归本轮所有。保留该会话并等待正常关闭，未开第二个 Editor。旧 DLL SHA 为 `BAB8A9498509AAFA03B30ED48531E6AEF2F310A23A74CCD125BEEB53B5C5E7E0`，本轮未启动构建、新原生回归、新 PIE 或性能 ABBA。旧证据保持历史归属。
- 三份源码增量为 `Source/CastleDemo/Melee/CastleMeleeCombatComponent.h`、`Source/CastleDemo/Melee/CastleMeleeCombatComponent.cpp` 和 `Source/CastleDemo/Tests/CastleMeleeV1Tests.cpp`。共享实现、验收图、蓝图、现有动画和团队资产均未写入。保护检查涵盖 110 项，未发现 SHA 漂移。完整前后 SHA 及当前提交见 `Saved/CastleMeleeV1/TargetedR1/protection-r1.json`。未提交或推送。
- 正常关闭现有会话后，先构建 `testfordemoEditor Win64 Development` 并核对新 DLL，再在同一验收图使用种子 24680。进房后按上文位置启动 `CastleMeleeV1.Cohort Single`。每次独立 PIE 运行一个 `CastleMeleeV1.Input Cancel|Death|Hitch|Outside|Blocked` 用例，另验正常帧和剩余边界。功能门禁通过后再做前台固定编组 ABBA。
- 逐文件恢复使用 `Saved/CastleMeleeV1/TargetedR1/Baseline/` 中对应文件。先核对现态 SHA 和后续编辑，按需取回本轮前差异，再串行构建。现地图和动画无需恢复。当前状态为源码定向修复及独立复核完成，集成验证等待 Editor 释放 DLL。完整运行、性能与手感均未放行。


### 历史：R1 续接构建与官方 MCP（2026-11-26）

| 阶段 | 本轮证据 |
|---|---|
| 构建前 | 重新查询实时 Unreal 进程，无活动 Editor。使用只读写权限验证打开现 DLL 的独占句柄并立即关闭，未写 DLL。两份近战组件源码和既有测试源码均与上轮交接 SHA 一致。 |
| 原生构建 | UE 5.8.4／CL58902052，`testfordemoEditor Win64 Development`，UBT 返回 0，耗时 184.22 秒。真实日志 `Saved/CastleMeleeV1/build-20261126-000653-280-ubt.log`。已记录既有 `CastleLanguage.cpp` 的 Slate 字体弃用警告。 |
| 实际加载 | 任务自有渲染 Editor 加载 `Binaries/Win64/UnrealEditor-CastleDemo.dll`，SHA256 `682FD63A15E1EA370F1EE483B3E79FA50C08DACDED775DB7F5CE8CC0A44BDC56`，与本轮 UBT 输出一致。D3D12 初始化记录及本地 MCP 监听归属均已核对。 |
| 官方 MCP | 初始化、工具发现、真实地图、PIE 状态及 Dirty 读取通过。正确验收图，PIE=false，验收图、近战蓝图和当前 Montage 均无未保存状态。见 `TargetedR1/resume-editor-module.json` 与 `prebuild-editor-state.jsonl`。 |
| 资源门禁 | 启动前约 5.91 GiB 可用。渲染 Editor 开图后实读约 1.87–1.96 GiB，低于 2 GiB。没有启动原生回归、PIE 或采样。按官方窗口操作正常退出本轮 Editor，日志记录 LogExit，恢复约 5.94 GiB。见 `TargetedR1/resume-memory.jsonl` 与 `editor-20261126-001018-347.log`。 |


### 历史：R1 补充用例构建及原资源门槛

- 仅 T 的 `CastleMeleeV1Tests.cpp` 新增专项用例。原 SHA `4174816C704864F0BB3CF38B84534B893F6F4A96DFA97EA4CDC5E75678D4AB6C`，新 SHA `9735C33381AE868BCF037373F143E46D9E3E70204B4FB84060CEC0187BD55147`。第一轮已编译的测试文件在 `Saved/CastleMeleeV1/TargetedR1/Baseline/CastleMeleeV1Tests.BeforeSupplement.cpp` 可恢复。两份生产近战组件未在本次续接中再改。
- 最终增量构建 UBT 返回 0，耗时 67.72 秒。日志 `Saved/CastleMeleeV1/build-20261126-001508-904-ubt.log`。最终 DLL SHA256 为 `215C9301C0AB1F679F8F7AD80D35A7A7967968BD64B6EDFA2E1E7A17CCC4C165`。监测确认第二个任务自有渲染 Editor 实际加载该 DLL。官方 MCP 再次读回正确验收图、PIE=false，三个目标包 Dirty=false。
- 第二次 Editor 启动前可用约 6.16 GiB。正确模块和监听确认时为 2.24 GiB。工具发现完毕后，在首项 `CastleDemo.MeleeV1.Selection` 前实读 1.890 GiB，未提交任何 `RunTests` 请求。未启动独立 PIE、FIX05 或 ABBA。通过官方窗口操作正常退出，日志有 LogExit，退出后约 5.99 GiB 可用。见 `Saved/CastleMeleeV1/TargetedR1/supplement-editor-module.json`、`measured-memory-gates.jsonl`、`supplement-editor-exit.json`。资源不足期间未改变画质、后台限帧或其他进程。

| 补充用例 | 已完成的源码范围与仍需取得的证据 |
|---|---|
| `HitchBlocked`、`HitchOutside` | 复用 Montage 定位的单次停顿，核对同一攻击和范围／遮挡分类。后撤在停顿前已校验真实位置与松键。实际运行须关联该攻击的 `Source=NotifyBegin`、同帧窗口和零伤害。 |
| `Cancel`、`Death` | 继续由本地玩家输入路径发出实际弹体。新增检查旧攻击窗口在原截止前一直未开启。弹体出生时的前摇与后续目标扣血采样时刻分开记录。受控长帧下已取消旧击的负例仍需实测。 |
| `ActiveDeath` | 只在原攻击 ID 的实际 Active 和窗内播放位置调用原生伤害入口。该用例会测试一次击杀、敌人和控制器清理、真实 Ragdoll 物理状态与剩余寿命。测试注入与实弹用例分别归属。挡板保留，撤去遮挡后的旧回调和代理自然到期仍需补证。 |
| `Sustain`、`Recovery` | 持续按键验证两颗真实弹体、弹药和目标合计伤害，松键后无第三发。独立收招用例检查原 ID 自然完成，再取得新 ID。FIX05 寻路恢复、真实地图墙体、高差与横移仍需独立验证。 |

两名独立审核者复核了专项夹具、证据边界和加载记录。以上新增用例均已编译，均未取得本轮 PIE 结果。原短测和历史 6/6 保留。

### 历史：原 2 GiB 门槛下的续接记录（已失效）

加载内存稳定达到门槛后，优先复用当前最终 DLL，核对 SHA 后开启同一验收图 `/Game/Art/Grunt01/MeleeV1/Tests/L_CastleMelee_V1_Dev`。真实渲染 Standalone PIE 使用 Seed 24680。每个专项重开 PIE，先等生成与导航，再按前文站位启动 `CastleMeleeV1.Cohort Single`。优先依次运行正常攻击、`Input Hitch`、`Input Cancel`、`Input Death`、`Input Outside`、`Input Blocked`，随后是补充用例。独立 FIX05 使用原测试图与 `CastlePursuitFix05.Run Art Reopen 1`，以静止玩家、导航更新、完整路径和守卫步行日志放行。功能通过后再运行四个前台性能会话。原验收地图仍是唯一交付地图，当前不标记可交最终手感验收。


### 历史：R1 运行验收续接（2026-11-26 02:08，UTC+8）

用户撤销固定 2 GiB 停止门槛。任务专用启动、构建与验收脚本已改为仅记录并警告。无替代数值门槛。核对三份源码和最终 DLL 后直接执行，没有再次构建。唯一渲染 Editor 加载 SHA256 `215C9301C0AB1F679F8F7AD80D35A7A7967968BD64B6EDFA2E1E7A17CCC4C165`。Epic MCP 握手、工具发现、工程地图、实际加载模块及 PIE 状态均核对通过。未见分配失败、OOM 或进程崩溃。正式地图、模板和配置未写入。

本 DLL 的原生测试 **6/6**。四项 MeleeV1 和两项 EnemyPresentation 的官方工具结果、逐项 Unreal `Test Completed` 日志一致。负例 `FailClosed` 记录预期的缺组件警告。原 `SpawnAndDamage` 临时测试世界仍有 EndPlay 清理警告。证据为 `r1-live-module.json`、`automation-progress.json`、`editor-20261126-014831-243.log`。

起初独立用例受控制台焦点与站位时间影响，未进入指定交战。已核实房间附近的真实环境敌人，并缩短就绪到放兵的时间，保留夹具原有环境敌人门禁。进一步确认 UE `BugItGo` 将玩家留在 Ghost，使最初的命中负例失去物理前提。续测均先 `Walk`，实读 `MOVE_Walking` 与该 PIE 玩家 `bActorEnableCollision=True`。保留早期拒绝及 Ghost 日志，仅以下正常行走用例计入运行验收。

| 用例 | 运行结果 |
|---|---|
| Standing | 通过。真实前台 D3D SM6，前两次攻击为同一近战兵的不同 Attack 与 Montage 实例。实际扫中玩家 `CollisionCylinder` 并各扣 10 护盾，60→40。两次均自然收招完成。 |
| Hitch | 通过。人工停顿 219.913 ms，引擎实际 Game Delta 235.770 ms。同帧开关窗中一次真实 Sweep 命中胶囊，护盾 60→50，无 Tick 补伤。 |
| Cancel / Death | 通过。实弹一发使目标 85→63 并取消原前摇，旧窗截止前无补伤。实弹死亡以四发命中耗弹 24→20，致死一发在原前摇发出，敌人清理且金币 35→47。弹体出生和后续伤害由不同日志事件记录。当前证据不提供精确物理命中时刻，未单独诱发已取消攻击的长帧场景。 |
| Outside / Blocked | 通过。S 键正常后撤 72.6 cm，三次真实挥空，零伤害。临时实体碰撞盒两次实际阻挡，零伤害。真实地图静态门墙、高差及横移仍待实测。 |
| HitchBlocked | 通过。人工停顿 219.828 ms，同攻击同帧一次 `NotifyBegin` Sweep 命中夹具盒，零伤害。 |
| HitchOutside | 两次未满足夹具的受控停顿条件。实测后撤 288.1 cm／197.4 cm，各一次自然同帧 `NotifyBegin` 挥空，均零伤害；两次均无 `HITCH_STALL` 事件。保留 FAIL，不把自然跨窗作为人工停顿通过。停止重复运行。 |
| ActiveDeath | 通过。原攻击真实处于有效段时由夹具调用原生伤害入口，非实弹用例。敌人和原控制器销毁，恰好一个 Ragdoll 代理，物理启用且 Pawn 忽略。另一次在同 Seed 同楼层仍 Ready 的 PIE 中观察该代理从存在到消失，首次采到消失约在开始观测后 12.03 墙钟秒。保持遮挡仍在的前提。 |
| Sustain / Recovery | 通过。一段持续按键产生两颗实际弹体、耗弹 24→22，合计伤害 44，松键无第三发。独立收招用例撤去阻挡后，旧攻击自然结束并生成新 Attack ID。尚未把持续开火用例解释为已覆盖连续打断后全部恢复时序。 |

合计 **10/11**，11 组均记录到真实前台 D3D SM6。证据在 `Saved/CastleMeleeV1/TargetedR1/` 的 `r1-walking-directed-sequence.jsonl` 和各用例时间戳 JSON、原始日志、前台采样。独立审核复核原生结果、站定同一敌人与两次唯一攻击、真实弹体与伤害、受控时序、死亡和追击证据。

FIX05 通过本轮规定的静止目标路径重新开放恢复用例。仅在原开发测试图执行 `CastlePursuitFix05.Run Art Reopen 1`。两次可核对的部分路径请求先完成，玩家在封闭、开启、恢复期间位移均为 0。移除阻挡后收到 `NavUpdated`，完整非部分路径 Request 88 被接受，守卫从静止恢复至 270 cm/s，实际改变位置并沿恢复路线行走。恢复射击后路径按原停步逻辑停止。原生类未改变。证据在 `fix05-reopen-raw.log`。原历史测试的部分请求数归原记录。

ABBA **未执行**。受控后撤条件未满足以及真实地图边界仍待验证，并非可用内存数值造成停止。记录的运行及交接单点分别约 2.44、2.58 GiB 可用，提交约 23.43／23.35 GiB，提交限额 28.80 GiB。两个样本的页读写速率均为 0，不能据此断定整个运行没有分页。旧短测继续保留原归属，没有新增近战性能归因。

#### 当前人工试玩入口

同一候选 DLL、同一 `L_CastleMelee_V1_Dev` 已留下干净的独立预览 PIE，Seed 24680，Floor 1，正常行走。开局自然生成 11 名持枪普通兵、4 名近战普通兵，另读到 1 个原生 CastleEnemy。未注入固定编组、人工停顿或传送。实际窗口与地图读回见 `r1-manual-handoff.json`、`r1-handoff-foreground.jsonl`。可直接进入预览窗口，用 WASD 移动、鼠标观察及左键开火。观察近战起手后侧移或后退能否躲开，比较点射打断、松键和收招，再操作真实门墙、高差与窄门。若窗口已关闭，重新打开原开发图、Play、输入 Seed `24680` 后开始游戏。完整运行与手感验收仍待补齐。

为避免等待期间自然敌人击倒玩家，最终交接使用新开的同 Seed 正常 PIE，并在就绪后用原生暂停功能保留起始状态。已读回当前 PIE `PauserPlayerState` 与玩家 100 HP／60 护盾。聚焦预览窗口后按 Esc 恢复游戏，若看到死亡界面则按 K 重试同种子。交接验证见 `r1-manual-handoff-paused.json`。


## R1 收尾（2026-11-26，UTC+8）

最终 DLL SHA256 `215C9301C0AB1F679F8F7AD80D35A7A7967968BD64B6EDFA2E1E7A17CCC4C165` 与三份源码均与检查点一致，复用同一官方 MCP 确认的任务 Editor。无源码、资产或画质配置改动，无重新构建。原通过用例不重跑，保留此前全部原始证据。

### 剩余功能证据

| 项目 | 结论 |
|---|---|
| HitchOutside | 复核确认，夹具要求 S 松键与按键处理先完成，再在 Montage 距开窗 25–110 ms、距关窗不超过 200 ms 的采样时刻触发停顿。调整外部命令提交时刻无法可靠保证内部条件。本轮未重复两次历史 FAIL。两次历史记录均有真实后撤、自然同帧一次 NotifyBegin Miss 与零伤害，人工停顿未触发。 |
| 连续打断后恢复 | 同一近战兵实弹 85→63→41 HP，弹量 24→23→22。两次独立按下／释放和弹体、结算帧分别记录。Attack 1、2 均在原有效窗前取消，随后实际生成 Attack 3／Instance 612 的新前摇。该证据止于第三次前摇。 |
| 横向位置／高差 | 同一攻击前摇中使用测试定位并恢复原碰撞，未注入玩家移动键。横向位置从 (-350,-350,90.15) 到 (-375,-250,90.15)，读取正常 Walking，原攻击 Instance 599 同帧扫掠 Miss。高差场景读回 Z≈411、处于 Falling，原攻击 Instance 603 同帧报告 OutsideVolume。两组均在扫掠前实读 Actor 碰撞为 true，生命和护盾不变。不能替代真实 A/D 和 Space 操作。 |
| 实体墙 | 定位到地图墙侧后，原攻击 Instance 606 保持同一身份，恢复碰撞后真实扫掠为 Miss、无首个阻挡物，零伤害。尚未证实真实门框或墙体首个阻挡。已有临时实体碰撞盒结果独立保留。 |

证据位于 `Saved/CastleMeleeV1/TargetedR1/`。连续打断见 `repeat-cancel-20261125182541522.json` 及同名前缀原始日志。横向、高差和实体墙见 `geometry-lateral-*`、`geometry-height-*`、`geometry-wall-*`。独立审核检查了两次真实弹体、伤害变化和第三次起手；未据夹具条件不足改动近战逻辑。

### ABBA 四轮性能诊断

每轮新开真实 D3D12 SM6 PIE、Seed 24680、Floor 1、Room0，使用相同六个槽位，原环境兵 16。前后均记录到 22 个敌人 Actor 且组内身份一致。玩家从 100 HP / 60 护盾开始，采用同一个静止机位、相同朝向和 FOV95，保持正常碰撞。四次各得到 24/24 前台样本。每次仅采 CSV，未加人工停顿。Trace.Status 显示无活动连接，未改变 CPU channel。记录系统分辨率 1280×720、VSync 关闭，未独立确认内部渲染像素与 SP。画质未修改。

| 次序 | 编组 | CSV 时长／分析帧 | 分析 FPS | Frame P50／P95 ms | Game P50／P95 ms | GPU P50／P95 ms | 结束 HP／护盾 |
|---|---|---:|---:|---:|---:|---:|---:|
| A1 | 6 远程 | 24.756 s／856 | 38.96 | 23.67／39.03 | 17.76／23.55 | 14.02／33.26 | 50／60 |
| B1 | 4 远程＋2 近战 | 24.724 s／863 | 39.29 | 23.20／40.17 | 17.20／21.75 | 15.08／33.70 | 0／0 |
| B2 | 4 远程＋2 近战 | 24.716 s／872 | 39.68 | 22.95／37.48 | 16.92／21.74 | 15.10／34.19 | 0／0 |
| A2 | 6 远程 | 24.736 s／818 | 37.21 | 24.91／43.78 | 18.70／24.80 | 15.14／37.57 | 50／60 |

分析统一取从 CSV 启动累计 1.5–23.5 秒的完整帧，约 22 秒。Draw P50 约 0.0007 ms，无法用于渲染线程归因。A1 实际从生成到开始捕获约 6.37 秒，其余约 3.60–3.67 秒。两轮混编结束时玩家阵亡，未取得逐帧死亡标记或共同全存活区间。弹体计数有差异，B1、A2 记录到 GC。可用物理内存单点 1.67–2.10 GiB，提交 24.24–24.45／28.80 GiB。页读入单点最高 25,647 页/s，页写出采样为 0。记录可证明本轮在内存低于 2 GiB 时仍继续，没有分配失败或进程崩溃。页读入与生命周期差异使本轮不具备独立归因能力。没有复现可隔离的近战帧时惩罚，也不声称近战更快或性能通过。

原始四组副本、设置、前台与对应原始日志前缀 `abba-20261125181940136-`，可复核汇总 `abba-20261125181940136-analysis.json`。独立审核复算了 CSV 、前台、机位、资源、玩家状态和生命周期限制。当前只交付正常输入策略下的诊断对照。若继续要求近战开销因果对照，需要可核对的全存活同路线与相同预热，并记录环境兵活动及帧内死亡/状态边界。

### 人工交接

唯一入口仍为 `/Game/Art/Grunt01/MeleeV1/Tests/L_CastleMelee_V1_Dev`。交接用干净的 Seed 24680 自然生成，不添加固定编组或改变地图。聚焦预览窗口按 Esc 恢复。人手验证真实门洞、窄门、横移与高差，以及打断后重新起手的手感。交接时暂停状态单独记录在 `r1-closeout-handoff-paused.json`。


## 近战兵空手与移动表现（2026-11-26，香港时间）

本轮在用户确认的 `C:/Users/User/Desktop/PCG_AI_demo/testfordemo.uproject` 上执行，使用 UE 5.8.4 和 `codex/castle-melee-v1`。通过 Epic 官方 Unreal MCP 检查资产、实施修改并读取真实 DX12 PIE。用户要求结束流程后，停止了后续验证。

### 保存的资产

| 资产 | 改动 |
| --- | --- |
| `/Game/Art/Grunt01/MeleeV1/BP_CastleEnemy_Melee` | 将近战专属的继承组件 `RifleVisual` 网格清空，关闭可见与阴影，保留 `NoCollision`。`CharacterMesh0` 继续使用 Animation Blueprint 模式，Anim Class 绑定到 `ABP_CastleGrunt_Melee_C`。 |
| `/Game/Art/Grunt01/MeleeV1/Animation/ABP_CastleGrunt_Melee` | 继承 `/Game/Art/Grunt01/IntegrationV2/ABP_CastleGrunt`，通过 Asset Override 将 `AnimGraph` 的原 Blendspace Player 指向近战 BlendSpace，沿用速度更新和 `DefaultGroup.DefaultSlot`。 |
| `/Game/Art/Grunt01/MeleeV1/Animation/BS_CastleGrunt_Melee` | 0 cm/s 继续使用 `/Game/Characters/MF_Rifle_Idle_ADS`。135 和 270 cm/s 均使用 `/Game/Art/Grunt01/MeleeV1/Animation/AS_Grunt_Melee_Walk`，样本播放倍率均为 1.0。 |
| `/Game/Art/Grunt01/IntegrationV2/Ragdoll/BP_CastleEnemy_Grunt_Art_Ragdoll` | 原 `BP_OnDeathVisual` 硬编码生成同一个布娃娃代理并显示枪。局部调整该代理的 `SetVisibility`，以生前 `RifleVisual` 是否有有效的静态网格为条件。保留原死亡代理类、死亡动画和物理初始化流程。 |

`AS_Grunt_Melee_Walk` 的类型为 AnimSequence，与角色 Mesh 使用同一个 `/Game/Art/Grunt01/SK_CastleGrunt_Corrected_Skeleton`。原序列启用了根运动和 Force Root Lock，锁定方式为 `RefPose`。子 AnimBP 沿用父类的 `IgnoreRootMotion`，角色位移仍由 CharacterMovement 驱动。攻击继续使用 `/Game/Art/Grunt01/MeleeV1/Animation/AS_Grunt_Melee_Strike_Montage`。本轮复用该 Montage 及其既有 `CastleMeleeHitWindow`，并使用现有构建。

### 验证与交接

- 冷重开后，用 MCP 读回近战枪网格为空、近战 Mesh 的 Anim Class、走路 BlendSpace 覆盖和死亡代理可见性连线。测试使用 `/Game/Art/Grunt01/MeleeV1/Tests/L_CastleMelee_V1_Dev`，Seed 24680。
- 近战追击实测从 160.4 cm/s 达到 270 cm/s，进入攻击距离后速度回到 0。高速画面中近战兵双手为空、双臂自然摆动。停步后回到原来的持枪手势，手中无枪，也没有原地踏步。
- 现有 `Cancel` 夹具通过。一次实弹将近战兵血量从 85 降至 63 并打断前摇，受击及再次起手时枪网格仍为空。日志记录后续攻击进入有效段及完成收招。攻击后在玩家拉开距离时恢复追击的专项采样尚未执行。
- 现有 `ActiveDeath` 夹具通过。代理读取到 `AM_Grunt_Death`，随后进入 `PhysicsOnly` 且启用物理模拟。死亡动画和布娃娃阶段的代理枪均不可见、关闭隐藏阴影且为 `NoCollision`。观察到代理按生命周期清理。
- 两名原远程兵 `BP_CastleEnemy_Grunt_Art_Ragdoll_C_0` 与 `_C_1` 实测移动达到 270 cm/s，使用 `ABP_CastleGrunt_C`，枪网格 `SM_CastleRifle` 可见。运行日志将 `CastleBolt_0` 和 `CastleBolt_1` 归到这两名远程兵。
- 用户叫停时，朝向空手近战兵的暂停 PIE 交接尚未布置。收尾检查时，最后的测试编辑器已收到退出指令，MCP 监听已关闭。

本地验证日志为 `Saved/Logs/CastleMeleeV1PresentationPIE.log`。收尾读到 HEAD `eea95e5`（`Melee update`），四个资产均已在该提交中。本次收尾追加本报告。

## 空手待机与动画衔接验收（2026-11-26）

实际工程为 **E:/Unreal Projects/PCG_AI_demo/testfordemo.uproject**，UE 5.8.4 / CL 58902052，分支 codex/castle-melee-v1 快进至 eea95e5。已对照补送的 CASTLE_MELEE_V1(6) 附件，并保留上方的前轮记录。前轮使用其报告所列的桌面工程，本轮以实际的 E 盘工程及冷加载证据为准。Git 账号窗口曾等待操作。四个所需资产通过 GitHub 公开下载取得，逐个核对提交中记录的 LFS 长度和 SHA-256 后写入标准缓存，快进完成。两个渲染编辑器均加载既有 DLL，SHA-256 为 215C9301C0AB1F679F8F7AD80D35A7A7967968BD64B6EDFA2E1E7A17CCC4C165。[官方 MCP 门禁](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/Official-MCP-Gate.json>) · [LFS 对照](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/LFS-Verified.json>) · [冷启动模块](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/Cold-LoadedModule.json>)

### 资产配置

只将 /Game/Art/Grunt01/MeleeV1/Animation/BS_CastleGrunt_Melee 的 0 cm/s 样本由 MF_Rifle_Idle_ADS 改为已有 **/Game/Characters/MM_Idle.MM_Idle**。序列时长 7.5667 秒，骨架为 SK_CastleGrunt_Corrected_Skeleton，根运动关闭。135 与 270 cm/s 均保留 AS_Grunt_Melee_Walk，采样值、速率和轴设置保持。定向保存并由新进程冷读取核对。保留近战专属 AnimBP、CharacterMovement 位移驱动、DefaultGroup.DefaultSlot、攻击 Montage 及原约 96 ms 命中窗口。本轮未另加状态机和混合规则。[保存前](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/ReadBefore.json>) · [定向保存](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/BlendSpace-Edit.json>) · [三个样本的冷加载结果](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/Cold-Readback.json>)

### 正常渲染 PIE

验收图为 /Game/Art/Grunt01/MeleeV1/Tests/L_CastleMelee_V1_Dev，Seed 24680，D3D SM6 Standalone PIE。使用既有 Cohort 夹具布置试次。BugItGo 之后复核 Walking、落地和 Actor 碰撞。首次 Standing 尚在 Falling，未进入交战，等落地后重跑。移动和实弹试次由原夹具合成键鼠输入，经正常 PlayerController 路径处理。ActiveDeath 是独立的原生伤害注入。视频以实际游戏窗口关键帧和对应日志交付。

| 编号 | 状态 | 实测结果 |
| --- | --- | --- |
| T01 空手待机 | PASS | 自然生成的同一个近战兵在运行中观察约 8.1 秒。9 个样本的实际速度和 AnimBP Speed 均为 0，原地位置保持。旁观相机单独定位，玩家本体未移动。可辨识画面中守卫双手下垂、无枪，无明显原地踏步。[数据](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/idle-20261126062907466.json>) · [开始画面](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/idle-20261126062907466-pose-0.png>) · [后段画面](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/idle-20261126062907466-pose-6.png>) |
| T02 移动与停步 | PASS | Single 中同一近战兵沿路径改变位置，实际速度和专用 AnimBP Speed 均达到 270 cm/s，然后跌至 0。移动帧可见新 Walk。0 速对应已冷加载的空手样本。空手无 Montage 待机由 T01 画面独立复核，交战末段全身挥击连续衔接。[速度与位置](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/chase-20261126062613358.json>) · [移动帧](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/chase-20261126062613358-movement.png>) |
| T03 完整攻击 | PASS | 同一 Attack ID 经前摇、窗口、命中、收招到 Completed=1。连续完成两次，各扣 10 护盾，60→40。另测真实后撤挥空，伤害为 0。[完整日志](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/standing-20261126061919004-raw.log>) · [前摇](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/standing-20261126061919004-windup.png>) · [接触附近](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/standing-20261126061919004-contact.png>) · [收招附近](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/standing-20261126061919004-recovery.png>) |
| T04 受击恢复 | PASS | 同一守卫两次由真实 CastleWeaponProjectile 命中，弹量 24→23→22，目标血量 85→63→41。原前摇被取消，两次之后均观察到新 Attack ID 起手。[弹体、扣血与恢复](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/repeat-cancel-20261126062328417.json>) |
| T05 恢复追击 | PASS | S 输入使玩家真实后撤约 190.9 cm。第一次挥空自然完成后，守卫再以 129.6→270 cm/s 移动，复追约 182 cm 并起手第二击。[坐标与速度](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/chase-20261126062613358.json>) · [后撤及收招日志](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/chase-20261126062613358-raw.log>) |
| T06 死亡表现 | PASS | 四发卡宾枪弹使近战兵死亡，弹量 24→20、金币 35→47。单独 ActiveDeath 在原有效段杀死近战兵后，核实唯一 Ragdoll 代理、物理启用、Pawn 忽略及同一 PIE 内清理。真实画面可见倒地近战兵双手空着。[实弹死亡](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/death-20261126062055176.json>) · [布娃娃与清理](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/activedeath-20261126062113052.json>) · [尸体画面](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/activedeath-20261126062113052-end.png>) |
| T07 远程回归 | PASS | 固定六远程组以约 270 cm/s 追击，实际画面持枪瞄准。采到 8 个真实 CastleBolt 实例，玩家护盾 60→0、生命 100→50。官方反射未开放 Bolt Owner，数量来自场景内活弹快照。[弹体与伤害](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/T07-RangedBolts.json>) · [持枪画面](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/ranged-20261126062630527-last.png>) |

关键帧与运行数据覆盖技术验收，逐帧脚滑、短时穿模和手感留给连续试玩确认。未复跑 FIX05、长帧专项或 ABBA 性能测试。

### 保护、回滚和人工交接

复核的 12 个文件中只有近战 Blend Space 发生任务内变化。两张地图、远程父蓝图、近战 AnimBP／蓝图／Walk／攻击 Montage、现有 Idle、配置与核查的源码均与快进后的基线一致。Blend Space SHA-256 从 **3f6a671c1352ff2e95c22f020bad1413949afcf752bdbc3c1876c3efdd2aaaf7** 变为 **312400cbbfcc580f6cd2ce697d16d233497837adc24ed30018b04706b8b5369f**。最小回滚是在同一 Blend Space 把 0 cm/s 样本改回 MF_Rifle_Idle_ADS，定向保存该包。确无后续编辑且编辑器已关闭时，也可使用当次字节备份恢复。[最终 SHA 对照](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/Final-Hashes.json>) · [原始 Blend Space](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/BS_CastleGrunt_Melee.before.uasset>)

已在原图留下一次冷启动后的 **testfordemo Preview**，Seed 24680，Floor 1。玩家面向一名近身空手近战兵，生命 100、护盾 60。恢复窗口焦点后按 **Esc** 即可继续并交战。[实际暂停状态](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/Paused-Handoff-Final.json>) · [暂停画面](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleMeleeV1/AnimationPass_20261126/Cold/handoff-20261126063337620-paused.png>)
