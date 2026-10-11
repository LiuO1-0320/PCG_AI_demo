# P03-C2 编辑器使用与人工验收

仅在 `/Game/Roguelite/Maps/L_Roguelite_CorridorAssembly_Dev` 验收；旧图保持原布局。所有尺寸为厘米，Actor Scale 保持 1。

## 按钮位置

在 Outliner 选择 **P03C2_Assembly**，查看右侧 **Details**：

- `Roguelite → Assembly Setup`：Room A、Socket A、Corridor、Corridor Endpoint A、Room B、Socket B、Connection Mode。
- `Roguelite Assembly Actions`：**Align Corridor Connection / 拼接走廊连接**、**Revalidate Connections / 重新检查连接**、**Disconnect Corridor Connection / 释放走廊连接**。
- `Roguelite → Assembly Result`：最近结果、两处距离/法线、Geometry、Capsule、Navigation、详细 Probes。
- `Roguelite → Connections`：统一逻辑记录与预约。

若按钮不在可视区域：清空 Details 搜索，选 All，折叠 Transform、Assembly Setup、Connections、Assembly Result 等分类，再向下找到 Actions。不要选 Room 或 Corridor 寻找 Assembly 按钮。

## 保存的主测试配置

| Actor | 参数 | 已保存门洞配置 |
|---|---|---|
| P03C2_RoomA | 2000×2000×400；墙厚40，地板厚20 | North，200×250；Auto Center关闭，Manual Offset=250 |
| P03C2_Corridor | 2000×500×350；墙厚25，地板厚30 | 两端200×250；Start对A，End对B |
| P03C2_RoomB | 3000×2500×500；墙厚35，地板厚35 | South，200×250；Auto Center关闭，Manual Offset=-300 |

Room A `(0,0,0)`；走廊 `(2040,250,0)`；Room B `(4575,550,0)`；Yaw均0、Scale均1。房间中心并不对齐，两处门槛实际对齐。

## 人工验收

1. 打开新图，选择 Assembly，展开 Setup，确认引用是上述三个 Actor，模式 **StraightCorridor**。点击 Align；A 不动，C/B 应恢复合法位置。Result 的 Geometry/Capsule 为真，两处 Socket Distance≤0.1 cm、Normal Dot≈-1。
2. 选择 Corridor，把 Length 改为2500。回到 Assembly，点击 Revalidate：应报告门槛不对齐、记录 Failed，但仍保留预约；不会自动移动任何 Actor。点击 Align 后应恢复连接。将 Length 改回2000并 Align。修改房间 ManualOffset、A 的 Yaw=90/180/270也需明确 Align；不每帧自动重排。
3. 在本图放一个有 Pawn 碰撞的 Cube 阻挡门口或走廊，记录 C/B 的 Transform。Align 应拒绝，Probes 指明阻挡组件，两 Actor 恢复原 Transform、Connection GUID/预约不增加。移开障碍再 Align。测试时避免复制/改变旧图的 POI；带 ManualPOIActors 的 B 拒绝自动移动。
4. 选择 **P03C2_TestRig**，在 Actions 中点击 **Build Local Navigation / 构建本图导航**；等待约数秒，再回 Assembly 点 Revalidate。可用时应显示 Navigation=Passed、非部分路径及路径点。按 P 可查看绿网（此快捷键仅供人工操作，不是碰撞证据）。**重载后默认 Agent 高度会恢复144 cm，需要重新点局部构建；未构建时 Unavailable 是真实限制。** 不修改 Project Settings。
5. 保存本图，打开另一张地图后重开；参数与 GUID 应保留，记录先为 NotChecked。再次 Revalidate 验几何，按上一步重建导航。Play 后查看 Output Log `P03C2_RUNTIME_RESULT Passed=1` 与 `LegacyActorCount=0`，再 Stop。默认 SpectatorPawn 的穿墙表现不作为碰撞验收；Result 内实际 Pawn Capsule Sweep 才是技术证据。
6. 点击 Disconnect 两次：记录/预约释放，但 C/B 不移动、不删除，门洞配置不变。再次 Align 可重建单条记录。Changing Room Socket 属于新的端点连接，须先 Disconnect 原连接，避免旧预约被抢占。

## Runtime 调用规则

共用 API 见 `Source/RoguelitePrototype/Public/RogueliteRoomAssembly.h`：`ComputeCorridorTransforms`（只计算）、`TryAlignCorridorConnection`（真正验证并登记）、`RevalidateConnection`、`ReleaseConnection`。

房间与走廊外壳仍是 Static。生成器应配置 Deferred Spawn Actor、计算 Transform，再 FinishSpawning 于目标位置，最后调用真正的验证/登记。PIE 已验证这条最小流程。BeginPlay 后移动已注册 Static 外壳会被 C2 拒绝；本轮没有实现运行时重新排列子图、动态导航或随机生成器。

TestRig 仅允许 C2 地图；`bRunRuntimeProbe` 可关闭。开启时只在 PIE 的远处临时生成一组探针 Actor，Stop 后消失，不写入保存地图。

交付与精确恢复范围见 `P03-C2_RESULT.md`；不要对整个插件、整个 Content/Roguelite 或整个仓库进行撤销。
