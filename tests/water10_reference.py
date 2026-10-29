# SPDX-License-Identifier: MIT
# Independent flat-water quadrature: no renderer equations copied. Ray angles and power are integrated directly.
import math
I,R,n,sigma,h=20.,.05,1.3333,.2,2.
F=((n-1)/(n+1))**2
def point(d): return I*(1-F)*math.exp(-sigma*d)/(h+d/n)**2
def sphere(d):
    lo,hi=0.,math.asin(1/n)
    for _ in range(60):
        a=(lo+hi)/2; b=math.asin(n*math.sin(a))
        miss=d*math.tan(a)*math.cos(b)+h*math.sin(b)
        if miss<R:lo=a
        else:hi=a
    lim=(lo+hi)/2; total=0.
    for j in range(4000):
        a=lim*(j+.5)/4000; b=math.asin(n*math.sin(a)); ca,cb=math.cos(a),math.cos(b)
        rs=(n*ca-cb)/(n*ca+cb); rp=(ca-n*cb)/(ca+n*cb)
        total+=(1-.5*(rs*rs+rp*rp))*math.exp(-sigma*d/ca)*math.sin(a)
    return total*lim/4000*2*math.pi*n*n*I/(math.pi*R*R)
for d in (.05,.55,1.05):
    print(f'depth {d:.2f}: paraxial {point(d):.9g}, finite sphere integral {sphere(d):.9g}, ratio {sphere(d)/point(d):.8f}')
ref=0.
for j in range(100000):
    s=(j+.5)/100000
    ref+=math.exp(-sigma*s)*.02/(4*math.pi)*point(.05+s)/100000
print(f'one-scatter body reference {ref:.12g}')
print(f'controlled mirror proxy radiance {I/(math.pi*.2*.2):.12g} versus material radiance 1')
