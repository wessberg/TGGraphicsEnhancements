"""Splits the DXBC containers out of decompressed Unity shader chunks, names each by its stage and the RealHair
properties its reflection mentions, and disassembles the ones asked for with d3dcompiler_47's D3DDisassemble."""
import ctypes, struct, sys, os, glob

d3dc = ctypes.WinDLL("d3dcompiler_47.dll")
class Blob(ctypes.Structure): pass
# ID3DBlob: IUnknown with GetBufferPointer (slot 3) and GetBufferSize (slot 4)
def disassemble(code):
    out = ctypes.c_void_p()
    hr = d3dc.D3DDisassemble(ctypes.c_char_p(code), ctypes.c_size_t(len(code)), ctypes.c_uint(0), None, ctypes.byref(out))
    if hr != 0: return "D3DDisassemble failed 0x%08x" % (hr & 0xFFFFFFFF)
    vtbl = ctypes.cast(ctypes.cast(out, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
    getptr = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(vtbl[3])
    getsize = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(vtbl[4])
    release = ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(vtbl[2])
    ptr, size = getptr(out), getsize(out)
    text = ctypes.string_at(ptr, size).decode("latin1")
    release(out)
    return text

def containers(data):
    pos = 0
    while True:
        i = data.find(b"DXBC", pos)
        if i < 0: return
        pos = i + 4
        if i + 32 > len(data): return
        total = struct.unpack_from("<I", data, i + 24)[0]
        nch = struct.unpack_from("<I", data, i + 28)[0]
        if not (32 < total < 8_000_000 and 0 < nch < 24) or i + total > len(data): continue
        yield i, data[i:i + total]
        pos = i + total

def chunk_map(blob):
    nch = struct.unpack_from("<I", blob, 28)[0]
    offs = struct.unpack_from("<%dI" % nch, blob, 32)
    out = {}
    for o in offs:
        out[blob[o:o + 4].decode("latin1")] = blob[o + 8:o + 8 + struct.unpack_from("<I", blob, o + 4)[0]]
    return out

def stage(blob):
    c = chunk_map(blob)
    code = c.get("SHEX") or c.get("SHDR")
    if not code: return "?"
    t = struct.unpack_from("<I", code, 0)[0] >> 16
    return {0: "ps", 1: "vs", 2: "gs", 3: "hs", 4: "ds", 5: "cs"}.get(t, str(t))

if __name__ == "__main__":
    want = sys.argv[1:]  # substrings to look for in the reflection
    rows = []
    for path in sorted(glob.glob("chunk*.bin")):
        data = open(path, "rb").read()
        for off, blob in containers(data):
            c = chunk_map(blob)
            rdef = c.get("RDEF", b"")
            names = [w for w in ["_RootColor", "_EndColor", "_HighlightAColor", "_IDMap", "_RootMap", "_DiffuseColor", "_VertexBaseColor", "_VertexColorStrength", "_BaseColorStrength", "_GlobalStrength"] if w.encode() in rdef]
            rows.append((path, off, stage(blob), len(blob), names))
    for r in rows:
        if r[2] == "ps" and (not want or all(w in r[4] for w in want)):
            print(r[0], hex(r[1]), r[2], r[3], ",".join(r[4]))
