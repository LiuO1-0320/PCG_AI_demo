#include "CastleLanguage.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

namespace
{
    bool bLoaded = false;
    bool bChinese = true;
    const TMap<FString, FString>& Translations()
    {
        static const TMap<FString, FString> Values = {
            {TEXT("Vault: collect relics along the four-room path, then leave through the exit portal."),TEXT("藏宝洞：沿四房路线收集藏品，再通过终点传送门离开。")},
            {TEXT("VAULT MAP"),TEXT("藏宝洞地图")},
            {TEXT("ARENA MAP"),TEXT("首领场地地图")},
            {TEXT("IN entrance / C relic / OUT exit"),TEXT("IN 入口 / C 藏品 / OUT 出口")},
            {TEXT("Four rooms; follow the path to OUT"),TEXT("四房贯通，沿路线前往 OUT")},
            {TEXT("Purple: raised decks / Grey: cover"),TEXT("紫色：高台 / 灰色：掩体")},
            {TEXT("Objective: collect relics and reach the vault exit"),TEXT("目标：收集藏品，前往藏宝洞出口")},
            {TEXT("THE HOLLOW CITADEL"), TEXT("空寂古堡")},
            {TEXT("THREE FLOORS  |  NODE EXPEDITION"), TEXT("三层古堡 | 随机探索")},
            {TEXT("Enter a seed to repeat a map, or leave it blank for a random run."), TEXT("输入种子可复现地图，留空则开始随机冒险。")},
            {TEXT("Seed: 1 - 2147483647"), TEXT("种子：1 - 2147483647")},
            {TEXT("RANDOM SEED"), TEXT("随机种子")},
            {TEXT("START GAME"), TEXT("开始游戏")},
            {TEXT("Language: English (switch to 简体中文)"), TEXT("语言：简体中文（切换为 English）")},
            {TEXT("Explore connected rooms. Reveal nearby nodes and find the exit."), TEXT("探索房间，揭示相邻节点，寻找出口。")},
            {TEXT("Random seed selected. Press START GAME."), TEXT("已选择随机种子，点击“开始游戏”。")},
            {TEXT("Enter a whole number from 1 to 2147483647."), TEXT("请输入 1 至 2147483647 之间的整数。")},
            {TEXT("EXPLORATION"), TEXT("探索区域")},
            {TEXT("BOSS ARENA"), TEXT("首领竞技场")},
            {TEXT("TREASURE CAVE"), TEXT("藏宝洞")},
            {TEXT("FLOOR  %d / 3     %s"), TEXT("楼层 %d / 3     %s")},
            {TEXT("SEED %d   |   KILLS %d   |   RELICS %d"), TEXT("种子 %d | 击杀 %d | 藏品 %d")},
            {TEXT("SHIELD  %.0f / %.0f"), TEXT("护盾 %.0f / %.0f")},
            {TEXT("HEALTH  %.0f / %.0f"), TEXT("生命 %.0f / %.0f")},
            {TEXT("RELOADING..."), TEXT("换弹中……")},
            {TEXT("CARBINE Lv.%d    DAMAGE %.0f"), TEXT("卡宾枪 等级 %d    伤害 %.0f")},
            {TEXT("GOLD  %d     R: reload"), TEXT("金币 %d     R：换弹")},
            {TEXT("FLOOR MAP"), TEXT("楼层地图")},
            {TEXT("S shop / F foes / C chest"), TEXT("S 商店 / F 战斗 / C 宝箱")},
            {TEXT("? fog / R rest / EL elite / T trap"), TEXT("? 未知 / R 休息 / EL 精英 / T 机关")},
            {TEXT("Objective: explore rooms; the exit is to the northeast"), TEXT("目标：探索房间，前往东北方向的出口")},
            {TEXT("Objective: use the ASCEND portal"), TEXT("目标：使用上楼传送阵")},
            {TEXT("Objective: defeat the guardian"), TEXT("目标：击败守护首领")},
            {TEXT("Objective: gather relics, find B gate"), TEXT("目标：收集藏品，寻找 B 首领入口")},
            {TEXT("THE IRON WARDEN"), TEXT("钢铁守卫")},
            {TEXT("THE ASH REGENT"), TEXT("灰烬摄政王")},
            {TEXT("THE HOLLOW KING"), TEXT("空寂之王")},
            {TEXT("  /  ENRAGED"), TEXT(" / 狂暴")},
            {TEXT("WASD move  |  Mouse aim / fire  |  E interact  |  SPACE jump  |  F1 help  |  ESC pause"), TEXT("WASD 移动 | 鼠标瞄准/射击 | E 交互 | 空格跳跃 | F1 帮助 | Esc 暂停 | F2 语言")},
            {TEXT("ENTER THE CASTLE"), TEXT("进入古堡")},
            {TEXT("Explore connected rooms. Chests grant gold, ammo and relics."), TEXT("探索相连的房间，宝箱提供金币、弹药和藏品。")},
            {TEXT("Spend gold at S rooms. E altars trade health for attributes."), TEXT("S 商店可消费金币；E 祭坛可用生命交换藏品。")},
            {TEXT("R rests once. OUT leads to floor 2. EL / T guard side loot."), TEXT("R 可休息一次；OUT 通往二层；EL / T 有额外奖励。")},
            {TEXT("V portals lead to vaults. B gates lead to the boss."), TEXT("宝箱房旁的传送门通往藏宝洞；B 入口通往首领。")},
            {TEXT("Shield recharges after 5 seconds without damage."), TEXT("连续 5 秒未受伤后，护盾自动回复。")},
            {TEXT("Clear all 3 floors to win. Fire or press F1 to close."), TEXT("完成三层即可通关。射击或按 F1 关闭帮助。")},
            {TEXT("YOU FELL"), TEXT("你倒下了")},
            {TEXT("THE CITADEL IS YOURS"), TEXT("古堡已被你征服")},
            {TEXT("PAUSED"), TEXT("已暂停")},
            {TEXT("GENERATING CASTLE..."), TEXT("正在生成古堡……")},
            {TEXT("ENTER: random run   |   K: same seed"), TEXT("Enter：随机新局 | K：同种子重试 | F2：语言")},
            {TEXT("ESC: resume"), TEXT("Esc：继续游戏 | F2：切换语言")},
            {TEXT("PCG is building the first-floor node map."), TEXT("正在生成第一层房间与路线。")},
            {TEXT("PCG is building connected rooms, cover and the vault."), TEXT("正在生成房间、掩体、藏宝洞与首领场地。")},
            {TEXT("No reserve ammo. Buy supplies at the shop or collect dropped ammo."), TEXT("备用弹药不足，请在商店购买补给或拾取掉落弹药。")},
            {TEXT("RELIC: Ember Fang | bullet damage +6"), TEXT("藏品：余烬之牙 | 子弹伤害 +6")},
            {TEXT("RELIC: Azure Ward | shield capacity +25"), TEXT("藏品：苍蓝守护 | 护盾上限 +25")},
            {TEXT("RELIC: Heart of Stone | maximum health +25"), TEXT("藏品：磐石之心 | 生命上限 +25")},
            {TEXT("You fell in the castle. Press ENTER to begin a new run."), TEXT("你倒在了古堡中。按 Enter 开始新一局。")},
            {TEXT("E  Open chest: coins, ammo and a relic"), TEXT("E 打开宝箱：金币、弹药与藏品")},
            {TEXT("E  Upgrade weapon + supplies | %d coins"), TEXT("E 升级武器并补给 | %d 金币")},
            {TEXT("E  Make an offering: 15 health for a relic"), TEXT("E 献祭：消耗 15 生命换取藏品")},
            {TEXT("E  Enter the treasure cave"), TEXT("E 进入藏宝洞")},
            {TEXT("E  Return to exploration"), TEXT("E 返回探索区域")},
            {TEXT("E  Enter boss arena (no return until victory)"), TEXT("E 进入首领竞技场（获胜前无法返回）")},
            {TEXT("E  Claim victory"), TEXT("E 完成通关")},
            {TEXT("E  Ascend to the next floor"), TEXT("E 前往下一层")},
            {TEXT("E  Acquire reinforced carbine: +10 damage, +4 magazine"), TEXT("E 拾取强化卡宾枪：伤害 +10，弹匣 +4")},
            {TEXT("E  Rest once: health, shield and reserve ammo"), TEXT("E 休息一次：回复生命、护盾与备用弹药")},
            {TEXT("E  Collect ammo and coins"), TEXT("E 拾取弹药与金币")},
            {TEXT("Not enough coins. Search chests or defeat sentinels."), TEXT("金币不足，请寻找宝箱或击败守卫。")},
            {TEXT("Carbine upgraded: +8 damage, +4 magazine. Health, shield and ammo restored."), TEXT("武器升级：伤害 +8，弹匣 +4；生命、护盾和弹药已补充。")},
            {TEXT("The altar needs more than 15 health."), TEXT("祭坛要求当前生命大于 15。")},
            {TEXT("CASTLE CLEARED | Press ENTER for a new seed."), TEXT("古堡已通关 | 按 Enter 开始随机新局。")},
            {TEXT("Supplies collected: +36 ammo, +8 coins"), TEXT("补给已拾取：弹药 +36，金币 +8")},
            {TEXT("WEAPON ACQUIRED: reinforced carbine | +10 damage, +4 magazine"), TEXT("获得强化卡宾枪 | 伤害 +10，弹匣 +4")},
            {TEXT("RESTED: +35 health, +30 shield, +48 reserve ammo"), TEXT("休息完成：生命 +35，护盾 +30，备用弹药 +48")},
            {TEXT("TRAP TRIGGERED: 15 damage"), TEXT("触发机关：受到 15 点伤害")},
            {TEXT("BOSS SLAM: leave the red circle!"), TEXT("首领重击：立即离开红色圆圈！")},
            {TEXT("Generating floor %d..."), TEXT("正在生成第 %d 层……")},
            {TEXT("Floor 1 | Fight, explore side rooms, then find the ASCEND portal."), TEXT("第一层 | 战斗、探索支路，然后寻找上楼传送阵。")},
            {TEXT("Floor %d | Explore rooms, collect relics, then find the BOSS GATE."), TEXT("第 %d 层 | 探索房间、收集藏品，然后寻找首领入口。")},
            {TEXT("Boss arena sealed. Dodge bolts, use cover, leave red telegraphs."), TEXT("竞技场已封闭。躲避弹体、利用掩体、离开红色预警区域。")},
            {TEXT("Optional vault: rare relics guarded by sentinels. Return through the blue portal."), TEXT("可选藏宝洞：守卫看守着藏品。通过蓝色传送门返回。")},
            {TEXT("BOSS DEFEATED | Use the portal at the arena center."), TEXT("首领已击败 | 使用竞技场中央的传送阵。")},
        };
        return Values;
    }
}

bool CastleIsChinese()
{
    if(!bLoaded)
    {
        GConfig->GetBool(TEXT("CastleDemo.Language"),TEXT("Chinese"),bChinese,GGameUserSettingsIni);
        bLoaded=true;
    }
    return bChinese;
}

void CastleSetChinese(bool Value, bool bSave)
{
    bLoaded=true; bChinese=Value;
    if(bSave)
    {
        GConfig->SetBool(TEXT("CastleDemo.Language"),TEXT("Chinese"),Value,GGameUserSettingsIni);
        GConfig->Flush(false,GGameUserSettingsIni);
    }
}

FString CastleText(const FString& English)
{
    if(CastleIsChinese()) if(const auto* Chinese=Translations().Find(English)) return *Chinese;
    return English;
}

FSlateFontInfo CastleUIFont(float Size)
{
    // This engine-bundled font includes Chinese glyphs and needs no system font install.
    return FSlateFontInfo(FPaths::EngineContentDir()/TEXT("Slate/Fonts/DroidSansFallback.ttf"),Size);
}
