# CASTLE_ENEMY_ART_PIPELINE_V1-FIX01 验收报告

## 最新验收 · 176 cm、+Y 朝向和原点统一

**尺寸、朝向、原点和单兵验证通过。** 新版参考姿势实测 **175.999980861 cm**，资产局部朝 +Y，沿用现有 Mesh Yaw −90° 时朝 Actor +X。原胶囊和组件单位缩放通过实机检查。**UE 局部法线一致性仍有差异**，本轮保留独立 Mesh 和 Skeleton，不覆盖正式资产。

### 1. 基线与连接

工程为 `E:/Unreal Projects/PCG_AI_demo/testfordemo.uproject`，UE 5.8.4 / CL 58902052，Blender 5.2.3 LTS。读取了工作约定和既有报告。Blender MCP 协议 16 真实读到本轮工作副本。UE 官方 MCP 协议 `2025-11-25` 完成初始化、工具发现、工程核对和实际资产、组件、临时世界读取。

需求列出的 `Grunt01/SK_CastleGrunt` 和原同名 Skeleton 在本轮开始前已不可加载。用户编辑器现有日志记录了旧包被移除，并打开了移到 `Grunt01` 根目录的 +Y 版。本轮以实际存在的 `SK_CastleGrunt_YForward`、同名 Skeleton 和已验的 +Y Blender 工作文件为基线。正式 `BP_CastleEnemy_Grunt` 仍使用 Manny 与 `ABP_Unarmed`。当前正式值读回 Mesh Relative Z −88 cm、Yaw −90°、Scale (1,1,1)，Capsule R36／H88，最大步行速度 270 cm/s。

[UE 只读基线](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_ScaleFix_20261120/UEBaseline.json>) · [Blender 诊断及回读](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_ScaleFix_20261120/BlenderValidation.json>)

### 2. 实测尺寸、坐标与骨架

| 测量项 | 本轮 +Y 基线 | 176 cm 修复版 |
|---|---|---|
| UE 实测参考姿势身高 | 99.951132561 cm | **175.999980861 cm** |
| 资产前方／上方 | +Y／+Z | +Y／+Z |
| Mesh 相对 Yaw −90° 后 | Actor +X | Actor +X |
| 双脚最低 0.5 cm 鞋底带的等权 XY 中点 | (+0.000000834, +0.000002742) cm | (−0.000003219, +0.000004053) cm |
| 脚底最低 Z | −0.000006462 cm | −0.000011379 cm |
| 身体 AABB 中心水平偏移 | (约 0, −1.85543) cm | (约 0, −3.26715) cm，沿用组件 Yaw 后约在 Actor 后方 3.27 cm |
| 骨骼数与父子层级 | 41 | 41，名称和父链相同 |
| UE 组件空间 Root Scale | (100,100,100)，旧 FBX 单位表示 | (1,1,1)，厘米坐标已烘焙 |

原历史 +X 版侧身来自资产前向与默认预览相机方向的差异。实际基线 +Y 版和项目 Manny 都面朝资产局部 +Y。本轮只改变角色大小，保留上一版已修正的朝向和鞋底原点。Manny 同镜头参考姿势实测 180.54390 cm。

按当前 UE 高度计算倍率 **176／99.95113256056811 = 1.760860487432176**。Blender 参考姿势网格与这次实测一致，注册到米制场景的角色高度从约 0.99951137 m 到 1.75999987 m。等比缩放烘入网格顶点、Armature 的 Rest 平移尺寸和 Pose 平移分量，保留名、父链与局部旋转。临时厘米导出场景中，角色独立转换为厘米坐标，FBX 综合输出单位系数为 1、全局 Scale 1，保留 `-Y Forward / Z Up`。UE 的 `Import Uniform Scale=1`，Import Rotation 与 Translation 均为 0，`Convert Scene=true`，`Force Front XAxis=false`，`Convert Scene Unit=false`。独立新 Skeleton，`Update Skeleton Reference Pose=false`，`Use T0 As Ref Pose=false`。没有叠加组件缩放。

| UE 组件空间参考位置 | 基线 XYZ cm | 修复后 XYZ cm |
|---|---|---|
| Root | (0.219804, 0.225578, 约 0) | (0.387046, 0.397216, 约 0) |
| Hip | (0.219810, 0.225578, 56.095040) | (0.387054, 0.397216, 98.775536) |
| Pelvis | (0.219814, 0.225574, 56.095048) | (0.387064, 0.397212, 98.775552) |

全部骨骼组件空间位置按倍率匹配，最大误差 0.000123 cm，四元数分量偏差最大约 0.0000029。保留 Root、Hip、Pelvis 原合法层级和相对姿势。新 `R_Hand` 参考骨骼组件空间 Scale 实测约 (1,1,1)，Mesh 组件最终亦为 (1,1,1)，因此该绑定姿势没有人物尺寸倍率继续传给枪。Weapon_R 是新 Mesh 上重建的同名 Socket，仍由 `R_Hand` 持有，局部零平移、零旋转、单位缩放，已从磁盘冷加载确认。武器包 SHA 一致，长度仍为 78.12505 cm。未在本轮制作、预览持枪动作。

[UE 导入与 Socket 记录](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_ScaleFix_20261120/UEImportValidation.json>) · [独立进程冷加载](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_ScaleFix_20261120/UEColdRead.json>)

### 3. 绑定、变形与法线验收

Blender 保留原材质、UV、顶点组、蒙皮记录及多边形索引。缩放前后施加同样的两侧上臂、前臂与小腿屈曲，六处都有顶点响应。将结果除去等比缩放比较，最大空间残差低于 0.000001 m。Blender 角法线最大方向向量差约 0.000072，骨架局部方向基最大分量差约 0.0000044。

独立读取最终 UE LOD0 数据，13,819 个渲染顶点按缩放后的位置映射，UV、颜色、有效骨骼索引与量化权重、分区骨骼映射全部配对。三角总数仍为 9,304，UE 导入重新排序了索引，22 个三角的局部三角化与基线不同。法线也有局部差异，13,819 个渲染顶点中 60 个相差超过 1°，5 个超过 10°，腰甲同一局部位置约 Z 110.74 cm 的两个渲染顶点最大约 35.6°。共用原材质，参考姿势全身预览与 SIE 未观察到错骨、撕裂或错误的姿势播放。此观察不能替代腰甲高光近景或法线数据一致性检查。官方 SkeletalMesh 导入接口未提供 Normal Import Method。本轮保留独立资产，精确法线一致性暂未验收。 [Epic FBX 导入选项](https://dev.epicgames.com/documentation/unreal-engine/fbx-import-options-reference-in-unreal-engine)

### 4. 原 CastleEnemy Capsule 和模拟验证

在独立进程的未保存 `/Engine/Maps/Entry` 放入原 Grunt 实例，临时替换实例 Mesh，采用无播放序列的 AnimationSingleNode 参考姿势。正式蓝图、实际关卡和 Director 无写入。测试地面用可阻挡立方体，射线回读平台顶部 Z=0。测试用 GameModeBase，未运行地牢 AI。截图的 Capsule 线为该实例的原生碰撞组件。

- **静态对齐** Actor 位于 Z=88，Mesh Relative Z −88。最低脚底与实际地面重合至浮点误差，头顶约 176 cm。R36／H88 的 Capsule 圆柱段至少留有约 9.62 cm 余量。按实际逐顶点几何检查，上圆帽头顶最大约外伸 0.025 cm，下圆帽附近的靴底侧缘最大外伸约 3.17 cm。这是胶囊圆帽的轮廓差，脚底没有穿到地面下方，不需要改变胶囊尺寸。
- **真实 SIE** 同一 Grunt 使用同一新 Mesh 和原 36／88 Capsule，Mesh Scale 与 Capsule Scale 都是 (1,1,1)，`MOVE_Walking` 且最大速度仍为 270 cm/s。SIE 脚底带有 UE 步行约 2.15 cm 的间隙。只在运行实例把视觉 Mesh Relative Z 微调到 −90.149983 cm，按运行时读回变换与实际网格最低点计算脚底 Z=0。未把此数值写回正式蓝图。
- **真实前向位移** 对运行中原 CharacterMovement 注入一次 +X 速度，随后读回 X 位移 **+3.42068 cm**，Y、Z 和 Actor Yaw 不变。此为原移动组件的控制脉冲验证，未测试 AI 追击。保存下来的角色参考姿势和运行实例均以相同骨架、相同材质显示。
- **Manny 对照** 在同一个临时世界、相同地面、相同网格世界轴变换下使用同一相机，位置 (293,0,108) cm，Yaw 180°，Pitch −4°，FOV 90°。两者均正面朝 Actor +X。

[176 cm 原 Capsule 对齐截图](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_ScaleFix_20261120/Grunt-Capsule-176cm-Close.png>) · [Grunt 同镜头](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_ScaleFix_20261120/Grunt-176cm-Compare.png>) · [Manny 同镜头](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_ScaleFix_20261120/Manny-Compare.png>) · [SIE 数值证据](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_ScaleFix_20261120/SIEValidation.json>) · [截图相机数据](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_ScaleFix_20261120/Screenshots.json>)

### 5. 文件、SHA 和回滚

现有证据不足以安全覆盖正式旧资源。新旧的绑定单位表示有明确差异，直接重导至旧 Skeleton 的既有动画兼容性未经验证，局部法线也不完全一致。后续需要先复核腰甲近景或测试保留法线的隔离导入，并将经过验证的兼容动画接入新 Skeleton。正式接入时可用新 Mesh 与独立 Skeleton 对，沿用已测试的 Yaw −90° 及单位 Scale。地面微调以接入场景为准。此次只保存两个新 UE 包。

| 文件 | SHA-256 |
|---|---|
| 当前基线 `Grunt01/SK_CastleGrunt_YForward.uasset` | `E15C8732569FCFD5EAB2208AB4CE8687C0DA917693140AA0EF1C291DE5F03A9D` |
| 当前基线 `Grunt01/SK_CastleGrunt_YForward_Skeleton.uasset` | `AAFA32E651A526DCAE225535EC26DA4C9DA2D14D10AD1F623C268B87D8D1D00C` |
| 新 `/Game/CastleDemo/Enemies/Art/Grunt01/ScaleFixTest/SK_CastleGrunt_Corrected` | `5EFAB4FC435131D3343E0BCE91F57D176361285EDB5C8803614B6EE49AACCAF2` |
| 新 `/Game/CastleDemo/Enemies/Art/Grunt01/ScaleFixTest/SK_CastleGrunt_Corrected_Skeleton` | `E80B7F1FB59802B61EC382C5ACF8262F233632C84E483A94F919B20CED24BBE4` |
| [176 cm Blender 工作副本](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_ScaleFix_20261120/CastleEnemy_UE_Work_ScaleFix.blend>) | `D81142C199F2EEA088D190A4EC61DC6546A8A409EA10E9C79B543F3DF6571D8E` |
| [新 FBX](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_ScaleFix_20261120/FBX/SK_CastleGrunt_Corrected.fbx>) | `C63B3B062BAC346707D5D7D67CE1C2E78EB98CD1DE99A72668BBCC5A91D96186` |
| [原 V1 工作副本备份](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_ScaleFix_20261120/Backups/CastleEnemy_UE_Work_V1_before.blend>) | `90D76F6F2DD2F96101D3BEC887BD777440239C9231DDFB9D6B3E51ABFA8DF2E0` |
| [当前 +Y Blender 工作副本备份](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_ScaleFix_20261120/Backups/CastleEnemy_UE_Work_YForward_before.blend>) | `13B1426390DCC9BB57D44635DF78945AB1C3AB2B666C023E1FA19DF07F5ABF5B` |

核对 27 个保护文件的施工前后 SHA 全部一致。原始 Blender、现有蓝图、武器、碰撞、移动、AI C++、正式地图及配置都在清单内。用户原编辑器保留。没有提交、推送、重置、清理缓存或进行动画重定向。

回滚时直接继续使用当前 +Y 正式基线即可。新包冷加载无正式外部依赖。如果日后增加引用，先用 Reference Viewer 核对引用再移出使用。Blender 恢复时从命名明确的备份创建新副本，保留 176 cm 版以便复核。备份另存到其他位置时，在工作文件中重连同批 `Textures/T_CastleGrunt_BaseColor.jpg`，避免相对路径随文件夹移动失效。

[保护文件 SHA 清单](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_ScaleFix_20261120/ProtectedBeforeSHA256.txt>) · [两包保存日志](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_ScaleFix_20261120/ScaleFix-PIE-MCP.log:2265>)

---


## 朝向订正：与 Quinn／Manny 的局部 +Y 对齐

用户补充的两张默认透视图使用相同的坐标轴显示。图一红色 +X 指向屏幕右侧，骑士也朝右。图二的模板角色正对镜头。验收目标已明确为 **Skeletal Mesh 资产局部 +Y**。下方保留此前独立候选的原点修复记录。

从已居中的候选出发，将角色和骨架整体绕 UE Z 轴旋转 +90°（Blender Z 轴 −90°）。全新打开 UE 默认 Perspective，+Y 版骑士与 Quinn 都正对镜头。保存后的网格中，两侧 Foot→ToeBase 的前向 Y 分量为 +2.8890 cm 与 +3.1073 cm，鞋底中点 XY 距原点小于 0.000003 cm。脚底高度、尺寸、41 根骨骼层级、13,819 个顶点的量化蒙皮、UV 和 27,912 个索引通过对照。Mesh Socket `Weapon_R` 仍在 `R_Hand`。

[修复前默认透视](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX02_YForward_20261120/Grunt-Before-DefaultPerspective.png>) · [修复后默认透视](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX02_YForward_20261120/Grunt-YForward-DefaultPerspective.png>) · [Quinn 同视角](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX02_YForward_20261120/Quinn-DefaultPerspective.png>)

- Mesh `/Game/CastleDemo/Enemies/Art/Grunt01/FIX02_YForward/SK_CastleGrunt_YForward`，SHA-256 `486AE643BDBB286183B35E4FB229E0D36752DD4192FB74EE6061142AD221BFE9`。
- Skeleton `/Game/CastleDemo/Enemies/Art/Grunt01/FIX02_YForward/SK_CastleGrunt_YForward_Skeleton`，SHA-256 `B4FBC43B27FCC7F45E6E83A969750EDEA7DD1848E3F1FFBF318F56B6C995AF9F`。
- [Blender 工作副本](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX02_YForward_20261120/CastleEnemy_UE_Work_YForward.blend>)，SHA-256 `13B1426390DCC9BB57D44635DF78945AB1C3AB2B666C023E1FA19DF07F5ABF5B`。
- [修复 FBX](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX02_YForward_20261120/FBX/SK_CastleGrunt_YForward.fbx>)，SHA-256 `8D0BD6FE1C2AD3063CDA857082095ED07892FAFABCC6231347B6B5279977A7FD`。
- [原始验证记录](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX02_YForward_20261120/Verification.json>)。正式网格、Skeleton、蓝图和武器按原 SHA 保留。共享材质沿用用户编辑器在此次导入前已保存的版本，本轮只写入两个新包。恢复时直接使用此前的资产即可。


2026-11-20 · `E:/Unreal Projects/PCG_AI_demo/testfordemo.uproject` · UE 5.8.4 / CL 58902052 · Blender 5.2.3 LTS

**验收结论：真实原点偏差已在独立候选资产中修复。** Grunt 的原始网格已朝局部 **+X**，Z 向上。默认透视预览从侧向观察，所以会显示侧身。同一正交 Front 下，Grunt 正面、Manny 侧身，因为项目中的 Manny **原始网格朝 +Y**。本轮只校正 Grunt 双脚的水平原点，保持朝向、尺寸和脚底 Z。正式 Skeletal Mesh 和 Skeleton 保留原文件。

## 1. 同轴截图与预览相机

Blender MCP 完成协议 16 的连接和源工作副本真实读取。官方 Unreal MCP 完成协议 `2025-11-25` 握手、工具发现、工程路径和 `/Engine/Maps/Entry` 世界确认，PIE 未运行。取证用独立编辑器，原用户编辑器会话持续运行。

| UE 截图 | 修复前 | 修复后 | Manny 对照 |
|---|---|---|---|
| Skeletal Mesh 编辑器，参考姿势、**Front 正交** | [原 Grunt](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX01_20261120/Grunt-Before-FrontOrtho.png>) | [候选 Grunt](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX01_20261120/Grunt-After-FrontOrtho.png>) | [Manny](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX01_20261120/Manny-FrontOrtho.png>) |
| 临时世界固定 +X 相机，原点标线，未按资产自动居中 | [原 Grunt](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX01_20261120/Grunt-Before-FrontPlusX.png>) | [候选 Grunt](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX01_20261120/Grunt-After-FrontPlusX.png>) | [Manny](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX01_20261120/Manny-FrontPlusX.png>) |
| 同一斜向相机及地面网格，检查双脚偏差 | [原 Grunt](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX01_20261120/Grunt-Before-FeetOblique.png>) | [候选 Grunt](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX01_20261120/Grunt-After-FeetOblique.png>) | — |

重新打开原 Grunt 的默认透视预览可重现侧身。由图中的轴和角色侧面可判断默认视线大致沿 Y。两个原资产包均未见自定义默认相机属性。官方 MCP 未暴露用户原已打开的资产标签页的即时相机平移和旋转，本轮用重新打开的默认视图和可重复的固定坐标取证。固定 +X 透视相机位置为 `(255,0,75) cm`，Yaw `180°`，FOV `90°`。前后都把网格 Actor 放在 `(0,0,0)`，零旋转、单位缩放，关闭自动落地。Skeletal Mesh 正交截图验证**朝向**，固定世界相机和几何数据验证**原点**。

在 Skeletal Mesh 编辑器左上的视口投影菜单选 **Front**，即可从 +X 方向看 Grunt 正面。Manny 在 Castle Grunt 蓝图原有 Mesh 的相对 Yaw 为 `-90°`，正是因为 Manny 原网格朝 +Y。该蓝图未修改。

[原 Grunt 默认透视截图](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX01_20261120/Grunt-Before-DefaultPreview.png>) · [Manny 默认透视截图](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX01_20261120/Manny-DefaultPreview.png>) · [相机坐标及回读原始记录](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX01_20261120/UEVerification.json>)

## 2. 根因、坐标修正及量化数据

原 FBX 在参考姿势下已经带有水平偏移。UE 真实回读的原资产 Import Translation 和 Rotation 均为 0，Import Uniform Scale 为 1。原 Grunt、Manny 及修复候选均为 `Convert Scene=true`、`Force Front XAxis=false`、`Convert Scene Unit=false`。候选的 `Update Skeleton Reference Pose` 和 `Use T0 As Ref Pose` 均为 false。独立官方 FBX 导入工具没有公开 Translation／Rotation 参数。本轮保持导入设置不变，在 Blender 副本中对 Armature 应用一次整体平移，绑定角色网格继承同一位移。 [Epic FBX 导入选项](https://dev.epicgames.com/documentation/unreal-engine/fbx-import-options-reference-in-unreal-engine)

导出的实际坐标换算为 UE `(100X,-100Y,100Z)`。Blender 中平移约 `(-0.018554872,+0.118163727,0) m`，UE 中对应 `(-1.855487,-11.816373,0) cm`。沿用原 FBX 的 `-Y Forward / Z Up` 与已验证的厘米数值导出。保留 Armature 原有旋转与缩放，单独放置的枪未被平移，原枪数据和对象矩阵通过对照。工作副本显示切为 **Rest Position** 便于检查绑定几何，原姿势通道保留。

从两个已保存 UE 包的实际参考网格顶点计算。每只脚在距最低点 0.5 cm 的鞋底带内取 22 个唯一点。先对每脚取 XY 包围盒中心，再把两脚中心等权平均。

| 指标 | 原 Grunt | 候选 Grunt |
|---|---|---|
| 实际模型前方／上方 | +X／+Z | +X／+Z |
| 双脚中点 XY（cm） | `(+1.855486393,+11.816372514)` | `(-0.000000834,+0.000000209)` |
| 最低脚底 Z（cm） | `-0.000006462` | `-0.000006462` |
| Root 局部平移 XYZ（cm） | `(2.081063509,11.596569061,0.000000507)` | `(0.225576386,-0.219804049,0.000000507)` |

Foot→ToeBase 的水平向量，两脚的前向 X 分量都为正，约 +2.89 cm 和 +3.11 cm，Y 分量接近 0。Manny 的 Foot→Ball 前向主要为 +Y。两组骨骼测量与正交截图一致。Root 的旋转与缩放逐位一致。其原有非零参考姿势及 FBX 单位表示保留；Root 平移随整体原点移动。Hip、Pelvis 与其他非根骨骼的父子局部变换最大分量差约 `2.39e-8`，属于浮点误差。骨骼关节坐标和鞋底几何测量各自独立，Root 最终允许保留小幅偏移。

[Blender 前后测量、姿势和冷读取证](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX01_20261120/BlenderFixAndReadback.json>) · [UE 冷加载读回](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX01_20261120/UEColdRead.json>)

## 3. 骨骼、蒙皮、材质与 Weapon_R

- Blender 对比了 41 根骨骼的父子、Rest 矩阵与 Pose 通道，同一角色网格的顶点、权重、拓扑、UV 和角法线摘要一致。对右前臂和左小腿施加同一组临时测试旋转，移除整体平移后，前后变形的最大残差约 `6.1e-8 m`。诊断姿势未保存。新 Blender 文件及位移已重新打开确认。
- UE 新 Mesh 和新 Skeleton 的 41 根骨骼名称、顺序及父子层级均与原版相同。LOD0 的 9,304 个三角形及 27,912 个索引相同。13,819 个渲染顶点的量化权重、骨骼索引、分区骨骼映射和 UV 相同。平移对齐后顶点位置最大误差小于 `0.000004 cm`，法线最大分量差约 `5.01e-6`。材质沿用原 `M_CastleGrunt_Atlas`，Skeletal Mesh Front 预览中原贴图正常显示。
- 候选的 `Weapon_R` 已重建为 Mesh Socket，挂于原同名 `R_Hand`，局部位置和旋转均为 0，缩放均为 1。正式旧 Mesh 的同名 Socket 和武器资产保持原样。候选冷加载后确认 Socket、父骨和偏移仍在。本轮只验证挂点属于正确骨骼，没有布置持枪或重定向测试。

## 4. 交付文件、SHA 与回滚

正式旧网格和共享参考不替换。新旧资产的参考姿势包含这次有意的根骨平移，带既有 Skeleton 重导及其引用的下游兼容未验证。候选独立保存，冷加载时只检出候选 Skeleton 对候选网格的内部引用。目录下只有以下两个新 UE 包。

- `/Game/CastleDemo/Enemies/Art/Grunt01/FIX01_Origin/SK_CastleGrunt_OriginFix.SK_CastleGrunt_OriginFix`
- `/Game/CastleDemo/Enemies/Art/Grunt01/FIX01_Origin/SK_CastleGrunt_OriginFix_Skeleton.SK_CastleGrunt_OriginFix_Skeleton`

| 文件 | 原版或修改前 SHA-256 | 本轮结果 SHA-256 |
|---|---|---|
| `C:/Users/12854/Desktop/Untitled.blend` | `8701B625EA9F8F6595DF3863E7AB302BD437EEB090F1A141CD7BA415BD1D7394` | 相同 |
| [V1 Blender 工作副本](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_20261120/CastleEnemy_UE_Work.blend>) | `90D76F6F2DD2F96101D3BEC887BD777440239C9231DDFB9D6B3E51ABFA8DF2E0` | 相同 |
| 正式 `SK_CastleGrunt.uasset` | `A49525C0687E6953BDB3E1ACBFDABDA06E8EDB35F63B3BC6BAA8C1727A784781` | 相同 |
| 正式 `SK_CastleGrunt_Skeleton.uasset` | `52823F98F124945DF9F53A31C7A464316AF5364C1E85210304E0EC504A864D4F` | 相同 |
| [独立修复 Blender](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX01_20261120/CastleEnemy_UE_Work_FIX01.blend>) | 字节复制自 V1 工作副本 | `9BBA4184BC99A7254F669A6ED756046DC6B692D538593C64574C61B3D0DA617C` |
| [独立修复 FBX](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX01_20261120/FBX/SK_CastleGrunt_OriginFix.fbx>) | 新文件 | `D12B6EA9544F86C2C2ECDBD7336518028AB891D2E61B19CF3BC0BC72105B6BD0` |
| `SK_CastleGrunt_OriginFix.uasset` | 新文件 | `284A7313E4614EDA660BF3EF3684D9D6184915E95E61FFF257602D598D4D9998` |
| `SK_CastleGrunt_OriginFix_Skeleton.uasset` | 新文件 | `DBE315527A65BDA52220098F47F010751B82722951333AC15FC59BA9DE5F2B5E` |

原 Grunt 蓝图 SHA 仍为 `2B44897835A5C5BCBF977D4D0AFACBA9480208C825360D3DB391614AA7F2978A`。其余 25 个保护清单文件前后相同，包含原 Blender 文件、原网格和 Skeleton、武器、材质、Manny、正式地图、相关源码与配置。未开启正式地图 PIE、未构建、未创建 IK Rig、未重定向，未改 Capsule、蓝图或 AI。

**回滚** 当前正式文件无需恢复。需要停用候选时，继续使用正式旧 Mesh 和 Skeleton 即可。Blender 可直接打开未修改的 V1 工作副本，或在保留新修复文件的情况下，用独立目录内的 `CastleEnemy_UE_Work_FIX01.blend1` 另存本轮居中校正前的工作状态。若以后给候选增加引用，先用 Reference Viewer 核对，再决定是否归档，避免影响新引用。没有自动删除任何 UE 资产。

[保护文件 SHA 清单](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX01_20261120/ProtectedBeforeSHA256.txt>) · [两个新包保存日志](<E:/Unreal Projects/PCG_AI_demo/Saved/CastleEnemyArtPipelineV1_FIX01_20261120/Orientation-Candidate-MCP.log:2277>)
