# Unreal v2.0 任务进度与版本记录

**初始化：2026-10-11｜所有状态初始为待做；本文不代表已经连接用户工程、已编译或测试通过。**

状态说明：`TODO` 待做；`PLAN` 已审查；`WIP` 实施中；`BUILT` 实际编译过但 PIE 未过；`PASS` 编译/编辑器与人工验收真实通过；`BLOCKED` 阻断。

| 任务 | 本轮目标 | 状态 | Git Commit / Seed | 验收证据 | 备注 |
|---|---|---|---|---|---|
| P00 | 项目与官方 MCP 只读审查 | TODO | - | - | 不修改资产 |
| P01 | 可调长宽高参数化房间 | TODO | - | - | 固定 POI 不缩放 |
| P02 | Door Socket 偏移、直接拼接 | TODO | - | - | 不做走廊 |
| P03 | 主线/分支地牢与多种子回退 | TODO | - | - | Boss 可达 |
| P04 | PCG_RoomCover | TODO | - | - | 开阔+密集、POI 排除 |
| P05 | PCG_EnemySpawn | TODO | - | - | 散布+小群组、入口禁刷 |
| P06 | PCG_RoomProps / Loot Profile | TODO | - | - | 设计师可编辑 |
| P07 | RoomEncounterDirector / 门控/生成同步 | TODO | - | - | Preparation/Combat/Cleared |
| P08 | AI 阵营、生命周期与目标锁定 | TODO | - | - | 最近首次锁定 |
| P09 | 近战 BT 与远程 BT/EQS/武器 | TODO | - | - | EQS 战斗后首次查 |
| P10 | 感染者转化 + Follow/Defend/Rally | TODO | - | - | 不堵门不友伤 |
| P11 | Friendly 倒地、救援与遗弃 | TODO | - | - | 无跨局继承 |
| P12 | Boss 接口/商店/掉落协作 | TODO | - | - | 不改 Boss-owned 资产 |
| P13 | 100 Seed / 打包 / 压力 / 课程演示 | TODO | - | - | 真实指标 |

## 每轮填写记录

```text
日期：
任务 ID：
使用的工程版本 / Git Commit：
代码变更：
UE 资产变更：
官方 MCP 工具证据：
实际编译状态：
人工 PIE 状态：
测试 Seed/房间 ID：
成功截图或日志：
未解决问题：
用户确认可继续下一任务：是/否
```
