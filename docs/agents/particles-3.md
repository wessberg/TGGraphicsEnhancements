# particles-3: the game's VFX Graph particles, decoded from the captured buffers (PT-112 step C, PT-67)

2026-10-11. Worker `particles-3` (Claude subagent). Evidence: run `artifacts/harness/20261009-150909` (recipe
`particles-capture`, scenes `candle` and `campfire`, Path Tracing Off). Scratch, scripts and pictures:
`%TEMP%\tgb\particles3\` (= `C:\Users\Frede\AppData\Local\Temp\tgb\particles3\`). No game, no deploy, no native
build, no GPU test, no source edit. The disassembled game programs live only in the scratch folder (`dis\`); this
report quotes offsets and single instructions, nothing more.

## State

Done. Questions 1 and 2 are answered for the candle flames (Single and the candle sets) and for the campfire's Fire,
Smoke, Sparks, BottomFire ("Glow" output) and Distortion. Debris is not answerable from this capture (it never runs:
see 1.4). Question 3 is answered except "does it still simulate with HDRP's transparents off" (the one run that
answers it is named in 3.4). Question 4: what is needed is listed.

## The short answer

- Unity 6 VFX Graph runs these effects as **instanced batches**: one raw attribute buffer per particle system per
  batch of VisualEffect instances ("slots": 128 per batch for EmberGen and the single candle, 12 or 15 for the
  candle sets, 29 for the floating dust), structure-of-arrays inside each slot's slice; one graph-values
  buffer per batch with one record per *dispatched* instance; one dead list per batch for systems that can die.
- **Smoke and Sparks simulate in world space** (positions in the buffer are world coordinates). **Fire, Distortion,
  BottomFire and every candle flame are local space**: the buffer holds a local position, and the world position is the
  instance's localToWorld times it. That matrix is in the graph values at +16 (column-major float4x4) for Fire, Smoke and
  all candle families (Sparks keep their spawn sphere's world transform there), and equals the VisualEffect
  component's transform exactly (0.000 m).
- Proof on bytes: in all 16 captured frames, 154 of 154 (instance, system) checks give "alive flags = capacity - dead
  count", 522 of 522 live particles have 0 <= age <= lifetime, the frozen frames are bit-identical, and the decoded
  positions projected over the game's own frame sit on the fires, the smoke columns and on all 8 lit candle wicks
  (pictures in section 2).
- **Effects the camera does not see are not simulated.** Turned away, the campfire's three fires get no dispatch at all;
  one of them comes back with its old particles aged 0.179 s over 0.683 s of game time (frozen while away), the other
  two come back reset to zero particles because their VisualEffect components were switched off (`enabled: false`)
  while out of view, most likely by the game's own scripts (Unity's culling does not do that). This collides with the owner's ruling "off-screen effects must be included" (section 3.3).

## 1. Layouts

### 1.0 What all families share (from the init/update programs; addresses in bytes)

- Constant buffer of every init/update dispatch: `cb0[0].x` dispatch width; `cb0[1] = (dispatched instances, valid
  instances, batch slots, offset)`; `cb0[2].x` offset into the index list. Example campfire frame 7731 Smoke update:
  `cb0[1] = (3, 3, 128, 0)`, `cb0[2].x = 3`.
- Index list (structured uint SRV, `t1` or `t2`): entry `[g]` is the attribute **slot** of graph-values record `g`.
  Every kernel does exactly this (update 5dd03cbe52db: `ld_structured r1.x, r0.y, t1` then `movc r1.x, r0.w, r1.x,
  r0.y` with `r0.w = cb0[1].y < cb0[1].z`). So: for `g < cb0[1].y`: `slot = (cb0[1].y < cb0[1].z) ? index[g] : g`.
  Campfire 7731: `index = (11, 31, 96)`, the same three slots and the same `g` order in all five EmberGen systems.
- Graph values (raw SRV `t0`), one record per `g`: `+0` uint capacity or count to draw (Smoke 30, Sparks 64, BottomFire
  4, Fire 1, candle Single 1, Candle Set 01: 8), `+4` uint seed, `+8` uint spawned-so-far (particle id base), `+16`
  localToWorld (64 B, column-major, translation at +64..+76) where the system has one; the rest are the graph's own
  constants (offsets per family below).
- Attribute buffer (raw UAV `u0`): slot `s` starts at `s * sliceBytes`. Inside, attributes are grouped in "buckets";
  a bucket holds `paddedCapacity` elements of a fixed stride.
- Dead list (structured uint UAV `u1`, only for systems that can die): `[deadCount[0..N-1] | deadCountCopy[N..2N-1] |
  N x capacity free indices]`, `N` = batch slots (Smoke: 16384 B = 2*128 + 128*30 uints). Init pops with
  `imm_atomic_iadd ... l(-1)` on `deadCount[slot]`, update pushes with `+1` when a particle dies (790a8c9e9d64,
  5dd03cbe52db). **Live set** = records whose alive flag is 1; their number is `capacity - deadCount[slot]`.
- Threads of an update: `slot` = thread >> log2(threads per instance) (Smoke `ushr r0.y, r0.x, l(6)`: 64 threads per
  instance), particle index < `gv+0`, then `if (alive)`.

### 1.1 Candle flames (local space, immortal: no alive flag, no dead list, no update kernel)

**VFX_CandleFlame_Single** (init 1f1e2c19..., 128 slots, slice 144 B = 36 dwords, capacity 1 padded to 4):

| bytes in slice | attribute | evidence (1f1e2c1981ef) |
|---|---|---|
| +0 +32*i (12 B) | position, local, written as (0,0,0) | `store_raw u0.xyz, r0.w, l(0,0,0,0)` with `r0.w = (slot*36 + i*8)*4` |
| +12 +32*i | pivot Y = -0.25 | `store_raw u0.x, r1.x, l(-0.250000)` |
| +16 +32*i | scale X, random 0.4..0.8 | `mad r0.y, r0.y, l(0.400000), l(0.400000)` |
| +128 +4*i | particle id | `iadd r0.y, r0.y, l(128)`; `store_raw u0.x, r0.y, r0.x` |

Graph values 176 B: `+0` count 1, `+16` localToWorld, `+80` worldToLocal, `+144` size 0.15, `+156` seed (float view).
The output VS (39787aa241...) checks `index < gv+0`, reads `gv+144`, `gv+156`, and moves the quad with a per-instance
localToWorld from its CB1 minus the camera position (`add r6.xyw, -cb0[48].xyxz, cb1[r2.w + 3].xyxz`): local space.
So **world position of a candle flame = translation of gv+16**, and the flame quad is lifted by pivot -0.25 x size.

**Candle sets** (init programs read a point-cache texture: `ld_indexable(texture2d) ... t1`, positions are local
offsets of the set's wicks; slot stride `capacity*8` dwords; 8 dwords per particle; graph values 192 B, `+16`
localToWorld, `+144` last point index, `+148` point-cache width):

| program | asset (matched by gv+16 = component transform, 0.000 m) | dwords per slot | points |
|---|---|---|---|
| 8bde72207fda | VFX_CandleFlame_Set_Candles_01 | 64 | 8 |
| 155b31760ec1 | VFX_CandleFlame_Set_Candles_02 | 32 | 4 |
| d097b82ccb5a | VFX_CandleFlame_Set_Candles_Straight | 416 | 52 |

Per particle (`+32*i` in the slot): `+0` position (local, from the point cache), `+12` pivot Y -0.25, `+16` scale X
0.4..0.8, `+20` seed, `+24` particle id (8bde72207fda: `bfi r1.xyzw, ..., l(3, 4, 5, 6)` and the four stores after it).
Example: Set_Candles_01 at (-1265.22, 88.96, -3168.17), first wick local (0.113, 0.125, 0.019), world (-1265.36,
89.07, -3168.17). The sets were not on screen in either captured scene, so they have no overlay (only Single has).

### 1.2 Campfire: VFX_Fire_EmberGen (6 particle systems; the capture's three fires are slots 11, 31, 96 of one batch)

The system names come from matching graph-value constants to the component's exposed properties (FireMultiColor
5.992/5.186/3.477 at Fire `gv+144`; DistortionScaleSize 0.6/1.2 at Distortion `gv+32`; BottomFireColor 47.94/25.53 at
`gv+80` and BottomFlipbook 8x8 at `gv+104`; Sparks' Density 3 at `gv+332`, Size 0.02/0.04 at `+300/+316`, Lifetime
0.3/1.0 at `+340/+344`; Smoke's SmokeLifeTime 2.5 at `gv+268`, SmokeFlipbookFramerate 25 at `gv+300`).

**Smoke** — world space. Init 790a8c9e..., update 5dd03cbe... Capacity 30 (padded 32), slice 1792 B (448 dwords),
graph values 336 B, dead list `30` entries per slot.

| bytes in slice | attribute | evidence |
|---|---|---|
| +0 +32*i | position (world) | init `store_raw u0.xyz, r1.y, r5.xyzx` with `r5 = gv+224` (= fire position; gv+224 == gv+64) |
| +12 +32*i | texIndex (flipbook frame, +25 per second) | update `mad r2.w, r9.x, r9.y, r2.w` with `r9.x = gv+300 = 25`, `r9.y = gv+304 = dt` |
| +16 +32*i | age | update `add r4.x, r9.y, r4.w`, stored back to `idx*8+4` |
| +20 +32*i | alive (uint 0/1) | update `bfi r0.w, l(29), l(3), r0.z, l(5)` then `if_nz` |
| +1024 +16*i | velocity (world) | update loads `r11` from `+1024` |
| +1536 +4*i | lifetime | update `lt r1.w, r1.w, r4.x` (lifetime < age kills) |
| +1664 +4*i | angle Z in degrees (0..90) | output VS `iadd r0.z, r0.w, l(1664)` then `mul r0.z, r0.z, l(0.017453)`, `sincos` |

Size and colour are not attributes: the output VS (b9e2fa10...) takes size from `gv+312` (0.7), colour and alpha from
a baked gradient (texture `t2` of the draw) at age/lifetime times `gv+264`, an octagon billboard facing the camera,
and fades alpha by camera distance (`gv+320`, `gv+324`: 0.5 m to 2 m).

**Sparks** — world space. Init 1e6ccc1f..., update 869d6c73... Capacity 64, slice 3072 B (768 dwords), graph values
368 B.

| bytes in slice | attribute | evidence |
|---|---|---|
| +0 +32*i | position (world) | init: sphere point times `gv+16` matrix plus `gv+64` (world spawn sphere) |
| +12 +32*i | age | update `add r1.z, r9.z, r3.w` stored to `idx*8+3` |
| +16 +32*i | velocity (world) | update `mad r2.xyz, r2.xyzx, r9.zzzz, r11.xyzx` (v += F/m*dt) |
| +28 +32*i | alive | update `iadd r0.w, r0.x, l(7)` then `if_nz` |
| +2048 +8*i | size (diameter, 0.02..0.04) | init lerp of `gv+300`, `gv+316`; mass derives from it |
| +2052 +8*i | particle id | init `store_raw u0.x, r1.w, r0.x` |
| +2560 +4*i | mass | update divides the force by it |
| +2816 +4*i | lifetime (0.3..1.0) | update `lt r1.w, r1.w, r1.z` |

Output (dc244605...): a quad stretched along the velocity, colour/alpha from a baked gradient at age/lifetime,
width = size x (0.3..0.8 random) x `gv+360`, alpha faded within 0.1..0.5 m of the camera.

**BottomFire** (output "Glow") — local space. Init 9888916b..., update 8a7b3675... Capacity 4, slice 192 B (48 dwords),
graph values 128 B (no matrix in it).

| bytes in slice | attribute | evidence |
|---|---|---|
| +0 +4*i | lifetime (2.0..2.5) | init `mad r1.z, r1.z, l(-0.500000), l(2.500000)`; update `lt r1.w, r1.w, r1.z` |
| +16 +32*i | position, local (0, 0.08, 0) = BottomFireYOffset | init `store_raw u0.xyz, r3.y, r0.yzwy` from `gv+48` |
| +28, +32, +36 +32*i | angle X, Y, Z (90, 0, random 0..359) | init `mul r2.z, r0.x, l(359.000000)` |
| +144 +12*i | texIndex (+24 per second) | update `mad r0.y, r0.y, r1.z, r2.z` (`gv+44` = 24, `gv+60` = dt) |
| +148 +12*i | alive | update `iadd r0.w, r0.z, l(148)` then `if_nz` |
| +152 +12*i | age | update `add r1.z, r1.z, r2.w` |

**Fire** — local space, immortal (no dead list). Init 20264a89..., update fdea2d81... Capacity 1 (padded 4), slice
80 B, graph values 304 B (`+16` localToWorld, `+144` FireMultiColor, `+220` FireSize 0.7, `+224` FireScale, `+232`
flipbook 8x8). Per particle: `+0 +4*i` particle id, `+16 +16*i` position (local). The update writes the position every
frame from `gv+176` (`imad r0.y, r0.y, l(304), l(176)`, `store_raw u0.xyz, r0.x, r1.xyzx`). For slot 11 (identity
rotation, scale 1) `gv+176` = (-0.102, 0.118, 0.368) = 0.4 x the unit vector from the fire to the camera
("FireFixPositionInterpolation" 0.4): **the game pulls the fire quad towards the camera**. For the tracer the fire's
centre is the instance origin; the other two instances' offsets were not checked against a formula (unproven).

**Distortion** (heat haze) — local space, immortal. Init 2ea9b431..., update 4ae5c8ec... Slice 80 B, graph values
80 B. Per particle: `+0 +16*i` position (0,0,0), `+64 +4*i` texIndex (`mad r0.y, r0.y, l(25.000000), r0.z`).

### 1.3 Where the world transform is

| family | space | world position |
|---|---|---|
| Smoke, Sparks | world | the attribute itself (output VS: `add r3.yzw, r1.yyzw, -cb0[48].xxyz`, camera-relative only) |
| Fire, candle Single, candle sets | local | `gv+16` localToWorld x attribute position |
| BottomFire, Distortion | local | localToWorld of the same `g` taken from a sibling system of the batch (Fire or Smoke `gv+16`); their own graph values have no matrix; their output VS reads it from a per-draw CB1 (`cb1[r0.z + 0..3]`) |

`gv+16` was compared with the image json's `localToWorld` of the components: EmberGen slots 11/31/96, all candle
Single records and all candle-set records match to 0.000 m.

### 1.4 Not decoded

- **Debris**: `SpawnDebris` is false on these fires (exposed property), its ParticleSystem info says alive 0, sleeping,
  and no program writes a capacity-8 dead list in any captured frame. Its layout needs a save where debris spawns.
- Output-side size and colour of Fire, BottomFire and the candles were read only as far as noted; a full replica of
  each output's expression is a separate job.

## 2. Proof on the captured bytes

Scripts: `decode.py` (layouts), `run_campfire.py`, `candle_single.py`, `candle_sets.py`, `check_all.py`,
`overlay*.py` in the scratch folder. Decoded records: `dec-campfire-<phase>-<frame>.json` (one batch per family and frame: the last update dispatched;
`check_all.py` covers every batch).

- **Pictures** (dots = decoded positions projected with the frame's own camera matrices over the game's own frame):
  `overlay-campfire-vfx-live-facing-7731-crop.jpg` (smoke lavender, sparks yellow, BottomFire orange, Distortion
  green, Fire red): the smoke dots form two rising columns over the two fire pits, sparks around them, BottomFire and
  Distortion at the bases. `overlay-candle-vfx-live-facing-3627-crop.jpg`: one dot at each of the 8 lit wicks, none at
  the unlit ones. Full frames: same names without `-crop`.
- **Counts**: `check_all.py` over all 16 frames: 154/154 (frame, batch, slot, system) checks give alive flags equal
  to `capacity - deadCount[slot]`; 522/522 live particles have `0 <= age <= lifetime`; every alive word is exactly 0
  or 1. Campfire 7731: Smoke 7/4/7, Sparks 6/7/5, BottomFire 4/4/4 alive; 7730 Smoke 7/5/8, Sparks 5/6/7: the counts
  change between live frames.
- **Frozen**: frames 7748 and 7749 (`timeScale` 0): the updates still dispatch with dt = 0 (`gv+304` = 0.0 for
  Smoke, `gv+60` = 0.0 for BottomFire) and all 27 live records are bit-identical between the two frames (and to
  returned 7743). Live 7730 vs 7731: none of the 52 compared records is bit-identical.
- **Particles are particles**: Smoke rises (y 105.2 to 106.5 over ages 0.05 to 2.57 s), its newest particle has
  velocity (0, 1.249, 0) at age 0.05; Sparks have ids 45..55 and masses from size; candle Single local position is
  exactly (0,0,0) and every wick lands on a lit candle.

## 3. What a reader in the tracer needs each frame

### 3.1 Which buffers, and when they are current

- No public API maps a VisualEffect to its buffers. The native side can: each family's init and update programs have
  fixed DXBC hashes (full hashes below). At the update dispatch, the bound resources are: `u0` attribute buffer, `u1`
  dead list, `t0` graph values, the structured index list, `cb0` (counts). The same resources (observer ids, stable in
  every phase that dispatches them) are what the output draws bind: campfire Smoke attr 14684 / dead 15794 / gv 15788;
  Sparks 14688 / 15100 / 15094; BottomFire 15108 / 15110 / 15102; Fire 15084 / - / 15078; Distortion 15092 / - / 15086;
  candles 5196 and 5204 with gv 5190 and 5198.
- Order in campfire frame 7731 (1640 events): Smoke and Sparks inits at events 17-18, the five updates at 26-30, and
  the output draws at 1548-1590. **After the last update of a batch (event ~30) its buffers hold the frame's final
  state**; no captured dispatch writes them again before the next frame (the only VFX-looking kernels in between, at
  events 99-101, write a separate 8 KB buffer from other batches' buffers).
  Several batches can exist per asset: turned away, EmberGen had three (attr 15084, 14968, 14273).
- Candle families have no update kernel: their records are written once at init and stay (same init serial 59584/59585
  in every phase). Which `g` maps to which slot this frame is then only in the output draw's CB3 (`cb3[i].x` slot,
  `cb3[i].y` g, read by 39787aa241...).
- World transforms: `gv+16` (see 1.3); a managed-side match to the VisualEffect transform is not needed for positions.

### 3.2 What an adapter would copy (per live particle)

| family | read from the game's buffers | needs from graph values / output |
|---|---|---|
| Smoke | pos 12, texIndex 4, age 4, alive 4, lifetime 4, angle 4 = 32 B | size `gv+312`, gradient at age/lifetime, flipbook `gv+272` |
| Sparks | pos 12, age 4, velocity 12, alive 4, size 4, id 4, lifetime 4 = 44 B | gradient, width `gv+360` |
| BottomFire | lifetime 4, pos 12, angles 12, texIndex 4, alive 4, age 4 = 40 B | localToWorld (sibling `gv+16`), colour `gv+80`, scale `gv+96` |
| Fire | pos 12 (camera-pulled) | localToWorld `gv+16`, colour `gv+144`, size `gv+220`, flipbook `gv+232` |
| Candle Single | pivot 4, scaleX 4 | localToWorld `gv+16`, size `gv+144` |
| Candle sets | pos 12, pivot 4, scaleX 4 | localToWorld `gv+16` |

A packed record of about 48 B (world position, size or two half-sizes, rotation, colour/alpha as half4, texIndex,
family/material id) covers all six; colour and size must be evaluated the way each output VS does, not read.

### 3.3 Effects out of view: not simulated (finding)

- Turned away (frames 7736/7737), batch 15084 dispatches 7 other instances (slots 36, 45, 54, 58, 87, 88, 115) and not
  11/31/96; across the three batches 17 instances are dispatched, all 17 in front of the camera, none behind; Smoke
  gets no dispatch at all.
- Slot 11's component stays enabled but `culled`: its particles survive (same lifetimes 2.51475, 2.56815, ... before
  and after) but aged only 0.179 s while game time advanced 0.683 s (59.253 -> 59.936): **frozen while out of view**.
- Slots 31 and 96: those VisualEffect components (and their renderers) were disabled while away (image json
  `enabled: false`; who disables them is not proven, most likely the game's scripts); on return every particle is new (age 0.071, Smoke empty and sleeping): **reset**.
- So a flame behind the camera has stale or no particles. Before an adapter can honour "off-screen effects must be
  included" something must keep these systems simulating and enabled; that is a design question for the coordinator
  and the owner (not decided here).

### 3.4 HDRP's transparent drawing switched off

The capture cannot tell: HDRP drew all transparents in it. Unity decides simulation by its CPU culling of the
VisualEffect (3.3), not by the draw, so turning off only the draw pass may keep the simulation; unproven. The one run
that answers it: recipe `particles-capture`, scene `campfire`, the facing phase only, two frames, with HDRP's
transparent drawing switched off exactly the way the tracer will do it; answer = whether 5dd03cbe52db (Smoke update)
and 869d6c73cb2a (Sparks update) still dispatch with `cb0[1].y = 3` and the alive counts still change.

## 4. Families the capture does not cover

Waterfalls (VFX_WaterfallGroundSplash: DownFOG 70, Droplets 90), moths (VFX_Moth_Enviro: System 250, Smoke 2000,
System (2) 36, Smoke (1) 500, Strips 800), Wyrd effects (VFX_Status_Wyrdnight_SME: Fire 64, Strip 2560; the
WyrdNight_Runes assets) and trails (all "ParticleStrip" outputs) were loaded in both scenes but culled, so they have
no dispatch in the capture (3.3 explains why). Needed for each: one `particles-capture` run of a save where the effect
is on screen, then the same decode (init/update programs give the slice and buckets, graph values give the space and
transform, the dead list gives the live set). Strips need one thing more: VFX strips keep a per-strip ring of indices
in an extra buffer; the strip system's init/update will bind it as one more UAV, and the reader must decode it to join
the points in order. In this capture all of them were `culled` in both scenes (image json), except VFX_Lines (a strip
effect, 572 m away) whose component was disabled with both systems sleeping and 0 alive; none of them dispatched. I did
not check the other scenarios' saves, so a save that shows each one on screen has to be found or made first.

## Program hashes (sha256 of the game's DXBC; local evidence only)

Candle Single init `1f1e2c1981ef2e0070feb2594064c6f321f99dfdf0498229fe312856fdb34db8`, output VS
`39787aa241763982a94b3c90843fd733b32a98c44eb3a21a48675bac2b556af2`. Sets init: 01
`8bde72207fda9ae93972938d58040a4295d95f57faa2701c2ebd05af34b0b501`, 02
`155b31760ec1bc3be91ea72ac4dff052bc3e68e7c7291fc33f478b34ec63b748`, Straight
`d097b82ccb5a38edc20ca194cbe268d418c701d9fec1dbc83a211a00b59606e8`. Smoke init/update
`790a8c9e9d649c2cad866cf5d2bbb3fac8c852787490c143e3c299d52f0d5619` /
`5dd03cbe52db19b285521cf83cf1fd61a426ad5aeb87098fb8cf77a15ae7c5ad`. Sparks
`1e6ccc1f091aa24d7216f856c431959c528664d849f6c1675c8eac1601e54cc8` /
`869d6c73cb2aa55949f7f4541cc9793cbedc0b0fac4089e2b0ef8d6f1407d32c`. Fire
`20264a893f2d003ea0f87141e52eea0d9a38575249161834aedc8772dfb41cdc` /
`fdea2d81408f870754bdcac4f4b625d97a37d13b12e7f67806c8d88d6cdc9991`. Distortion
`2ea9b431f3c9fd8ac2e976a9079eb1098d0a97759adeb4b7f5a053e98be1cd15` /
`4ae5c8ec7fdde640db132fbd9d00d29d3b8270d15e19e9e45a67d6c10d7222ec`. BottomFire
`9888916bd39bed0d3a2280889988434f349b162dbad69e21c38a50850935a532` /
`8a7b3675e7460768e4152f7333b609141ee12c6ec15c3a549a6e44c6bb98226c`. Output VS: Smoke
`b9e2fa1088c65af20980c457c37a36aecc801a68a223ee26a7c5b004dbbaf0a1`, Sparks
`dc244605509345637ecd8f56aeae057dd384b67889cf3398ad616f5689398763`, Fire
`a1328245ab4162f60aac739f112ca877748950e888d0f1fc9a339020b7be6ace`, BottomFire
`9c433897c6cdb7c45bb94da5757cce2cc34b953f6a80331d84f6eb8e0cb7c32f`, Distortion
`816f0ae8b5685989a1526f13705b581d4d2818c55956647fba5753d9272f97eb`. A game patch that recompiles a graph changes
its hash: a reader keyed by hash must log unknown VFX-shaped programs instead of guessing.

## Proven and not proven

Proven by bytes and program lines: every layout in sections 1.1 and 1.2 (offsets as listed), the live-set rule
(154/154), world space for Smoke and Sparks, local space plus `gv+16` for Fire and the candles, positions on screen
(two overlays), frozen frames unchanged, the frame order of simulation and draws, out-of-view instances not simulated
(frozen or reset).

Not proven: Debris (never ran); BottomFire's and Distortion's transform taken from a sibling system (same slot and `g`
order in all five systems is proven, the draw itself uses its CB1); the Fire camera-pull formula for rotated or scaled
instances; simulation with HDRP's transparent pass off (3.4); waterfalls, moths, Wyrd effects and strips (section 4).

## Workarounds met

None removed (no source touched). The game itself has one camera-dependent trick in its data: Fire's position is
pulled 0.4 m towards the camera every frame (1.2); a tracer adapter should place the fire at the instance origin.
