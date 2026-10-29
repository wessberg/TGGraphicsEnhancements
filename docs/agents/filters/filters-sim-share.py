"""PT-75, on the CPU: what the normal map's share around NRD does to the mean, in the limit where NRD blurs everything.

A patch of a normal-mapped surface (geometric normal +z, shading normals tilted at random), lit by a light field L(d).
Each pixel's samples are drawn cosine-distributed about its SHADING normal, as pt_gi draws them; one that points below
the plane brings no light. The truth is the mean of the samples as the shading normal takes them (what the picture
with no denoiser converges to). Three ways to go through a filter that blurs the whole patch into one value:

  old      hand the light as the geometric normal takes it; multiply by NRD's linear SH ratio after (base54)
  linear   hand the light as the shading normal takes it divided by the linear SH ratio of the filtered light and
           direction; multiply the same ratio back (the pre-division, with the old share formula)
  parts    the same with the share as a single direction plus an even half-space (pt_nrd_prep bump_share today)

Printed: each way's mean over the truth. (Where NRD blurs nothing, 'linear' and 'parts' are exactly 1 by construction.)
"""
import numpy as np

rng = np.random.default_rng(7)
NG = np.array([0.0, 0.0, 1.0])


def normals(n, tilt_deg):
    t = np.abs(rng.normal(0.0, np.radians(tilt_deg), n))
    t = np.minimum(t, np.radians(75.0))
    phi = rng.uniform(0, 2 * np.pi, n)
    return np.stack([np.sin(t) * np.cos(phi), np.sin(t) * np.sin(phi), np.cos(t)], 1)


def cosine_about(ns, m):
    """m cosine-distributed directions about each normal: (pixels, m, 3)."""
    n = ns.shape[0]
    u1, u2 = rng.uniform(size=(n, m)), rng.uniform(size=(n, m))
    r, phi = np.sqrt(u1), 2 * np.pi * u2
    local = np.stack([r * np.cos(phi), r * np.sin(phi), np.sqrt(1 - u1)], 2)
    helper = np.where(np.abs(ns[:, :1]) < 0.9, np.array([[1.0, 0, 0]]), np.array([[0, 1.0, 0]]))
    tx = np.cross(helper, ns); tx /= np.linalg.norm(tx, axis=1, keepdims=True)
    ty = np.cross(ns, tx)
    return local[..., 0:1] * tx[:, None, :] + local[..., 1:2] * ty[:, None, :] + local[..., 2:3] * ns[:, None, :]


def share_linear(ns, c0, c1):
    level = c0 + 3.0 * c1 @ NG
    bumped = np.maximum(c0 + 3.0 * ns @ c1, 0.0)
    return np.clip(bumped / level, 0.125, 2.0) if level > 1e-12 else np.ones(len(ns))


def share_old(ns, c0, c1):
    level = c0 + 3.0 * c1 @ NG
    bumped = np.maximum(c0 + 3.0 * ns @ c1, 0.0)
    return np.clip(bumped / level, 0.0, 2.0)


def share_parts(ns, c0, c1):
    mean = c1 / c0
    b = 3.0 - 2.0 * mean @ NG
    k = (b - np.sqrt(max(b * b - 5.0 * (1.0 - min(mean @ mean, 1.0)), 0.0))) / 2.5
    single = float(np.clip(1.0 - 1.5 * k, 0.0, 1.0))
    frm = mean - k * NG
    direction = NG
    if single > 1e-3 and frm @ frm > 1e-8:
        direction = frm / np.linalg.norm(frm)
    else:
        single = 0.0
    high = direction @ NG
    single *= float(np.clip(high / 0.2, 0.0, 1.0))
    share = (1.0 - single) * 0.5 * (1.0 + ns @ NG) + single * (np.maximum(ns @ direction, 0.0) + 0.01) / (max(high, 0.0) + 0.01)
    return np.clip(share, 0.125, 8.0)


def run(name, light, tilt, pixels=4000, m=600):
    ns = normals(pixels, tilt)
    d = cosine_about(ns, m)
    above = d[..., 2] > 0.0
    L = light(d) * above                                     # the sample's light as the shading normal takes it (pdf cos/pi)
    cos_s = np.einsum("pmk,pk->pm", d, ns)
    flat = L * np.where(cos_s > 1e-3, d[..., 2] / np.maximum(cos_s, 1e-3), 0.0)   # as the geometric normal takes it
    truth = L.mean()
    # old: blur the flat light and its direction over the patch, multiply the linear ratio on
    c0, c1 = flat.mean(), (flat[..., None] * d).mean((0, 1))
    old = (c0 * share_old(ns, c0, c1)).mean() / truth
    out = {"old": old}
    for label, share in (("linear", share_linear), ("parts", share_parts)):
        s = np.ones(pixels)
        for _ in range(12):                                  # the last frame's filtered light and direction, to the fixed point
            h = L / s[:, None]
            c0, c1 = h.mean(), (h[..., None] * d).mean((0, 1))
            s = share(ns, c0, c1)
        out[label] = (h.mean() * s).mean() / truth
        out[label + " detail"] = np.corrcoef(s, L.mean(1))[0, 1], s.std() / (L.mean(1).std() / L.mean())
    print(f"{name:44s} tilt {tilt:2d} deg: old {out['old']:.3f} | pre-divided, linear share {out['linear']:.3f} | pre-divided, two parts {out['parts']:.3f}"
          f"   (the share's detail against the true shading's: linear {out['linear detail'][1]:.2f}, two parts {out['parts detail'][1]:.2f})")


def uniform(d):
    return np.ones(d.shape[:2])


def lamp(direction, width):
    v = np.array(direction, float); v /= np.linalg.norm(v)
    return lambda d: np.exp((np.einsum("pmk,k->pm", d, v) - 1.0) / width) / width


def mix(a, b, wa, wb):
    return lambda d: wa * a(d) + wb * b(d)


if __name__ == "__main__":
    for tilt in (10, 20):
        run("light from all around, evenly", uniform, tilt)
        run("one lamp-lit wall, 30 degrees off the normal", lamp((0.5, 0, 0.866), 0.02), tilt)
        run("one lamp-lit wall, 60 degrees off the normal", lamp((0.866, 0, 0.5), 0.02), tilt)
        run("one low light, 78 degrees off the normal", lamp((0.978, 0, 0.208), 0.02), tilt)
        run("even light plus a wall 60 degrees off", mix(uniform, lamp((0.866, 0, 0.5), 0.02), 1.0, 0.25), tilt)
        run("two low lights from opposite sides", mix(lamp((0.94, 0, 0.34), 0.02), lamp((-0.94, 0, 0.34), 0.02), 1.0, 1.0), tilt)
