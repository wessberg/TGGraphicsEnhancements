from pathlib import Path
import runpy, numpy as np
m=runpy.run_path('tools/candle/albedo.py'); inv=m['inv']; rng=np.random.default_rng(1878)
for A in [.9,.85,.7]:
 s=A**(1/2.4)*1.055-.055; s=round(s*255)/255; a=((s+.055)/1.055)**2.4
 sigma=100/(1.9-a+3.5*(a-.8)**2); omega=1-inv(a)**2
 N=150000; p=np.zeros((N,3)); p[:,1]=.06; u=rng.random(N); phi=rng.random(N)*2*np.pi
 d=np.c_[np.sqrt(u)*np.cos(phi),-np.sqrt(1-u),np.sqrt(u)*np.sin(phi)]; half=np.array([.15,.06,.15]); out=0
 while len(p):
  t=-np.log(rng.random(len(p)))/sigma
  dist=np.min((np.where(d>0,half,-half)-p)/d,axis=1)
  escaped=dist<t; out+=escaped.sum(); keep=(~escaped)&(rng.random(len(p))<omega)
  p=p[keep]+d[keep]*t[keep,None]
  z=rng.random(len(p))*2-1; phi=rng.random(len(p))*2*np.pi; d=np.c_[np.sqrt(1-z*z)*np.cos(phi),z,np.sqrt(1-z*z)*np.sin(phi)]
 print('finite slab albedo',a,'escape',out/N,'demodulated',out/N/a)
