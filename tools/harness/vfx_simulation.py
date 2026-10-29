# SPDX-License-Identifier: MIT
"""Validate raw simulation evidence. Never interpret a buffer as particles by plausible floats."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct

import vfx_capture


def validate(folder, doc, image):
    errors, rows = [], []
    frame = doc.get("frame")
    if doc.get("schema") != 1 or doc.get("particleCaptureComplete") is not False:
        errors.append("unsupported schema or unearned particle-capture claim")
    if frame != image.get("frame") or image.get("nativeCompletedAtScreenshot") != frame or not image.get("grabbed"):
        errors.append("simulation/screenshot frame mismatch")
    if not image.get("primer") and doc.get("previousBoundary") != frame - 1:
        errors.append("nonconsecutive simulation boundaries")
    if doc.get("faults"):
        errors.append("simulation capture faults")
    seen = set()
    for packet in doc.get("packets", []):
        serial = packet["serial"]
        if serial in seen:
            errors.append("duplicate packet serial")
        seen.add(serial)
        if packet.get("submittedAfter", frame) >= frame:
            errors.append("packet claims future frame ownership")
        if packet.get("predicate"):
            errors.append("predicated dispatch does not prove simulation execution")
        if packet.get("executions") != 1 or not packet.get("bindingsComplete"):
            errors.append("reused command list or incomplete compute bindings")
        code = folder / "simulation" / "programs" / (packet["cs"] + ".dxbc")
        if not code.is_file() or hashlib.sha256(code.read_bytes()).hexdigest() != packet["cs"]:
            errors.append("compute program missing or hash mismatch")
        groups = packet.get("groups")
        resources = []
        for binding in packet["buffers"]:
            if binding.get("resource", {}).get("dimension") != 1:
                if binding.get("file"):
                    errors.append("a texture was copied")
                continue
            file = folder / binding.get("file", "missing")
            if not file.is_file():
                errors.append("missing buffer file")
                continue
            data = file.read_bytes()
            if len(data) != binding["bytes"] or len(data) != binding["resource"]["bytes"]:
                errors.append("buffer byte count mismatch")
            if hashlib.sha256(data).hexdigest() != binding.get("sha256"):
                errors.append("buffer value/hash mismatch")
            if binding["role"] == "indirect":
                offset = packet["indirectOffset"]
                if offset < 0 or offset % 4 or offset + 12 > len(data):
                    errors.append("indirect argument offset outside buffer")
                else:
                    groups = list(struct.unpack_from("<3I", data, offset))
            resources.append({"role": binding["role"], "slot": binding["slot"],
                              "resource": binding["resource"]["id"], "bytes": len(data),
                              "file": binding["file"], "sha256": binding["sha256"]})
        rows.append({"serial": serial, "cs": packet["cs"], "submittedAfter": packet["submittedAfter"],
                     "retained": packet["submittedAfter"] != doc.get("previousBoundary"),
                     "groups": groups, "buffers": resources})
    current = sum(p.get("submittedAfter") == doc.get("previousBoundary") for p in doc.get("packets", []))
    if not image.get("primer") and current != doc.get("submittedThisSample"):
        errors.append("submitted packet count/ownership mismatch")
    return {"frame": frame, "submitted": doc.get("submittedThisSample"),
            "retainedPackets": len(rows) - current, "packets": rows, "errors": sorted(set(errors))}


def analyze(run):
    frames, rows, errors = [], [], []
    for path in sorted(run.glob("*/*/vfx-*/*-image.json")):
        image = json.loads(path.read_text(encoding="utf-8-sig"))
        stem = path.name.replace("-image.json", "")
        native = path.with_name(stem + ".json")
        snapshot = path.with_name(stem + "-simulation.json")
        relative = native.relative_to(run)
        if not native.is_file() or not snapshot.is_file():
            errors.append(str(relative) + ": missing native/simulation evidence")
            continue
        doc = json.loads(native.read_text())
        sim = json.loads(snapshot.read_text())
        frames.append((relative, doc, image))
        result = validate(path.parent, sim, image)
        result["file"] = str(snapshot.relative_to(run))
        result["primer"] = image.get("primer", False)
        for error in result["errors"]:
            errors.append(result["file"] + ": " + error)
        rows.append(result)
    if not frames:
        errors.append("no simulation frames")
    if not any(r["packets"] for r in rows):
        errors.append("no candidate simulation buffers captured")
    try:
        request = json.loads((run / "request.json").read_text(encoding="utf-8-sig"))
        for scenario in request.get("scenarios", []):
            for step in scenario.get("steps", []):
                if not step.get("vfxSimulation"):
                    continue
                directory = run / scenario["key"] / step["name"]
                status = directory / "step.json"
                if not status.is_file():
                    errors.append(str(status.relative_to(run)) + ": requested step missing")
                else:
                    info = json.loads(status.read_text(encoding="utf-8-sig"))
                    if info.get("error") or info.get("expectedTraced") != info.get("traced"):
                        errors.append(str(status.relative_to(run)) + ": failed step or wrong PT state")
                for phase in ("vfx-live-facing", "vfx-live-away", "vfx-live-returned", "vfx-frozen"):
                    selected = [r for r in rows if not r["primer"] and
                                (run / r["file"]).parent == directory / phase]
                    if len(selected) != 2:
                        errors.append(str((directory / phase).relative_to(run)) + ": expected two matched frames")
    except (OSError, ValueError) as error:
        errors.append("request/step metadata unavailable: " + str(error))
    for failure in run.glob("*/*/vfx-*/capture-error.json"):
        errors.append(str(failure.relative_to(run)) + ": " + failure.read_text(encoding="utf-8-sig"))
    # The old summary still helps link output reads with compute writers. Its broad relevance
    # test includes shared camera constants; keep those errors separate from capture integrity.
    observer = vfx_capture.summarize(frames)
    errors.extend(vfx_capture.pictures(run, frames))
    for _, doc, image in frames:
        if doc.get("schema") != 2 or doc.get("frame") != image.get("frame"):
            errors.append("native metadata schema/frame mismatch")
        if not image.get("primer") and doc.get("previousBoundary") != image["frame"] - 1:
            errors.append("native metadata frame boundaries are incomplete")
        sequence = [event.get("sequence") for event in doc.get("events", [])]
        if sequence != list(range(1, len(sequence) + 1)):
            errors.append("native event sequence is incomplete")
        if image.get("mode") != "Off" or any(event.get("kind") == "traceBegin" for event in doc.get("events", [])):
            errors.append("simulation diagnostic expected the game picture with tracing Off")
        if any(doc.get(key) for key in ("faults", "hookFailures", "unknownLists", "staleLists", "boundaryDrops")):
            errors.append("observer coverage incomplete")
        if doc.get("deviceGeneration") != doc.get("currentDeviceGeneration"):
            errors.append("device generation changed")
    summary = {"schema": 1, "particleCaptureComplete": False, "frames": rows,
               "errors": sorted(set(errors)), "observer": observer,
               "unknown": ["No decoded particle records, world-space positions, alive counts or material mappings.",
                           "Raw-UAV programs are candidates; structured-only families may be absent.",
                           "Retained initialization snapshots are not evidence of currently live systems.",
                           "Missing shader creation and reused command lists require inspection."]}
    (run / "particles-summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    lines = ["# Simulation buffer evidence", "", "Diagnostic only. No particles are rendered or declared captured.", "",
             "| Frame | Submitted candidate dispatches | Retained snapshots | Errors |",
             "|---|---:|---:|---:|"]
    lines += [f'| {r["file"]} | {r["submitted"]} | {r["retainedPackets"]} | {len(r["errors"])} |' for r in rows if not r["primer"]]
    lines += ["", "Read vfx-overview.jpg for the camera turn. Simulation/*.bin are exact buffer bytes;",
              "simulation/programs/*.dxbc are local diagnostic evidence only, never source to merge.",
              "Use buffer lifetime IDs and compute declarations to decode batch offsets/live lists before building an adapter.",
              "No dots are drawn: mapping arbitrary floats to positions would manufacture evidence.", "", "Unresolved:"]
    lines += ["- " + s for s in summary["unknown"]]
    lines += ["", "Capture errors:"] + ["- " + s for s in summary["errors"]]
    (run / "particles-report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return summary


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    result = analyze(parser.parse_args().run)
    print(f'Simulation diagnostic: {len(result["frames"])} frames, {len(result["errors"])} capture errors; particle mapping remains open.')
    raise SystemExit(bool(result["errors"]))
