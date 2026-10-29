from pathlib import Path
import shutil
root=Path(__file__).resolve().parents[2]
files=[]
for sub in ['src','native']:
    for p in (root/sub).rglob('*'):
        if p.suffix in {'.cs','.csproj','.props','.cpp','.hpp','.json'} and not any(x in p.parts for x in ['obj','bin']): files.append(p)
files += [root/p for p in ['CMakeLists.txt','tools/Package.Common.ps1','tests/source_checks.py','tests/test_autoload_integration.py']]
for p in files:
    raw=p.read_bytes()
    if b'1.2.1' not in raw: continue
    backup=root/'.testruns/nr/base'/p.relative_to(root); backup.parent.mkdir(parents=True,exist_ok=True)
    if not backup.exists(): shutil.copyfile(p,backup)
    changed=raw.replace(b'1.2.1',b'1.2.2')
    assert p.read_bytes()==raw, f'Concurrent edit: {p}'
    p.write_bytes(changed)
    print(p.relative_to(root))
