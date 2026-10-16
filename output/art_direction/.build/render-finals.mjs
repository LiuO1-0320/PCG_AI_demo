import fs from 'node:fs/promises';
import path from 'node:path';
import {PresentationFile,FileBlob} from '@oai/artifact-tool';
import {GlobalFonts} from '@napi-rs/canvas';
GlobalFonts.registerFromPath('C:/Windows/Fonts/msyh.ttc','Microsoft YaHei');
GlobalFonts.registerFromPath('C:/Windows/Fonts/msyhbd.ttc','Microsoft YaHei');
const root='D:/ue5project/PCG_AI_demo/output/art_direction';
for(const file of await fs.readdir(path.join(root,'presentations'))){
 if(!file.endsWith('.pptx'))continue;
 const p=await PresentationFile.importPptx(await FileBlob.load(path.join(root,'presentations',file)));
 for(let i=0;i<p.slides.items.length;i++){
  const blob=await p.export({slide:p.slides.items[i],format:'png',scale:1});
  await fs.writeFile(path.join(root,'.build',`${file}-${i+1}.png`),new Uint8Array(await blob.arrayBuffer()));
 }
 console.log(file+' '+p.slides.items.length+' pages rendered');
}
