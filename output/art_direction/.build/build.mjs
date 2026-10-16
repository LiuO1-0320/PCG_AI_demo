import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {Presentation, PresentationFile} from '@oai/artifact-tool';
import {GlobalFonts} from '@napi-rs/canvas';

process.env.PYTHONUTF8='1';
process.env.RUNTIME_NODE_MODULES='C:/Users/22475/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const ROOT='D:/ue5project/PCG_AI_demo/output/art_direction';
const SKILL='C:/Users/22475/.codex/plugins/cache/openai-primary-runtime/presentations/26.1011.11113/skills/presentations';
const PYTHON='C:/Users/22475/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe';
const FONT='Microsoft YaHei';
GlobalFonts.registerFromPath('C:/Windows/Fonts/msyh.ttc',FONT);
GlobalFonts.registerFromPath('C:/Windows/Fonts/msyhbd.ttc',FONT);
const prompts=JSON.parse(await fs.readFile(path.join(ROOT,'assets/generation-prompts.json'),'utf8')).prompts;
const themes=[
 {id:'clock',name:'停摆的机关王城',title:'停摆的\n机关王城',english:'THE CLOCKWORK CITADEL',bg:'#111A23',accent:'#DBB071',muted:'#A7B5BD',subtitle:'石砌古堡，黄铜机械',
  story:'永昼之心停止后，王城仍不断重组。机械守卫清除所有未登记人员。\n\n玩家是一名身份被抹去的维修骑士，回城寻找启动钥匙。',
  hook:'维修记录写着：\n“如果你再次抵达这里，\n请不要启动它。”\n落款是玩家自己的名字。',
  zones:[['探索区','军械库与维修街巷'],['藏宝洞','封存档案与旧零件仓'],['Boss 场地','巨型钟楼检修大厅']],
  colors:[['灰蓝石材','#343D49'],['旧黄铜','#A67C45'],['青蓝能源','#54C7CC'],['琥珀奖励','#E8AF54']],
  rows:[['P0\n探索区','石墙、门框、尖拱\n铜管、栏杆、格栅','灰蓝石砖、旧黄铜\n油污、裂缝、接缝贴花'],['P0\n藏宝洞','档案柜、货架、能源容器\n机械心脏、零件宝箱','磨损金属、旧纸张\n编号、锈迹、发光遮罩'],['P0\nBoss 场地','钟面、齿轮、活塞、配重\n高台、坡道、机械机座','铆钉钢板、格栅材质\n油渍、磨损边缘贴花'],['P0\n角色武器','发条骑士、重甲精英\n铆钉枪、压力表','护甲金属、黄铜枪身\n青蓝核心发光遮罩']],
  keywords:'modular castle\nsteampunk pipes\nclockwork knight\naged brass PBR',
  extension:'P1 氛围扩展：旗帜、仪表、小型蒸汽特效。先用静态配重表现停摆。',
  materials:'核心材质：石砖、黄铜、钢板。奖励用琥珀色，危险预警用红橙色。'},
 {id:'memory',name:'吞噬记忆的古堡',title:'吞噬记忆的\n古堡',english:'THE MEMORY CASTLE',bg:'#1A1520',accent:'#D3A1B4',muted:'#BCB0BE',subtitle:'哥特建筑，失去身份的宫廷',
  story:'古堡吸收人的记忆，房间来自被遗忘的往事。守卫失去姓名，仍执行旧命令。\n\n玩家寻找失踪者的线索，并逐渐发现自己也来过这里。',
  hook:'商店主人不收金币，\n而是询问你的名字。\n某幅肖像的脸，\n正逐渐变成玩家的模样。',
  zones:[['探索区','肖像长廊与贵族旧居'],['藏宝洞','封存记忆的密室'],['Boss 场地','画像王后的王座厅']],
  colors:[['炭黑','#211D28'],['旧石灰岩','#A99D88'],['暗红绒布','#6F263C'],['记忆紫光','#A89BD4']],
  rows:[['P0\n探索区','尖拱、石柱、门框\n画像框、烛台、挂帘','风化石灰岩、红绒布\n灰尘、裂纹、蜡痕贴花'],['P0\n藏宝洞','壁龛、展示柜、石棺\n怀表、吊坠、记忆晶体','旧纸、裂纹玻璃、黑晶\n擦除肖像、紫光遮罩'],['P0\nBoss 场地','王座、大画框、祭坛\n台阶、坡道、石质掩体','雕花石材、磨损镀金\n破损纹章、腐蚀贴花'],['P0\n角色武器','无面守卫、宫廷重甲\n古式卡宾枪、装饰护甲','旧银、黑铁、刺绣布料\n护甲磨损、面部空白']],
  keywords:'gothic modular kit\nportrait frame\nfaceless armor\nvelvet PBR',
  extension:'P1 氛围扩展：漂浮字迹、画像褪色、晶体微光。首版用静态贴图讲故事。',
  materials:'核心材质：石灰岩、暗红绒布、旧金属。紫光只用于记忆与超自然线索。'},
 {id:'sea',name:'沉没的灯塔王城',title:'沉没的\n灯塔王城',english:'THE DROWNED BEACON',bg:'#102129',accent:'#E2B46C',muted:'#A8C1C5',subtitle:'盐蚀石墙，仍在发光的灯塔',
  story:'王城沉入海中，灯塔仍不断召唤归航者。玩家追寻一艘从未返航的船，进入废弃城堡。\n\n残存的守卫沿旧航线巡逻，港口记录藏着失踪者的去向。',
  hook:'灯塔每次转过，\n就照出一条新的航线。\n最后一页航海日志，\n记录着玩家今天的到来。',
  zones:[['探索区','盐蚀回廊与旧码头'],['藏宝洞','打捞仓与航海档案'],['Boss 场地','灯塔透镜与泵房大厅']],
  colors:[['深海蓝','#17333E'],['铜绿','#4D887C'],['盐蚀石灰','#B8B3A4'],['灯笼琥珀','#E7B55A']],
  rows:[['P0\n探索区','石墙、拱门、铁栅窗\n锈蚀步道、栏杆、铜管','盐蚀石材、湿润材质\n水位线、滴痕、锈迹贴花'],['P0\n藏宝洞','木箱、绞盘、绳索\n罗盘、航海箱、灯笼','湿木、绳纤维、旧纸\n铜绿、船号、盐斑贴花'],['P0\nBoss 场地','灯塔透镜、泵机、阀门\n检修高台、坡道、机座','锈铁、铜绿、格栅\n积水、水痕、油渍贴花'],['P0\n角色武器','覆甲水手、重型潜水甲\n鱼叉外形卡宾枪、罗盘','锈蚀金属、湿布\n藤壶附着、磨损遮罩']],
  keywords:'lighthouse machinery\nnautical props\ncorroded copper\nbarnacle decal',
  extension:'P1 氛围扩展：海雾、扫光、窗外海面。首版用静态积水，主路线保持干燥。',
  materials:'核心材质：盐蚀石、铜绿、锈铁。冷色环境中，用暖灯标记可通行路线。'}
];

function text(slide,name,value,x,y,w,h,size,color,bold=false){
 const shape=slide.shapes.add({name,geometry:'textbox',position:{left:x,top:y,width:w,height:h},fill:'none',line:{fill:'none',width:0}});
 shape.text=value;
 shape.text.style={typeface:FONT,fontSize:size,color,bold,wrap:'square',autoFit:'none',insets:0,verticalAlignment:'top'};
 return shape;
}
async function picture(slide,name,x,y,w,h,fit='cover'){
 const bytes=await fs.readFile(path.join(ROOT,'assets',name+'.png'));
 slide.images.add({name,blob:bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength),contentType:'image/png',alt:name+' original AI concept',fit,position:{left:x,top:y,width:w,height:h}});
}
function notes(t,asset){
 return '主题提案，故事与视觉均为原创设计建议，尚未确定为最终游戏设定。\n图片为内置 image_gen 生成的概念图，不是游戏截图，也不是可直接导入的模型或材质。\n生成提示词：'+prompts[asset]+'\n\n素材搜集要求：优先可拼接建筑模块，检查底部枢轴、统一厘米尺度、UV、简单碰撞与可步行面。角色需要骨骼、基础待机/移动/受击动画，武器需要独立可拆部件。PBR贴图至少包括Base Color、Normal、Roughness、AO，金属件另需Metallic，能源或灯具另需Emissive遮罩。建议首版采用2K可平铺材质及独立贴花。按项目使用范围核对授权和来源，保持同一写实或风格化程度。关键词用于检索素材类型，不指向已验证的具体商品。\n\n'+t.extension;
}
async function addTheme(p,t,base,total){
 const a=p.slides.add();a.background.fill=t.bg;
 text(a,'section',`${String(themes.indexOf(t)+1).padStart(2,'0')}  世界观与视觉`,48,30,390,32,22,t.accent);
 text(a,'title',t.title,48,89,390,146,58,'#F2EFE8',true);
 text(a,'subtitle',t.subtitle,48,258,390,46,27,t.accent);
 text(a,'story-label','故事设定',48,331,390,34,25,t.accent,true);
 text(a,'story',t.story,48,375,386,170,24,'#E2E0DC');
 text(a,'hook',t.hook,48,565,390,108,22,t.muted);
 await picture(a,t.id+'_hero',480,76,752,423);
 text(a,'image-caption','AI 概念示意图，展示目标氛围与空间语言',480,508,752,24,17,t.muted);
 for(let i=0;i<3;i++){
   const x=480+i*256;
   text(a,'zone-'+i,t.zones[i][0],x,552,240,32,25,t.accent,true);
   text(a,'zone-note-'+i,t.zones[i][1],x,598,242,34,23,'#E2E0DC');
 }
 for(let i=0;i<4;i++)text(a,'palette-'+i,t.colors[i].join(' '),480+i*190,649,180,27,18,'#C5CED1');
 text(a,'page',`${base+1} / ${total}`,1180,683,60,22,16,t.muted);
 a.speakerNotes.text=notes(t,t.id+'_hero');

 const b=p.slides.add();b.background.fill=t.bg;
 text(b,'title',`${t.name}：素材清单`,48,46,1174,64,44,'#F2EFE8',true);
 text(b,'priority','P0 首版必需，先完成可拼接建筑、基础敌人和一把武器',48,121,1174,35,25,t.accent);
 const vals=[['用途','需要找的模型','需要找的贴图 / 材质'],...t.rows];
 const table=b.tables.add({rows:5,columns:3,left:48,top:174,width:842,height:410,columnWidths:[112,365,365],values:vals});
 table.styleOptions={headerRow:false,bandedRows:false};
 table.borders.assign({style:'solid',fill:'#52616A',width:.5});
 for(let r=0;r<5;r++){
   table.rows[r].height=r===0?50:90;
   for(let c=0;c<3;c++){
     const cell=table.getCell(r,c);
     cell.fill=r===0?t.bg:'#1C2B34';
     if(t.id==='memory' && r>0)cell.fill='#2B2230';
     cell.text.style={typeface:FONT,fontSize:r===0?24:23,color:r===0?t.accent:'#EBE8E1',bold:r===0,wrap:'square',autoFit:'none',verticalAlignment:'middle',insets:{left:12,right:10,top:10,bottom:10}};
   }
 }
 await picture(b,t.id+'_assets',918,174,314,177,'contain');
 text(b,'asset-caption','形状与材质参考（AI 概念图）',918,363,314,28,18,t.muted);
 text(b,'keyword-title','检索关键词',918,413,314,32,25,t.accent,true);
 text(b,'keywords',t.keywords,918,456,314,122,23,'#E2E0DC');
 text(b,'extension',t.extension,48,605,1184,37,23,t.accent);
 text(b,'requirements','统一要求：模块有简单碰撞，角色带骨骼；PBR 需 Base Color、Normal、Roughness、AO，金属件补 Metallic。',48,651,1160,54,21,'#D3D8D8');
 text(b,'page',`${base+2} / ${total}`,1180,686,60,22,16,t.muted);
 b.speakerNotes.text=notes(t,t.id+'_assets')+'\n配色：'+t.colors.map(x=>x.join(' ')).join(', ')+'\n'+t.materials;
}

const {finalizePresentation}=await import(pathToFileURL(path.join(SKILL,'container_tools/artifact_tool_utils.mjs')).href);
async function finish(p,name,tableSlides,render){
 const candidate=path.join(ROOT,'.build',name+'.v2.draft.pptx');
 await(await PresentationFile.exportPptx(p)).save(candidate);
 if(render){
   for(let i=0;i<p.slides.items.length;i++){
     const image=await p.export({slide:p.slides.items[i],format:'png',scale:1.5});
     await fs.writeFile(path.join(ROOT,'.build',`slide-${i+1}.png`),new Uint8Array(await image.arrayBuffer()));
   }
 }
 const result=await finalizePresentation({workspaceDir:ROOT,candidatePath:candidate,finalPath:path.join(ROOT,'presentations',name+'.pptx'),pythonExecutable:PYTHON,integrityValidatorPath:path.join(SKILL,'container_tools/inspect_presentation_package_integrity.py'),layoutValidatorPath:path.join(SKILL,'container_tools/inspect_presentation_layout_geometry.py'),layoutArgs:['--expected-slide-size-emu','12192000,6858000','--validate-heading-fit',...tableSlides.flatMap(n=>['--require-native-table-slide',String(n)])],explicitTotalSlideCount:p.slides.items.length,requiredNativeTableOwnerSlides:tableSlides,fontPolicy:{basis:'design',families:[FONT]},verifyArtifactToolImport:true,receiptPath:path.join(ROOT,'.build',name+'.validation.json')});
 console.log(JSON.stringify({name,result}));
}

await fs.mkdir(path.join(ROOT,'presentations'),{recursive:true});
const all=Presentation.create({slideSize:{width:1280,height:720}});
for(let i=0;i<themes.length;i++)await addTheme(all,themes[i],i*2,6);
await finish(all,'三种主题_视觉与素材总览',[2,4,6],true);
for(const t of themes){const p=Presentation.create({slideSize:{width:1280,height:720}});await addTheme(p,t,0,2);await finish(p,t.name+'_视觉与素材',[2],false);}
