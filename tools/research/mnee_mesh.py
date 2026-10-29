"""mnee_mesh.py [grid step]: the MNEE walk of pt_common.hlsli on the harness's 32x64 UV glass sphere (its triangles, its
interpolated normals) under the sun: how many floor points in its shadow solve, and the light back on the floor."""
import numpy as np, sys
import os
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "mnee_check.py")).read().split("# ---- slab: glass between")[0])  # the formulas
stacks, slices = 32, 64
Ps = []; Ns = []
for i in range(stacks + 1):
    for j in range(slices + 1):
        th = np.pi * i / stacks; ph = 2 * np.pi * j / slices
        n = np.array([np.sin(th) * np.cos(ph), np.cos(th), np.sin(th) * np.sin(ph)])
        Ps.append(n * 0.5); Ns.append(n)
Ps = np.array(Ps) + np.array([-2.0, 0.8, 1.0]); Ns = np.array(Ns)
T = []
for i in range(stacks):
    for j in range(slices):
        a = i * (slices + 1) + j; b = a + slices + 1
        T += [(a, b, a + 1), (a + 1, b, b + 1)]
T = np.array(T)
A, B, Cc = Ps[T[:, 0]], Ps[T[:, 1]], Ps[T[:, 2]]
E1, E2 = B - A, Cc - A
def mesh_hit(o, d, tmin, tmax):
    p = np.cross(d, E2); det = np.einsum('ij,ij->i', E1, p)
    ok = np.abs(det) > 1e-12
    inv = np.where(ok, 1.0 / np.where(ok, det, 1), 0)
    s = o - A; u = np.einsum('ij,ij->i', s, p) * inv
    q = np.cross(s, E1); v = (q @ d) * inv; t = np.einsum('ij,ij->i', E2, q) * inv
    good = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > tmin) & (t < tmax)
    if not good.any(): return None
    k = np.argmin(np.where(good, t, np.inf))
    return k, u[k], v[k], t[k]
def mvertex(k, u, v):
    i0, i1, i2 = T[k]
    w0, w1, w2 = Ps[i0], Ps[i1], Ps[i2]; m0, m1, m2 = Ns[i0], Ns[i1], Ns[i2]
    dpdu = w1 - w0; dpdv = w2 - w0; ng = np.cross(dpdu, dpdv)
    if np.dot(ng, ng) < 1e-20: return None
    s = m0 * (1 - u - v) + m1 * u + m2 * v; ln = np.linalg.norm(s); n = s / ln
    dndu = (m1 - m0) / ln; dndv = (m2 - m0) / ln
    dndu -= n * np.dot(n, dndu); dndv -= n * np.dot(n, dndv)
    ilu = 1 / np.linalg.norm(dpdu); dpdu = dpdu * ilu; dndu = dndu * ilu
    al = np.dot(dpdu, dpdv); dpdv = dpdv - al * dpdu; dndv = dndv - al * dndu
    ilv = 1 / np.linalg.norm(dpdv); dpdv = dpdv * ilv; dndv = dndv * ilv
    t, b = tangent_frame(normalize(ng))
    cT = np.dot(dpdu, t); sT = -np.dot(dpdv, t)
    vv = V(); vv.p = w0 * (1 - u - v) + w1 * u + w2 * v; vv.n = n
    vv.dndu = cT * dndu - sT * dndv; vv.dndv = sT * dndu + cT * dndv; vv.dpdu = t; vv.dpdv = b
    return vv
def project(o, to, tmin):
    d = to - o; ln = np.linalg.norm(d); d = d / ln
    h = mesh_hit(o, d, tmin, 2 * ln)
    return None if h is None else mvertex(h[0], h[1], h[2])
L = normalize(np.array([0.35, 1.0, 0.55]))
def solve(rcv, v0, v1):
    beta = 0.1; resolve = True; dx0 = dx1 = np.zeros(2); why = "iterations"
    for it in range(24):
        if resolve:
            constraint(v0, rcv - v0.p, None, False, v1.p - v0.p, v1, True, False)
            constraint(v1, v0.p - v1.p, v0, True, L, None, False, True)
            if max(np.linalg.norm(v0.constraint), np.linalg.norm(v1.constraint)) < 1e-3: return (v0, v1), it
            try:
                li0 = np.linalg.inv(m(v0.b)); Am = m(v1.a) @ li0; li1 = np.linalg.inv(m(v1.b) - Am @ m(v0.c))
            except np.linalg.LinAlgError: return None, "singular"
            dx1 = li1 @ (v1.constraint - Am @ v0.constraint); dx0 = li0 @ (v0.constraint - m(v0.c) @ dx1)
        s0 = beta * (dx0[0] * v0.dpdu + dx0[1] * v0.dpdv); s1 = beta * (dx1[0] * v1.dpdu + dx1[1] * v1.dpdv)
        if np.linalg.norm(s0) < 1e-7 and np.linalg.norm(s1) < 1e-7: return None, "stuck"
        red = False; n0, n1 = v0, v1
        if np.linalg.norm(s0) >= 1e-7:
            p0 = v0.p - s0
            if p0[1] - rcv[1] <= 0: p0 = v0.p + s0
            n0 = project(rcv, p0, 0.0)
            if n0 is None: red = True; why = "v0 off"
        if not red and np.linalg.norm(s1) >= 1e-7:
            n1 = project(v0.p, v1.p - s1, 1e-4)
            if n1 is None: red = True; why = "v1 off"
        if not red and (np.dot(n0.n, rcv - n0.p) * np.dot(n0.n, n1.p - n0.p) >= 0 or np.dot(n1.n, v0.p - n1.p) * np.dot(n1.n, L) >= 0):
            red = True; why = "not through"
        if red:
            resolve = False; beta *= 0.5
            if beta < 1e-4: return None, why
            continue
        v0, v1 = n0, n1; resolve = True; beta = min(1, 2 * beta)
    return None, why
from collections import Counter
res = Counter(); total = 0.0; area = 0.0; h = float(sys.argv[1]) if len(sys.argv) > 1 else 0.03
for x in np.arange(-3.4, -1.0, h):
    for z in np.arange(-0.2, 1.8, h):
        rcv = np.array([x, 0.0, z])
        h0 = mesh_hit(rcv, L, 1e-6, 1e9)
        if h0 is None: continue
        area += h * h
        p0 = rcv + L * h0[3]
        h1 = mesh_hit(p0, L, 1e-4, 1e9)
        if h1 is None: res["one crossing"] += 1; continue
        v0 = mvertex(h0[0], h0[1], h0[2]); v1 = mvertex(h1[0], h1[1], h1[2])
        if v0 is None or v1 is None: res["degenerate"] += 1; continue
        r, why = solve(rcv, v0, v1)
        if r is None: res[why] += 1; continue
        res["solved"] += 1
        a, b = r
        G = geometry(a, b, rcv, L, True); w0 = normalize(a.p - rcv)
        total += G * max(w0[1], 0) / L[1] * h * h
print(dict(res), "light back %.3f (no Fresnel)" % (total / area))
