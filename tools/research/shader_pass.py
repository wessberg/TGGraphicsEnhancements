# Inspect Unity shader pass variants and named parameter layouts.
"""shader_pass.py <shader name> <pass name> [vertex|fragment]: for one pass of one of the game's shaders in resources.assets,
its keyword sets with their blob indices, and the parameters Unity recorded for it (constant buffers with each value's name
and byte offset, textures with their registers). Read only. Also dumps the raw typetree keys once with --keys."""
import json
import sys
import UnityPy

GAME = r"C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA\Fall of Avalon_Data\resources.assets"
shader, wanted = sys.argv[1], sys.argv[2]
stage = "progFragment" if len(sys.argv) < 4 or sys.argv[3].startswith("f") else "progVertex"
env = UnityPy.load(GAME)
for obj in env.objects:
    if obj.type.name != "Shader":
        continue
    try:
        tree = obj.read_typetree()
    except Exception:
        continue
    form = tree.get("m_ParsedForm", {})
    if form.get("m_Name", "") != shader:
        continue
    keywords = form.get("m_KeywordNames", [])
    for sub in form.get("m_SubShaders", []):
        for ps in sub.get("m_Passes", []):
            state = ps.get("m_State", {})
            if state.get("m_Name") != wanted:
                continue
            names = {v: k for k, v in ps.get("m_NameIndices", [])}
            prog = ps.get(stage, {})
            for a in sys.argv:
                if a.startswith("--names="):
                    json.dump({str(k): v for k, v in names.items()}, open(a[8:], "w"))
            if "--keys" in sys.argv:
                print(list(ps.keys())); print(list(prog.keys()))
            def show_params(params, indent="  "):
                for cb in params.get("m_ConstantBuffers", []):
                    print(indent + "cbuffer %s size %d" % (names.get(cb.get("m_NameIndex")), cb.get("m_Size")))
                    for v in sorted(cb.get("m_VectorParams", []), key=lambda v: v.get("m_Index")):
                        print(indent + "  %4d %-52s dim %d type %d array %d" % (v.get("m_Index"), names.get(v.get("m_NameIndex")), v.get("m_Dim"), v.get("m_Type"), v.get("m_ArraySize")))
                    for v in sorted(cb.get("m_MatrixParams", []), key=lambda v: v.get("m_Index")):
                        print(indent + "  %4d %-52s matrix" % (v.get("m_Index"), names.get(v.get("m_NameIndex"))))
                for b in params.get("m_ConstantBufferBindings", []):
                    print(indent + "cb binding %s register %d" % (names.get(b.get("m_NameIndex")), b.get("m_Index")))
                for t in sorted(params.get("m_TextureParams", []), key=lambda t: t.get("m_Index")):
                    print(indent + "texture t%-3d %-60s sampler s%d dim %d" % (t.get("m_Index"), names.get(t.get("m_NameIndex")), t.get("m_SamplerIndex"), t.get("m_Dim")))
            common = prog.get("m_CommonParameters", {})
            print("== pass", wanted, stage, "common parameters:")
            show_params(common)
            subs = prog.get("m_SubPrograms", [])
            tiers = prog.get("m_PlayerSubPrograms", [])
            blobs = prog.get("m_ParameterBlobIndices", [])
            print("sub programs:", len(subs), "player tiers:", [len(t) for t in tiers], "parameter blob index tiers:", [len(b) for b in blobs])
            for ti, tier in enumerate(tiers):
                for si, sp in enumerate(tier):
                    kw = [keywords[i] if i < len(keywords) else str(i) for i in sp.get("m_KeywordIndices", [])]
                    pb = blobs[ti][si] if ti < len(blobs) and si < len(blobs[ti]) else None
                    print("  variant %3d: blob %4d param blob %s gpu %s: %s" % (si, sp.get("m_BlobIndex"), pb, sp.get("m_GpuProgramType"), " ".join(kw)))
            for si, sp in enumerate(subs):
                kw = [keywords[i] if i < len(keywords) else str(i) for i in sp.get("m_KeywordIndices", sp.get("m_GlobalKeywordIndices", []))]
                print("  variant %3d: blob %4d gpu %s: %s" % (si, sp.get("m_BlobIndex"), sp.get("m_GpuProgramType"), " ".join(kw)))
                if si == 0 or "--all" in sys.argv:
                    show_params(sp, "      ")
