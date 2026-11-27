# Castle PCG 可视化图

在内容浏览器打开 `/Game/Demo/PCGAuthoring`。五张图可以放到空关卡的 PCG Volume 中调试，输出点数据，不需要放置 Castle Director。

| 资产 | 可看、可调的内容 |
| --- | --- |
| `PCG_Castle_Exploration_Layout` | 选择种子和楼层。查看 4×4 网格内的房间、七房主线、支路及走廊。 |
| `PCG_Castle_Vault_Layout` | 选择种子和楼层。查看 2×3 网格上选中的四房路径、入口、出口和走廊。正式游戏第二层开始启用宝库。 |
| `PCG_Castle_Boss_Layout` | 调整角落平台候选点、必选平台数、额外平台抽取比例、掩体数和位置旋转浮动。对照原流程时，在 PCG Component 上设置对应的 Boss 子种子，例如游戏种子 24680、第二层对应 636119642。 |
| `PCG_Castle_Guard_Loot` | 查看指定击杀序号范围的抽签。默认 15 张票，1、2、3 分别对应速射、散射、无限弹药，4–15 不掉落。可改总票数及比较节点。 |
| `PCG_Castle_Chest_Placement` | 查看记忆箱、陷阱箱、宝库箱及可选精英掉落箱。各分支的 Transform Points 节点可调摆放偏移。点上带有 `RelicKind`、`Fragments` 和 `Ammo`。 |

## 编辑器调试

1. 新建一个空关卡，放置 PCG Volume，在 PCG Component 的 Graph 中指定一张图。
2. 打开图，选中起始数据节点，在 Details 调 `Run Seed`、`Floor` 等参数。Boss 图使用 PCG Component 的 Seed。
3. 选中 PCG Component 并点击 Generate。打开 PCG 图的调试对象下拉，选中这个 Component，在目标节点使用 Inspect 看点位置及属性。需要在视口看点时，开启该节点的 Debug，然后重新生成。
4. 房间点的 `CellIndex` 是从 0 开始的网格编号。主路用 `RouteOrder` 标出顺序，支路为 −1。`DoorMask` 的四位依次代表 +X、+Y、−X、−Y。走廊点的 `FromCell` 和 `ToCell` 对应两端房间。

这些图供可视化、调参和方案比较使用。正式 Director 仍使用现有生成入口。房间连通校验和种子流在数据节点中，图里的比较、分流和偏移节点可以直接查看及调整。实际组装场景、击杀触发、开箱交互、发奖和防重复领取由现有 C++ 执行。
