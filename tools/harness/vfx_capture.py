# SPDX-License-Identifier: MIT
"""Summarize the VFX dispatch/draw diagnostic. No particle decoding or inferred component mapping."""
from __future__ import annotations
import argparse
from collections import defaultdict
import json
from pathlib import Path


def bindings(event, kind):
    """Buffer bindings only; shader slots and constant-buffer ranges remain in the source JSON."""
    for stage in ("VS", "PS", "CS"):
        for b in event.get(stage, {}).get(kind, []):
            r = b.get("resource") or {}
            if r.get("dimension") == 1:
                yield stage, b, r


def inputs(event):
    for kind in ("srvs", "constantBuffers"):
        for stage, b, r in bindings(event, kind):
            yield stage + ":" + kind + ":" + str(b["slot"]), r
    for b in event.get("vertices", []):
        if b.get("resource"):
            yield "IA:vertex:" + str(b["slot"]), b["resource"]
    if event.get("indices") and event["indices"].get("resource"):
        yield "IA:index", event["indices"]["resource"]
    if event.get("indirect"):
        yield "indirectArgs", event["indirect"]


def is_dispatch(e):
    return e.get("kind") in ("dispatch", "dispatchIndirect")


def summarize(frames):
    """Frames: (relative path, native document, matching image document). Also used by independent fixtures."""
    errors, evidence, resources, roles = [], [], {}, defaultdict(set)
    # Lifetime IDs are local to this observer process. Never group them by address or byte size.
    for path, doc, image in frames:
        if image.get("primer"):
            continue
        for e in doc.get("events", []):
            if e.get("candle") and not is_dispatch(e):
                for role, r in inputs(e):
                    roles[r["id"]].add(role)
                    resources[r["id"]] = r
    writers = defaultdict(set)
    for _, doc, image in frames:
        if image.get("primer"):
            continue
        for e in doc.get("events", []):
            if is_dispatch(e):
                for _, _, r in bindings(e, "uavs"):
                    if r["id"] in resources and e.get("cs"):
                        writers[r["id"]].add(e["cs"])
    # Attribute inputs are VS t0, by the shipped shader audit; CB/global buffers alone do not identify a candle kernel.
    attribute_ids = {rid for rid, rr in roles.items() if "VS:srvs:0" in rr}
    attribute_kernels = set().union(*(writers[rid] for rid in attribute_ids)) if attribute_ids else set()
    rows, candidates = [], []
    for path, doc, image in frames:
        if image.get("primer"):
            continue
        prefix = str(path)
        if doc.get("schema") != 2:
            errors.append(prefix + ": unsupported native schema")
        if doc.get("faults") or doc.get("hookFailures") or doc.get("unknownLists") or doc.get("staleLists") or doc.get("boundaryDrops"):
            errors.append(prefix + ": observer faults, refused hooks or unknown/stale command lists")
        if doc.get("deviceGeneration") != doc.get("currentDeviceGeneration"):
            errors.append(prefix + ": device changed during capture")
        if doc.get("previousBoundary") != doc.get("frame", 0) - 1:
            errors.append(prefix + ": frame boundaries are not consecutive")
        if not image.get("grabbed") or image.get("nativeCompletedAtScreenshot") != doc.get("frame") or image.get("frame") != doc.get("frame"):
            errors.append(prefix + ": screenshot/native acknowledgement mismatch")
        if "mode" in image and image["mode"] != ("On" if image.get("expectedTraced") else "Off"):
            errors.append(prefix + ": requested PT mode was not active")
        if image.get("expectedTraced") and image.get("tracer", {}).get("disabled"):
            errors.append(prefix + ": native tracer disabled itself")
        events = doc.get("events", [])
        sequence = [e.get("sequence") for e in events]
        if sequence != list(range(1, len(sequence) + 1)):
            errors.append(prefix + ": event ordering is incomplete")
        trace = [e["sequence"] for e in events if e.get("kind") == "traceBegin"]
        if image.get("expectedTraced") and len(trace) != 1:
            errors.append(prefix + ": expected exactly one actual trace marker, saw " + str(len(trace)))
        if not image.get("expectedTraced") and trace:
            errors.append(prefix + ": tracer ran during Off sample")
        dispatches = [e for e in events if is_dispatch(e)]
        draws = [e for e in events if not is_dispatch(e) and e.get("kind") != "traceBegin"]
        read_by = defaultdict(list)
        for e in draws:
            for role, r in inputs(e):
                read_by[r["id"]].append({"sequence": e["sequence"], "kind": e["kind"], "role": role,
                    "vs": e.get("vs"), "ps": e.get("ps"), "candle": bool(e.get("candle"))})
        phase = Path(path).parent.name
        clock = image.get("clock", {})
        if "live" in phase and clock.get("timeScale", 0) <= 0:
            errors.append(prefix + ": live phase is frozen")
        frame_row = {"file": prefix, "frame": doc.get("frame"), "phase": phase,
            "traceSequences": trace, "dispatches": len(dispatches), "draws": len(draws),
            "candleDraws": sum(bool(e.get("candle")) for e in draws), "buffers": []}
        for e in events:
            relevant = e.get("candle") or any(r["id"] in resources for _, r in inputs(e))
            if is_dispatch(e):
                relevant |= any(r["id"] in resources for _, _, r in bindings(e, "uavs"))
            if relevant and (not e.get("bindingsComplete", True) or e.get("predicate")):
                errors.append(prefix + ": relevant event " + str(e["sequence"]) + " has unknown bindings or GPU predication")
            if is_dispatch(e) and relevant and not e.get("cs"):
                errors.append(prefix + ": relevant compute shader was not seen at creation")
        for rid in sorted(resources):
            writes = []
            for e in dispatches:
                for _, b, r in bindings(e, "uavs"):
                    if r["id"] != rid:
                        continue
                    writes.append({"sequence": e["sequence"], "hash": e.get("cs"), "slot": b["slot"],
                        "groups": e.get("groups"), "indirect": e.get("indirect"), "indirectOffset": e.get("indirectOffset"),
                        "beforeTrace": e["sequence"] < trace[0] if trace else None,
                        "deferred": e.get("deferred"), "commandList": e.get("list"),
                        "executionUnknown": e.get("groups") is None or bool(e.get("predicate"))})
            reads = read_by.get(rid, [])
            if writes or reads:
                frame_row["buffers"].append({"id": rid, "writes": writes, "drawReads": reads,
                    "submittedWithoutDraw": bool(writes) and not reads})
        for e in dispatches:
            if e.get("cs") not in attribute_kernels:
                continue
            for _, b, r in bindings(e, "uavs"):
                if r["id"] not in resources:
                    candidates.append({"file": prefix, "sequence": e["sequence"], "hash": e["cs"], "resource": r,
                        "slot": b["slot"], "reason": "Same shader as a known attribute writer; component/family membership unproven."})
        rows.append(frame_row)
    if not attribute_ids:
        errors.append("No identified candle VS t0 attribute allocation was observed.")
    if not attribute_kernels:
        errors.append("No compute UAV writer overlaps an identified candle attribute allocation. Inspect upstream copies/CPU updates before choosing capture.")
    # A UAV binding proves a submitted potential writer, not which bytes the shader actually writes.
    return {"schema": 2, "diagnosticOnly": True, "particleCaptureComplete": False,
        "errors": sorted(set(errors)), "attributeBuffers": sorted(attribute_ids),
        "resources": [{**r, "roles": sorted(roles[rid]), "writerHashes": sorted(writers[rid])} for rid, r in sorted(resources.items())],
        "attributeWriterHashes": sorted(attribute_kernels), "frames": rows, "unmappedWriterAllocations": candidates,
        "limits": ["Bindings and submitted calls only. UAV write instructions/affected ranges require compute decoding.",
                   "Indirect counts are unknown: zero groups cannot be excluded without a later buffer readback.",
                   "An allocation can batch multiple systems. Public VFX IDs/counts/culling are evidence, not a buffer-to-system mapping.",
                   "No buffers read/copied, no captured particles, no D3D12 transfer, no rendering suppression."]}


def pictures(run, frames):
    """Keep full-size evidence, plus a small labelled sheet to check that the camera really turned."""
    from PIL import Image, ImageDraw
    tiles = {}
    formats = {"RGB24": ("RGB", "RGB"), "RGBA32": ("RGBA", "RGBA"),
               "ARGB32": ("RGBA", "ARGB"), "BGRA32": ("RGBA", "BGRA")}
    errors = []
    for path, _, metadata in frames:
        if metadata.get("primer") or not metadata.get("grabbed"):
            continue
        folder = (run / path).parent
        try:
            mode, rawmode = formats[metadata["format"]]
            size = (metadata["width"], metadata["height"])
            data = (folder / metadata["image"]).read_bytes()
            expected = size[0] * size[1] * (3 if mode == "RGB" else 4)
            if len(data) != expected:
                raise ValueError("Screenshot byte count differs from metadata")
            picture = Image.frombytes(mode, size, data, "raw", rawmode, 0, -1).convert("RGB")
            picture.save((folder / metadata["image"]).with_suffix(".jpg"), quality=95)
            if folder not in tiles:
                picture.thumbnail((640, 360))
                tile = Image.new("RGB", (640, 390)); tile.paste(picture, (0, 30))
                ImageDraw.Draw(tile).text((6, 6), str(folder.relative_to(run)), fill="white")
                tiles[folder] = tile
        except (OSError, ValueError, KeyError) as ex:
            errors.append(str(path) + ": screenshot conversion: " + str(ex))
    if tiles:
        sheet = Image.new("RGB", (640 * 4, 390 * ((len(tiles) + 3) // 4)))
        for index, tile in enumerate(tiles.values()):
            sheet.paste(tile, ((index % 4) * 640, (index // 4) * 390))
        sheet.save(run / "vfx-overview.jpg", quality=95)
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    args = parser.parse_args()
    frames, errors = [], []
    for image_path in sorted(args.run.rglob("frame-*-image.json")):
        try:
            image = json.loads(image_path.read_text(encoding="utf-8-sig"))
            path = image_path.with_name(image_path.name.replace("-image.json", ".json"))
            native = json.loads(path.read_text(encoding="utf-8-sig"))
            frames.append((str(path.relative_to(args.run)), native, image))
        except (OSError, ValueError) as ex:
            errors.append(str(image_path.relative_to(args.run)) + ": " + str(ex))
    for failure in args.run.rglob("capture-error.json"):
        errors.append(str(failure.relative_to(args.run)) + ": " + failure.read_text(encoding="utf-8-sig"))
    errors += pictures(args.run, frames)
    result = summarize(frames)
    request_file = args.run / "request.json"
    try:
        request = json.loads(request_file.read_text(encoding="utf-8-sig"))
        for scenario in request.get("scenarios", []):
            for step in scenario.get("steps", []):
                if step.get("vfxCapture") and not (args.run / scenario["key"] / step["name"] / "step.json").exists():
                    errors.append(scenario["key"] + "/" + step["name"] + ": requested diagnostic step missing")
    except (OSError, ValueError) as ex:
        errors.append("request.json unavailable: " + str(ex))
    if not frames:
        errors.append("No VFX native/image frames found.")
    # Missing whole phases must not look like successful no-draw evidence.
    for step in args.run.rglob("step.json"):
        try:
            info = json.loads(step.read_text(encoding="utf-8-sig"))
            if "vfx" not in info.get("name", ""):
                continue
            if info.get("error") or info.get("traced") != info.get("expectedTraced"):
                errors.append(str(step.relative_to(args.run)) + ": step failed or PT activation mismatch")
            for phase, minimum in (("vfx-live-facing", 4), ("vfx-live-away", 8), ("vfx-live-returned", 4), ("vfx-frozen", 4)):
                observed = [f for f in frames if Path(f[0]).parent == step.parent.relative_to(args.run) / phase and not f[2].get("primer")]
                if len(observed) < minimum:
                    errors.append(str(step.parent.relative_to(args.run) / phase) + ": missing sampled frames")
        except (OSError, ValueError) as ex:
            errors.append(str(step) + ": " + str(ex))
    result["errors"] = sorted(set(result["errors"] + errors))
    (args.run / "vfx-summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    lines = ["# VFX dispatch/draw diagnostic", "", "Metadata only; particle capture remains open. Read vfx-overview.jpg to verify the view turn; full-size JPGs sit beside the raw screenshots.", "",
        "| Phase/frame | Dispatch calls | Draw calls | Candle draws | Trace order | Writer allocations without a draw |",
        "|---|---:|---:|---:|---|---|"]
    for f in result["frames"]:
        undrawn = [str(b["id"]) for b in f["buffers"] if b["submittedWithoutDraw"] and b["id"] in result["attributeBuffers"]]
        lines.append(f"| {f['file']} | {f['dispatches']} | {f['draws']} | {f['candleDraws']} | {f['traceSequences']} | {', '.join(undrawn) or '-'} |")
    lines += ["", "Attribute allocations: " + ", ".join(map(str, result["attributeBuffers"])), "",
        "Compute hashes bound while those allocations were UAV outputs:"] + ["- `" + h + "`" for h in result["attributeWriterHashes"]]
    lines += ["", "Read `vfx-summary.json`: resources gives addresses/size/stride/roles, frames.buffers gives every writer and draw reader with its sequence. Compare the same lifetime ID facing/away/returned. A direct dispatch with positive groups and no draw is evidence of submitted off-view work; an indirect one needs its arguments read before claiming it ran. `beforeTrace` locates writers against the actual TGPT event on the On samples.", "",
        "The matching `frame-*-image.json` contains all loaded VFX components and their public culling/sleeping/active state. Do not match them to buffers by bounds or names. Unmapped allocations using the same compute hash are listed separately; they are candidates, not captured systems.", ""]
    lines += ["- " + text for text in result["limits"]]
    lines += ["", "Evidence errors:"] + (["- " + e for e in result["errors"]] or ["None in the recorded metadata. This is not all-system capture proof."])
    (args.run / "vfx-report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"VFX diagnostic: {len(result['frames'])} frames, {len(result['attributeBuffers'])} attribute allocations, {len(result['attributeWriterHashes'])} writer hashes, {len(result['errors'])} evidence errors.")
    return 1 if result["errors"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
