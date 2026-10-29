"""NR evidence in the shared scene harness, including runs with PT left on throughout."""
import math
import json
import numpy as np

NR = "com.wessberg.tgneuralrendering"


def matched_motion(before, after, label="motion"):
    """Reject a changed route before interpreting brightness changes as the model's response."""
    a, b = before.get("frames", []), after.get("frames", [])
    if not a or len(a) != len(b):
        return f"{label} frame counts differ or are empty"
    for index, (x, y) in enumerate(zip(a, b)):
        if any(k not in x or k not in y for k in ("px", "py", "pz", "rx", "ry", "rz", "fov")):
            return f"{label} camera metadata is missing"
        position = math.sqrt(sum((x[k] - y[k]) ** 2 for k in ("px", "py", "pz")))
        angle = max(abs((x[k] - y[k] + 180) % 360 - 180) for k in ("rx", "ry", "rz"))
        fov = abs(x["fov"] - y["fov"])
        if not math.isfinite(position + angle + fov) or position > .001 or angle > .02 or fov > .01:
            return f"{label} pose differs at frame {index} ({position:.4f} m, {angle:.3f} degrees)"
    return ""


def colour_motion(before, after):
    residual = np.asarray(after, dtype=float) - np.asarray(before, dtype=float)
    luma = residual @ np.array([.2126, .7152, .0722])
    return dict(mean_rgb_shift=residual.mean(axis=0).tolist(), rgb_shift_range=np.ptp(residual, axis=0).tolist(),
                mean_luma_shift=float(luma.mean()), luma_shift_range=float(np.ptp(luma)))


def rgb_blocks(rgb, size=32):
    """Retain local RGB so opposite colour shifts cannot cancel in the whole-image average."""
    h, w = rgb.shape[:2]
    return rgb[:h-h % size, :w-w % size, :3].reshape(h//size, size, w//size, size, 3).mean(axis=(1, 3)).astype(np.float32)


def local_temporal(before, after, hold_start, threshold=2):
    a, b = np.asarray(before, dtype=np.float32), np.asarray(after, dtype=np.float32)
    if a.shape != b.shape or a.ndim != 4 or a.shape[0] < 2 or a.shape[-1] != 3 or not np.isfinite(a).all() or not np.isfinite(b).all():
        raise ValueError("Local colour comparison requires matching finite RGB block sequences")
    residual = b - a
    jumps = np.abs(np.diff(residual, axis=0)).max(axis=-1)
    area = (np.abs(residual).max(axis=-1) > threshold).mean(axis=(1, 2)) * 100
    # A fixed spatial look has no temporal range once the camera is stationary. Model settling does.
    held = residual[hold_start:]
    if len(held) < 2:
        raise ValueError("Local colour comparison needs at least two held-still frames")
    spread = np.ptp(held, axis=0).max(axis=-1)
    result = dict(block_size=32, threshold=threshold, hold_start=hold_start,
        max_step=float(jumps.max()), hold_max_range=float(spread.max()), hold_p95_range=float(np.percentile(spread, 95)),
        changed_area_range=float(np.ptp(area)), changed_area_percent=area.tolist(),
        worst_block_step=jumps.max(axis=(1, 2)).tolist(), p95_block_step=np.percentile(jumps, 95, axis=(1, 2)).tolist())
    return result, spread


def write_local(folder, name, result, spread):
    from PIL import Image, ImageDraw
    path = folder / ("nr-local-" + name)
    path.with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    # Fixed scale across every take: yellow is eight display levels or more, never per-image normalization.
    value = np.clip(spread / 8, 0, 1)
    colour = np.stack([np.minimum(value*2, 1), np.maximum(value*2-1, 0), value*.2], axis=-1)
    heat = Image.fromarray(np.uint8(colour*255)).resize((960, round(960*spread.shape[0]/spread.shape[1])), Image.Resampling.NEAREST)
    picture = Image.new("RGB", (960, heat.height+48), (20, 20, 20))
    picture.paste(heat, (0, 48)); draw = ImageDraw.Draw(picture)
    draw.text((8, 6), name + " / NR-minus-Off change while camera held still", fill="white")
    draw.text((8, 24), "Black: 0; orange: 4; yellow: 8+ RGB levels. 32-pixel blocks; internal model mask is not observed.", fill="white")
    picture.save(path.with_suffix(".png"))


def counters(request, result):
    expected = request.get("neural")
    if expected is None:
        return ""
    evaluations = result.get("neuralEvaluations")
    if evaluations is None or evaluations < 0:
        return "no valid Neural Rendering evaluation count"
    if expected and evaluations == 0:
        return "Neural Rendering did not evaluate: " + result.get("neuralReason", "")
    if not expected and evaluations != 0:
        return "Neural Rendering evaluated while expected Off"
    if expected:
        frames = result.get("frames", [])
        for frame in frames:
            # Older evidence predates per-frame diagnostics. Validate the fields whenever present;
            # missing fields remain an explicit limitation of that older capture, not invented proof.
            late = request.get("neuralPostToneMap", False)
            if "neuralBeforeLensEffects" in frame and frame["neuralBeforeLensEffects"] is not (not late):
                return "Neural Rendering used the wrong pre-lens or diagnostic post-tone-map placement"
            if late and (frame.get("neuralPostToneMap") is not True or frame.get("lensEffectsSuppressed") is not True):
                return "Tone-mapped NR comparison lacks confirmed placement or lens suppression"
            if "neuralFrame" in frame and frame["neuralFrame"] != frame.get("frame"):
                return "Neural Rendering did not evaluate this captured frame"
        for previous, current in zip(frames, frames[1:]):
            if "neuralEvaluated" in previous and "neuralEvaluated" in current:
                if current["neuralEvaluated"] - previous["neuralEvaluated"] != current["frame"] - previous["frame"]:
                    return "Neural Rendering skipped or repeated an evaluation during capture"
    return ""


def analyze(steps, folder, limits, restore_check):
    records, problems = [], []
    baseline = None
    motion_baselines = {}
    for request, result, directory, data in steps:
        if request.get("neural") is None:
            continue
        error = counters(request, result)
        if error:
            problems.append(f"{request['name']}: {error}")
        record = dict(name=request["name"], evaluations=result.get("neuralEvaluations"),
                      gpu_ms=result.get("neuralGpuMs"), transfer_bytes=result.get("neuralTransferBytes"),
                      placement=result.get("neuralPlacement"), width=result.get("neuralWidth"), height=result.get("neuralHeight"))
        records.append(record)
        if request.get("walkSpeed", 0) > 0:
            record["motion_comparison"] = "Real-time walk: per-frame poses differ, no frame-index colour comparison."
        if data is not None and data.ok and result.get("frozen") and request.get("motionFrames", 0) > 0 and not request.get("walkSpeed"):
            blur_suppressed = bool(request.get("disableMotionBlur", False))
            motion_baseline = motion_baselines.get(blur_suppressed)
            if motion_baseline is None and request["neural"] is False:
                motion_baseline = (result, data)
                motion_baselines[blur_suppressed] = motion_baseline
            elif motion_baseline is not None:
                mismatch = matched_motion(motion_baseline[0], result)
                if mismatch:
                    problems.append(f"{request['name']}: {mismatch}; colour comparison excluded")
                    record["motion_error"] = mismatch
                else:
                    record["motion_colour"] = colour_motion(motion_baseline[1].frame_rgb, data.frame_rgb)
                    a = getattr(motion_baseline[1], "frame_blocks_rgb", [])
                    b = getattr(data, "frame_blocks_rgb", [])
                    if len(a) and len(b) and request.get("motionHoldFrames", 0) >= 3:
                        start = request["motionFrames"] + 1
                        record["local_colour"], spread = local_temporal(a, b, start)
                        write_local(folder, request["name"], record["local_colour"], spread)
        if data is None or not data.ok or not result.get("frozen") or request.get("slide") or request.get("walkForward"):
            continue
        if baseline is None and request["neural"] is False:
            baseline = (result, data)
            continue
        neutral = any(p.get("plugin") == NR and p.get("key") == "Strength" and float(p["value"]) == 0
                      for p in request.get("set", []))
        if baseline and (request["neural"] is False or neutral) and baseline[0].get("traced") == result.get("traced"):
            mismatch = matched_motion(baseline[0], result, "static")
            if mismatch:
                problems.append(f"{request['name']}: {mismatch}; restoration comparison excluded")
                record["restoration_error"] = mismatch
                continue
            check = restore_check(baseline[1], data, limits, folder / f"nr-restore-{directory.name}.jpg")
            record["restoration"] = check
            if not check["same_picture"]:
                problems.append(f"{request['name']}: NR {'neutral strength' if neutral else 'Off'} changed the scene "
                                f"(mean {check['difference']:.3f}, worst block {check['block_max']:.3f} display levels)")
    return records, problems
