"""Read-only inventory/hash/Git checks; write only this task's new evidence."""
import hashlib,json,os,subprocess
from pathlib import Path
from c2_mcp import HERE,ROOT
out=HERE/'final-audit.json'
if out.exists():raise RuntimeError('Evidence exists')
base=json.loads((HERE/'baseline-files.json').read_text(encoding='utf-8'))
manifest=json.loads((HERE/'backup-manifest.json').read_text(encoding='utf-8'))
allowed={s.casefold() for s in manifest['targets']}
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
changed=[];missing=[];same=[];backups=[]
for item in base:
    p=ROOT/item['path']
    if not p.is_file():missing.append(item['path']);continue
    new=digest(p)
    if new==item['sha256']:same.append(item['path'])
    else:changed.append({'path':item['path'],'before':item['sha256'],'after':new,'authorized':item['path'].casefold() in allowed})
    if item['path'].casefold() in allowed:
        b=HERE/'Backups'/item['path']
        backups.append({'path':str(b.relative_to(ROOT)).replace('\\','/'),'original_matches':b.is_file() and digest(b)==item['sha256']})
skip={'.git','.vs','Binaries','Intermediate','Saved','DerivedDataCache','node_modules','__pycache__'}
known={v['path'].casefold() for v in base};newfiles=[]
for p in ROOT.rglob('*'):
    rel=p.relative_to(ROOT)
    if any(part in skip for part in rel.parts) or not p.is_file():continue
    if rel.as_posix().casefold() not in known:newfiles.append({'path':rel.as_posix(),'bytes':p.stat().st_size,'sha256':digest(p)})
git_env=os.environ.copy();git_env['GIT_OPTIONAL_LOCKS']='0'
git_result=subprocess.run(['git','-c','safe.directory='+ROOT.as_posix(),'status','--short'],cwd=ROOT,env=git_env,capture_output=True,text=True)
if git_result.returncode:raise RuntimeError(git_result.stderr)
status=git_result.stdout
e={'baseline_count':len(base),'changed':changed,'unchanged_count':len(same),'missing':missing,'backups':backups,'new_files':newfiles,'git_status':status,
   'project_unchanged':'testfordemo.uproject' in same,'demo_dirty_map_unchanged':'Content/Demo/L_CastleDemo.umap' in same,
   'existing_assets_unchanged':all(x in same for x in [i['path'] for i in base if Path(i['path']).suffix in ('.uasset','.umap')]),
   'no_unexpected_existing_change':not missing and all(x['authorized'] for x in changed),'all_backups_match':all(x['original_matches'] for x in backups)}
with out.open('x',encoding='utf-8') as f:json.dump(e,f,ensure_ascii=False,indent=2)
print(json.dumps({k:v for k,v in e.items() if k not in ('new_files','backups','git_status')},ensure_ascii=False))
assert e['no_unexpected_existing_change'] and e['all_backups_match'] and e['project_unchanged'] and e['demo_dirty_map_unchanged'] and e['existing_assets_unchanged']
