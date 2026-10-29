# Inspect a local Unity shader blob and its parameter table using D3DDisassemble.
"""blob.py <dir> <program blob index> [param blob index]: one program of a shader unpacked by unity_shader_blob.py
(chunk*.bin in <dir>): its DXBC disassembled into <dir>/blob<index>.txt, and, with a parameter blob index, the
parameters Unity recorded for it (names resolved through the name table given as <dir>/names.json if present,
else printed as indices). Read only, apart from the text file it writes."""
import ctypes
import json
import os
import struct
import sys

d3dc = ctypes.WinDLL("d3dcompiler_47.dll")


def disassemble(code):
    out = ctypes.c_void_p()
    hr = d3dc.D3DDisassemble(ctypes.c_char_p(code), ctypes.c_size_t(len(code)), ctypes.c_uint(0), None, ctypes.byref(out))
    if hr != 0:
        return "D3DDisassemble failed 0x%08x" % (hr & 0xFFFFFFFF)
    vtbl = ctypes.cast(ctypes.cast(out, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
    getptr = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(vtbl[3])
    getsize = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(vtbl[4])
    release = ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(vtbl[2])
    text = ctypes.string_at(getptr(out), getsize(out)).decode("latin1")
    release(out)
    return text


def entry(folder, index):
    table = open(os.path.join(folder, "chunk0.bin"), "rb").read()
    n = struct.unpack_from("<I", table, 0)[0]
    if index >= n:
        sys.exit("only %d blobs" % n)
    off, length, segment = struct.unpack_from("<III", table, 4 + 12 * index)
    data = open(os.path.join(folder, "chunk%d.bin" % segment), "rb").read()
    return data[off:off + length]


class Reader:
    def __init__(self, data):
        self.d, self.p = data, 0

    def i32(self):
        v = struct.unpack_from("<i", self.d, self.p)[0]; self.p += 4; return v

    def i8(self):
        v = struct.unpack_from("<b", self.d, self.p)[0]; self.p += 1; return v

    def align(self):
        self.p = (self.p + 3) & ~3


def vector_params(r):
    out = []
    for _ in range(r.i32()):
        name, index, array = r.i32(), r.i32(), r.i32()
        typ, dim = r.i8(), r.i8(); r.align()
        out.append(("vector", name, index, array, typ, dim))
    return out


def matrix_params(r):
    out = []
    for _ in range(r.i32()):
        name, index, array = r.i32(), r.i32(), r.i32()
        typ, rows = r.i8(), r.i8(); r.align()
        out.append(("matrix", name, index, array, typ, rows))
    return out


def parameters(data, names):
    r = Reader(data)
    nm = lambda i: names.get(str(i), names.get(i, "#%d" % i))
    vectors = vector_params(r)
    matrices = matrix_params(r)
    for v in vectors:
        print("  loose vector %s at %d dim %d" % (nm(v[1]), v[2], v[5]))
    for _ in range(r.i32()):
        name, index, sampler = r.i32(), r.i32(), r.i32()
        ms, dim = r.i8(), r.i8(); r.align()
        print("  texture t%-3d %-64s sampler s%d dim %d" % (index, nm(name), sampler, dim))
    for _ in range(r.i32()):
        name, index, array = r.i32(), r.i32(), r.i32()
        print("  buffer %s register %d" % (nm(name), index))
    for _ in range(r.i32()):
        name = r.i32()
        ms = matrix_params(r)
        vs = vector_params(r)
        structs = r.i32()
        for _ in range(structs):
            sname, sindex, sarray, ssize = r.i32(), r.i32(), r.i32(), r.i32()
            vector_params(r); matrix_params(r)
        size = r.i32()
        partial = r.i8(); r.align()
        print("  cbuffer %s size %d%s" % (nm(name), size, " (partial)" if partial else ""))
        for v in sorted(vs + ms, key=lambda v: v[2]):
            print("    %4d %-56s %s %d" % (v[2], nm(v[1]), "dim" if v[0] == "vector" else "matrix rows", v[5]))
    for _ in range(r.i32()):
        name, index, array = r.i32(), r.i32(), r.i32()
        print("  cb binding %s register b%d" % (nm(name), index))
    for _ in range(r.i32()):
        name, index, original = r.i32(), r.i32(), r.i32()
        print("  uav %s register %d" % (nm(name), index))
    for _ in range(r.i32()):
        sampler, bind = struct.unpack_from("<Ii", data, r.p); r.p += 8
        print("  sampler 0x%08x at s%d" % (sampler, bind))
    print("  (read %d of %d bytes)" % (r.p, len(data)))


if __name__ == "__main__":
    folder, index = sys.argv[1], int(sys.argv[2])
    blob = entry(folder, index)
    at = blob.find(b"DXBC")
    print("blob %d: %d bytes, header %s, DXBC at %d" % (index, len(blob), blob[:min(at, 40)].hex() if at >= 0 else blob[:40].hex(), at))
    if at >= 0:
        text = disassemble(blob[at:at + struct.unpack_from("<I", blob, at + 24)[0]])
        with open(os.path.join(folder, "blob%d.txt" % index), "w") as f:
            f.write(text)
        print("  %d lines of disassembly in blob%d.txt" % (text.count("\n"), index))
    if len(sys.argv) > 3:
        names = {}
        path = os.path.join(folder, "names.json")
        if os.path.exists(path):
            names = json.load(open(path))
        print("parameter blob %s:" % sys.argv[3])
        parameters(entry(folder, int(sys.argv[3])), names)
