const fs=require('fs');
const path=require('path');
const {createCanvas,loadImage,GlobalFonts}=require('C:/Users/22475/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/@napi-rs/canvas');
GlobalFonts.registerFromPath('C:/Windows/Fonts/msyh.ttc','Microsoft YaHei');
GlobalFonts.registerFromPath('C:/Windows/Fonts/msyhbd.ttc','Microsoft YaHei');
const root='D:/ue5project/PCG_AI_demo/output/ai_tasks_en_2026-10-14';
const W=4096,H=4096,c=createCanvas(W,H),ctx=c.getContext('2d'),parts=[];
const col=1960,left=64,right=2072,ink='#152B43',purple='#8844CA',teal='#15AEB8';
function rect(x,y,w,h,color){ctx.fillStyle=color;ctx.fillRect(x,y,w,h);parts.push(`<rect x="${x}" y="${y}" width="${w}" height="${h}" fill="${color}"/>`);}
function text(s,x,y,size,color=ink,bold=false){ctx.fillStyle=color;ctx.font=`${bold?'bold ':''}${size}px "Microsoft YaHei"`;ctx.textBaseline='top';ctx.fillText(s,x,y);parts.push(`<text x="${x}" y="${y+size*.88}" font-family="Microsoft YaHei" font-size="${size}" font-weight="${bold?700:400}" fill="${color}">${s.replaceAll('&','&amp;').replaceAll('<','&lt;')}</text>`);}
async function shot(name,x,y,w){const p=path.join(root,'screenshots',name+'.png');const im=await loadImage(p);const h=w*im.height/im.width;ctx.drawImage(im,x,y,w,h);parts.push(`<image x="${x}" y="${y}" width="${w}" height="${h}" href="data:image/png;base64,${fs.readFileSync(p).toString('base64')}"/>`);return h;}
function rowLabel(label,x,y,color,bg){rect(x,y,col,78,bg);rect(x,y,14,78,color);text(label,x+32,y+6,48,ink,true);}
(async()=>{
rect(0,0,W,H,'#FFFFFF');rect(2043,32,2,3990,'#D9E3ED');
rect(left,25,18,100,purple);text('BOSS',left+44,12,88,ink,true);
rect(right,25,18,100,teal);text('GUARD',right+44,12,88,ink,true);
const ys=[156,1436,2716];
for(let i=0;i<3;i++){rowLabel(['RANGED ATTACK','MELEE ATTACK','CHARGING'][i],left,ys[i],purple,'#F0E7FA');rowLabel(['RANGED ATTACK','MELEE ATTACK','CHARGING'][i],right,ys[i],teal,'#E2F7F7');}
// Four Boss screenshots and two Guard screenshots are all retained, in two columns.
text('NORMAL — 3 PROJECTILES',left+18,258,45,ink,true);
text('ENRAGED — 5 PROJECTILES',left+1008,258,45,ink,true);
await shot('03_boss_ranged',left,326,966);
await shot('04_boss_enraged_ranged',left+994,326,966);
text('Normal: 1.35 s attack interval',left+48,930,60,ink,true);
text('Enraged: 0.85 s attack interval',left+48,1042,60,ink,true);
text('Enrage activates below 50% health.',left+48,1154,48,'#587083');
text('Both ranged screenshots are shown above.',left+48,1240,46,'#587083');
await shot('01_guard_ranged',right,250,col);
text('Single projectile | 1.9 s attack interval',right+24,1360,43,ink,true);
await shot('05_boss_melee',left,1530,col);
await shot('02_guard_melee',right,1530,col);
text('22 damage | Shield 60 → 38',left+28,2646,47,ink,true);
text('10 damage | Shield 60 → 50',right+28,2646,47,ink,true);
await shot('06_boss_slam_charge',left,2810,col);
text('1.4 s telegraph | 400 cm radius',left+28,3934,47,ink,true);
rect(right,2810,col,1102.5,'#F0FAFC');
text('NO CHARGING SKILL',right+235,3190,80,ink,true);
text('Guard uses ranged and melee attacks only.',right+160,3340,53,'#587083');
text('Six original game screenshots • Boss left / Guard right',64,4030,33,'#587083');
fs.writeFileSync(path.join(root,'Boss_Guard_Comparison_2x3.png'),c.toBuffer('image/png'));
fs.writeFileSync(path.join(root,'Boss_Guard_Comparison_2x3.svg'),`<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}">${parts.join('')}</svg>`);
fs.writeFileSync(path.join(root,'Comparison_Notes.txt'),'Layout: two columns, three rows. Boss on the left; Guard on the right. Boss normal and enraged ranged shots share the top-left cell. All six screenshots are embedded from the original files; screenshot content is not regenerated. PNG 4096x4096; SVG embeds original PNG bytes.\n');
console.log('Saved faithful 4096×4096 PNG and self-contained SVG.');
})();
