"""Checks the MNEE formulas of pt_common.hlsli (caustic_constraint, caustic_step, caustic_geometry) in numpy on analytic
surfaces: a horizontal glass slab (the generalized geometry term must be 1: a slab doesn't focus) and a glass sphere
(energy: the irradiance summed over the floor must equal what the sphere intercepts, less Fresnel)."""
import numpy as np

IOR = 1.5

def normalize(v):
    return v / np.linalg.norm(v)

def tangent_frame(n):
    sgn = 1.0 if n[2] >= 0 else -1.0
    a = -1.0 / (sgn + n[2]); c = n[0] * n[1] * a
    t = np.array([1.0 + sgn * n[0] * n[0] * a, sgn * c, -sgn * n[0]])
    b = np.array([c, sgn + n[1] * n[1] * a, -n[1]])
    return t, b

class V:
    pass

def vertex(p, n, dndu=None, dndv=None):
    v = V(); v.p = np.array(p, float); v.n = normalize(np.array(n, float))
    t, b = tangent_frame(v.n)  # a flat or smooth surface: frame about its normal (ng = n here)
    v.dpdu, v.dpdv = t, b
    v.dndu = np.zeros(3) if dndu is None else dndu
    v.dndv = np.zeros(3) if dndv is None else dndv
    return v

def constraint(v, toPrev, prev, prevFree, toNext, nxt, nextFree, fixedDirection):
    ili = 1.0 / np.linalg.norm(toPrev); ilo = 1.0 / np.linalg.norm(toNext)
    wi = toPrev * ili; wo = toNext * ilo
    eta = IOR if np.dot(wi, v.n) >= 0 else 1.0 / IOR
    H = -(wi + eta * wo); ilh = 1.0 / np.linalg.norm(H); H = H * ilh
    ilo *= eta * ilh; ili *= ilh
    dpn = np.dot(v.dpdu, v.n); s = v.dpdu - dpn * v.n; ils = 1.0 / np.linalg.norm(s); s = s * ils
    t = np.cross(v.n, s)
    v.a = np.zeros(4); v.c = np.zeros(4)
    if prevFree:
        du = (prev.dpdu - wi * np.dot(wi, prev.dpdu)) * ili; dv = (prev.dpdv - wi * np.dot(wi, prev.dpdv)) * ili
        du -= H * np.dot(du, H); dv -= H * np.dot(dv, H)
        v.a = -np.array([np.dot(du, s), np.dot(dv, s), np.dot(du, t), np.dot(dv, t)])
    if (not nextFree) and fixedDirection:
        du = ili * (-v.dpdu + wi * np.dot(wi, v.dpdu)); dv = ili * (-v.dpdv + wi * np.dot(wi, v.dpdv))
    else:
        du = -v.dpdu * (ili + ilo) + wi * (np.dot(wi, v.dpdu) * ili) + wo * (np.dot(wo, v.dpdu) * ilo)
        dv = -v.dpdv * (ili + ilo) + wi * (np.dot(wi, v.dpdv) * ili) + wo * (np.dot(wo, v.dpdv) * ilo)
    du -= H * np.dot(du, H); dv -= H * np.dot(dv, H)
    du = -du; dv = -dv
    dsdu = -ils * (np.dot(v.dpdu, v.dndu) * v.n + dpn * v.dndu); dsdv = -ils * (np.dot(v.dpdu, v.dndv) * v.n + dpn * v.dndv)
    dsdu -= s * np.dot(s, dsdu); dsdv -= s * np.dot(s, dsdv)
    dtdu = np.cross(v.dndu, s) + np.cross(v.n, dsdu); dtdv = np.cross(v.dndv, s) + np.cross(v.n, dsdv)
    v.b = np.array([np.dot(du, s) + np.dot(H, dsdu), np.dot(dv, s) + np.dot(H, dsdv), np.dot(du, t) + np.dot(H, dtdu), np.dot(dv, t) + np.dot(H, dtdv)])
    if nextFree:
        du = (nxt.dpdu - wo * np.dot(wo, nxt.dpdu)) * ilo; dv = (nxt.dpdv - wo * np.dot(wo, nxt.dpdv)) * ilo
        du -= H * np.dot(du, H); dv -= H * np.dot(dv, H)
        v.c = -np.array([np.dot(du, s), np.dot(dv, s), np.dot(du, t), np.dot(dv, t)])
    v.constraint = np.array([np.dot(s, H), np.dot(t, H)])

def m(a):
    return np.array([[a[0], a[1]], [a[2], a[3]]])

def geometry(v0, v1, receiver, light, directional):
    li0 = np.linalg.inv(m(v0.b)); u0 = li0 @ m(v0.c)
    li1 = np.linalg.inv(m(v1.b) - m(v1.a) @ u0)
    s = normalize(v1.dpdu - np.dot(v1.dpdu, v1.n) * v1.n); t = np.cross(v1.n, s)
    wi = normalize(v0.p - v1.p)
    eta = IOR if np.dot(wi, v1.n) >= 0 else 1.0 / IOR
    measure = 1.0
    if directional:
        wo = light; H = -(wi + eta * wo); ilh = 1.0 / np.linalg.norm(H); H = H * ilh
        ilo = -eta * ilh
        cosT = np.dot(wo, v1.n); sinT = np.sqrt(max(0.0, 1 - cosT * cosT))
        cosP = np.dot(wo, s) / sinT if sinT > 1e-6 else 1.0; sinP = np.dot(wo, t) / sinT if sinT > 1e-6 else 0.0
        dth = ilo * (cosT * (cosP * s + sinP * t) - sinT * v1.n); dph = ilo * sinT * (-sinP * s + cosP * t)
        dth -= H * np.dot(dth, H); dph -= H * np.dot(dph, H)
        dc = np.array([[np.dot(dth, s), np.dot(dph, s)], [np.dot(dth, t), np.dot(dph, t)]])
        measure = 1.0 / max(sinT, 1e-3)
    else:
        wo = light - v1.p; ilo = 1.0 / np.linalg.norm(wo); wo = wo * ilo
        H = -(wi + eta * wo); ilh = 1.0 / np.linalg.norm(H); H = H * ilh
        ilo *= eta * ilh
        lu, lv = tangent_frame(-wo)
        du = (lu - wo * np.dot(wo, lu)) * ilo; dv = (lv - wo * np.dot(wo, lv)) * ilo
        du -= H * np.dot(du, H); dv -= H * np.dot(dv, H)
        dc = -np.array([[np.dot(du, s), np.dot(dv, s)], [np.dot(du, t), np.dot(dv, t)]])
    tp = u0 @ (li1 @ dc)
    w0 = v0.p - receiver; l2 = np.dot(w0, w0)
    return abs(np.dot(w0 / np.sqrt(l2), v0.n)) / l2 * abs(np.linalg.det(tp)) * measure

def refract(d, n, eta):  # d incident direction (travelling), n facing against d, eta = n1/n2
    cosi = -np.dot(n, d); k = 1 - eta * eta * (1 - cosi * cosi)
    return eta * d + (eta * cosi - np.sqrt(k)) * n

# ---- slab: glass between y=0.5 and y=1.3, receiver on the floor, sun from L
L = normalize(np.array([0.35, 1.0, 0.55]))
# the light travels -L; it enters the top face (normal up), refracts, exits the bottom face (normal down) parallel to -L
receiver = np.array([0.0, 0.0, 0.0])
inside = refract(-L, np.array([0, 1.0, 0]), 1.0 / IOR)  # direction inside the glass (downward)
# the bottom exit point: the receiver seen along +L (the exit ray is parallel to -L)
p0 = receiver + L * (0.5 / L[1])
# back up through the glass along -inside to the top face
p1 = p0 - inside * (0.8 / -inside[1])
v0 = vertex(p0, [0, -1, 0]); v1 = vertex(p1, [0, 1, 0])
constraint(v0, receiver - v0.p, None, False, v1.p - v0.p, v1, True, False)
constraint(v1, v0.p - v1.p, v0, True, L, None, False, True)
print("slab constraints (should be 0):", v0.constraint, v1.constraint)
print("slab directional G (should be 1):", geometry(v0, v1, receiver, L, True))
# positional: a lamp far along L behind the slab, at distance D from p1
for D in [2.0, 5.0, 50.0]:
    lamp = p1 + L * D
    constraint(v1, v0.p - v1.p, v0, True, lamp - v1.p, None, False, False)
    constraint(v0, receiver - v0.p, None, False, v1.p - v0.p, v1, True, False)
    G = geometry(v0, v1, receiver, lamp, False)
    straight = 1.0 / np.dot(lamp - receiver, lamp - receiver)
    print("slab positional D=%.0f: G %.5f vs a straight 1/r^2 %.5f, ratio %.3f (a slab brings a lamp closer: ratio > 1 expected, -> 1 far)" % (D, G, straight, G / straight))

# ---- the solver on the slab, projecting onto its planes as the HLSL projects onto the mesh
def plane_hit(origin, to, y):
    d = to - origin
    if abs(d[1]) < 1e-12: return None
    t = (y - origin[1]) / d[1]
    if t <= 0: return None
    return origin + d * t

def solve(receiver, light, directional, v0, v1, log=False, minProgress=1e-6):
    beta = 0.1; resolve = True; dx0 = dx1 = np.zeros(2)
    for it in range(24):
        if resolve:
            constraint(v0, receiver - v0.p, None, False, v1.p - v0.p, v1, True, False)
            constraint(v1, v0.p - v1.p, v0, True, light if directional else light - v1.p, None, False, directional)
            err = max(np.linalg.norm(v0.constraint), np.linalg.norm(v1.constraint))
            if log: print("  it %d beta %.3f err %.2e" % (it, beta, err))
            if err < 1e-3: return True, it
            li0 = np.linalg.inv(m(v0.b)); A = m(v1.a) @ li0
            li1 = np.linalg.inv(m(v1.b) - A @ m(v0.c))
            dx1 = li1 @ (v1.constraint - A @ v0.constraint)
            dx0 = li0 @ (v0.constraint - m(v0.c) @ dx1)
        p0 = v0.p - beta * (dx0[0] * v0.dpdu + dx0[1] * v0.dpdv)
        q0 = plane_hit(receiver, p0, v0.p[1])
        reduce = q0 is None
        if not reduce:
            if np.linalg.norm(q0 - v0.p) < minProgress: return False, ("no progress at v0", it)
            p1 = v1.p - beta * (dx1[0] * v1.dpdu + dx1[1] * v1.dpdv)
            q1 = plane_hit(v0.p, p1, v1.p[1])
            reduce = q1 is None
            if not reduce and np.linalg.norm(q1 - v1.p) < minProgress: return False, ("no progress at v1", it)
        if reduce:
            resolve = False; beta *= 0.5
            if beta < 1e-4: return False, ("step too small", it)
            continue
        v0 = vertex(q0, v0.n); v1 = vertex(q1, v1.n)
        resolve = True; beta = min(1.0, 2 * beta)
    return False, "iterations"

seed0 = receiver + L * (0.5 / L[1]); seed1 = receiver + L * (1.3 / L[1])
print("slab solve from the straight seed:", solve(receiver, L, True, vertex(seed0, [0, -1, 0]), vertex(seed1, [0, 1, 0]), log=True))

def solve2(receiver, light, directional, v0, v1, log=False):
    beta = 0.1; resolve = True; dx0 = dx1 = np.zeros(2)
    for it in range(24):
        if resolve:
            constraint(v0, receiver - v0.p, None, False, v1.p - v0.p, v1, True, False)
            constraint(v1, v0.p - v1.p, v0, True, light if directional else light - v1.p, None, False, directional)
            err = max(np.linalg.norm(v0.constraint), np.linalg.norm(v1.constraint))
            if log: print("  it %d beta %.3f err %.2e" % (it, beta, err))
            if err < 1e-3: return True, it
            li0 = np.linalg.inv(m(v0.b)); A = m(v1.a) @ li0
            li1 = np.linalg.inv(m(v1.b) - A @ m(v0.c))
            dx1 = li1 @ (v1.constraint - A @ v0.constraint)
            dx0 = li0 @ (v0.constraint - m(v0.c) @ dx1)
        step0 = beta * (dx0[0] * v0.dpdu + dx0[1] * v0.dpdv); step1 = beta * (dx1[0] * v1.dpdu + dx1[1] * v1.dpdv)
        if np.linalg.norm(step0) < 1e-7 and np.linalg.norm(step1) < 1e-7: return False, ("stuck", it)
        reduce = False
        n0, n1 = v0, v1
        if np.linalg.norm(step0) >= 1e-7:
            q0 = plane_hit(receiver, v0.p - step0, v0.p[1])
            if q0 is None: reduce = True
            else: n0 = vertex(q0, v0.n)
        if not reduce and np.linalg.norm(step1) >= 1e-7:
            q1 = plane_hit(v0.p, v1.p - step1, v1.p[1])
            if q1 is None: reduce = True
            else: n1 = vertex(q1, v1.n)
        if reduce:
            resolve = False; beta *= 0.5
            if beta < 1e-4: return False, ("step too small", it)
            continue
        v0, v1 = n0, n1
        resolve = True; beta = min(1.0, 2 * beta)
    return False, "iterations"

print("slab solve2 from the straight seed:", solve2(receiver, L, True, vertex(seed0, [0, -1, 0]), vertex(seed1, [0, 1, 0]), log=True))
for D in [2.0, 5.0]:
    lamp = receiver + L * (1.3 / L[1] + D)
    print("slab solve2, lamp at %.0f m past the slab:" % D, solve2(receiver, lamp, False, vertex(seed0, [0, -1, 0]), vertex(seed1, [0, 1, 0])))

# ---- a glass sphere (radius R at C) over the floor under the sun: MNEE at floor points, energy against the intercepted light
C = np.array([-2.0, 0.8, 1.0]); R = 0.5
def svertex(p):
    n = normalize(p - C); t, b = tangent_frame(n)
    v = V(); v.p = C + n * R; v.n = n; v.dpdu = t; v.dpdv = b; v.dndu = t / R; v.dndv = b / R
    return v
def sphere_hit(origin, d, tmin=1e-6):
    oc = origin - C; bq = np.dot(oc, d); cq = np.dot(oc, oc) - R * R; disc = bq * bq - cq
    if disc < 0: return None
    s = np.sqrt(disc)
    for t in (-bq - s, -bq + s):
        if t > tmin: return origin + d * t
    return None
def project(origin, to, tmin):
    d = to - origin; ln = np.linalg.norm(d); d = d / ln
    p = sphere_hit(origin, d, tmin)
    if p is None or np.linalg.norm(p - origin) > 2 * ln: return None
    return p
def solve_sphere(receiver, light, v0, v1):
    beta = 0.1; resolve = True; dx0 = dx1 = np.zeros(2)
    for it in range(24):
        if resolve:
            constraint(v0, receiver - v0.p, None, False, v1.p - v0.p, v1, True, False)
            constraint(v1, v0.p - v1.p, v0, True, light, None, False, True)
            if max(np.linalg.norm(v0.constraint), np.linalg.norm(v1.constraint)) < 1e-3: return v0, v1
            try:
                li0 = np.linalg.inv(m(v0.b)); A = m(v1.a) @ li0
                li1 = np.linalg.inv(m(v1.b) - A @ m(v0.c))
            except np.linalg.LinAlgError: return None
            dx1 = li1 @ (v1.constraint - A @ v0.constraint)
            dx0 = li0 @ (v0.constraint - m(v0.c) @ dx1)
        step0 = beta * (dx0[0] * v0.dpdu + dx0[1] * v0.dpdv); step1 = beta * (dx1[0] * v1.dpdu + dx1[1] * v1.dpdv)
        if np.linalg.norm(step0) < 1e-7 and np.linalg.norm(step1) < 1e-7: return None
        reduce = False; n0, n1 = v0, v1
        if np.linalg.norm(step0) >= 1e-7:
            q0 = project(receiver, v0.p - step0, 0.0)
            if q0 is None: reduce = True
            else: n0 = svertex(q0)
        if not reduce and np.linalg.norm(step1) >= 1e-7:
            q1 = project(v0.p, v1.p - step1, 1e-4)
            if q1 is None: reduce = True
            else: n1 = svertex(q1)
        if not reduce:
            if np.dot(n0.n, receiver - n0.p) * np.dot(n0.n, n1.p - n0.p) >= 0 or np.dot(n1.n, n0.p - n1.p) * np.dot(n1.n, light) >= 0: reduce = True
        if reduce:
            resolve = False; beta *= 0.5
            if beta < 1e-4: return None
            continue
        v0, v1 = n0, n1; resolve = True; beta = min(1.0, 2 * beta)
    return None
def fresnel(cosi, eta):
    cosi = min(abs(cosi), 1.0); sint2 = (1 - cosi * cosi) / (eta * eta)
    if sint2 >= 1: return 1.0
    cost = np.sqrt(1 - sint2)
    rs = (cosi - eta * cost) / (cosi + eta * cost); rp = (eta * cosi - cost) / (eta * cosi + cost)
    return 0.5 * (rs * rs + rp * rp)

floorN = np.array([0.0, 1.0, 0.0])
h = 0.01; total = 0.0; shadowArea = 0.0; solved = 0; seeded = 0; peak = 0.0
xs = np.arange(-3.4, -1.0, h); zs = np.arange(-0.2, 1.8, h)
for x in xs:
    for z in zs:
        rcv = np.array([x, 0.0, z])
        p0 = sphere_hit(rcv, L)
        if p0 is None: continue
        shadowArea += h * h
        p1 = sphere_hit(p0, L, 1e-6)
        seeded += 1
        r = solve_sphere(rcv, L, svertex(p0), svertex(p1))
        if r is None: continue
        v0, v1 = r; solved += 1
        G = geometry(v0, v1, rcv, L, True)
        w0 = normalize(v0.p - rcv)
        T = (1 - fresnel(np.dot(L, v1.n), IOR)) * (1 - fresnel(np.dot(normalize(v1.p - v0.p), v0.n), 1.0 / IOR))
        f = T * G * max(np.dot(floorN, w0), 0) / np.dot(floorN, L)
        peak = max(peak, f); total += f * h * h
print("sphere: %d floor points in its shadow, %d solved; light back on the floor %.3f of the shadow's, the brightest x%.1f" % (seeded, solved, total / shadowArea, peak))

# ---- the harness's glass cube: 0.8 m about (-0.3, 1, 2), the sun, floor points in its shadow
BC = np.array([-0.3, 1.0, 2.0]); BH = 0.4
def box_hits(origin, d, tmin=1e-6):
    hits = []
    for axis in range(3):
        for sgn in (-1.0, 1.0):
            if abs(d[axis]) < 1e-12: continue
            plane = BC[axis] + sgn * BH
            t = (plane - origin[axis]) / d[axis]
            if t <= tmin: continue
            p = origin + d * t
            ok = all(abs(p[k] - BC[k]) <= BH + 1e-9 for k in range(3) if k != axis)
            if ok:
                n = np.zeros(3); n[axis] = sgn
                hits.append((t, p, n))
    hits.sort(key=lambda h: h[0])
    return hits
def bproject(origin, to, tmin):
    d = to - origin; ln = np.linalg.norm(d); d = d / ln
    hs = [h for h in box_hits(origin, d, tmin) if h[0] <= 2 * ln]
    return (hs[0][1], hs[0][2]) if hs else None
def solve_box(receiver, light, directional, v0, v1):
    beta = 0.1; resolve = True; dx0 = dx1 = np.zeros(2); reason = "iterations"
    for it in range(24):
        if resolve:
            constraint(v0, receiver - v0.p, None, False, v1.p - v0.p, v1, True, False)
            constraint(v1, v0.p - v1.p, v0, True, light if directional else light - v1.p, None, False, directional)
            if max(np.linalg.norm(v0.constraint), np.linalg.norm(v1.constraint)) < 1e-3: return True, it
            li0 = np.linalg.inv(m(v0.b)); A = m(v1.a) @ li0
            li1 = np.linalg.inv(m(v1.b) - A @ m(v0.c))
            dx1 = li1 @ (v1.constraint - A @ v0.constraint)
            dx0 = li0 @ (v0.constraint - m(v0.c) @ dx1)
        step0 = beta * (dx0[0] * v0.dpdu + dx0[1] * v0.dpdv); step1 = beta * (dx1[0] * v1.dpdu + dx1[1] * v1.dpdv)
        if np.linalg.norm(step0) < 1e-7 and np.linalg.norm(step1) < 1e-7: return False, "stuck"
        reduce = False; n0, n1 = v0, v1
        if np.linalg.norm(step0) >= 1e-7:
            p0 = v0.p - step0
            if p0[1] - receiver[1] <= 0: p0 = v0.p + step0
            h = bproject(receiver, p0, 0.0)
            if h is None: reduce = True; reason = "v0 off the box"
            else: n0 = vertex(h[0], h[1])
        if not reduce and np.linalg.norm(step1) >= 1e-7:
            h = bproject(v0.p, v1.p - step1, 1e-4)
            if h is None: reduce = True; reason = "v1 off the box"
            else: n1 = vertex(h[0], h[1])
        if not reduce:
            toL = light if directional else light - n1.p
            if np.dot(n0.n, receiver - n0.p) * np.dot(n0.n, n1.p - n0.p) >= 0 or np.dot(n1.n, n0.p - n1.p) * np.dot(n1.n, toL) >= 0:
                reduce = True; reason = "not through"
        if reduce:
            resolve = False; beta *= 0.5
            if beta < 1e-4: return False, reason
            continue
        v0, v1 = n0, n1; resolve = True; beta = min(1.0, 2 * beta)
    return False, reason
from collections import Counter
for name, directional, light in [("sun", True, L), ("lamp", False, np.array([-0.3, 3.2, 2.0]))]:
    results = Counter(); faces = Counter()
    for x in np.arange(-2.5, 2.0, 0.02):
        for z in np.arange(0.0, 4.5, 0.02):
            rcv = np.array([x, 0.02, z])
            d = light if directional else normalize(light - rcv)
            hs = box_hits(rcv, d)
            if len(hs) != 2: continue
            v0 = vertex(hs[0][1], hs[0][2]); v1 = vertex(hs[1][1], hs[1][2])
            ok, why = solve_box(rcv, light, directional, v0, v1)
            results["solved" if ok else why] += 1
            faces[(tuple(hs[0][2]), tuple(hs[1][2]), ok)] += 1
    print(name, dict(results))
