# P03-A 两房直接拼接

## 编辑器操作

打开 `/Game/Roguelite/Maps/L_Roguelite_RoomAssembly_Dev`，不要覆盖 P01/P02 测试地图。
在 Outliner 选择 `P03A_Assembly`。它是 `ARogueliteRoomAssembly`，没有 Tick，也不会通过 OnConstruction 自动拼接。

在 Details 的 **Assembly Setup** 指定：

- Room A：固定房间；Socket A：North/South/East/West。
- Room B：要移动的房间；Socket B：对应的门洞。
- Connection Mode：本轮选 Direct。

点击 **Align Selected Rooms / 拼接所选房间**。此操作只计算并改变 B 的位置和直角旋转；不修改 A、房间尺寸、门配置或 Actor Scale。成功时记录连接；失败时 B 保留或恢复原 Transform，连接记录不新增。每个端点必须是 P02 已启用且合法的 Socket。两门净宽/净高须相同（默认误差 0.1 cm），并足以容纳当前检查胶囊。

结果在 **Assembly Result → Last Result**，连接在 **Connections**；Output Log 的 `P03A RoomA=... SocketA=... RoomB=... SocketB=...` 给出具体端点和原因。检查结果不是自动视口弹窗。当前官方 MCP 对 UObject 中的中文字符串有编码问题，本地 UTF-8 Output Log 保留完整原因；不要只看 MCP 的点击返回值。

房间尺寸、门偏移或 Transform 改变后，点击 **Revalidate Connections / 重新检查连接**。它只检查，不移动房间。需要调整位置时再明确点击 Align。保存重载保留连接 ID、端点、模式和占用，但把旧验证状态标为未检查，重载后要重新检查。

**Disconnect Selected Rooms / 释放所选连接** 仅解除逻辑占用，不移动房间，不封墙、不恢复旧位置。其他 Assembly Actor 中的活动连接也会占用同一 Socket。无效连接继续保留占用，需修复或明确释放。

## 坐标、占用与通行

复用 P02 `TryGetDoorSocket` 的门槛坐标：局部 Z=0，位于墙体外表面；北 +X、南 −X、东 +Y、西 −Y。Direct 让两个门槛世界坐标一致、外法线相反，由此计算 B 的平移和 0/90/180/270° 旋转；不要求房间中心对齐。

房间占用从净长宽、墙厚、地板厚和墙高计算，再转换到世界空间。它不使用包含隐藏墙段、箭头、POI 的 Actor 视觉 Bounds。包含室内体积、墙体与地板；允许外边界接触，拒绝超过容差的实体空间相交，并检查场景中的其他参数化房间。A/B 必须 Scale=1、Pitch/Roll=0、Yaw 为直角。其他斜转房间的世界 AABB 检查可能保守拒绝。

每次 Direct 检查实际运行：

1. 门槛距离、法线、门规格及参数化占用检查。
2. 9 条穿过开口的 Visibility 射线，4 条两侧实体墙射线、2 条横梁射线、2 条地板射线。
3. Pawn 通道简单碰撞胶囊 Sweep：A→B 中央及左右边缘共三次，B→A 中央一次，另检查起终点 Overlap；门槛中央及左右三次向下 Sweep 必须命中两房间之一的地板并得到向上接触法线。默认半径 34 cm、半高 88 cm、净距 2 cm，均可调整；该默认取自 UE 的 ACharacter，不代表最终玩家规格。
4. 可用且尺寸合适的 NavMesh：投影门两侧点并查询完整路径；没有数据、未覆盖、仍构建或 Agent 尺寸不足时返回 Unavailable。几何/胶囊通过不代表导航已通过；可用导航路径失败则连接失败。

`LastResult.Probes` 保存每条查询的起终点、是否命中、是否符合预期及命中组件。默认 SpectatorPawn 只能用于画面观察，不能证明角色不会穿墙。

复杂射线恰好落在两块地板的公共三角形边界时可能漏检。补验中在共边两侧 ±0.001 cm 都命中地板，共边射线未命中；这是小于允许容差的边界精度问题。门槛地面使用实际胶囊向下 Sweep 验证支撑，不能把该复杂射线漏检当作完整地板证明。

## 手工内容保护

- 修改 RoomLength / RoomWidth 时沿用 P01 规则：手工 POI 不变形、不自动移动。
- 本轮拒绝自动移动有 ManualPOIActors 引用或附加 Actor 的 B；A 固定不动。
- 未来完整房间模板应让手工结构按房间局部坐标进行整体刚性平移/旋转，保持自身尺寸；需要专门模板及归属数据，本轮未实现。
- 不把 POI 挂在会按房间尺寸缩放的外壳组件上，不运行旧 P01/P02 验证脚本改动原地图。

## C++ / Blueprint 接口与走廊扩展

`TryAlignRoomToSocket(FixedRoom, FixedSocket, TargetRoom, TargetSocket, Mode)` 返回 `FRogueliteAssemblyResult`，可在 Blueprint 调用。
`RevalidateConnections()` 检查现有连接；`ReleaseConnection(ConnectionId)` 释放一条记录。

`FRogueliteRoomConnection` 保存 GUID、两个 Room/Socket、Mode、bActive、上次几何/胶囊有效状态、导航状态及原因。`FRogueliteAssemblyResult` 另外保存建议 Transform、占用矩形、距离/法线和实际碰撞查询。端点、门规格与预约检查独立于 `ValidateDirect` 的贴合/射线/Sweep 逻辑。

连接模式已有 Direct、StraightCorridor、CornerCorridor、CustomConnector 的序列化定义。后三种仅是数据预留；本轮调用会明确拒绝，未创建走廊 Actor、Mesh、路径或转角算法，也不对它们套用 Direct 的邻接限制。

未来需要新增走廊实体/生成与对应几何、占用、胶囊及导航验证，然后扩展模式分派并复用端点、规格、占用预约和释放。现有连接数据可以继续使用；届时可能增加连接器 Actor/路径引用及设置。当前 `TryAlignRoomToSocket` 明确面向移动 B，未来固定两端的走廊连接可能另增创建接口；不要宣称现有接口已经实现通用走廊生成。

## 人工验收（只用新地图）

1. 选择 A/B 检查尺寸：A 2000×2000，B 3000×2500；B 南门关闭 Auto Center、Manual Offset=300。选择 Assembly 点击 Align，观察门洞衔接与 B Scale=1，A 不动。
2. 释放连接后设置 A Yaw=90/180/270，选择对应 Socket 再拼接；观察门洞方向与错开的房间中心。之后恢复演示布局，保存。
3. 改 A 长度或 B 门偏移，先点 Revalidate，应报告连接失效且 B 不动；再点 Align 修复。观察 LastResult 的几何、胶囊与导航状态。
4. 给 B 的 ManualPOIActors 添加一个独立测试 Actor，再拼接应拒绝；缩小 B 检查 POI Transform 不变、Bounds 越界提示。删去的是引用而非 POI Actor；本轮不自动装配 POI。
5. 保存、切到其他地图再返回，重新检查连接；启动 PIE 看衔接画面和独立 GameMode，再停止。角色走门、Undo/Redo 和 NavMesh 连通体验仍需单独验收。

仅撤销 P03-A 的方法和实际验证结果见 `P03A_RESULT.md`，不要移走整个插件或整个 Roguelite Content。
