# SPDX-License-Identifier: MIT
# Isotropic half-space transport, H(mu) = 1 / [q + (omega/2) integral
# u H(u)/(mu+u) du], q=sqrt(1-omega). Diffuse reflectance is
# 1 - 2 q integral mu H(mu) dmu. Integrals: 64-point Gauss-Legendre.
# Fit the inverse with exact black/white endpoints for shader evaluation.
import numpy as np
u,w=np.polynomial.legendre.leggauss(64); u=(u+1)/2; w=w/2
K=w[None,:]*u[None,:]/(u[:,None]+u[None,:])
def refl(q):
 om=1-q*q; H=np.ones(64)
 for _ in range(500):
  h=1/(q+0.5*om*(K@H)); h=(H+h)/2
  if max(abs(h-H))<1e-13: break
  H=h
 return 1-2*q*np.dot(w*u,h)
def inv(A):
 lo,hi=0.,1.
 for _ in range(43):
  q=(lo+hi)/2
  if refl(q)>A: lo=q
  else: hi=q
 return (lo+hi)/2
if __name__ == '__main__':
 a=np.linspace(0,.99999,257)
 q=np.array([inv(x) for x in a]); r=q/(1-a)
 fit=np.polynomial.Chebyshev.fit(a[1:],(r[1:]-1)/a[1:],6,domain=[0,1]).convert(kind=np.polynomial.Polynomial)
 coeff=np.r_[1,fit.coef]
 print('Inverse coefficients, ascending power:',coeff.tolist())
 print('old:',[(a,refl(np.exp(a*(-5.09406+a*(2.61188-a*4.31805))/2))) for a in [.3,.5,.7,.9,1.]])
