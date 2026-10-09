from pathlib import Path
from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak, KeepTogether

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'output' / 'pdf' / 'first_floor_pcg_design.pdf'
OUT.parent.mkdir(parents=True, exist_ok=True)
pdfmetrics.registerFont(TTFont('SimHei', r'C:\Windows\Fonts\simhei.ttf'))
teal = colors.HexColor('#117F84')
dark = colors.HexColor('#173049')
pale = colors.HexColor('#E9F4F4')
styles = getSampleStyleSheet()
styles.add(ParagraphStyle(name='TitleCN', fontName='SimHei', fontSize=21, leading=30, textColor=dark, spaceAfter=12))
styles.add(ParagraphStyle(name='H1CN', fontName='SimHei', fontSize=12, leading=18, textColor=teal, spaceBefore=13, spaceAfter=7))
styles.add(ParagraphStyle(name='BodyCN', fontName='SimHei', fontSize=9, leading=16, textColor=dark, spaceAfter=6))
styles.add(ParagraphStyle(name='CellCN', fontName='SimHei', fontSize=8.2, leading=14, textColor=dark))
styles.add(ParagraphStyle(name='CellHeadCN', fontName='SimHei', fontSize=8.2, leading=14, textColor=colors.white))
styles.add(ParagraphStyle(name='SmallCN', fontName='SimHei', fontSize=7.2, leading=11, textColor=colors.HexColor('#5D7480')))

def P(text, style='BodyCN'):
    return Paragraph(text, styles[style])

def section(title):
    return P(title, 'H1CN')

def table(rows, widths):
    data = [[P(str(cell), 'CellHeadCN' if index == 0 else 'CellCN') for cell in row]
            for index, row in enumerate(rows)]
    t = Table(data, colWidths=widths, hAlign='LEFT', repeatRows=1)
    t.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,0), teal),
        ('TEXTCOLOR', (0,0), (-1,0), colors.white),
        ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.white, pale]),
        ('VALIGN', (0,0), (-1,-1), 'TOP'),
        ('LEFTPADDING', (0,0), (-1,-1), 9), ('RIGHTPADDING', (0,0), (-1,-1), 9),
        ('TOPPADDING', (0,0), (-1,-1), 7), ('BOTTOMPADDING', (0,0), (-1,-1), 7),
        ('LINEBELOW', (0,-1), (-1,-1), .4, colors.HexColor('#B8D7D9')),
    ]))
    return t

def page(canvas, doc):
    canvas.saveState()
    canvas.setFont('SimHei', 7)
    canvas.setFillColor(colors.HexColor('#6A8491'))
    canvas.drawString(19*mm, 12*mm, 'THE HOLLOW CITADEL  /  第一层地图 PCG 策划 v2.0')
    canvas.drawRightString(191*mm, 12*mm, str(doc.page))
    canvas.setStrokeColor(colors.HexColor('#CBDCDD'))
    canvas.line(19*mm, 16*mm, 191*mm, 16*mm)
    canvas.restoreState()

doc = SimpleDocTemplate(str(OUT), pagesize=(210*mm, 297*mm), rightMargin=19*mm,
                        leftMargin=19*mm, topMargin=20*mm, bottomMargin=23*mm)
story = [Spacer(1, 8*mm), P('第一层节点地图 PCG 系统', 'TitleCN'),
         P('游戏策划与开发验收基线 | v2.0 | 2026-10-09', 'SmallCN'),
         section('1. 目标与范围'),
         P('将现有“四套七房路线”的第一层改为可复现的平面节点图。玩家从入口出发，在真实 3D 房间与通道中选择路线，逐步了解邻近房间，最终抵达普通出口。第一层不设置 Boss 房、藏宝洞或提前跳层房。'),
         P('新增开局主界面：玩家可输入正整数种子或选择随机种子，再开始游戏。相同种子在同一版本下应得到相同的地图拓扑、房间类型与初始奖励。死亡或通关后提供同种子重试与随机新局。'),
         P('本版不加入行动力、地图点击传送或跨房间跳跃。玩家通过第一人称移动与房门探索。课程所需的另两套 PCG 与两类敌对 AI NPC 属于后续阶段。'),
         section('2. 参考机制与本作转化'),
         table([
             ['参考要点', '本作第一层的处理'],
             ['平面地图、多路线', '4×4 候选格上生成 10–12 个实际房间节点，至少两条入口到出口的可行路线。'],
             ['未知节点', '小地图先显示节点和道路；玩家抵达一个房间后，揭示该房间及相邻房间类型。'],
             ['收益与风险', '主路保证战斗、商店与休整；支路放宝箱、事件、精英或机关，让玩家选择绕行收益。'],
             ['行动力与道具移动', '本版不引入。路线选择由实际移动、战斗和资源消耗形成。'],
         ], [35*mm, 137*mm]),
         section('3. 房间配额与玩法'),
         table([
             ['类型', '首层配额', '用途'],
             ['入口 / 出口', '各 1', '起点与普通换层出口；不要求首层 Boss 战。'],
             ['普通战斗', '至少 3', '获取金币、弹药与战斗奖励。'],
             ['商店 / 休整', '各 1', '购置强化；一次性恢复生命、护盾和备用弹药。'],
             ['宝箱', '1', '支路奖励：金币、弹药、遗物和武器强化。'],
             ['事件 / 精英 / 机关', '1–2', '从不同风险类型中按种子抽取；精英最多 1 间。'],
         ], [37*mm, 22*mm, 113*mm]),
         PageBreak()]

story += [section('4. 节点图生成规则'),
          P('先用独立的布局随机流，在 4×4 格中生成从左下入口到右上出口的 7 节点保证路线，再从邻接格扩展 3–5 个支路节点。只连接物理上相邻的格子；加入 1–2 条可选连接，形成能影响入口至出口走法的环路。总活跃节点数为 10–12。'),
          table([
              ['阶段', '约束与输出'],
              ['A. 拓扑', '至少两条入口到出口的简单路径；全部活跃节点可达；门双向匹配；不出现斜向通道。'],
              ['B. 内容', '保证路线依次含入口、战斗、战斗、商店、战斗、休整、出口。支路至少有宝箱，并按种子放入特殊房或额外战斗房。'],
              ['C. 空间', '房间中心间距 2400 cm；只有活跃节点生成地板、墙、门、通道和玩法实体。'],
              ['D. 校验', '生成后检查节点数、连通性、替代路线、类型配额、出口位置和导航；不合格则用同种子的下一次候选重试，设确定性回退图。'],
          ], [30*mm, 142*mm]),
          section('5. 种子与复现'),
          P('主界面允许输入 1–2147483647 的十进制整数；空输入等同“随机种子”。无效字符、0 或越界值提示错误，不进入游戏。界面显示最终采用的种子。'),
          P('生成器把 RunSeed、楼层编号与固定用途常量混合，分别得到布局、类型、奖励三个随机流。新增装饰或奖励时不应改变既有拓扑。调试命令行 -CastleSeed=数值 仍可跳过主界面直接进入游戏，以支持自动验证。'),
          P('同种子重试清空本局状态并再次使用原种子；随机新局重新取种子。存档与跨版本完全一致性不在本版范围。'),
          section('6. 地图揭示与 UI'),
          P('地图开始时入口已知；进入某房间时标记“已访问”，并揭示与它直接相连的房间类型。其他活跃节点显示“？”；已知道路始终可见。HUD 显示当前种子、当前位置与出口方向。菜单采用可输入种子的文本框及明确的开始按钮。'),
          P('物理通道保持自由行走，地图不提供点击传送，也不引入行动力。进入房间的判定以房间中心范围为准；跨楼层时重置揭示状态。'),
          PageBreak()]

story += [section('7. 实施拆分'),
          table([
              ['模块', '具体改动'],
              ['主界面', '游戏启动先显示种子输入、随机按钮与开始按钮；通过运行状态把选定种子交给 Director。'],
              ['PCG 节点', '替换首层四模板拓扑；动态输出活跃房间的 RoomType、Region、DoorMask、RouteOrder 等点元数据。'],
              ['空间与 HUD', '按 4×4 活跃节点摆放真实房间，动态缩放地图；按访问状态显示 ? / 房间类型。'],
              ['玩法复用', '沿用现有商店、宝箱、休整、事件、敌人、精英、机关与普通出口交互。第二、三层暂沿用原型。'],
          ], [28*mm, 144*mm]),
          section('8. 验收标准'),
          P('① 主界面能接受合法种子、拒绝非法输入；随机开始和同种子重试的种子行为符合预期。'),
          P('② 至少三个固定种子与一个随机种子生成成功；相同固定种子两次的拓扑、房间类型与初始奖励一致。'),
          P('③ 每次首层有 10–12 个活跃房间，所有房间及出口可达，存在至少两条入口到出口的路线；房门与通道对称，导航可抵达各房间。'),
          P('④ 小地图只揭示入口及已访问节点的邻居；实际进入后更新。首层没有 Boss、藏宝洞、提前跳层房。'),
          P('⑤ 商店、宝箱、休整、随机特殊房和普通出口的交互通过自动验证；在本机 UE 5.8.3 上编译并运行。'),
          section('9. 风险与后续'),
          P('多路线可能允许玩家绕开商店或战斗，这属于路线选择；首版通过奖励与敌人配置平衡，不增加强制门锁。节点图与实际 3D 房间共用同一连通数据，避免地图标示与可行走通道不一致。'),
          P('本策划替代 v1.0 的首层固定四路线规则。实际制作中如果调整格网、房间配额或揭示范围，应同步修改本文件和自动验证。'),
          Spacer(1, 8*mm),
          P('参考：《明日方舟》官方“沉沦者的黑流树海”玩法介绍；PRTS 玩家共同构筑的机制资料。借鉴平面探索与未知节点理念，不复制其数值、行动力与地图资产。', 'SmallCN')]

doc.build(story, onFirstPage=page, onLaterPages=page)
print(OUT)
