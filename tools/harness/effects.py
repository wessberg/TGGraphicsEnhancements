#!/usr/bin/env python3
"""Summarize effects.json inventories, without image dependencies or a running game.

python tools/harness/effects.py artifacts/harness/<run>
Off/On are separate censuses. Multi-shader systems occur in each family they use;
never sum family counts as a unique-world total. Unknown values are never zero.
"""
import argparse
import json
from collections import defaultdict
from pathlib import Path


def number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def obj(value):
    return value if isinstance(value, dict) else {}


def array(value):
    return value if isinstance(value, list) else []


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def unknowns(value, path=""):
    if isinstance(value, str) and value.lower().startswith("unknown:"):
        yield {"path": path, "reason": value}
    elif isinstance(value, dict):
        for key, child in value.items():
            yield from unknowns(child, f"{path}.{key}" if path else key)
    elif isinstance(value, list):
        for i, child in enumerate(value):
            yield from unknowns(child, f"{path}[{i}]")


def freeze(samples):
    """A change is affirmative evidence. Still samples with unreadable fields remain unknown."""
    samples = array(samples)
    if len(samples) != 3 or any(not isinstance(s, dict) for s in samples):
        return {"valid": False, "reason": "unknown: expected three readable rendered-frame samples"}
    times = [s.get("realtime") for s in samples]
    frames = [s.get("frame") for s in samples]
    spaced = all(number(t) for t in times) and all(b - a >= 0.95 for a, b in zip(times, times[1:]))
    rendered = all(number(f) for f in frames) and all(b > a for a, b in zip(frames, frames[1:]))
    result = {"valid": spaced and rendered, "seconds": times, "frames": frames,
              "frozen": all(s.get("timeScale") == 0 for s in samples),
              "particleSampling": "count plus first eight particles; still samples do not prove the entire simulation is frozen"}

    def compare(values, missing):
        changed = any(a != b for a, b in zip(values, values[1:]))
        return "unknown: incomplete observation" if missing else changed

    for field, label in (("particles", "particleStateChanged"), ("vfx", "vfxCountChanged")):
        values, missing = [], False
        for sample in samples:
            records = sample.get(field)
            if not isinstance(records, list):
                missing = True
                records = []
            by_id = {}
            for record in records:
                ident = obj(record.get("identity")).get("id")
                data = obj(record.get("data"))
                if ident is None or not data:
                    missing = True
                    continue
                if field == "particles":
                    current = obj(data.get("current"))
                    if not number(current.get("liveCount")) or not isinstance(current.get("firstParticles"), list):
                        missing = True
                    by_id[str(ident)] = {"count": current.get("liveCount"), "first": current.get("firstParticles"), "time": data.get("systemTime")}
                else:
                    if not number(data.get("aliveParticleCount")):
                        missing = True
                    by_id[str(ident)] = data.get("aliveParticleCount")
            values.append(by_id)
        result[label] = compare(values, missing)
    clocks, missing = [], False
    for sample in samples:
        raw = obj(sample.get("shaderTimes"))
        # FrameCount changes even when animation time stops. Missing optional historical fields are recorded
        # in the raw inventory; the two current HDRP animation clocks must both be readable for this verdict.
        selected = {k: raw.get(k) for k in ("_Time", "_TimeParameters")}
        if any(not isinstance(v, list) or not v or not all(number(n) for n in v) for v in selected.values()):
            missing = True
        clocks.append(selected)
    result["shaderClockChanged"] = compare(clocks, missing)
    result["vfxAttributes"] = "unknown: public API exposes counts, not GPU positions or shader simulation time"
    if not result["valid"]:
        result["reason"] = "unknown: samples are not separate frames spaced a second apart"
    return result


def families(effects, lights):
    rows = {}
    selected = {obj(l.get("identity")).get("id") for l in array(lights)
                if obj(obj(l.get("data")).get("tracer")).get("status") == "selected"}
    for effect in array(effects):
        if not isinstance(effect, dict) or obj(effect.get("identity")).get("id") is None:
            continue
        renderer = effect.get("rendererType", "unknown: renderer")
        shaders = {obj(m).get("shader", "unknown: shader") for m in array(effect.get("materials"))} or {"unknown: no readable material"}
        links = obj(effect.get("lights"))
        attached, nearby = array(links.get("attached")), array(links.get("nearby"))
        for shader in sorted(shaders):
            key = (renderer, shader)
            if key not in rows:
                rows[key] = {"renderer": renderer, "shader": shader, "systems": 0, "knownLiveParticles": 0,
                             "unknownLiveSystems": 0, "withAttachedLights": 0, "withNearbyLights": 0,
                             "withParticleLightModule": 0, "withSelectedAttachedLights": 0,
                             "withSelectedNearbyLights": 0, "glowRecognized": 0, "unknownLightSystems": 0,
                             "unknownGlowSystems": 0, "geometryRoutes": {}, "instances": []}
            row = rows[key]
            row["systems"] += 1
            count = effect.get("liveCount")
            if number(count):
                row["knownLiveParticles"] += count
            else:
                row["unknownLiveSystems"] += 1
            row["withAttachedLights"] += bool(attached)
            row["withNearbyLights"] += bool(nearby)
            row["unknownLightSystems"] += not isinstance(links.get("attached"), list) or not isinstance(links.get("nearby"), list)
            row["withParticleLightModule"] += obj(obj(effect.get("particleSystem")).get("lights")).get("enabled") is True
            row["withSelectedAttachedLights"] += any(obj(l).get("id") in selected for l in attached)
            row["withSelectedNearbyLights"] += any(obj(l).get("id") in selected for l in nearby)
            tracer = obj(effect.get("tracer"))
            glow = [obj(obj(m).get("cachedGlowDetection")).get("Item1") for m in array(tracer.get("glowMaterials"))]
            row["glowRecognized"] += any(v is True for v in glow)
            row["unknownGlowSystems"] += not glow or (True not in glow and any(v is not False for v in glow))
            route = tracer.get("geometry", "unknown: geometry capture")
            row["geometryRoutes"][route] = row["geometryRoutes"].get(route, 0) + 1
            ident = obj(effect.get("identity"))
            row["instances"].append({k: ident.get(k, "unknown") for k in ("id", "scene", "path", "cameraDistance", "activeInHierarchy")})
    return sorted(rows.values(), key=lambda r: (-r["systems"], r["renderer"], r["shader"]))


def merge_families(censuses):
    combined = {}
    for census in censuses:
        for row in census["families"]:
            key = (census["phase"], row["renderer"], row["shader"])
            if key not in combined:
                combined[key] = {"phase": key[0], "renderer": key[1], "shader": key[2], "scenarios": [], "geometryRoutes": {}}
            target = combined[key]
            if census["scenario"] not in target["scenarios"]:
                target["scenarios"].append(census["scenario"])
            for name, value in row.items():
                if isinstance(value, int):
                    target[name] = target.get(name, 0) + value
            for route, count in row["geometryRoutes"].items():
                target["geometryRoutes"][route] = target["geometryRoutes"].get(route, 0) + count
    return sorted(combined.values(), key=lambda r: (r["phase"], -r["systems"], r["renderer"], r["shader"]))


def analyze(root):
    root = Path(root)
    censuses, issues = [], []
    for path in sorted(root.glob("*/*/effects.json")):
        try:
            data = read(path)
            if data.get("schemaVersion") != 1:
                raise ValueError("unsupported effects schema")
            if not isinstance(data.get("effects"), list):
                raise ValueError("effects scan missing: " + str(data.get("effects")))
            expected = data.get("expectedTraced", data.get("traced"))
            census = {"scenario": data.get("scenario", path.parent.parent.name), "step": path.parent.name,
                      "phase": "On" if expected is True else "Off" if expected is False else "unknown",
                      "tracerCompositing": data.get("traced", "unknown"),
                      "source": str(path.relative_to(root)), "effectRecords": len(data["effects"]),
                      "unidentifiedRecords": sum(obj(obj(e).get("identity")).get("id") is None for e in data["effects"]),
                      "families": families(data["effects"], data.get("lights")), "freeze": freeze(data.get("freezeSamples")),
                      "fog": data.get("fog", "unknown: no fog record"), "unknowns": list(unknowns(data)),
                      "lights": data.get("lights", "unknown: no light records")}
            scenario_path = path.parent.parent / "scenario.json"
            census["save"] = read(scenario_path) if scenario_path.exists() else "unknown: no scenario.json"
            if data.get("traced") is not expected:
                issues.append(f"{census['source']}: requested PT {census['phase']}, compositing={data.get('traced', 'unknown')}")
            live = path.parent.parent / "effects-live.json"
            census["liveSequence"] = freeze(read(live).get("samples")) if live.exists() else {"valid": False, "reason": "unknown: no live sequence"}
            if not census["freeze"]["valid"]:
                issues.append(f"{census['source']}: invalid freeze probe")
            censuses.append(census)
        except (ValueError, TypeError, AttributeError, OSError) as error:
            issues.append(f"{path.relative_to(root)}: {error}")
    # A partial run must not look like proof that the missing scene contains no effects.
    request = root / "request.json"
    if request.exists():
        for scenario in read(request).get("scenarios", []):
            for step in scenario.get("steps", []):
                if step.get("effects") and not any(c["scenario"] == scenario["key"] and c["step"] == step["name"] for c in censuses):
                    issues.append(f"missing inventory: {scenario['key']}/{step['name']}")
    if not censuses:
        issues.append("no readable effects inventories")
    return {"schemaVersion": 1, "censuses": censuses, "allScenarios": merge_families(censuses), "issues": issues,
            "countMeaning": "Off and On totals stay separate. Systems means inventoried components (one ParticleSystem, VisualEffect, trail, line or mesh). VFX internal system records are in the source inventory; their mapping to output shaders is unknown. Live counts are whole-component counts, not particles assigned to each output. Multi-shader components and their counts occur in multiple families. Counts are observations per scenario, not unique assets across saves. Unknown live counts are excluded from the known sum and counted separately."}


def cell(value):
    return str(value).replace("|", "\\|").replace("\n", " ").replace("\r", " ")


def table(rows):
    text = ["Renderer / shader | Systems | Live (known) | Unknown counts | Attached / nearby lights | Particle light modules | Selected attached / nearby | Glow recognized | Unknown light / glow data",
            "--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---:"]
    for r in rows:
        text.append(f"{cell(r['renderer'])} / {cell(r['shader'])} | {r['systems']} | {r['knownLiveParticles']} | {r['unknownLiveSystems']} | "
                    f"{r['withAttachedLights']} / {r['withNearbyLights']} | {r['withParticleLightModule']} | "
                    f"{r['withSelectedAttachedLights']} / {r['withSelectedNearbyLights']} | {r['glowRecognized']} | {r['unknownLightSystems']} / {r['unknownGlowSystems']}")
    return text


def markdown(report):
    text = ["# Effects and fog inventory", "", report["countMeaning"], "",
            "Selected lights and glow recognition are tracer handling of illumination evidence, not particle geometry capture. Nearby lights are not ownership.", ""]
    for phase in sorted({r["phase"] for r in report["allScenarios"]}):
        text += [f"## All scenarios: PT {phase}", ""] + table([r for r in report["allScenarios"] if r["phase"] == phase]) + [""]
    for c in report["censuses"]:
        text += [f"## {c['scenario']} / {c['step']} (requested PT {c['phase']}, compositing={c['tracerCompositing']})", ""] + table(c["families"]) + [""]
        save = obj(c["save"])
        text += [f"Loaded save: {cell(save.get('slotName', 'unknown'))}; fallback={save.get('standIn', 'unknown')}. "
                 "Unseen rain/dust or other unloaded families cannot be ruled out by this run.", ""]
        f = c["freeze"]
        text += [f"Freeze probe: valid={f.get('valid')}, timeScale zero={f.get('frozen', 'unknown')}; particle state changed={f.get('particleStateChanged', 'unknown')}; "
                 f"HDRP shader clocks changed={f.get('shaderClockChanged', 'unknown')}; VFX counts changed={f.get('vfxCountChanged', 'unknown')}.",
                 f"Live probe: valid={c['liveSequence'].get('valid')}; particle state changed={c['liveSequence'].get('particleStateChanged', 'unknown')}; shader clocks changed={c['liveSequence'].get('shaderClockChanged', 'unknown')}.", ""]
        fog = obj(c["fog"])
        text += [f"Fog: {len(array(fog.get('localVolumes')))} local-object records; {sum(bool(obj(v.get('data')).get('fog')) for v in array(fog.get('fogOverrides')))} Volumes with Fog. "
                 "Density, masks/materials, transforms, fades, blends and camera stack are in effects-summary.json.", "",
                 "Tracer light decision | Count | Why", "--- | ---: | ---"]
        light_groups = defaultdict(int)
        for light in array(c["lights"]):
            data = obj(light.get("data"))
            tr = obj(data.get("tracer"))
            light_groups[(tr.get("status", "unknown"), tr.get("reason", "unknown"))] += 1
        for (status, reason), count in sorted(light_groups.items()):
            text.append(f"{cell(status)} | {count} | {cell(reason)}")
        text += ["", f"Unreadable or unsupported values: {len(c['unknowns'])}. Every reason and JSON path is retained in effects-summary.json. Source: `{c['source']}`.", ""]
    if report["issues"]:
        text += ["## Incomplete evidence", ""] + ["- " + issue for issue in report["issues"]] + [""]
    return "\n".join(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    args = parser.parse_args()
    report = analyze(args.run)
    (args.run / "effects-summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (args.run / "effects-report.md").write_text(markdown(report), encoding="utf-8")
    print(f"Effects: {len(report['censuses'])} censuses, {len(report['issues'])} incomplete-evidence issues; {args.run / 'effects-report.md'}")
    return 1 if report["issues"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
