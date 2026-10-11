# P03-C1 参数化直线走廊使用说明

只提供走廊 Actor 本身。正式地牢将使用走廊连接，现有 P03-A Direct 与原测试地图保持不变。未实施自动房间连接、迷雾、加载、门控、POI 或 PCG。

## 在编辑器里使用

打开 `/Game/Roguelite/Maps/L_Roguelite_Corridor_Dev`，在 Outliner 选择 `P03C1_Corridor`（测试区 X=90000 cm）。清空 Details 搜索框，按 F 聚焦，展开 `Roguelite / Corridor Dimensions`。

已保存的测试尺寸是 2000×500×350 cm、墙厚 25、地板厚 30、端部开口 200×250；下表是新建 Actor 的默认值。测试区离原灯光较远，Lit 模式内腔偏暗；可在视口上方 Lit 菜单选择 Unlit 查看几何，未添加雾效或专用照明。实际 Details 数字框长度 2000→2200→2000 输入补验已通过；自动验证不替代用户人工验收。

| 属性 | 测试默认值 | 规则 |
|---|---|---|
| CorridorLength | 1000 cm | X 轴直线，至少 100 cm；不绑定任何穿行秒数 |
| CorridorWidth | 400 cm | 室内净宽，至少 100 cm |
| CorridorHeight | 300 cm | 地面 Z=0 到天花板底面净高，至少 100 cm |
| WallThickness | 20 cm | 至少 1 cm；同时用于天花板与端框；长度必须大于两倍墙厚 |
| FloorThickness | 20 cm | 至少 1 cm，向 Z=0 下方延伸 |
| DoorWidth / DoorHeight | 200 / 250 cm | 两端统一净开口，正数且不超过走廊净宽/净高 |

尺寸安全上限为 100000 cm，用于避免病态几何，不是最终玩法尺寸。五个外壳参数不设置 ClampMin，因此非法输入会保留并报告。Actor Scale 保持 `(1,1,1)`；Pitch/Roll=0，Yaw 为 90° 倍数。

走廊 XY 中心为原点，完成面 Z=0。Start 门槛在局部 `(-Length/2,0,0)`、朝 -X；End 在 `(+Length/2,0,0)`、朝 +X。端框厚度向走廊内部延伸，门槛正好是外表面，不要再给端点额外加墙厚。

四个外壳网格和六个端框网格在构造器创建；端框封住走廊与较小开口之间的左右/上部差额。开口等于内腔时零尺寸端框停用，不创建零尺度碰撞。属性修改通过 OnConstruction/编辑器属性通知更新原组件，无 Tick。

## 警告与碰撞

`ValidationWarnings` 属于走廊 Actor；清空 Details 搜索框后在 Validation 查看。Output Log 搜索 `P03C1_CORRIDOR_WARNING`，包含 Actor 路径和具体属性/原因。两个编辑器箭头合法为绿、非法为红；并非自动弹出通知。

- 外壳尺寸/Transform 非法：原输入不改，全部外壳隐藏并停用碰撞，端点无效；修改为合法值后恢复。
- 外壳合法、开口非法：外壳保留，两端恢复完整实体封板，端点无效；不保留旧开口碰撞。
- 合法实体地板/侧墙/天花板/端框为 `BlockAll + QueryAndPhysics`；开口内没有网格碰撞，箭头无碰撞。

Validation 下 `Check Passage / 检查胶囊通行` 显式执行四次 Pawn 胶囊 Sweep 和起终点 Overlap，默认半径 34/半高 88/净距 2 cm，可调。它不会忽略走廊自身，结果在 Passage Check 中；修改参数或重载后旧结果失效。SpectatorPawn 不是碰撞角色，不能用它穿墙判定碰撞有效；该检查也不是导航或真人行走验收。

## Socket 与未来连接

`GetCorridorSockets()` 与 `TryGetCorridorSocket(Start/End, OutSocket)` 返回新计算的数据。`FRogueliteCorridorSocket.DoorSocket` 直接复用 `FRogueliteDoorSocket`，含局部/世界门槛、方向、净开口尺寸、墙厚和有效性；Start 的 Side=South，End=North，Endpoint 是走廊自己的稳定端点 ID，不能把 Side 当世界方位。

`bConnected` / `ConnectionId` 为未来连接记录预留；C1 始终未连接/空 GUID，不管理预约或持久化连接。`IsRoomSocketCompatible()` 只检查合法端点与相同开口宽高（0.1 cm 容差），不要求走廊内腔等于门洞；不检查实际摆放/碰撞/导航，不会移动 Actor。`GetLocalOccupancyBounds()` / `GetWorldOccupancyBounds()` 提供独立于可视 Bounds 的外壳占用，非法外壳返回无效 Box。

现有 P03-A 的数据可复用，但执行 API 只接受两间 Room 且拒绝非 Direct。P03-C2 需新增独立走廊连接执行入口/模式处理、连接器引用和两端预约管理，逐端对齐，再验证房间/走廊占用、接缝、胶囊和必要导航；不能直接把 Corridor 传入旧 Room 参数，也不能用两次 Direct Align 冒充走廊连接。C1 不改 P03-A 私有检查/预约函数；C2 如何共用唯一预约权威须单独批准设计。

## 人工验收

1. 在新图选走廊，分别改变长度 1000→2000、宽度 400→600、高度 300→450；观察原组件更新和两端箭头位置，Actor Scale 仍为 1。
2. 改墙厚/地板厚，检查端框收口；内腔宽高大于 200×250 开口仍应封住左右/顶部差额。也可把内腔设为 200×250，端框停用但两端畅通。
3. 交替改变参数并点击 Check Passage；合法默认胶囊应通过。输入 DoorWidth=500（内腔宽400）应保留数值、端点变红、两端封闭；输入 CorridorLength=-20 应保留并停用无效外壳，恢复合法值后无旧碰撞。
4. 保存，仅切换/重新加载新图；参数保持，旧胶囊检查变未测试，重新点击 Check Passage 得到当前结果。不要用重置验证脚本覆盖自己后来摆放的布局。
5. 进入 PIE 观察外壳与独立 GameMode，再停止；当前为 SpectatorPawn，真人角色通行、Undo/Redo 和 NavMesh 仍需专门验收，不凭 Spectator 移动判断碰撞。

实际构建/MCP/PIE 结果与精准恢复范围见 `P03C1_RESULT.md`，不以本说明代替测试报告。
