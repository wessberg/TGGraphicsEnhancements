from pathlib import Path
exec(Path('.testruns/nr/add-signal-diagnostic.py').read_text().split("edit('native/neural/src/diagnostics.hpp'")[0])
edit('tools/harness/neural_plan.py', [
    ('\ndef main():', '''
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


def main():'''),
    ('    parser.add_argument("--replay",', '    parser.add_argument("--signal", action="store_true", help="Tavern and face: full-effect A with input conditioning 0.25/1/4, inverted on output, both placements")\n    parser.add_argument("--replay",'),
    ('    if args.long_hold and not args.replay:', '    if args.signal and (args.replay or args.stages or args.long_hold):\n        parser.error("--signal is a separate diagnostic recipe.")\n    if args.long_hold and not args.replay:'),
    ('    if args.stages or args.replay:', '    if args.stages or args.replay or args.signal:'),
    ('        make_recipe = (lambda face=False: replay_recipe(face, args.long_hold)) if args.replay else stage_recipe', '        make_recipe = signal_recipe if args.signal else (lambda face=False: replay_recipe(face, args.long_hold)) if args.replay else stage_recipe')])
