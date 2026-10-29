"""Read shipped candle output shaders; write hashes and binding/signature metadata, not shader code.

Windows, Python + the already installed lz4 package. No GPU, game process, asset changes,
or network. This is a shader audit, NOT a particle capture or a VisualEffectAsset reader.
The Unity shader container layout is from this repo's unity_shader_blob.py research.
Optional disassembly is local research output and must not be shipped/committed.
"""
import argparse
import hashlib
import json
import mmap
from pathlib import Path
import re
import struct

import lz4.block

from dxbc_split import chunk_map, containers, disassemble, stage
from unity_shader_blob import GAME, arrays


def candle_names(data):
    """Require the serialized length, so Single does not match Single_UnscaledTime."""
    result = {}
    for match in re.finditer(rb"Hidden/VFX/VFX_CandleFlame_", data):
        at = match.start()
        if at < 4:
            continue
        length = struct.unpack_from("<I", data, at - 4)[0]
        if not 24 < length < 512 or at + length > len(data):
            continue
        raw = data[at:at + length]
        if not raw.endswith(b"/Flame/Output Particle|Shader Graph Quad - Unlit"):
            continue
        name = raw.decode("ascii")
        if name in result:
            raise ValueError("Ambiguous shader name: " + name)
        result[name] = at
    if not result:
        raise ValueError("No length-validated candle output shaders in this asset file")
    return result


def programs(data, at):
    fallback = data.find(b"Hidden/Shader Graph/FallbackError", at, at + 4096)
    if fallback < 0:
        raise ValueError("Shader Graph fallback marker missing")
    aligned = (fallback + len(b"Hidden/Shader Graph/FallbackError") + 3) & ~3
    pos = next((p for p in range(aligned, aligned + 64, 4)
                if struct.unpack_from("<III", data, p) == (1, 4, 1)), None)
    if pos is None:
        raise ValueError("Expected one Direct3D 11 platform")
    offsets, pos = arrays(data, pos + 8)
    compressed, pos = arrays(data, pos)
    expanded, pos = arrays(data, pos)
    if not (len(offsets) == len(compressed) == len(expanded) == 1 and
            len(offsets[0]) == len(compressed[0]) == len(expanded[0])):
        raise ValueError("Inconsistent shader chunk tables")
    total = struct.unpack_from("<I", data, pos)[0]
    pos += 4
    if total != sum(compressed[0]) or pos + total > len(data):
        raise ValueError("Invalid shader payload size")
    for index, (offset, count, size) in enumerate(zip(offsets[0], compressed[0], expanded[0])):
        if offset + count > total:
            raise ValueError("Chunk outside shader payload")
        chunk = lz4.block.decompress(data[pos + offset:pos + offset + count], uncompressed_size=size)
        for address, code in containers(chunk):
            yield index, address, code


def signature(data):
    if data is None:
        return []
    count = struct.unpack_from("<I", data)[0]
    result = []
    for index in range(count):
        name, semantic, system, kind, register, mask, used = struct.unpack_from("<5IBB", data, 8 + index * 24)
        end = data.index(b"\0", name)
        result.append(dict(name=data[name:end].decode("ascii"), index=semantic,
                           systemValue=system, componentType=kind, register=register,
                           mask=mask, readWriteMask=used))
    return result


def bindings(assembly):
    result = []
    for line in assembly.splitlines():
        m = re.fullmatch(r"dcl_constantbuffer CB(\d+)\[(\d+)\], (\w+)", line)
        if m:
            result.append(dict(kind="constantBuffer", slot=int(m[1]), float4Count=int(m[2]), indexing=m[3]))
            continue
        m = re.fullmatch(r"dcl_resource_(raw|structured) t(\d+)(?:, (\d+))?", line)
        if m:
            item = dict(kind=m[1], slot=int(m[2]))
            if m[3]:
                item["strideBytes"] = int(m[3])
            result.append(item)
            continue
        m = re.fullmatch(r"dcl_resource_(texture\w+) \(([^)]+)\) t(\d+)", line)
        if m:
            result.append(dict(kind=m[1], slot=int(m[3]), components=m[2]))
    return result


def audit(assets, disassembly=None):
    rows = []
    with assets.open("rb") as file, mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as data:
        for name, at in sorted(candle_names(data).items()):
            variants = []
            for chunk, address, code in programs(data, at):
                kind = stage(code)
                assembly = disassemble(code)
                if not re.search(r"(?m)^(vs|ps|cs|gs|ds|hs)_\d_\d", assembly):
                    raise ValueError("Disassembler failed for " + name)
                parts = chunk_map(code)
                if "ISG1" in parts or "OSG1" in parts:
                    raise ValueError("New signature format; extend the reader before interpreting it")
                variants.append(dict(chunk=chunk, offset=address, stage=kind, bytes=len(code),
                                     sha256=hashlib.sha256(code).hexdigest(),
                                     reflectionPresent="RDEF" in parts,
                                     inputs=signature(parts.get("ISGN")), outputs=signature(parts.get("OSGN")),
                                     bindings=bindings(assembly)))
                if disassembly is not None:
                    target = disassembly / name.split("/")[2]
                    target.mkdir(parents=True, exist_ok=True)
                    (target / f"{chunk}-{address:x}-{kind}.txt").write_text(assembly, encoding="utf-8")
            if not variants:
                raise ValueError("No DXBC programs found for " + name)
            rows.append(dict(name=name, assetNameOffset=at, variants=variants))
    return dict(schema=1, assets=str(assets.resolve()), bytes=assets.stat().st_size,
                meaning="Compiled output-shader metadata only. No live GPU resource, particle or simulation layout capture.",
                shaders=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, default=Path(GAME))
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--disassembly", type=Path)
    parser.add_argument("--header", type=Path, help="Write the native observer's hash-only candle catalogue")
    args = parser.parse_args()
    result = audit(args.assets, args.disassembly)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    if args.header:
        write_header(result, args.header)
    print(f"{len(result['shaders'])} candle shaders, "
          f"{sum(len(s['variants']) for s in result['shaders'])} programs; metadata: {args.out}")


def write_header(result, path):
    """No game bytecode: duplicate programs retain every possible family/variant."""
    unique = {}
    for shader in result["shaders"]:
        for v in shader["variants"]:
            key = (v["sha256"], v["stage"], v["bytes"])
            unique.setdefault(key, []).append(
                shader["name"].split("/")[2] + f":{v['chunk']}:{v['offset']:x}")
    lines = ["// SPDX-License-Identifier: MIT",
             "// Generated by tools/research/vfx_shader_audit.py --header. Hashes only; no game code.",
             "#pragma once", "namespace tgpt::vfx {",
             "struct KnownProgram { const char* hash; const char* stage; unsigned bytes; const char* variants; };",
             "inline constexpr KnownProgram candlePrograms[]={"]
    for (digest, stage_name, size), variants in sorted(unique.items()):
        lines.append(f'    {{"{digest}", "{stage_name}", {size}, "{";".join(variants)}"}},')
    lines += ["};", "}", ""]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\r\n")


if __name__ == "__main__":
    main()
