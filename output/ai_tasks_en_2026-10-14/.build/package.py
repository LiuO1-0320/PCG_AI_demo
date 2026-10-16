from pathlib import Path
from html import escape
from zipfile import ZipFile, ZIP_DEFLATED
from PIL import Image
import re
root=Path(r'D:\ue5project\PCG_AI_demo\output\ai_tasks_en_2026-10-14')
source=(root/'AI_Behavior_Trees_EN.md').read_text(encoding='utf-8')
def inline(s):
    s=escape(s)
    s=re.sub(r'\*\*(.+?)\*\*',r'<strong>\1</strong>',s)
    return re.sub(r'`([^`]+)`',r'<code>\1</code>',s)
import base64
body=[]
table=False
fence=False
code=[]
def close_table():
    global table
    if table: body.append('</tbody></table></div>');table=False
for line in source.splitlines():
    if line.startswith('```'):
        close_table()
        if fence: body.append('<pre>'+escape('\n'.join(code))+'</pre>');code=[]
        fence=not fence
        continue
    if fence: code.append(line);continue
    line=line.strip()
    if not line: close_table();continue
    match=re.fullmatch(r'!\[(.*?)\]\((.*?)\)',line)
    if match:
        close_table()
        data=base64.b64encode((root/match[2]).read_bytes()).decode('ascii')
        body.append(f'<figure><img alt="{escape(match[1])}" src="data:image/png;base64,{data}"><figcaption>{escape(match[1])}</figcaption></figure>')
    elif line.startswith('|'):
        cells=[c.strip() for c in line.strip('|').split('|')]
        if all(re.fullmatch(r':?-+:?',c) for c in cells):continue
        first=not table
        if first:body.append('<div class="table"><table><tbody>');table=True
        tag='th' if first else 'td'
        body.append('<tr>'+''.join(f'<{tag}>{inline(c)}</{tag}>' for c in cells)+'</tr>')
    else:
        close_table()
        heading=re.match(r'^(#{1,3}) (.+)',line)
        if heading:
            n=len(heading[1]);body.append(f'<h{n}>{inline(heading[2])}</h{n}>')
        else:body.append('<p>'+inline(line)+'</p>')
close_table()
page='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Enemy AI — Behavior Trees and Task Screenshots</title><style>
body{font-family:Segoe UI,Arial,sans-serif;color:#183444;background:#f4f7fb;max-width:1180px;margin:40px auto;padding:0 24px;line-height:1.75}h1{font-size:34px;line-height:1.3}h2{border-bottom:2px solid #d3e6ef;margin-top:48px;padding-bottom:8px}h3{margin-top:30px}pre{background:#183444;color:#edf5fa;border-radius:12px;padding:24px;overflow-x:auto;line-height:1.65;font-size:14px}code{background:#e2edf4;padding:2px 5px;border-radius:4px}figure{margin:20px 0;background:white;border:1px solid #d5e1e8;border-radius:12px;overflow:hidden}img{width:100%;display:block}figcaption{padding:10px 18px;color:#586d7c}.table{overflow-x:auto}table{width:100%;border-collapse:collapse;background:white}td,th{text-align:left;padding:12px;border:1px solid #d5e1e8;vertical-align:top}th{background:#e7f2fa}@media print{body{background:white;margin:0;max-width:none}figure,tr,pre{break-inside:avoid}h2,h3{break-after:avoid}}</style><body>'''+''.join(body)+'</body></html>'
(root/'AI_Behavior_Trees_EN.html').write_text(page,encoding='utf-8')
log=(root.parents[1]/'Saved/Logs/CastleAITaskCapture.log').read_text(encoding='utf-8',errors='replace')
proof='\n'.join(l for l in log.splitlines() if 'CASTLE_AI_TASK_CAPTURE' in l or 'CASTLE_AI_CAPTURE_MELEE' in l)
assert 'CASTLE_AI_TASK_CAPTURE_OK:' in proof
(root/'Capture_Evidence.txt').write_text('UE renderer capture evidence — 2026-10-14\n'+proof+'\n',encoding='utf-8')
shots=sorted((root/'screenshots').glob('*.png'))
assert len(shots)==6
for p in shots:
    with Image.open(p) as im:assert im.size==(1920,1080);im.verify()
assert page.count('data:image/png;base64,')==6
archive=root/'AI_Tasks_English_Pack.zip'
files=[p for p in root.rglob('*') if p.is_file() and '.build' not in p.parts and p.suffix!='.zip']
with ZipFile(archive,'w',ZIP_DEFLATED) as z:
    for p in files:z.write(p,p.relative_to(root).as_posix())
with ZipFile(archive) as z:assert z.testzip() is None
print(f'Created English HTML/Markdown, six 1920x1080 PNGs and capture evidence. ZIP: {archive.stat().st_size:,} bytes.')
