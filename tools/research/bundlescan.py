# Read UnityFS bundle metadata and selected serialized nodes for shader/material research.
"""bundlescan.py <out.txt> <needle> [...]: where byte strings occur in the serialized files inside the game's asset bundles
(UnityFS, LZ4 blocks; the texture and mesh streams are skipped). Read only; decompresses in memory, writes only the hit list."""
import os
import struct
import sys
import time

import lz4.block

ROOT = r"C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA\Fall of Avalon_Data\StreamingAssets\aa\StandaloneWindows64"


def cstr(buf, o):
    e = buf.index(b"\0", o)
    return buf[o:e].decode("utf-8", "replace"), e + 1


def open_bundle(path):
    f = open(path, "rb")
    head = f.read(4096)
    if not head.startswith(b"UnityFS\0"):
        f.close(); return None
    o = 8
    version = struct.unpack_from(">I", head, o)[0]; o += 4
    _, o = cstr(head, o)
    _, o = cstr(head, o)
    size, csize, usize, flags = struct.unpack_from(">qIII", head, o); o += 20
    if version >= 7:
        o = (o + 15) & ~15
    if flags & 0x80:
        f.seek(size - csize); info = f.read(csize); data_start = o
    else:
        f.seek(o); info = f.read(csize); data_start = o + csize
    comp = flags & 0x3F
    if comp in (2, 3):
        info = lz4.block.decompress(info, uncompressed_size=usize)
    elif comp != 0:
        f.close(); return None
    if flags & 0x200:
        data_start = (data_start + 15) & ~15
    p = 16
    nblocks = struct.unpack_from(">i", info, p)[0]; p += 4
    blocks = []
    for _ in range(nblocks):
        u, c, fl = struct.unpack_from(">IIH", info, p); p += 10
        blocks.append((u, c, fl))
    nnodes = struct.unpack_from(">i", info, p)[0]; p += 4
    nodes = []
    for _ in range(nnodes):
        off, sz, fl = struct.unpack_from(">qqI", info, p); p += 20
        name, p = cstr(info, p)
        nodes.append((off, sz, fl, name))
    return f, data_start, blocks, nodes


def node_bytes(f, data_start, blocks, node):
    """The node's bytes: decompresses only the blocks it spans."""
    off, sz = node[0], node[1]
    out = bytearray()
    upos = 0; cpos = data_start
    for u, c, fl in blocks:
        if upos + u > off and upos < off + sz:
            f.seek(cpos); raw = f.read(c)
            comp = fl & 0x3F
            if comp in (2, 3): raw = lz4.block.decompress(raw, uncompressed_size=u)
            elif comp != 0: raise ValueError("compression %d" % comp)
            a = max(off - upos, 0); b = min(off + sz - upos, u)
            out += raw[a:b]
        upos += u; cpos += c
        if upos >= off + sz: break
    return bytes(out)


def main():
    out_path = sys.argv[1]
    needles = [a.encode("latin1").decode("unicode_escape").encode("latin1") for a in sys.argv[2:]]
    names = sorted(os.listdir(ROOT))
    t0 = time.time(); scanned = 0; hits = 0
    with open(out_path, "w", encoding="utf-8") as out:
        for i, name in enumerate(names):
            path = os.path.join(ROOT, name)
            try:
                b = open_bundle(path)
            except Exception as ex:
                out.write(f"!! {name}: {ex}\n"); continue
            if b is None:
                continue
            f, data_start, blocks, nodes = b
            try:
                for node in nodes:
                    low = node[3].lower()
                    if low.endswith(".ress") or low.endswith(".resource"):
                        continue
                    try:
                        data = node_bytes(f, data_start, blocks, node)
                    except Exception as ex:
                        out.write(f"!! {name}/{node[3]}: {ex}\n"); continue
                    scanned += len(data)
                    for n in needles:
                        s = 0
                        while True:
                            k = data.find(n, s)
                            if k < 0: break
                            out.write(f"{name}\t{node[3]}\t{k}\t{n!r}\n"); hits += 1
                            s = k + 1
            finally:
                f.close()
            if i % 200 == 0:
                print(f"{i}/{len(names)} bundles, {scanned / 1e9:.1f} GB scanned, {hits} hits, {time.time() - t0:.0f} s", flush=True)
    print(f"done: {scanned / 1e9:.1f} GB scanned, {hits} hits, {time.time() - t0:.0f} s")


if __name__ == "__main__":
    main()
