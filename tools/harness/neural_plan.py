#!/usr/bin/env python3
"""Make an NR tuning plan from the existing saved views; Run-PtHarness owns launching and GPU gating."""
import argparse
import copy
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NR = "com.wessberg.tgneuralrendering"
def setting(key, value, plugin=NR, section="Video"):
    return {"plugin": plugin, "section": section, "key": key, "value": str(value)}

def recipe(path_tracing=False):
    result = []
    for name, mode, strength in [("1-game", "Off", 1), ("2-nr-neutral", "On", 0),
                                  ("3-nr-quarter", "On", .25), ("4-nr-half", "On", .5),
                                  ("5-nr-full", "On", 1), ("6-game-after", "Off", 1)]:
        result.append(dict(name=name, neural=mode == "On", traced=path_tracing,
                           set=[setting("Passes", 1), setting("Strength", strength), setting("Neural Rendering", mode)],
                           settle=4, settleFrames=120, frames=8, scale=1))
    # Same scene, with moving camera/objects: still-frame preferences alone do not establish stability.
    result += [dict(name="7-live-nr", neural=True, traced=path_tracing, live=True, slide=.15,
                    set=[setting("Passes", 1), setting("Strength", .5), setting("Neural Rendering", "On")],
                    settle=5, settleFrames=120, frames=32, perfSeconds=8),
               dict(name="8-live-off", neural=False, traced=path_tracing, live=True, slide=.15,
                    set=[setting("Passes", 1), setting("Neural Rendering", "Off")], settle=4, settleFrames=90, frames=32, perfSeconds=8)]
    return result

def comparison_recipe(path_tracing=False):
    result = []
    def add(name, mode="On", model="A", placement="After Upscaling", strength=1, tone=1, mask="Off", move=False):
        result.append(dict(name=f"{len(result)+1:02d}-{name}", neural=mode == "On", traced=path_tracing,
            set=[setting("Passes", 1), setting("Placement", placement), setting("Model", model), setting("Strength", strength),
                 setting("Local Tone", tone), setting("Detail", 1), setting("Automatic Mask", mask),
                 setting("Skin Detail", 1), setting("Neural Rendering", mode)],
            settle=3, settleFrames=90, frames=80 if move else 6, scale=2 if move else 1,
            slide=.65 if move else 0, turn=25 if move else 0, motionFrames=48 if move else 0,
            motionHoldFrames=32 if move else 0))
    add("off", mode="Off")
    add("after-neutral", strength=0)
    for model in "ABC":
        add(f"after-{model}", model=model)
    add("after-A-mask", mask="On")
    add("before-neutral", placement="Before Upscaling", strength=0)
    for model in "ABC":
        add(f"before-{model}", model=model, placement="Before Upscaling")
    add("before-A-mask", placement="Before Upscaling", mask="On")
    add("off-restored", mode="Off")
    add("motion-off", mode="Off", move=True)
    for model in "ABC":
        add(f"motion-after-{model}", model=model, move=True)
    for model in "ABC":
        add(f"motion-before-{model}", model=model, placement="Before Upscaling", move=True)
    add("motion-off-restored", mode="Off", move=True)
    return result

def make_plan(source, denoiser, compare=False):
    plan = copy.deepcopy(source)
    steps = (comparison_recipe if compare else recipe)(denoiser != "game")
    if denoiser != "game":
        steps[0]["set"] = [dict(key="Mode", value="On"), dict(key="Denoising", value=denoiser),
                            dict(key="Debug view", value="None")] + steps[0]["set"]
    plan["recipes"] = {"neural": steps}
    plan["defaults"] = {"recipe": "neural"}
    for scene in plan["scenarios"]:
        scene["recipe"] = "neural"
        scene.pop("steps", None)
    return plan


def face_recipe():
    """Real camera close-up, with model, placement, masking and skin controls at matching poses."""
    steps = comparison_recipe()
    extra = []
    for placement, prefix in (("After Upscaling", "after"), ("Before Upscaling", "before")):
        for model in "ABC":
            base = next(s for s in steps if s["name"].endswith(f"-{prefix}-{model}") and not s.get("motionFrames"))
            for skin in (1, 0):
                if model == "A" and skin == 1:
                    continue  # already in the standard comparison
                take = copy.deepcopy(base)
                take["name"] = f"{prefix}-{model}-mask-skin{skin}"
                for pair in take["set"]:
                    if pair["key"] == "Automatic Mask": pair["value"] = "On"
                    if pair["key"] == "Skin Detail": pair["value"] = str(skin)
                extra.append(take)
    # Keep final Off restoration after every static setting, then repeat the identical moving route.
    index = next(i for i, s in enumerate(steps) if s["name"].endswith("-off-restored"))
    steps[index:index] = extra
    for i, take in enumerate(steps):
        take["name"] = f"{i+1:02d}-" + (take["name"][3:] if take["name"][:2].isdigit() else take["name"])
        take.update(cameraForward=1.8, cameraFov=40)
        if take.get("motionFrames"):
            # A 65 cm strafe at this distance takes the face entirely out of view.
            take.update(slide=.2, turn=10, scale=2)
    return steps

def stage_recipe(face=False):
    """Same-frame input/output/exposure diagnosis, with a longer stationary hold."""
    source, steps = comparison_recipe(), []
    for suffix in ('motion-off', 'after-neutral', 'motion-after-A', 'motion-after-B',
                   'before-neutral', 'motion-before-A', 'motion-before-B', 'motion-off-restored'):
        step = copy.deepcopy(next(s for s in source if s['name'][3:] == suffix))
        name = suffix if suffix.startswith('motion-') else 'motion-' + suffix
        step.update(name=f'{len(steps)+1:02d}-{name}', neuralStages=step['neural'],
                    motionFrames=48, motionHoldFrames=96, frames=144, scale=4, slide=.65, turn=25,
                    settle=2, settleFrames=60)
        if face: step.update(slide=.2, turn=10, cameraForward=1.8, cameraFov=40)
        steps.append(step)
    return steps


def replay_recipe(face=False, long_hold=False):
    """Compare live inputs with one retained frame, with/without one history reset."""
    source, steps = stage_recipe(face), []
    for suffix in ('motion-off', 'motion-after-A', 'motion-before-A', 'motion-off-restored'):
        base = next(s for s in source if s['name'][3:] == suffix)
        for mode in (('live', 'replay', 'replay-reset') if base['neural'] else ('live',)):
            if long_hold and base['neural'] and mode != 'replay-reset':
                continue
            step = copy.deepcopy(base)
            step.update(name=f'{len(steps)+1:02d}-{suffix}-{mode}',
                        neuralReplay=mode != 'live', neuralReplayReset=mode == 'replay-reset')
            if long_hold: step.update(motionHoldFrames=600, frames=648, scale=8)
            steps.append(step)
    return steps


def motion_check_recipe(face=False):
    """Keep NR neutral and lens blur off while independently checking signed reprojection."""
    steps = [s for s in stage_recipe(face) if s['name'][3:] in
             ('motion-off', 'motion-after-neutral', 'motion-before-neutral', 'motion-off-restored')]
    for i, step in enumerate(steps):
        step.update(name=f'{i+1:02d}-'+step['name'][3:], motionFrames=96, motionHoldFrames=32,
                    frames=128, disableMotionBlur=True)
    return steps


def reset_history_recipe(face=False):
    """Separate retained-input temporal history from a fresh evaluation of each identical frame."""
    steps = []
    for base in replay_recipe(face):
        if base['neural'] and not base['neuralReplayReset']:
            continue
        for every in ((False, True) if base['neural'] else (False,)):
            step = copy.deepcopy(base)
            suffix = step['name'][3:].replace('-replay-reset', '-reset-every' if every else '-reset-once')
            step.update(name=f'{len(steps)+1:02d}-{suffix}', neuralReplayResetEveryFrame=every,
                        motionHoldFrames=96, frames=144, scale=4)
            steps.append(step)
    return steps


def model_history_recipe(face=False, auto_mask=False):
    """Compare A/B/C temporal response to retained inputs; not a live-motion acceptance test."""
    source, steps = stage_recipe(face), []
    for suffix in ('motion-off', 'motion-after-A', 'motion-before-A', 'motion-off-restored'):
        base = next(s for s in source if s['name'][3:] == suffix)
        for model in ('ABC' if base['neural'] else 'A'):
            step = copy.deepcopy(base)
            name = suffix.replace('-A', '-'+model)
            step.update(name=f'{len(steps)+1:02d}-{name}' + ('-retained' if base['neural'] else ''),
                        neuralReplay=base['neural'], neuralReplayReset=base['neural'],
                        motionHoldFrames=192, frames=240, scale=4)
            for control in step['set']:
                if control['key'] == 'Model': control['value'] = model
                if control['key'] == 'Automatic Mask': control['value'] = 'On' if auto_mask else 'Off'
            steps.append(step)
    return steps


def signal_recipe(face=False):
    """Change only reversible HDR conditioning, keeping NR's complete effect at full strength."""
    base, steps = stage_recipe(face), []
    for suffix in ('motion-off', 'motion-after-A', 'motion-before-A', 'motion-off-restored'):
        source = next(s for s in base if s['name'][3:] == suffix)
        for scale in ((1, .25, 4) if source['neural'] else (1,)):
            step = copy.deepcopy(source)
            step.update(name=f'{len(steps)+1:02d}-{suffix}-input{scale:g}', neuralInputScale=scale,
                        motionHoldFrames=128, frames=176)
            steps.append(step)
    return steps


def live_models_recipe(face=False):
    """Compare the stronger A baseline with C using the actual changing scene inputs."""
    steps = [s for s in model_history_recipe(face) if '-B-' not in s['name']]
    for i, step in enumerate(steps):
        step.update(name=f'{i+1:02d}-'+step['name'][3:].replace('-retained', '-live'),
                    neuralReplay=False, neuralReplayReset=False, motionHoldFrames=96, frames=144)
    return steps


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--source", type=Path, default=ROOT / "tests/ingame/scenarios.json")
    parser.add_argument("--denoiser", choices=["game", "Ray Reconstruction", "NRD"], default="game")
    parser.add_argument("--compare", action="store_true", help="Compare A/B/C at full tone, mask, placement and matched move/hold sequences")
    parser.add_argument("--face", action="store_true", help="Verified Ron close-up: A/B/C, both placements, mask/skin and matching motion")
    parser.add_argument("--stages", action="store_true", help="Tavern and face: NR input/output, motion and exposure readbacks; unsuitable for performance measurements")
    parser.add_argument("--signal", action="store_true", help="Tavern and face: full-effect A with input conditioning 0.25/1/4, inverted on output, both placements")
    parser.add_argument("--model-history", action="store_true", help="Tavern and face: A/B/C, both placements, retained colour/depth, zero motion and one reset over 192 held frames")
    parser.add_argument("--auto-mask", action="store_true", help="With --model-history, enable Automatic Mask at full Skin Detail for the same comparison")
    parser.add_argument("--motion-check", action="store_true", help="Neutral NR at both placements, sharp moving images and signed vector readbacks; launch with -KeepFrames")
    parser.add_argument("--live-models", action="store_true", help="Face/tavern/forest: full A versus C with normal changing NR inputs, both placements and Off bookends")
    parser.add_argument("--replay", action="store_true", help="Tavern and face: live versus retained NR inputs, with/without one history reset")
    parser.add_argument("--long-hold", action="store_true", help="With --replay: tavern only, 600 held frames with one history reset at each placement and Off bookends")
    parser.add_argument("--reset-every-frame", action="store_true", help="With --replay: tavern, retained inputs with one reset versus a reset on every held frame, both placements")
    args = parser.parse_args()
    if args.auto_mask and not args.model_history:
        parser.error('--auto-mask requires --model-history.')
    if sum((args.signal, args.model_history, args.motion_check, args.live_models, args.replay, args.stages)) > 1:
        parser.error("Choose one diagnostic recipe.")
    if args.long_hold and not args.replay:
        parser.error('--long-hold requires --replay.')
    if args.reset_every_frame and (not args.replay or args.long_hold):
        parser.error('--reset-every-frame requires --replay without --long-hold.')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    plan = make_plan(json.loads(args.source.read_text(encoding="utf-8-sig")), args.denoiser, args.compare)
    if args.stages or args.replay or args.signal or args.model_history or args.motion_check or args.live_models:
        if args.denoiser != 'game' or args.face or args.compare:
            parser.error('--stages is a separate two-scene diagnostic; use --denoiser game without --face/--compare.')
        if args.live_models: make_recipe = live_models_recipe
        elif args.motion_check: make_recipe = motion_check_recipe
        elif args.model_history: make_recipe = lambda face=False: model_history_recipe(face, args.auto_mask)
        elif args.signal: make_recipe = signal_recipe
        elif args.replay: make_recipe = reset_history_recipe if args.reset_every_frame else lambda face=False: replay_recipe(face, args.long_hold)
        else: make_recipe = stage_recipe
        plan['recipes'] = {'nr-stages-room': make_recipe(), 'nr-stages-face': make_recipe(True)}
        plan['defaults'] = {'recipe': 'nr-stages-room'}
        scenes = ('tavern-room', 'hair-eyebrows', 'forest') if args.live_models else ('tavern-room', 'hair-eyebrows')
        plan['scenarios'] = [s for s in plan['scenarios'] if s['key'] in scenes]
        if len(plan['scenarios']) != len(scenes):
            parser.error('Missing diagnostic scene in the source plan: '+', '.join(scenes))
        for scene in plan['scenarios']:
            scene['recipe'] = 'nr-stages-face' if scene['key'] == 'hair-eyebrows' else 'nr-stages-room'
            if scene['key'] == 'hair-eyebrows': scene['roi'] = [.2, .15, .8, .85]
        if args.long_hold or args.reset_every_frame:
            plan['scenarios'] = [s for s in plan['scenarios'] if s['key'] == 'tavern-room']
    if args.face:
        if args.denoiser != "game":
            parser.error("The close-up baseline currently requires --denoiser game; PT comparisons wait for matching DLSS guides.")
        plan["recipes"]["neural"] = face_recipe()
        plan["scenarios"] = [s for s in plan["scenarios"] if s["key"] == "hair-eyebrows"]
        if not plan["scenarios"]:
            parser.error("The source plan has no hair-eyebrows scene.")
        plan["scenarios"][0]["roi"] = [.2, .15, .8, .85]
        plan["scenarios"][0]["where"] = "Ron face close-up: camera 1.8 m forward, FOV 40; verified in nr-dlss-inputs-20261007."
    args.output.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    # The shared runner reads calibration beside the plan, including plans staged outside tests/ingame.
    camera = args.source.parent / "camera.json"
    if camera.exists() and camera.resolve() != (args.output.parent / "camera.json").resolve():
        shutil.copyfile(camera, args.output.parent / "camera.json")
    print(args.output)

if __name__ == "__main__":
    main()
