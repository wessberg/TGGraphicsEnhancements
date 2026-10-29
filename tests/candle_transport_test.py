# SPDX-License-Identifier: MIT
# Verify the shader inverse against an independently sampled half-space walk.
from pathlib import Path
import numpy as np, re
import runpy
refl=runpy.run_path('tools/candle/albedo.py')['refl']
s=Path('native/pathtracing/shaders/pt_common.hlsli').read_text()
body=s[s.index('float walk_albedo'):s.index('void subsurface_medium')]
coef=[float(re.search(r'float q = ([-.0-9]+);',body).group(1))]
for sign,n in re.findall(r'q = q \* a ([+-]) ([.0-9]+);',body): coef.append(float(sign+n))
coef.append(1.0)
def q_at(a): return (1-a)*np.polyval(coef,a)
err=max(abs(refl(q_at(a))-a) for a in np.linspace(0,1,4097))
print('Maximum diffuse reflectance error (4097 values):',err); assert err<2e-5
rng=np.random.default_rng(684241)
for a in [.22,.35,.6,.7,.85,.9,.97]:
 om=1-q_at(a)**2
 n=250000; z=-np.log(rng.random(n))*np.sqrt(rng.random(n)); ids=np.arange(n); out=0; steps=0
 while len(ids):
  keep=rng.random(len(ids))<om; ids=ids[keep]; z=z[keep]
  z+=-np.log(rng.random(len(ids)))*(2*rng.random(len(ids))-1)
  escaped=z<=0; out+=escaped.sum(); ids=ids[~escaped]; z=z[~escaped]; steps+=1
 got=out/n; print('A',a,'returned',got,'error',got-a,'steps',steps); assert abs(got-a)<.004
