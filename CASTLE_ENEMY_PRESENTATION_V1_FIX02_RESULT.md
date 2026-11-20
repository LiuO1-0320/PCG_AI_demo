# CASTLE_ENEMY_PRESENTATION_V1-FIX02 验收报告

工程 `E:/Unreal Projects/PCG_AI_demo/testfordemo.uproject` · UE 5.8.4 / CL 58902052 · 2026-11-20

**Grunt 可见性修复通过。** 正式工程保存后在新编辑器进程中从磁盘冷加载，三个原生占位组件在资产默认对象、编辑器实例和模拟运行实例上均不可见。原样运行的 `CastleDemo.EnemyPresentation.SpawnAndDamage` 成功，26 条警告、0 条错误，进程退出码 0。完整 PCG 回归保持 `Blocked_MissingAssets`。

## 1. 唯一修改的工程资产

`/Game/CastleDemo/Enemies/Presentation/BP_CastleEnemy_Grunt`

| 版本 | SHA-256 |
|---|---|
| 修复前，备份复核一致 | `63D5BD29A3DB7919DD0231C7E9C88C0B32BB66DC064E70091B02BE5564C407CF` |
| 修复后，冷加载和回归后复核 | `2B44897835A5C5BCBF977D4D0AFACBA9480208C825360D3DB391614AA7F2978A` |

Helmet 和 Visor 是 C++ 构造时创建的原生子对象。FIX01 的冷加载已证实，直接写子类默认对象的隐藏标志会在重新加载时恢复。本轮在 Grunt 中添加了两个不可按实例编辑的组件引用 `GruntNativeHelmetReference` 和 `GruntNativeVisorReference`，让蓝图保存对原有组件的引用和可见性覆盖。两条引用在实例上解析到该实例本身的原有组件，组件及静态网格引用均保留。SentinelBody 沿用原有隐藏值。

改动先在隔离副本验证，再应用到正式工程。目标编辑器通过官方 Unreal MCP 握手、工具发现、工程路径、当前地图 `/Game/Demo/L_CastleDemo` 和 PIE 状态的只读检查。使用警告视为错误的选项编译 Grunt，通过后仅保存该资产，再启动新进程冷读。保存日志只记录 Grunt 一个资产包。用户原有编辑器会话保留。

## 2. 冷加载和实际实例

| 继承组件 | 修复前冷读（Visibility / Hidden In Game） | 修复后冷读 | 编辑器与模拟运行实例 | 原静态网格 |
|---|---|---|---|---|
| SentinelBody | false / true | false / true | 两处均 false / true | Cube |
| Helmet | true / false | false / true | 两处均 false / true | Sphere |
| Visor | true / false | false / true | 两处均 false / true | Cube |

用未保存的 `/Engine/Maps/Entry` 放置并模拟一个真实 `BP_CastleEnemy_Grunt_C_0`。三个组件均为 `NoCollision`。Capsule 半径 36、半高 88，碰撞为 `QueryAndPhysics`，`MaxWalkSpeed` 为 270。Grunt 仍使用 `SKM_Manny_Simple` 和 `ABP_Unarmed_C`，Mesh 相对位置 `(0,0,-88)`、Yaw `-90°`、缩放 `(1,1,1)` 与修改前一致。用官方 MCP 读回真实实例的可见性状态。该会话使用 NullRHI 和无声模式，画面观感和听感标记为 `NotTested`。

[冷加载与实际实例原始读回](<E:/Unreal Projects/PCG_AI_demo/Saved/Backups/CastleEnemyPresentationV1_FIX02_20261120_004206/Target-ColdAndRuntime.json>) · [单资产保存日志](<E:/Unreal Projects/PCG_AI_demo/Saved/Backups/CastleEnemyPresentationV1_FIX02_20261120_004206/TargetWarm-MCP.log:2183>) · [临时世界模拟日志](<E:/Unreal Projects/PCG_AI_demo/Saved/Backups/CastleEnemyPresentationV1_FIX02_20261120_004206/TargetCold-MCP.log:2141>)

## 3. SpawnAndDamage 定向回归

正式工程发现并执行了 1 项 `CastleDemo.EnemyPresentation.SpawnAndDamage`，报告状态为 `Success`，汇总为 `succeededWithWarnings=1`、`failed=0`、`notRun=0`。Helmet 和 Visor 的隐藏断言均通过。测试复用 FIX01 已编译的 CastleDemo DebugGame 模块，测试源码及断言未修改。未执行新的 C++ 构建。

Seed `24680` 的临时世界里，由现有 Director 路径实际生成以下 Actor。表中的原生类为 `/Script/CastleDemo.CastleEnemy`，各组记录的 Actor Owner 均为 `None`。

| Director 普通兵类输入 | 探索战斗房普通兵 | 宝库普通守卫 | 探索精英 |
|---|---|---|---|
| 空 | 15 原生 | 3 原生 | 1 原生 |
| 原生 CastleEnemy | 15 原生 | 3 原生 | 1 原生 |
| 无效的 Actor 类 | 15 原生 | 3 原生 | 1 原生 |
| BP_CastleEnemy_Grunt | 15 Grunt | 3 Grunt | 1 原生 |

| 检查 | 本轮正式工程结果 |
|---|---|
| 攻击 | 一次有效远程提交发出 1 次表现事件和 1 枚原生弹体，立即重试未增加次数 |
| 受击 | 无效或零伤害为 0 次；一次有效非致死伤害扣血并触发 1 次 Hit |
| 死亡 | 致死只发出 1 次 Death，无额外 Hit；击杀计数 +1，原弹药掉落 +1，原敌人立即 Destroy |
| 空 Montage / Sound | 三种通知照常收到；未发生表现脚本错误；死亡代理生成数 0 |
| 已配置测试 Montage | 临时 `MM_Idle` 的动态 Montage 实际在唯一的死亡代理上开始播放。代理沿用原网格世界变换，无 Gameplay 碰撞、重叠、导航贡献、Pawn 身份或网络复制；寿命设为 7.717 秒，模拟到 7.900 秒时已自动销毁 |

26 条警告包括 20 条房间装饰资源不可用并保留原房间、5 条静态网格加载警告，以及 1 条临时世界清理时缺少 EndPlay 的警告。

[正式工程测试 JSON](<E:/Unreal Projects/PCG_AI_demo/Saved/Backups/CastleEnemyPresentationV1_FIX02_20261120_004206/Target-SpawnAndDamage/index.json>) · [完成状态与生成、事件、代理原始日志](<E:/Unreal Projects/PCG_AI_demo/Saved/Backups/CastleEnemyPresentationV1_FIX02_20261120_004206/Target-SpawnAndDamage.log:2094>)

## 4. PCG 独立阻断

**`Blocked_MissingAssets`**。沿用 FIX01 的前后对照根因。工程缺少 `/Game/Demo/CourtArt/Masonry` 下的 `SM_Wall_00`、`SM_Ground` 和 `SM_Pillar`，构造路径未生成顶部砌体，导致 Seed 1337 探针无法命中预期组件。本轮按授权未重跑完整 Seed 1337，未创建替代资源，未修改 PCG、导航或回归断言。正式地图的 Director 类设置未写入。

[FIX01 根因、V1 前对照和原断言证据](<E:/Unreal Projects/PCG_AI_demo/CASTLE_ENEMY_PRESENTATION_V1_FIX01_RESULT.md:14>)

## 5. 人工配置与验收

1. 在内容浏览器打开 `CastleDemo/Enemies/Presentation/BP_CastleEnemy_Grunt`，点 **Class Defaults（类默认值）**。**Animation** 中的 `Attack Montage`、`Hit Montage`、`Death Montage` 当前均为空。选择与 Manny 骨架兼容的 Montage，确认其 Slot 与 `ABP_Unarmed` 使用的全身 `DefaultSlot` 一致。三个播放倍率当前均为 1。
2. 在 **Audio** 中设置 `Attack Sound`、`Hit Sound`、`Death Sound` 和 `Sound Attenuation`。四项当前均为空，`Volume Multiplier` 为 1。试听近处和远处，确认音量随距离衰减。
3. 用有渲染和声音的编辑器在临时测试关卡体验一次攻击、非致死受击和击杀。检查姿势、脚底、朝向、Montage 和空间音效。修脚底偏移时，在 Grunt 的 **Components → Mesh → Details → Transform → Location Z** 小幅调整。用单兵测试时，击杀后确认死亡动画能播完，原敌人不会继续占用碰撞。正式战斗视听、完整正式地图流程仍待相应资源和场景验收。

## 6. 保护范围与定向回滚

对照施工前清单复核了 17 个文件，SHA 全部一致，包括正式地图、另外两个表现蓝图、C++、测试、配置及 `.uproject`。本轮工程内容的唯一写入为 Grunt；本报告、备份和证据另存。正式地图的 SHA-256 仍为 `C3BEE36C6454235EADD8BE7CE67B02954AA740F454A49ACC2AC5FA05907AAECD`。测试源码 SHA-256 仍为 `E69D44D06A25C52A0E62311994A926B6CD9460B65D143BF598FC7A5BADAEFE8E`。

需要回滚时先保存自己的工作并关闭任何载有 Grunt 的编辑器会话。核对当前 Grunt 文件 SHA 与本报告「修复后」一致，并核对备份 SHA 与「修复前」一致。如果当前 SHA 已变，先保留后续修改的副本再决定是否替换。仅把备份的 `BP_CastleEnemy_Grunt_before.uasset` 复制回 `E:/Unreal Projects/PCG_AI_demo/Content/CastleDemo/Enemies/Presentation/BP_CastleEnemy_Grunt.uasset`，再校验修复前 SHA 并重开工程。隔离测试副本和其他两个蓝图均无需恢复。

[原 Grunt 备份](<E:/Unreal Projects/PCG_AI_demo/Saved/Backups/CastleEnemyPresentationV1_FIX02_20261120_004206/BP_CastleEnemy_Grunt_before.uasset>) · [受保护文件 SHA 清单](<E:/Unreal Projects/PCG_AI_demo/Saved/Backups/CastleEnemyPresentationV1_FIX02_20261120_004206/ProtectedBeforeSHA256.txt>)