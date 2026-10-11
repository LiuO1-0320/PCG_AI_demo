# P01 参数化房间使用说明

本轮只提供矩形地板、四面完整外墙和碰撞，没有屋顶、门洞、拼接、PCG 或游戏玩法。

## 放置与调节

编译并重新打开工程后，在 Place Actors 中搜索 `RogueliteParametricRoom`，放到 `/Game/Roguelite/Maps/L_Roguelite_Dev`。选中房间，在 Details 的 `Roguelite / Room Dimensions` 调整以下属性；单位都是厘米。

本轮已保存测试实例 `P01_ParametricRoom`，位置为 `(10000,0,0)`，当前净尺寸为 `3000×2500×500`，墙厚/地板厚度均为 20。它与 P00.5 原始地板分开，以避免原地板干扰碰撞检查。选中并按 F 可找到房间。

| 属性 | 默认值 | 最小合法值 | 含义 |
|---|---:|---:|---|
| RoomLength | 2000 | 100 | 沿局部 X 轴的室内净长度 |
| RoomWidth | 2000 | 100 | 沿局部 Y 轴的室内净宽度 |
| RoomHeight | 400 | 100 | 地面到墙顶的高度 |
| WallThickness | 20 | 1 | 向室内边界之外延伸的墙厚 |
| FloorThickness | 20 | 1 | 向地面下方延伸的地板厚度 |

Actor 的 XY 中心是局部原点，地板完成面为局部 Z=0；北=+X、南=-X、东=+Y、西=-Y。尺寸是净尺寸，地板的外部占地为 `(RoomLength + 2×WallThickness) × (RoomWidth + 2×WallThickness)`。

通过尺寸属性编辑，保持 Actor Scale 为 `1,1,1`。外壳有一个根组件和五个固定 Static Mesh Component；只更新这些组件的相对变换，不创建新组件、不使用 Tick。网格来自引擎 Cube，每个外壳组件使用 `BlockAll` 和 `QueryAndPhysics`。

## 手工内容

在 `Roguelite / Manual POIs` 的 `Manual POI Actors` 中加入关卡内的手工 Actor。列表只是引用，不会自动附加、删除、移动、缩放或更改这些 Actor 的模型与组件配置。

手工 POI 保持独立放置；不要把它附加到 Floor 或四面墙组件上，否则会继承这些外壳组件的缩放和位移。本轮验证使用独立的 `P01_ManualPOI`。

缩小房间导致手工 Actor 越界时，`Validation Warnings`、`Out Of Bounds Manual POI Actors` 和 Output Log 的 `P01_POI_OUT_OF_BOUNDS` 会给出 Actor 名称、路径及实际 Bounds。Map Check 也会显示可定位到房间的警告。检查包含不参与碰撞的组件和子 Actor；并非只检查原点。

手工 Actor 的世界 AABB 转到房间局部空间后作保守检查。对于规范中的 90° 倍数房间旋转，边界对应准确；任意斜向旋转可能产生保守的越界提示。移动手工 POI 后，可在房间 Details 点击 `Rebuild Room` 重新检查；参数变更会自动检查。

## 人工补验步骤

1. 打开测试地图，在 Outliner 选择 `P01_ParametricRoom` 并按 F 对焦；确认 Actor Scale 为 1，查看地板和四面墙。
2. 将长宽改为 2000/2000，再改为 3000/2500；只改 RoomHeight，观察四面墙更新、地板完成面仍为 Z=0。无需进入 PIE。
3. 记录 `P01_ManualPOI` 的位置、旋转、Scale 和模型；来回改尺寸，确认它不变，房间始终只有五个网格组件。
4. 改为 2000/2000，测试 POI 的原点仍在室内，但部分 Bounds 越过北墙；确认越界提示。扩大房间后提示应消失。
5. 保存地图并重新打开，确认参数和手工 POI 保留。可进入 PIE 观察基础场景，但 SpectatorPawn 的穿墙表现不能用于碰撞验收；碰撞以本轮实际射线记录为证。

## 恢复范围

本轮备份与校验记录在 `Development/P01`。恢复前正常关闭本工程、保留本轮之后的新改动，再针对备份清单逐项处理测试地图、README 和执行状态文档；新增房间源文件、说明及验证脚本可单独归档停用。不要整目录撤销 P00.5 插件，不覆盖 `.uproject`，不使用全项目 Reset/Clean。

UE 运行与编译仍共享全局配置和构建缓存；本轮不会修复既有 GameFeatureData 配置错误。正式验证结果以 `Development/P01` 中本轮实际生成的报告为准。

本轮交付报告见 `P01_RESULT.md`；编译成功，107 项 MCP 自动检查通过，用户重启后的编辑器再次读到了保存参数，并取得了实际视口截图。人工 Details 操作、Undo/Redo、碰撞 Pawn 行走与打包未验收。
