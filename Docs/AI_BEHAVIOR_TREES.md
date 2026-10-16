# Demo 的 AIController、行为树与黑板

2026-10-14 改造：敌人决策由 Character::Tick 迁移到实际 UE 行为树。角色 Tick 现在仅更新头顶标签。现有场景网格、材质、弹体、技能预警、伤害和奖励沿用原实现。

## 编辑器展示

1. 打开项目，在内容浏览器中进入 **Content / Demo / AI**。
2. 打开 **BT_CastleGuard**：普通守卫与精英使用的行为树。
3. 打开 **BT_CastleBoss**：首领使用的行为树。
4. 在行为树编辑器切换到 **Blackboard**，查看 **BB_CastleEnemy**。
5. Play 后在行为树的调试对象列表选择对应 NPC，观察活动节点和黑板数值。Boss 进入竞技场后才激活。

NPC 的 AIController Class 为 ACastleAIController。Configure 完成后按 bBoss 选择树；控制器接管已配置角色时也会启动对应树。死亡时停止树、解除控制并销毁控制器，换层沿用现有角色清理流程。

## 实际树结构

守卫树：

```text
ROOT
└─ Selector: Combat or hold current state
   ├─ Service: Update target, sight and pending slam
   ├─ Simple Parallel: Pursuit and combat simultaneously
   │  ├─ Decorator: IsOperational is true (Abort Both)
   │  ├─ Decorator: InDecisionRange is true (Abort Both)
   │  ├─ Main Task: Face and chase / stop
   │  └─ Background Task: Melee / single projectile
   └─ Task: Out of range: hold; inactive: stop
```

Boss 树：

```text
ROOT
└─ Selector: Combat or hold current state
   ├─ Service: Update target, sight and pending slam
   ├─ Simple Parallel: Pursuit and combat simultaneously
   │  ├─ Decorator: IsOperational is true (Abort Both)
   │  ├─ Decorator: InDecisionRange is true (Abort Both)
   │  ├─ Main Task: Face and chase / stop
   │  └─ Simple Parallel: Attack and ground slam independently
   │     ├─ Main Task: Melee / fan attack + enrage
   │     └─ Background Task: Ground slam telegraph (no sight required)
   └─ Task: Out of range: hold; inactive: stop
```

服务附着在 Selector 上，图中的服务列表不是额外可选子分支。任务是持续运行的原生 C++ BTTask；Simple Parallel 让移动、普通攻击和首领地面技分别执行，避免传统“移动结束才攻击”的串行节奏。任务不把计时器存在共享节点模板中，每名敌人的冷却和蓄力状态仍独立保存在角色上。

## 技术分工

| 技术 | 本项目中的职责 |
| --- | --- |
| ACastleAIController | 接管敌人，加载并启动对应行为树，提供黑板，执行寻路 |
| UBehaviorTree | Selector、黑板条件与 Simple Parallel 决定活动分支和并行任务 |
| UBlackboardData / UBlackboardComponent | 保存目标、距离、可见性、可行动状态及 Boss 状态 |
| UBTService_CastlePerception | 每帧刷新状态与射线视线；在距离检查前结算已到期地面技 |
| UBTDecorator_CastleBool | 继承原生 Blackboard Decorator，监听状态变化，中止与切换分支 |
| UBTTask_CastleAction | 追击、攻击、地面预警、保持／停止任务，调用角色能力 |
| Recast + MoveToActor | 继续使用动态导航绕过障碍 |

## 黑板键

| 键 | 类型 | 含义 |
| --- | --- | --- |
| TargetActor | Object (Actor) | 玩家目标 |
| HomeLocation | Vector | 出生点，用于守卫范围约束 |
| TargetDistance | Float | 当前敌人与玩家的三维距离 |
| IsOperational | Bool | 玩家有效、敌人存活、生成完成、已激活且未死亡／胜利 |
| InDecisionRange | Bool | 是否允许新的追击和攻击决策 |
| CanSeeTarget | Bool | 射线未被阻挡，或命中玩家 |
| IsBoss | Bool | 是否为首领 |
| IsEnraged | Bool | Boss 生命严格低于 50% |
| IsChargingBlast | Bool | 地面技能是否正在蓄力 |

后两个状态用于调试与展示；攻击任务实时读角色生命，避免一帧状态刷新差导致狂暴判定延迟。任务在分支切换期间仍会重新核对存活、激活与距离条件。

## 保留的战斗规则

| 行为 | 守卫／精英 | Boss |
| --- | --- | --- |
| 新决策距离 | ≤2100 cm，且玩家距出生点二维距离 ≤2600 cm | ≤3600 cm |
| 移动检查 | 每 0.45 秒 | 每 0.45 秒 |
| 开始追击 | >450 cm 或无视线 | >650 cm 或无视线 |
| MoveToActor 接近阈值 | 320 cm | 500 cm |
| 近战范围 | <140 cm | <140 cm |
| 普通攻击冷却 | 1.9 秒 | 1.35 秒；半血以下 0.85 秒 |
| 远程弹体 | 1 枚，850 cm/s | 3 枚；半血以下 5 枚，1000 cm/s，间隔角 9° |

Boss 地面技保留 6 秒触发冷却、1.4 秒红圈预警、400 cm 二维半径、脚底高度差 <160 cm、伤害 ×2.4，并沿玩家实际脚下表面设置高度。触发不要求视线。已到期技能由根服务在距离检查前结算，因此进入范围外的保持分支不会取消已经蓄力的技能。

范围外保持原有路径，不新增返巢或巡逻。未激活、玩家死亡或通关时停止移动。精英生命／伤害倍率与两层 Boss 数值没有调整。

行为树调度取代角色 Tick 后，执行发生在行为树组件的帧更新中；上述数值和规则保留，不承诺两个版本逐帧轨迹完全相同。

## 编译与验证

资产随项目保存在 Content/Demo/AI，正常使用无需重新生成。

```powershell
./Scripts/BuildDemo.ps1
./Scripts/BuildDemo.ps1 -VerifyAIAssets
./Scripts/ValidateAI.ps1
./Scripts/ValidateDemo.ps1 -Seed 1337
```

只有需要重建 AI 资产时使用 `BuildDemo.ps1 -UpdateAIAssets`。它会覆盖这三份 AI 资产中的手工编辑，不会重建地图、PCG 或材质。

专项验证在真实关卡中检查树与黑板运行、移动和攻击并行、单发远程、近战伤害、遮挡、出生点范围、Boss 未激活、三发／五发弹幕、无视线地面技、范围外蓄力结算与玩家死亡停战。检查日志为 Saved/Logs/CastleAISmoke.log。全流程检查使用 Saved/Logs/CastleSmoke.log，覆盖地图、交互、奖励与三层通关。

2026-10-14 验证结果：Editor Development 编译通过；AI 专项检查出现 CASTLE_AI_SMOKE_OK；种子 1337 完整三层流程出现 CASTLE_SMOKE_OK；两套保存后的行为树经编辑器重开流程检查出现 CASTLE_AI_EDITOR_VERIFY_OK，条件、服务和行动节点均保留。最后一项只校验已有资产，不重新生成，也不保存改动。

此前 2026-10-13 的汇报资料描述的是改造前版本，其中“没有行为树资产”的说明已不适用于当前项目。汇报当前实现时，请使用本说明与编辑器中的真实行为树资产。
