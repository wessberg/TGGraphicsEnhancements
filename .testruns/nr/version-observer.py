from pathlib import Path
root=Path.cwd()
files=[]
for sub in ['src','native']:
    files.extend(p for p in (root/sub).rglob('*') if p.suffix in {'.cs','.csproj','.props','.cpp','.hpp','.json'} and not any(x in p.parts for x in ['obj','bin']))
files += [root/p for p in ['CMakeLists.txt','tools/Package.Common.ps1','tests/source_checks.py','tests/test_autoload_integration.py']]
for p in files:
    raw=p.read_bytes()
    if b'1.2.2' in raw:
        assert p.read_bytes()==raw,p
        p.write_bytes(raw.replace(b'1.2.2',b'1.2.3'))
        print(p.relative_to(root))
