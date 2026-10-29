"""unity_shader_blob.py <shader name> [out dir]: unpacks one of the game's Shader Graph shaders from resources.assets into
its decompressed program chunks (chunk0.bin, chunk1.bin, ...), for dxbc_split.py to find and disassemble the DXBC in.

How it was worked out (2026-09-28, TG Path Tracing round 3): the serialized Shader holds its name as a length-prefixed
string, then its custom editor and fallback names ("Hidden/Shader Graph/FallbackError" for Shader Graphs), a few zero
counts, then: the platforms (1 of them, 4 = Direct3D 11), and three nested arrays of offsets, compressed lengths and
decompressed lengths into the compressed blob that follows (LZ4 block chunks, not LZ4 frames). The DXBC containers in
the chunks carry no reflection (RDEF): the constant buffers are unnamed, so read UnityPerMaterial by the material's
property order (tools/research/dxbc_split.py, and PROJECT-CONTEXT 9.7). Needs Python's lz4 package."""
import os
import struct
import sys

import lz4.block

GAME = r"C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA\Fall of Avalon_Data\resources.assets"


def arrays(data, pos):
    """A vector of vectors of uint32, as Unity serializes them: the outer count, then each inner count and its values."""
    outer = struct.unpack_from("<I", data, pos)[0]; pos += 4
    result = []
    for _ in range(outer):
        n = struct.unpack_from("<I", data, pos)[0]; pos += 4
        result.append(list(struct.unpack_from("<%dI" % n, data, pos))); pos += 4 * n
    return result, pos


def unpack(name, out_dir, path=GAME):
    data = open(path, "rb").read()
    at = data.find(name.encode())
    if at < 0:
        sys.exit("no shader named %r in %s" % (name, path))
    fallback = data.find(b"Hidden/Shader Graph/FallbackError", at)
    if fallback < 0 or fallback - at > 4096:
        sys.exit("not a Shader Graph laid out as expected (no fallback name after it)")
    k = (fallback + len(b"Hidden/Shader Graph/FallbackError") + 3) & ~3
    # The platform list: one platform, Direct3D 11 (4), then the offsets' outer count of 1.
    for pos in range(k, k + 64, 4):
        if struct.unpack_from("<III", data, pos) == (1, 4, 1):
            break
    else:
        sys.exit("no Direct3D 11 platform list after the names")
    pos += 8
    offsets, pos = arrays(data, pos)
    compressed, pos = arrays(data, pos)
    decompressed, pos = arrays(data, pos)
    blob = struct.unpack_from("<I", data, pos)[0]; pos += 4
    if blob != sum(compressed[0]):
        sys.exit("the blob's size doesn't match its chunks")
    os.makedirs(out_dir, exist_ok=True)
    for i, (o, c, u) in enumerate(zip(offsets[0], compressed[0], decompressed[0])):
        chunk = lz4.block.decompress(data[pos + o:pos + o + c], uncompressed_size=u)
        with open(os.path.join(out_dir, "chunk%d.bin" % i), "wb") as f:
            f.write(chunk)
        print("chunk%d.bin: %d bytes, %d DXBC containers" % (i, len(chunk), chunk.count(b"DXBC")))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    unpack(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else ".")
