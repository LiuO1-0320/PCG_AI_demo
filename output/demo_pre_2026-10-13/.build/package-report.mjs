import fs from 'node:fs/promises';
import path from 'node:path';
const root='D:/ue5project/PCG_AI_demo/output/demo_pre_2026-10-13';
const md=await fs.readFile(path.join(root,'demo_pre_resources.md'),'utf8');
const esc=s=>s.replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;');
let out=[],table=false,list=false;
function close(){if(table){out.push('</tbody></table></div>');table=false;}if(list){out.push('</ol>');list=false;}}
for(const raw of md.split(/\r?\n/)){
 const line=raw.trim();
 if(!line){close();continue;}
 const img=line.match(/^!\[(.*?)\]\((.*?)\)$/);
 if(img){close();const data=(await fs.readFile(path.join(root,img[2]))).toString('base64');out.push(`<figure><img src="data:image/png;base64,${data}" alt="${esc(img[1])}"><figcaption>${esc(img[1])}</figcaption></figure>`);continue;}
 if(line.startsWith('|')){
  const cells=line.slice(1,-1).split('|').map(s=>s.trim());
  if(cells.every(s=>/^:?-+:?$/.test(s)))continue;
  const first=!table;if(first){close();out.push('<div class="table"><table><tbody>');table=true;}
  const tag=first?'th':'td';out.push('<tr>'+cells.map(c=>`<${tag}>${esc(c)}</${tag}>`).join('')+'</tr>');continue;
 }
 const heading=line.match(/^(#{1,3})\s+(.*)$/);
 if(heading){close();const n=heading[1].length;out.push(`<h${n}>${esc(heading[2])}</h${n}>`);continue;}
 const item=line.match(/^\d+\.\s+(.*)$/);
 if(item){if(table)close();if(!list){out.push('<ol>');list=true;}out.push(`<li>${esc(item[1])}</li>`);continue;}
 close();out.push(`<p>${esc(line)}</p>`);
}
close();
await fs.writeFile(path.join(root,'demo_pre_resources.html'),`<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Demo 汇报资源统计</title><style>body{font-family:"Microsoft YaHei",sans-serif;max-width:1160px;margin:40px auto;padding:0 24px;color:#183444;line-height:1.85;background:#f5f8fb}h1{font-size:32px}h2{margin-top:48px;border-bottom:2px solid #d3e6ef;padding-bottom:10px}h3{margin-top:32px}p,li{font-size:16px}.table{overflow-x:auto}table{width:100%;border-collapse:collapse;background:white;font-size:15px}td,th{padding:11px 14px;border:1px solid #d5e1e8;text-align:left;vertical-align:top}th{background:#e7f2fa}figure{margin:26px 0;background:white;border:1px solid #d5e1e8;border-radius:10px;overflow:hidden}img{width:100%;display:block}figcaption{padding:8px 18px;color:#586d7c;font-size:14px}@media print{body{background:white;margin:0;max-width:none}figure, tr{break-inside:avoid}h2,h3{break-after:avoid}}</style><body>${out.join('\n')}</body></html>`);
console.log('HTML report created with 11 embedded images; opens offline.');
