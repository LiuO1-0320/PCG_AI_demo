from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
from PIL import Image
import xml.etree.ElementTree as ET
root=Path(r'D:\ue5project\PCG_AI_demo\output\demo_pre_2026-10-13')
pngs=sorted((root/'images').glob('*.png'))
svgs=sorted((root/'svg').glob('*.svg'))
assert len(pngs)==8 and len(svgs)==8
for p in pngs:
    with Image.open(p) as im:
        assert im.size==(2560,1440), (p.name,im.size)
        im.verify()
for p in svgs:
    ET.parse(p)
html=(root/'demo_pre_resources.html').read_text(encoding='utf-8')
assert html.count('data:image/png;base64,')==11
assert html.count('<table>')==html.count('</table>')
archive=root/'demo_pre_resources.zip'
files=[p for p in root.rglob('*') if p.is_file() and '.build' not in p.parts and p.suffix!='.zip']
with ZipFile(archive,'w',ZIP_DEFLATED) as z:
    for p in files:
        z.write(p,p.relative_to(root).as_posix())
with ZipFile(archive) as z:
    assert z.testzip() is None
    assert len(z.namelist())==21
print(f'Verified: 8 PNGs, 8 SVGs, 3 screenshots, 2 reports; archive {archive.stat().st_size:,} bytes.')
