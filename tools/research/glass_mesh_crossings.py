"""glass_mesh_crossings.py: scans the game's asset bundles for meshes whose names say glass (jars, bottles, flasks, potions)
and counts how many times rays through each cross its surface: a closed solid is crossed twice, a hollow one (an inner
wall) four times. Glass caustics (pt_common.hlsli caustic_through) are solved for a closed object crossed twice; a hollow
one keeps the straight tint. Needs UnityPy (pip install UnityPy; set UNITYPY_PATH to a folder holding it otherwise) and
numpy. About 35 s over the game's 2224 bundles (scene bundles skipped). Result of 2026-09-28 (round 3): single bottles
(whisky, mead, wine, Bottle_01 to 06, Potion_01, Huge_Jar) are closed shells; Prop_Jars_01*, the drinking glasses and
Potion_02 are hollow."""
import os, sys, glob, time
if os.environ.get("UNITYPY_PATH"): sys.path.insert(0, os.environ["UNITYPY_PATH"])
import warnings; warnings.filterwarnings("ignore")
import UnityPy
import numpy as np
UnityPy.config.FALLBACK_UNITY_VERSION = "6000.0.64f1"
AA = r"C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA\Fall of Avalon_Data\StreamingAssets\aa\StandaloneWindows64"
WORDS = ("jar", "bottle", "flask", "potion", "vial", "phial", "glass")

def crossings(verts, tris, origin, direction):
    v0 = verts[tris[:, 0]]; e1 = verts[tris[:, 1]] - v0; e2 = verts[tris[:, 2]] - v0
    p = np.cross(direction, e2); det = np.einsum('ij,ij->i', e1, p)
    ok = np.abs(det) > 1e-12; inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
    t = origin - v0; u = np.einsum('ij,ij->i', t, p) * inv
    q = np.cross(t, e1); v = np.einsum('j,ij->i', direction, q) * inv; d = np.einsum('ij,ij->i', e2, q) * inv
    return int((ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (d > 0)).sum())

def analyse(m, name):
    data = m.export()
    vs, fs = [], []
    for line in data.splitlines():
        if line.startswith('v '): vs.append([float(x) for x in line.split()[1:4]])
        elif line.startswith('f '): fs.append([int(x.split('/')[0]) - 1 for x in line.split()[1:4]])
    if not vs or not fs: return name + ': no geometry'
    verts = np.array(vs); tris = np.array(fs)
    lo, hi = verts.min(0), verts.max(0); c = (lo + hi) / 2
    side = [crossings(verts, tris, np.array([lo[0] - 1.0, lo[1] + (hi[1] - lo[1]) * f, c[2]]), np.array([1.0, 0.0, 0.0])) for f in (0.3, 0.5, 0.7)]
    down = crossings(verts, tris, np.array([c[0], hi[1] + 1.0, c[2]]), np.array([0.0, -1.0, 0.0]))
    return '%s: %d vertices, %d triangles, size %s; sideways at 30/50/70%% height %s crossings, down the middle %d' % (
        name, len(verts), len(tris), np.round(hi - lo, 3), side, down)

start = time.time(); seen = set(); found = 0
bundles = sorted(glob.glob(os.path.join(AA, '*.bundle')))
for i, path in enumerate(bundles):
    if 'scenes_' in os.path.basename(path): continue  # scene bundles: props come from the shared ones
    try:
        env = UnityPy.load(path)
    except Exception:
        continue
    for obj in env.objects:
        if obj.type.name != 'Mesh': continue
        try:
            name = obj.peek_name()
        except Exception:
            try: name = obj.read().m_Name
            except Exception: continue
        low = (name or '').lower()
        if not any(w in low for w in WORDS) or name in seen: continue
        seen.add(name)
        try:
            print(os.path.basename(path), '|', analyse(obj.read(), name), flush=True); found += 1
        except Exception as e:
            print(os.path.basename(path), '|', name, 'unreadable:', e, flush=True)
    if i % 200 == 0: print('... %d of %d bundles, %d meshes, %.0f s' % (i, len(bundles), found, time.time() - start), flush=True)
print('done: %d glass-named meshes in %.0f s' % (found, time.time() - start))
