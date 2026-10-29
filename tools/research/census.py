# Count materials and shader properties from Unity asset bundles without loading texture streams.
"""census.py <out.json>: every Material in the game's asset bundles (the serialized files only; texture streams skipped),
by shader: how many, which keywords, and for a list of properties how many materials set them away from the value named.
Shaders are named from the Shader objects met on the way (by path id, which is the same in every bundle). Read only."""
import collections
import io
import json
import os
import sys
import time
import warnings

import bundlescan
import UnityPy
import UnityPy.config

warnings.simplefilter("ignore")
UnityPy.config.FALLBACK_UNITY_VERSION = "6000.0.64f1"
ROOT = bundlescan.ROOT
shader_names = {}
stats = {}
materials = []
t0 = time.time()
names = sorted(os.listdir(ROOT))
errors = 0
for i, name in enumerate(names):
    path = os.path.join(ROOT, name)
    try:
        b = bundlescan.open_bundle(path)
    except Exception as ex:
        errors += 1
        continue
    if b is None:
        continue
    f, data_start, blocks, nodes = b
    try:
        for node in nodes:
            low = node[3].lower()
            if low.endswith(".ress") or low.endswith(".resource") or low.endswith(".sharedassets"):
                continue
            try:
                data = bundlescan.node_bytes(f, data_start, blocks, node)
                if b"_SavedProperties"[:0] == b"" and data.find(b"m_SavedProperties") < 0 and data.find(b"_BaseColor") < 0 and data.find(b"Shader") < 0:
                    pass
                env = UnityPy.load(data)
            except Exception as ex:
                errors += 1
                continue
            for obj in env.objects:
                tn = obj.type.name
                if tn == "Shader":
                    if obj.path_id in shader_names:
                        continue
                    try:
                        tree = obj.read_typetree()
                        shader_names[obj.path_id] = tree.get("m_ParsedForm", {}).get("m_Name", "?")
                    except Exception:
                        shader_names[obj.path_id] = "(unreadable)"
                elif tn == "Material":
                    try:
                        tree = obj.read_typetree()
                    except Exception:
                        errors += 1
                        continue
                    sp = tree.get("m_SavedProperties", {})
                    materials.append({
                        "name": tree.get("m_Name"),
                        "bundle": name,
                        "shader": tree.get("m_Shader", {}).get("m_PathID", 0),
                        "keywords": tree.get("m_ValidKeywords", []) or [],
                        "floats": {k: v for k, v in sp.get("m_Floats", [])},
                        "colors": {k: [v.get(c, 0) for c in "rgba"] for k, v in sp.get("m_Colors", [])},
                        "textures": {k: [1 if v.get("m_Texture", {}).get("m_PathID", 0) else 0, v.get("m_Scale", {}).get("x", 1), v.get("m_Scale", {}).get("y", 1),
                                         v.get("m_Offset", {}).get("x", 0), v.get("m_Offset", {}).get("y", 0)] for k, v in sp.get("m_TexEnvs", [])},
                        "queue": tree.get("m_CustomRenderQueue"),
                    })
    finally:
        f.close()
    if i % 100 == 0:
        print("%d/%d bundles, %d materials, %d shaders, %d errors, %.0f s" % (i, len(names), len(materials), len(shader_names), errors, time.time() - t0), flush=True)
json.dump({"shaders": {str(k): v for k, v in shader_names.items()}, "materials": materials}, open(sys.argv[1], "w"))
print("done: %d materials, %d shaders, %d errors, %.0f s" % (len(materials), len(shader_names), errors, time.time() - t0))
