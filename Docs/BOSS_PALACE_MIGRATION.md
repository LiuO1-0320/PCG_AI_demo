# 王座厅 Boss 场景迁移（2026-11-19）

demo 已包含王座厅及其完整资源依赖，运行不需要外部素材工程。

## 试玩

先运行 `Scripts/BuildDemo.ps1` 编译，然后双击项目根目录的 `PlayBossPalace.cmd`。每次启动选取新种子，直接生成第二层并进入 Boss 区。种子也显示在游戏 HUD 中。

指定种子复现：`powershell -ExecutionPolicy Bypass -File Scripts/PlayBossPalace.ps1 -Seed 42`。

正常入口仍为 `/Game/Demo/L_CastleDemo`。第一层无 Boss；第二、三层的 Boss 区加载王座厅。原有战斗、传送、换层、奖励和小地图继续使用 demo 的流程。

## 场景和随机内容

- `/Game/BossPalace/Maps/L_BossPalace` 保存迁入的原始参考地图。
- `/Game/BossPalace/Maps/L_BossPalaceShell` 是移除原生成器、旧 PCG 结果和玩法对象的建筑场景。运行时将其中 860 个建筑、灯光、贴花及特效对象复制至 demo 的 Boss 区，并随区域清理。
- 宝座、肖像、拱顶、主要建筑和原灯光保持参考构图。承托拱顶的结构柱及宝座台阶保持固定。
- 战斗区额外的断柱掩体、侧边高台和配套楼梯参与随机生成。每层有 2–4 个高台，平台高度 160/240/320 cm；楼梯和高台位置联动，保持中央通路。掩体数量继续由 PCG 的 `ArenaCoverCount` 控制。
- 同种子、同楼层使用独立随机流复现布局；新种子改变组合和位置。随机化发生在开始游戏或换层时。仅在编辑器打开静态参考地图不会重新排列构件。
- 楼梯和平台使用石材模型显示，平滑碰撞代理负责行走和动态导航，不渲染方块代理。Boss 和玩家武器仍为 demo 原型外观。

## 文件和 Git

`Content/BossPalace` 保存专用资源；`Content/DarkFantasyCastle` 保存依赖的城堡模型、材质和贴图，共 321 个包，约 1.19 GB（十进制）。三处缺失的旧资源名已在项目内建立相应资产，并补齐间接依赖。

`.gitattributes` 已为 `.uasset`、`.umap` 配置 Git LFS。首次克隆需要安装 Git LFS 并执行 `git lfs pull`。现有 `.gitignore` 排除缓存、Saved、Intermediate 和编译产物。此次未提交或推送 Git。

## 验证

- UE 5.8.4 Editor Development 编译通过。
- 种子 1337、42 的三层回归通过；最终碰撞调整后种子 2026 的三层回归通过，包含高台导航、Boss 激活、击杀与通关。
- UE 资产注册表检查：321 个包，缺失 `/Game` 依赖为 0。可用 `Scripts/VerifyBossPalace.py` 在 UE Python commandlet 中复查。
- 最终入口视角实机离屏渲染已检查，见本目录的 `BossPalace_Preview.png`。
- 仍有原工程 RoguelitePrototype 的 `FRogueliteTemplateMemberState::Mobility` 未初始化启动日志；未修改该插件。尚未验证独立打包发行版本或做性能压测。

原始源码更新或调整固定建筑后，可用 `Scripts/PrepareBossPalace.py` 重新生成 Shell。需要 UE PythonScriptPlugin，仅准备和检查脚本使用 Python；游戏运行不依赖 Python。

## 与 ce9b564 室内城堡分支整合

当前分支为 `work/indoor-castle-art`，保留远端 ce9b564 的探索区、记忆房、开始界面、武器和玩法代码。三个冲突文件已合并：头文件同时保留开始界面预览和 Boss 接口；打包目录取两边合集；引擎查找保留新版版本检查，并兼容本机安装目录和未登记的注册表项。

`CastleCourtScenery.cpp`、`CastleMemoryRoomArt.cpp`、`CastleMenu.cpp`、`Config/DefaultEngine.ini` 和 `.gitignore` 与远端 HEAD 保持一致。未使用备用模型替换探索区。Boss 的全局雾和后处理只在 Boss 区启用，避免改变其他房间的表现。

项目成员提供的 `Fab-Court-Art-ce9b564-team-only.zip` 中 18 个资源文件已导入 demo 的 Content 目录。沿用远端的 Git 忽略规则，`Content/Dungeon` 和 `Content/Demo/CourtArt/Masonry` 不纳入 Git 提交；其他成员克隆后需要将同一 ZIP 的 Content 合入项目。Boss 的 321 个资产包使用 Git LFS。全部运行资源在本机均位于 demo 项目内部，但 Git 仓库不包含这批单独分发的 Fab 资源。

整合后 UE Development 编译通过；导入原版 Fab 素材后的种子 1337 三层检查通过，日志含 `CASTLE_MEMORY_ART_READY`、三层 `CASTLE_COURT_READY` 和 `CASTLE_SMOKE_OK`。扩大后的 UE 依赖检查覆盖 366 个包，缺失 `/Game` 依赖为 0。后续提交前检查无未解决冲突；321 个 LFS 对象均已存在于本地缓存，暂存区无超过 100 MiB 的普通 Git blob。未执行提交、推送或改写远端历史。

最终区域雾效隔离版本再次编译通过；`Scripts/ValidateDemo.ps1 -MenuTest` 通过，包括中英文菜单切换、种子 24680 的三层导航和通关。种子 42 的探索区与 Boss 正面实机截图已检查，武器使用远端新增的模型。321 个本地 LFS 对象的 SHA-256 校验全部通过。Fab 资源维持忽略规则，未上传；其他成员仍需单独安装项目成员 ZIP。

## Boss 地板连续性修复

Shell 中 26 块地砖在最初迁移时缺少网格引用，之后虽然补齐了模型包，已保存的空组件仍未自动恢复。已将这 26 块重新绑定到 `SM_InteriorFloor_2` 并恢复配套石材材质，77 块常规地板模块均有有效网格。原本隐藏的连续碰撞地面现在同时显示底层石材，遮住破损地砖下方的空白；原行走碰撞和随机布局保持不变。

`Scripts/RepairBossPalaceFloor.py` 可单独修复 Shell；`PrepareBossPalace.py` 也已接入修复步骤，避免重新生成时丢失修复。未修改原始参考地图或探索区。UE 修复保存为 0 错误、0 警告；种子 42 三层回归和实际渲染检查通过，预览图已更新。

## 女王角色更新

王座厅现使用 boss1 女王模型、贴图、动画、双炮与半血阶段。以 demo 行为树和导航为基础接入，具体能力与验证见 `QUEEN_BOSS_INTEGRATION.md`；旧文中的方块 Boss 外观描述已由此次角色迁移取代。

## 虚拟阴影队列优化

针对 `[VSM] Non-Nanite Marking Job Queue overflow`，只在 Boss 场景的运行时副本上取消 51 盏点光/矩形补光灯的投影，灯光仍参与照明。78 个平整地砖与底层地面不再投影，仍接收建筑和角色阴影；关键聚光灯、建筑和女王投影保留。未关闭 VSM、未隐藏溢出提示，也未修改探索区。

UE 编译通过。种子 109256265 的离屏实机流程完成，画面已检查，`Saved/Logs/PalaceShadowFix.log` 未出现 VSM 溢出警告，并记录 `CASTLE_PALACE_SHADOW_BUDGET FillLights=51 FlatFloors=78`。本次检查覆盖该种子的截图流程，不代表所有视角及硬件的长期性能压测。
