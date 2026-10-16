import fs from 'node:fs/promises';
import path from 'node:path';
import {createCanvas,GlobalFonts} from '@napi-rs/canvas';
const root='D:/ue5project/PCG_AI_demo/output/demo_pre_2026-10-13';
GlobalFonts.registerFromPath('C:/Windows/Fonts/msyh.ttc','Microsoft YaHei');
GlobalFonts.registerFromPath('C:/Windows/Fonts/msyhbd.ttc','Microsoft YaHei');
const C={ink:'#183444',muted:'#586D7C',line:'#78909C',blue:'#E7F2FA',teal:'#DCF3EE',gold:'#FFF2D6',red:'#FDE6E6',purple:'#EEE7FA',gray:'#EEF1F4',white:'#FFFFFF'};
const escape=s=>String(s).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;');
class Diagram{
 constructor(name,title,subtitle){
  this.name=name;this.canvas=createCanvas(2560,1440);this.ctx=this.canvas.getContext('2d');this.ctx.scale(4/3,4/3);this.parts=[];
  this.rect(0,0,1920,1080,C.white,'none');this.text(title,64,44,1792,54,48,C.ink,true);this.text(subtitle,66,114,1788,44,27,C.muted);
 }
 rect(x,y,w,h,fill=C.blue,stroke=C.line,r=12){
  const k=this.ctx;k.fillStyle=fill;k.strokeStyle=stroke;k.lineWidth=2;k.beginPath();k.roundRect(x,y,w,h,r);k.fill();if(stroke!=='none')k.stroke();
  this.parts.push(`<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${r}" fill="${fill}" stroke="${stroke}" stroke-width="2"/>`);
 }
 text(s,x,y,w,h,size=30,color=C.ink,bold=false,center=false){
  const k=this.ctx;k.font=`${bold?'bold ':''}${size}px "Microsoft YaHei"`;k.fillStyle=color;k.textBaseline='top';
  const lines=[];for(const raw of String(s).split('\n')){let line='';for(const ch of raw){if(k.measureText(line+ch).width>w&&line){lines.push(line);line=ch;}else line+=ch;}lines.push(line);}
  const lh=size*1.27; const used=(lines.length-1)*lh+size; if(used>h+4)throw Error(`Text overflow ${this.name}: ${s} (${used}>${h})`);
  for(let i=0;i<lines.length;i++){const left=center?x+(w-k.measureText(lines[i]).width)/2:x;k.fillText(lines[i],left,y+i*lh);this.parts.push(`<text x="${left}" y="${y+i*lh+size*.87}" font-family="Microsoft YaHei" font-size="${size}" font-weight="${bold?700:400}" fill="${color}">${escape(lines[i])}</text>`);}
 }
 box(s,x,y,w,h,fill=C.blue,size=30){this.rect(x,y,w,h,fill);this.text(s,x+16,y+15,w-32,h-25,size,C.ink,false,true);}
 line(points,color=C.line,width=3,dash=false,arrow=false){
  const k=this.ctx;k.strokeStyle=color;k.lineWidth=width;k.setLineDash(dash?[10,8]:[]);k.beginPath();points.forEach(([x,y],i)=>i?k.lineTo(x,y):k.moveTo(x,y));k.stroke();k.setLineDash([]);
  this.parts.push(`<polyline points="${points.map(p=>p.join(',')).join(' ')}" fill="none" stroke="${color}" stroke-width="${width}"${dash?' stroke-dasharray="10 8"':''}/>`);
  if(arrow){const a=points.at(-2),b=points.at(-1),angle=Math.atan2(b[1]-a[1],b[0]-a[0]);const p1=[b[0]-15*Math.cos(angle-.42),b[1]-15*Math.sin(angle-.42)],p2=[b[0]-15*Math.cos(angle+.42),b[1]-15*Math.sin(angle+.42)];k.beginPath();k.moveTo(b[0],b[1]);k.lineTo(...p1);k.lineTo(...p2);k.closePath();k.fillStyle=color;k.fill();this.parts.push(`<polygon points="${[b,p1,p2].map(p=>p.join(',')).join(' ')}" fill="${color}"/>`);}
 }
 arrow(x1,y1,x2,y2,label=''){const points=x1===x2||y1===y2?[[x1,y1],[x2,y2]]:[[x1,y1],[x1,(y1+y2)/2],[x2,(y1+y2)/2],[x2,y2]];this.line(points,C.line,3,false,true);if(label)this.text(label,(x1+x2)/2+12,(y1+y2)/2-20,240,35,24,C.muted);}
 note(title,body,x,y,w=610){this.text(title,x,y,w,48,34,C.ink,true);this.text(body,x,y+59,w,220,29,C.muted);}
 footer(s){this.text(s,64,1028,1790,34,23,C.muted);}
 async save(){await fs.writeFile(path.join(root,'images',this.name+'.png'),this.canvas.toBuffer('image/png'));await fs.writeFile(path.join(root,'svg',this.name+'.svg'),`<svg xmlns="http://www.w3.org/2000/svg" width="1920" height="1080" viewBox="0 0 1920 1080">${this.parts.join('\n')}</svg>`);console.log(this.name);}
}
function flow(d,steps){for(let i=0;i<steps.length;i++){const y=182+i*132;d.box(steps[i],76,y,1100,104,i===steps.length-1?C.teal:C.blue,30);if(i<steps.length-1)d.arrow(626,y+104,626,y+132);}}

let d=new Diagram('01_pcg_exploration_logic','探索区 PCG：约束拓扑生成','PCG_Exploration：4×4 候选格中生成 10–12 个房间，支持同种子复现');
flow(d,['1  种子＋楼层，分离拓扑随机流、房间类型随机流','2  从格 0 到格 15，每次向东或向北，生成七房主路线','3  从相邻空格扩展 3–5 个节点，每个新房至少连接一个旧房','4  添加最多两条可选连接，形成替代入口到出口路线','5  检查数量和替代路线，最多尝试 64 次，失败用固定回退图','6  分配房间功能，生成实体，输出房间点与门方向等元数据']);
d.note('保证的主路线','入口 → 战斗 → 战斗 → 商店\n→ 战斗 → 休息 → 出口',1250,196,595);
d.note('支路功能','至少一个宝箱房和额外战斗房。\n从事件、精英、机关中选 1–2 种。',1250,423,595);
d.note('连通性与进度','移除一条主路边后，出口仍可达，\n才认定存在替代路线。\n一层普通出口上楼，后两层接 Boss。',1250,651,595);
d.footer('来源：CastleNodeLayout.cpp、CastleGeneration.cpp。算法示意图，非 UE 编辑器 PCG 节点截图。');await d.save();

d=new Diagram('02_pcg_cave_logic','藏宝洞 PCG：四房贯通路径','PCG_TreasureCave：2 列×3 行，四个激活房间，三条连接，入口与出口分离');
flow(d,['1  使用独立的藏宝洞随机流（种子＋楼层＋区域标识）','2  枚举 2×3 网格中所有经过四个不同格子的简单路径','3  随机选择一条路径，仅激活四房并连接相邻节点','4  首房为入口、末房为出口，第二至第四房各放一个宝箱','5  两个中间房各生成 Floor 个守卫，末房设返回传送门','6  创建房间与走廊，输出四个房间点，完成后进入导航检查']);
d.note('无需回头的结构','每个奖励都位于贯通路线。\n入口、出口各一条连接，\n中间两房各两条连接。',1250,196,595);
d.note('资源配置','三个藏品宝箱。\n二层四个守卫，三层六个守卫。\n返回门只在最终房间。',1250,458,595);
d.note('传送与保留','出口返回探索区进入时的位置。\n已领取奖励、探索状态和属性保留。\n只在第二、三层生成。',1250,717,595);
d.footer('来源：CastleRegions.cpp。简单路径不重复格子，没有要求玩家折返领取的支路宝箱。');await d.save();

d=new Diagram('03_pcg_boss_logic','Boss 场地 PCG：随机高台与掩体','PCG_BossArena：一个独立竞技场，随机变化来自高台、坡道和掩体组合');
flow(d,['1  使用独立 Boss 随机流，生成半边长 2250 cm 的场地','2  打乱四个角的位置，随机选择 2–4 个角生成高台','3  高度选 160 / 240 / 320 cm，为每个高台搭建可步行坡道','4  生成默认 10 组基础掩体（可调 4–18），随机选择三种形状','5  放置未激活 Boss 与补给，保留入口到 Boss 的中央通路','6  输出场地中心、高台和掩体点，三个区域完成后恢复操作']);
d.note('掩体形状','矮墙：100 cm。\n立柱：300–440 cm。\nL 形墙：220 cm。',1250,194,595);
d.note('额外资源','每个高台还附带一个掩体和灯光。\n这些不计入基础 10 组掩体。\n坡道和相接平台有重叠，避免缝隙。',1250,456,595);
d.note('区域独立性','三个图使用三个 PCG 组件。\n各图只输出本区域点。\n重生成探索区不清理洞穴或 Boss。',1250,718,595);
d.footer('来源：CastleRegions.cpp、CastleGeneration.cpp。场地不是多个随机房间，是一个具有高低变化的战斗空间。');await d.save();

d=new Diagram('04_exploration_resources','探索区资源示意','示例采用代码中的十二房回退拓扑；支路功能为说明性标注，不代表固定种子结果');
const active=[0,1,2,3,4,5,6,7,10,11,14,15],route=[0,1,2,3,7,11,15];
const type={0:'入口',1:'战斗',2:'战斗',3:'商店',4:'宝箱',5:'战斗',6:'精英',7:'战斗',10:'机关',11:'休息',14:'战斗',15:'出口'};
const fill={入口:C.teal,战斗:C.red,商店:C.gold,宝箱:C.blue,精英:C.gold,机关:C.purple,休息:C.teal,出口:C.teal};
const center=i=>[154+(i%4)*190,294+(3-Math.floor(i/4))*190];
const edges=route.slice(1).map((i,n)=>[route[n],i]).concat([[0,4],[4,5],[5,1],[2,6],[6,10],[10,14],[14,15]]);
for(const [a,b] of edges)d.line([center(a),center(b)],'#7AB5AA',20);
for(let i=0;i<16;i++){const [x,y]=center(i);d.rect(x-79,y-77,158,154,active.includes(i)?fill[type[i]]:C.gray,active.includes(i)?'#587882':'#C6D0D8',10);d.text(active.includes(i)?type[i]:'未激活',x-65,y-24,130,50,31,C.ink,true,true);d.text('格 '+i,x-65,y+35,130,35,23,C.muted,false,true);}
d.note('空间构件','Cube 实例网格组成地板、墙、\n门洞、走廊、柱子和掩体。\n房间半边长 900 cm，格距 2400 cm。',990,196,830);
d.note('功能对象','商店、宝箱、祭坛、休息点、武器拾取。\n普通守卫、精英守卫、机关触发区。\n后两层宝箱房旁设藏宝洞入口。',990,432,830);
d.note('区域衔接','一层出口直接上楼。\n二、三层出口房设 Boss 入口。\n小地图逐步揭示相邻房间类型。',990,670,830);
d.footer('符号表示功能对象，非独立美术模型。图中未画事件房，实际可随机生成事件、精英、机关中的 1–2 种。');await d.save();

d=new Diagram('05_cave_resources','藏宝洞资源示意','种子 1337、第二层日志路径：1 → 3 → 2 → 0。图块关系真实，物件位置为示意');
const caveCenter=i=>[234+(i%2)*300,290+(2-Math.floor(i/2))*260],cr=[1,3,2,0];
for(let j=1;j<cr.length;j++)d.line([caveCenter(cr[j-1]),caveCenter(cr[j])],'#7AB5AA',24);
for(let i=0;i<6;i++){const [x,y]=caveCenter(i),order=cr.indexOf(i),label=order<0?'未激活':order===0?'入口':order===3?'出口':'奖励房 '+order;d.rect(x-113,y-92,226,184,order<0?C.gray:order===0||order===3?C.teal:C.blue,'#78909C',12);d.text(label,x-102,y-68,204,48,31,C.ink,true,true);d.text('格 '+i,x-102,y-12,204,35,24,C.muted,false,true);if(order>0)d.text('宝箱 '+order,x-102,y+38,204,38,26,'#946614',false,true);}
d.note('路径与奖励','房间 1：进入藏宝洞。\n房间 3 / 2：守卫＋宝箱。\n房间 0：第三个宝箱＋返回出口。',940,194,870);
d.note('生成资源','四个房间壳、三段走廊。\n三个宝箱、一个返回门。\n二层每个中间房两个守卫。\n中间房各一组掩体，房间配冷色灯光。',940,433,870);
d.note('空间参数','半边长 800 cm，图块中心距 2100 cm。\n未激活图块不生成实体。\n房间点的 RouteOrder 为 0–3。',940,720,870);
d.footer('依据：CastleRegions1337.log 与 CastleRegions.cpp。宝箱与角色是占位几何体，非已经完成的主题美术资产。');await d.save();

d=new Diagram('06_boss_resources','Boss 场地资源示意','三高台平面示例，用于解释空间构件；不是指定种子的逐物件实测布局');
const bx=80,by=220,side=760,scale=side/4500,cx=bx+side/2,cy=by+side/2;
const proj=(x,y)=>[cx+x*scale,cy-y*scale];d.rect(bx,by,side,side,'#F5F8FB','#69818F',0);
const platforms=[[-1,-1,160],[-1,1,240],[1,1,320]];
for(const [sx,sy,z] of platforms){const [x,y]=proj(sx*1450,sy*1250),[rx,ry]=proj(sx*625,sy*1250);d.rect(rx-950*scale/2,ry-600*scale/2,950*scale,600*scale,C.gold,'#B59759',0);d.rect(x-700*scale/2,y-850*scale/2,700*scale,850*scale,C.purple,'#9280AD',0);d.text('高台\n'+z+' cm',x-57,y-36,114,77,26,C.ink,true,true);}
for(let i=0;i<10;i++){const [x,y]=proj(-1500+(i%6)*600,(i%2?1:-1)*(570+Math.floor(i/6)*130));d.rect(x-17,y-17,34,34,'#7C8D98','#52616B',0);}
d.line([proj(-1850,0),proj(800,0)],'#31977E',5,true);const [px,py]=proj(-1700,0),[ex,ey]=proj(400,0);d.box('进入',px-52,py-26,104,59,C.teal,25);d.box('Boss',ex-57,ey-26,114,59,C.red,25);
d.note('资源组件','一个 4500×4500 cm 场地。\n高台、坡道、柱形 / 矮墙 / L 形掩体。\n一个 Boss、一个补给点和灯光。',960,191,860);
d.note('可走高低空间','紫色是平台，米黄色是坡道。\n绿色虚线表示预留的中央通路。\n平台最高 320 cm，需导航可达。',960,441,860);
d.note('战斗资源','球形弹体、地面预警圆盘、Boss 血条。\n击败后出现上楼 / 通关传送阵。\n地面技沿玩家所在表面设置高度。',960,692,860);
d.footer('来源：CastleRegions.cpp、CastleGameplay.cpp。默认 10 组基础掩体，另加平台上的掩体。');await d.save();

d=new Diagram('07_guard_behavior_tree','普通守卫行为逻辑树','对应 ACastleEnemy::Tick；精英使用相同逻辑。当前没有 UE Behavior Tree / Blackboard 资产');
d.box('逻辑循环：普通守卫 / 精英',665,170,590,74,C.teal,33);
d.box('运行条件：玩家有效、NPC 存活、导演有效、关卡生成完成',335,293,1250,86,C.blue,31);d.arrow(960,244,960,293);
d.box('条件不满足\n跳过本帧',55,291,250,104,C.gray,28);d.arrow(335,334,305,334);
d.box('未激活、玩家死亡\n或已通关：停止移动',1605,291,270,112,C.gray,26);d.arrow(1585,334,1605,334);
d.box('新决策范围：NPC 距玩家 ≤2100 cm，玩家距 NPC 出生点 ≤2600 cm',355,425,1210,92,C.blue,29);d.arrow(960,379,960,425);
d.text('范围外仅跳过本帧决策，不含主动返回出生点',370,531,1180,38,25,C.muted,false,true);
d.box('同一帧独立检查移动和攻击，两个分支可同时执行',510,586,900,76,C.teal,30);d.arrow(960,568,960,586);
d.box('移动分支：每 0.45 秒重新判定',80,713,800,77,C.blue,30);d.arrow(760,662,480,713);
d.box('攻击分支：有视线且冷却就绪，每 1.9 秒一次',1000,713,840,77,C.blue,29);d.arrow(1160,662,1420,713);
d.box('距离 >450 cm 或视线受阻\nMoveToActor，接近至 320 cm',65,866,435,112,C.gold,28);d.arrow(360,790,282,866);
d.box('其余情况\nStopMovement',535,866,345,112,C.gray,29);d.arrow(640,790,708,866);
d.box('距玩家 <140 cm\n近战造成伤害',1000,866,390,112,C.red,29);d.arrow(1290,790,1195,866);
d.box('其余距离：发 1 枚弹体\n速度 850 cm/s',1450,866,390,112,C.red,28);d.arrow(1560,790,1645,866);
d.footer('AIController + 动态 Recast 导航 + 射线视线检测。此图抽象实际 C++ 行为，不声称使用 UE 行为树。');await d.save();

d=new Diagram('08_boss_behavior_tree','Boss 行为逻辑树','对应 ACastleEnemy::Tick 的 Boss 分支。移动、普通攻击、地面技能可在同帧分别检查');
d.box('逻辑循环：Boss（bBoss=true）',675,168,570,70,C.teal,32);
d.box('NPC 存活、玩家有效、关卡就绪；Boss 已激活、玩家存活且未通关',315,278,1280,88,C.blue,29);d.arrow(960,238,960,278);
d.box('未激活 / 玩家死亡\n或通关：停止移动',1625,278,260,110,C.gray,26);d.arrow(1595,322,1625,322);
d.box('先结算已到期地面技：范围 400 cm，伤害 ×2.4\n脚底高度差 <160 cm 时命中，关闭预警',470,400,980,104,C.red,29);d.arrow(960,366,960,400);
d.box('后续新决策范围：距玩家 ≤3600 cm',630,547,660,72,C.blue,29);d.arrow(960,504,960,547);
d.text('范围外跳过新决策；已蓄力技能的到期结算在范围检查之前',70,538,510,75,25,C.muted);
d.box('移动：每 0.45 秒判定\n距离 >650 cm 或视线受阻？',55,692,530,107,C.blue,28);d.arrow(725,619,320,692);
d.box('常规攻击：有视线且冷却到期\n近战 / 扇形弹幕随距离选择',665,692,590,107,C.blue,28);d.arrow(960,619,960,692);
d.box('地面技：每 6 秒可触发\n当前未蓄力，无需视线',1350,692,525,107,C.blue,28);d.arrow(1200,619,1613,692);
d.box('是：MoveToActor\n接近至 500 cm',55,859,255,105,C.gold,24);d.arrow(220,799,183,859);
d.box('否：停止移动',350,859,235,105,C.gray,27);d.arrow(420,799,467,859);
d.box('HP ≥50%\n间隔 1.35 秒\n远程 3 枚弹体',655,851,285,126,C.red,28);d.arrow(845,799,798,851);
d.box('HP <50%（狂暴）\n间隔 0.85 秒\n远程 5 枚弹体',985,851,290,126,C.red,27);d.arrow(1070,799,1130,851);
d.box('射线找玩家脚下表面\n显示红圈预警\n等待 1.4 秒后结算',1350,843,525,132,C.purple,28);d.arrow(1613,799,1613,843);
d.text('距离 <140 cm 用近战；远程弹体速度 1000 cm/s',675,986,640,33,23,C.muted);
d.footer('地面技能冷却从触发时计算。Boss 初始未激活，进入竞技场后激活；死亡奖励 80 金币并生成上楼 / 通关门。');await d.save();

await fs.writeFile(path.join(root,'.build','image-manifest.json'),JSON.stringify({size:'2560x1440',svgSize:'1920x1080',font:'Microsoft YaHei',images:await fs.readdir(path.join(root,'images'))},null,2));
