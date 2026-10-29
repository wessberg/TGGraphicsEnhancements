from pathlib import Path
from datetime import datetime, timezone
import hashlib, json
root=Path.cwd()
components={'TGNeuralRendering':'TGNeuralRendering.Native.dll','TGGraphicsBridge':'TGGraphicsBridge.Native.dll','TGFrameGeneration':'TGFG.Native.dll','TGBetterUpscaling':'TGBetterUpscaling.Native.dll'}
items=[]
for component,name in components.items():
    built=root/'build/nr'/name
    installed=root/'build/nr/install'/component/name
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    assert digest(built)==digest(installed), component
    items.append(dict(component=component,sha256=digest(installed),size=installed.stat().st_size))
record=dict(time=datetime.now(timezone.utc).isoformat(),version='1.2.2',deployed=False,native=items,
            checks={'nrEvaluations':20,'coexistenceNr':30,'coexistenceRr':32,'sourceChecks':'88/89; PT lantern-short baseline mismatch','model':'310.8.0.0'})
(root/'build/nr/evidence.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
for name in ['src/TGNeuralRendering/Session.cs','src/TGNeuralRendering/VideoRows.cs','docs/DLSS5-IMPLEMENTATION.md','docs/rounds/round-08.md','BACKLOG.md']:
    p=root/name; b=p.read_bytes(); p.write_bytes(b.replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'))
print('Four native build/install hashes match. Evidence: build/nr/evidence.json. Game files untouched.')
