import copy,json
from pathlib import Path
root=Path('build/nr/tuning')
plan=json.loads((root/'compare.json').read_text())
base=plan['recipes']['neural']
by_name={s['name']:s for s in base}
names=['01-off','02-after-neutral','03-after-A','04-after-A-tone-zero','05-after-B','06-after-B-tone-zero','07-after-C','08-after-C-tone-zero','09-after-A-mask','10-before-neutral','11-before-A','14-off-restored',
       '15-motion-off','16-motion-after-A','17-motion-after-A-tone-zero','18-motion-after-B','19-motion-after-B-tone-zero','20-motion-after-C','21-motion-after-C-tone-zero','22-motion-before-A','23-motion-off-restored']
steps=[copy.deepcopy(by_name[n]) for n in names]
# Keep the same strong route for this first face comparison. If it collides with nearby objects the frames will show it.
plan['recipes']['neural']=steps
(root/'face.json').write_text(json.dumps(plan,indent=2)+'\n')
print('Character comparison: hair-eyebrows uses the existing PT hair-eyebrows save.')
