import json,sys
from pathlib import Path
import numpy as np
sys.path.insert(0,'tools/harness')
from analyze import read_frame
from neural_analysis import matched_motion,colour_motion

root=Path(sys.argv[1]); baseline=None; results=[]
for folder in sorted(root.iterdir()):
    if 'motion-' not in folder.name or not (folder/'step.json').exists(): continue
    step=json.loads((folder/'step.json').read_text())
    series=[]
    for info in step['frames']:
        image=read_frame(folder/info['file'],step['width'],step['height'],step['format'])
        h,w=image.shape[:2]
        series.append(image[h//10:h-h//10,w//10:w-w//10].mean(axis=(0,1)).tolist())
    if baseline is None:
        assert step['neuralEvaluations']==0
        baseline=(step,series)
        continue
    mismatch=matched_motion(baseline[0],step)
    entry={'name':folder.name,'error':mismatch}
    if not mismatch: entry.update(colour_motion(baseline[1],series))
    results.append(entry)
    print(json.dumps(entry),flush=True)
(root/'nr-motion-preview.json').write_text(json.dumps(results,indent=2)+'\n')
