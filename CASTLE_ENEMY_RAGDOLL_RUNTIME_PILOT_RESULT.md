# CASTLE_ENEMY_RAGDOLL_RUNTIME_PILOT 验收报告

工程 `E:/Unreal Projects/PCG_AI_demo/testfordemo.uproject`，UE 5.8.4 / CL58902052。官方 MCP 握手、工具发现和对象冷读均通过。使用独立 Development 编辑器中的 `L_CastleGruntArt_FIX01`，Seed `24680`。原编辑器的资产未被编辑、保存、重载或关闭。

**结果：隔离 PIE 单兵与三兵运行时技术验证通过。** 原来的动画切换跳动已在隔离试验中消除，现有 Physics Asset 保留原样。试验版未写入正式蓝图。

## 试验实现

- 在隔离编辑器内存中，对 `BP_CastleEnemy_DeathVisualProxy_Art` 的 `VisualMesh` 调用 `SetPhysicsAsset`，引用原 `PA_CastleGrunt_Ragdoll`。共享 `SK_CastleGrunt_Corrected` 的默认 Physics Asset 仍为 `None`。该试验没有改动 PA 的刚体、形状或约束。
- 原样使用 `AM_Grunt_Death`，完整时长 `1.100 s`、倍率 `1.0`。仅供代理使用的临时动画蓝图持续更新原倒地序列作为 `DefaultSlot` 的姿势来源。自然 `OnCompleted` 后等待下一帧，评估并暂停末帧，再开启物理。物理混合权重按 `0 → 0.15625 → 0.5 → 0.84375 → 1` 接管，每步延迟 `0.05 s`。尸体寿命从开启模拟时设为 `10 s`。
- `VisualMesh` 使用 `PhysicsOnly`、`PhysicsBody`，仅阻挡 `WorldStatic`，忽略 Pawn 和可见性射线，关闭 Overlap。Actor 根保持原位置，骨骼由 Chaos 模拟。枪械组件继承原 `Weapon_R` 附着，保持 `NoCollision`、无物理模拟。

## 交接问题定位

第一轮用原代理的站立/移动姿势作为 Slot 基底。逐帧样本记录到物理开启前，Montage 自动淡出回到了站立，恢复倒地末帧时骨盆单帧位移 `147.21 cm`。原 Montage 的淡出为 `0.08 s`。未保存的零淡出副本把回站立缩短到一帧，但仍出现 `147.21 cm` 跳动。问题已经出现在模拟开启之前。

最终试验继续使用原 Montage，由临时代理动画蓝图提供同步的倒地姿势。`Single.json`、`Single-ZeroBlend.json` 和 `Bridge-Single.json` 保留三个阶段的对照证据。物理测试没有发现需要调整某个刚体或约束的确定性异常。

## 单兵及三兵 PIE

最终版先通过单兵门禁，再串行运行三兵。下表的时间为 PIE 游戏时间。最后一帧仍能采到模拟及几毫秒余寿命，随后通过独立查询确认代理和附属枪械均消失。

| 试验对象 | 物理启动记录 | 最后有模拟样本 | 交接处骨盆最大单帧位移 | 骨盆高度变化 |
| --- | ---: | ---: | ---: | ---: |
| 单兵 | 3.114 s | 13.129 s | 1.96 cm | 92.33 → 17.00 cm |
| 三兵第 1 名 | 3.127 s | 13.133 s | 2.19 cm | 92.33 → 15.89 cm |
| 三兵第 2 名 | 3.793 s | 13.808 s | 1.96 cm | 92.33 → 15.87 cm |
| 三兵第 3 名 | 4.455 s | 14.462 s | 1.97 cm | 92.33 → 16.82 cm |

- 四个试验对象的 Actor 根位置漂移为 `0 cm`，取样到的骨骼下落后稳定。单兵的头部、右前臂和右小腿取样最低点分别为 `11.03 / 7.47 / 13.45 cm`。取样中未出现原来约 `147 cm` 的交接跳变。枪械相对 `Weapon_R` 的位置距离约 `7.199–7.201 cm`，缩放全程 `(2.4, 2.4, 2.4)`，无独立物理。
- 单兵及三兵均走真实伤害、原兵销毁和掉落入口。单兵金币 `35 → 47`、新增一份 `Ammo / SUPPLIES`。三兵金币 `35 → 71`、新增三份补给。原攻击、伤害、击杀及死亡源码的 SHA 保持一致。
- 胶囊扫掠和运行时 `PhysicsOnly`、忽略 Pawn 的配置证明尸体不进入普通 Pawn 碰撞查询。没有单独比较 NavMesh 重建成本、绘制近景录像或统计 Chaos 内部残余刚体句柄。试验未收到命中事件，落地判断依据实际 WorldStatic 地面、受重力下落及后续稳定位置。

地面证据来自当时尸体骨盆正下方的真实场景射线。最终单兵与三兵场景的射线均命中 `CastleDirector_0` 的 `InstancedStaticMeshComponent_9`，类型 `WorldStatic`，地面高度 `Z=0`，法线朝上。同一区域沿穿过骨盆的 `96 cm` 路段，以 `Pawn` Profile 分别扫掠半径 `32 cm` 和 `36 cm`、半高 `88 cm` 的胶囊，两次均无阻挡。原始结果见 `Final-Single.json`、`Final-Three.json` 及同名 `.PIE.log`。

## 文件与回滚

试验代码和动画蓝图只驻留隔离编辑器内存。已停止 PIE 并关闭隔离进程，正式死亡代理继续使用既有末帧表现。本轮已保留当前代理的原样备份，备份对应本轮开始时的用户版本。收尾时原编辑器中的代理带未保存标记，活动事件图读回与开始时一致。此状态原样保留，未保存或丢弃。

| 受保护文件 | 本轮起始 SHA-256 |
| --- | --- |
| `PA_CastleGrunt_Ragdoll.uasset` | `6D4C161DE6CF7B53F7C07B9FB90F2ED2670616D2BDA2844204A19DC517F0CC32` |
| `SK_CastleGrunt_Corrected.uasset` | `66CD9E9F71A503B51843120CDC9DE4EADCAEE476083BF1EBC85198F9D2AF8128` |
| `BP_CastleEnemy_DeathVisualProxy_Art.uasset` | `F50EB74EF3EC0EEF1D432316705EE57F3D454EE2F34C714AA44BEBE6295A453C` |

现有工程资产和源码的逐文件校验见 `Before.json` 与 `After.json`。试验、备份与复现资料均位于 `E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyRagdollRuntimePilot_20261120/`。其中 `BP_CastleEnemy_DeathVisualProxy_Art.BEFORE.uasset` 是本轮备份，`Unsaved-ProxyPilot-Final.dsl` 和 `Unsaved-AnimationPoseBridge.json` 记录了隔离版设置。报告与试验文件新增，未改正式地图、C++、IK 或团队动画。
