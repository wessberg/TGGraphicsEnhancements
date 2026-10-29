# ser1: the bounced light's pass designed for Shader Execution Reordering (PT-99)

> Closed on 2026-10-10 (docs/agents/rule-2.md): the study's build option, pt_ser_study.hlsli, its recipes and its
> check lines are gone. The bounced light's pass regroups once, after its first ray and outside its rays' loop, and
> its reuse across frames is a pass of its own (ser-2.md's 48), in every build.

Worker `ser1`, copy `...\scratchpad\wt\ser1`, base `base83`, brief `docs/agents/briefs/brief-ser-1.md`. 2026-10-10.
Nothing here ran in the game, nothing was deployed or committed, nothing was written into the main tree or the game
folder. No GPU test ran (a new reordering pass cannot be made under the offline test's memory ceiling: the brief).

## State when stopped

(kept current after every step; the newest line is the truth)

- 11:25. **HAND-OVER 1 IS READY, AND THE DESIGN IS WRITTEN. STOPPED HERE, as the brief says.**
- The study build: `%TEMP%\tgb\ser1\TGPathTracing.Native.dll`, 41,798,656 bytes, SHA-256 `D9677EF78AE2...`,
  built 11:11 with `-DTGPT_WITH_NRD=OFF -DTGPT_RAIN_WETNESS=ON -DTGPT_SER_STUDY=ON` (so: no NRD in it; the run uses
  no denoiser). Its two passes that ship are the main tree's own bytes (`pt_gi_ser.h` hash `D6928ACEF9BD` in both).
  All six study passes compiled (5 to 7.5 minutes each). NEVER RUN: whether the driver makes the six pipelines, and
  what they cost, is the game's to say. Section 4 says how to run it and what to read.
- Checks: source checks 112 of 112 in the copy; the plugin builds (0 warnings, 0 errors); the tracer's C++ with the
  study on compiles with no warning; the passes that ship get the same source text as before the edits
  (`ser1-tools\same-source.ps1`: 7 of 7 cases).
- NOT DONE, and not mine: the run. After it: section 5's table read against its numbers, then the design's order
  (section 6, "The order") decided.
- To merge or not: the study is one switch, OFF by default. The coordinator can deploy the DLL above as it is for
  the run, or merge the copy and build main with `-DTGPT_SER_STUDY=ON` (then NRD is in). When the game has
  answered, the study comes out the same day (AGENTS.md): `pt_ser_study.hlsli`, the `#if` lines in `pt_gi.hlsl`,
  `pt_common.hlsli`, `pt_shaders.hpp.in`, `CMakeLists.txt`, the `#ifdef TGPT_SER_STUDY` blocks of `tracer.cpp`, the
  two recipes and the check's lines.

## 1. The answer so far, in plain words

- Nobody knows yet why the pass is slow. The run of section 4 is made to tell.
- What reading already shows: we use the feature in its weakest form, and against four pieces of NVIDIA's advice
  (section 3). The likeliest cause is not "sorting is slow". It is what we sort BY and what we do AFTER the sort:
  we sort every pixel of the screen by a 16-bit material number, which throws the screen's order away, and then
  run work that needs the screen's order (the reuse across frames reads the pixel's neighbourhood and writes the
  pixel). NVIDIA names exactly this as the case where reordering turns into a loss.
- One thing nobody had seen: the compiler turned our ONE regrouping call into THREE in the bounced light's pass
  (section 2). The reflections' pass, which is not slower with reordering, has one.

## 2. What the compiled shaders say (counted, not guessed)

Counted from the DXIL text DXC leaves in the generated headers (`ser1-tools\ir_stats.py`, `ir_live.py`: the values
defined before a regrouping call and used after it, summed by type; an upper bound of what the driver must keep).

| Pass | instructions | ray query objects | regrouping calls | bytes of values live across a call | resource handles live |
|---|---|---|---|---|---|
| bounced light, as it ships (`pt_gi_ser.h`) | 307,486 | 116 | **3** | 266 / 542 / 547 | 7 |
| reflections, as it ships (`pt_specular_ser.h`) | 420,028 | 152 | 1 | 427 | 9 |
| bounced light, the plain compute pass | 306,784 | 116 | 0 | - | - |
| study 16 "none" (`pt_gi_ser_s1.h`) | 307,473 | 116 | 0 | - | - |
| study 32 "keys" (`pt_gi_ser_s2.h`) | 307,642 | 116 | **1** | 540 | 7 |
| study 48 "early" (`pt_gi_ser_s3.h`) | 308,106 | 116 | 1 | **205** | 7 |
| study 64 "hit" (`pt_gi_ser_s4.h`) | 308,100 | 116 | 1 | 205 | 7 |
| study 80 "back" (`pt_gi_ser_s5.h`) | 308,222 | 116 | 2 | 205, then 84 | 7, then 3 |

- **Three calls from one line.** `pt_gi.hlsl` has one `PT_REORDER`. The compiled pass has three: one where the ray
  was not traced (hint 65535, 266 bytes live), one where it met something (the material's key, 542), one where it
  met nothing (65535, 547). The compiler copied the code after the ray for each outcome, because two of the three
  keys are constants. So the pass has three places a thread can be moved at. With a key that is computed (every
  study pass) the outcomes join first and there is ONE call. So the study's 32 in its mode 0 (today's key, today's
  place, today's 540 bytes) differs from the pass that ships in this alone: one call in place of three.
- **What a thread carries today: about 545 bytes** of values (22 float3, 15 float, 14 int, 7 bool, 6 float2, 3
  float4, 2 int2 and one 3 x 4 matrix) and 7 resource handles. By hand from the source that is: the fetched hit
  (`Hit`: 272 bytes as declared, most of it live), the pixel's surface (`Surface` with its view: 168 bytes as
  declared), the ray's direction, the cloth's tint, the random state, the loop's sums (the reservoir's candidate:
  position, normal, radiance, weight sum, target, and the rays' mean: 56 bytes) and the static flags.
- **The study's rearranged passes carry 205 bytes** (10 int, 6 float3, 5 bool, 4 float, 2 int2, 1 float2, one
  3 x 4 matrix): the hit as the query gave it (instance, geometry, triangle, barycentrics, distance, transform: 72
  bytes), the direction, the random state, the cloth's count and tint, the loop's sums. The surface is read again
  after the call (4 more texture loads in the compiled pass: it really is read twice, not carried). A cut of 62 %.
  Before the second call of "back": 84 bytes (the reservoir's candidate and the mean).
- **Memory arrays.** The pass has 189 arrays on its stack (7,172 bytes). 83 carry DXC's lifetime marks and are dead
  at the call. The other 106 are 16 bytes each (1,696 bytes): DXC's way of indexing a 4-vector by a number
  (`v[i]`), written and read on the spot (`ir_alloca_uses.py`). A driver that trusts only the marks would keep those
  1,696 bytes across every call; one that looks at the stores keeps none. NOT KNOWN which ours does: the study logs
  the driver's own stack size for each pass (section 4), which tells.
- **Live state alone does not explain it.** The reflections' pass carries as much (427 bytes, the same arrays) and
  is not slower with reordering. What differs: three calls against one; the bounced light's call sits inside the
  loop over the pixel's rays; in a room nearly every bounced-light thread has a key of its own kind (every pixel
  traces and meets a material) while most reflection threads share one (a rough surface traces nothing: key 65535),
  so the bounced light's threads really are all moved; and the bounced light's pass does screen work after the call.
- The ray query objects (116) are allocated where they are used, not at the top: they are not live across the calls.

## 3. The documents, and what we do against their advice

Sources read (2026-10-10):
1. **Microsoft, HLSL proposal 0027 "Shader Execution Reordering"**
   (`github.com/microsoft/hlsl-specs/blob/main/proposals/0027-shader-execution-reordering.md`): the three overloads
   of `dx::MaybeReorderThread`; "Applications should set [the number of bits] to the lowest value required";
   "Implementations should generally prioritize the hit group specified in the HitObject higher than the coherence
   hint"; "Reordering before a known to be coherent or cheap shader can be counterproductive"; "it is crucial that
   the DXIL-generating compiler does not move non-uniform resource access across reorder points"; a `HitObject` made
   by `FromRayQuery` "is not bound to a shader table record" and can be given one with `SetShaderTableIndex`;
   `MaybeReorderThread` only in ray generation shaders, `HitObject` in ray generation, closest hit and miss;
   `HitObject::TraceRay` runs traversal with its any-hit and intersection shaders and no closest hit or miss; "Some
   implementations may achieve best performance when HitObject::TraceRay, MaybeReorderThread, and HitObject::Invoke
   are called back-to-back".
2. **Microsoft, DirectX Raytracing functional spec** (`microsoft.github.io/DirectX-Specs/d3d/Raytracing.html`; the
   first 100,000 characters read): ray queries may be used in any shader stage; of inline ray tracing: "It is
   likely that shoehorning fully dynamic shading via heavy uber-shading through inline raytracing will have
   performance that depends extra heavily on the degree of coherence across threads"; the pipeline's stack holds
   "parameters ... and/or live state"; the capability is `D3D12_FEATURE_DATA_D3D12_OPTIONS22
   .ShaderExecutionReorderingActuallyReorders`.
3. **NVIDIA, "Shader Execution Reordering" whitepaper v1.0, 2022**
   (`developer.nvidia.com/sites/default/files/akamai/gameworks/ser-whitepaper.pdf`), the fullest source:
   - the key "in most cases ... will be a HitObject"; the final key is, in descending priority, "1. Shader ID stored
     in the HitObject 2. Coherence hint, with the most significant hint bit having highest priority 3. Spatial
     information stored in the HitObject"; with a number alone the system "will also attempt to retain locality in
     the thread's launch indices";
   - "Live state consists of any variables that are defined before reordering and used after ... The smaller that
     state is, the lower the overhead of the ReorderThread call. Applications should therefore strive to reduce live
     state as much as possible" (by "packing values, using lossy compression, or restructuring control flow");
   - "The more coherence hint bits are used, the higher the granularity ... However, using more coherence hint bits
     also reduces the resolution at which the hit object can be taken into account"; the count "should only be set
     to the number of coherence hint bits that are actually relevant, and no higher"; at most 16 bits with a number
     alone, 8 beside a hit object;
   - WHEN NOT: "because it isn't free ... there are situations where reordering is not advisable"; "reordering
     threads with respect to their ray hit location means giving up on the 2D screen-space locality that physical
     threads have by default. This can negatively impact the performance of reading gbuffer data or writing output
     buffers, which can turn reordering into a net loss when the rest of the workload is particularly cheap";
   - loops: "It is often beneficial to make sure that the entire warp finishes the loop at the same time", with a
     hint bit for "will this be the last turn"; "when multiple rays are traced in a loop, it can be important to
     reorder in a way that is optimized towards subsequent rays";
   - ray queries: a hit object made from a ray query's hit "provides an easy path to integrate reordering into
     existing applications that use RayQuery", with hit shaders or with the shading left in the ray generation
     shader; "applications that only use a single anyhit/intersection shader combination across all objects ... all
     instances can refer to the same shader table range used only to trigger anyhit ... while the closesthit or miss
     shader is selected using a manually computed index";
   - Nsight Graphics shows live state across a call and "Active Threads Per Warp" line by line (the tool for the
     next job: it shows what the driver really keeps, which no count of ours can).
4. **NVIDIA developer blog, "Improve Shader Performance and In-Game Frame Rates with Shader Execution Reordering"**:
   trace to a hit object, reorder, invoke; 20 to 50 % in Unreal's path tracer, 20 to 30 % in Lumen's reflections;
   "Other rays are performing cheaper operations and are less important to reorder".
Nothing of these was copied into the tree: the quotes above are short, in this report alone.

What the tracer does against that advice:
1. **It sorts by a number alone, never by a hit object.** So the driver knows nothing of where the hit lies: no
   grouping by place, which is half of what the feature is for (data that lies together is read together), and
   the rays a path traces next start from points all over the scene.
2. **It uses all 16 bits, for a material's number.** Advice: the fewest bits that say what the thread does next. A
   material's number does not say that (most materials run the same code); "water, glass, plain, sky" does, in 2.
3. **It does screen work after the sort.** The reuse across frames (RTXDI's: the neighbourhood's surfaces and
   reservoirs read, a ray traced to check, the pixel written) runs in the sorted order. The whitepaper's "net loss"
   case, word for word.
4. **It carries about 545 bytes across the call**, most of which could be read again or fetched after (205 in the
   study's rearrangement).
5. And the compiled pass has three calls where the source has one (section 2): not against a written rule, but
   nobody's intent.

## 4. HAND-OVER 1: the study build, and how to run it

### What is built
ONE compile switch, CMake's `TGPT_SER_STUDY`, OFF by default. OFF: the build is today's (no pass is compiled with
`PT_SER_STUDY`; the passes that ship preprocess to the same text as the base's). ON: six MORE ray generation passes
beside the two that ship; each is a pipeline of its own, made when the tracer starts, and runs only in a frame whose
hidden `Speed trial` asks for it. Nothing switches the regrouping off in a pass that ships. The plain compute passes
are made too in this build (the yardstick, `Speed trial` 8), with no environment variable.

`Speed trial` = arrangement x 16 + mode x 256 (8 alone: the yardstick):

| Number | Name | What it is |
|---|---|---|
| 0 | as it ships | today's two passes |
| 8 | yardstick | both passes as plain compute passes (never for players) |
| 16 | none | BOTH passes as ray generation shaders with NO regrouping call compiled in |
| 32 | keys | bounced light: the call where it is today (one call, 540 bytes), the key and its bits by the mode |
| 48 | early | bounced light: the call right after the ray, before the hit is fetched; 205 bytes; key by the mode |
| 64 | hit | as 48, regrouped by a hit object made from the ray query, the mode's key as its hint |
| 80 | back | as 48, and regrouped again by the screen (32 x 32 pixel blocks) before the reuse across frames |

Modes (x 256): 0 today's key, 16 bits (with 64: the hit object alone); 1 what comes next (plain, water, glass, sky),
2 bits; 2 that and "feeds the cache", 3 bits; 3 the material's low 7 bits and the feeding, 8 bits; 4 no bits; 5 hit
or none, 1 bit; 6 a hash of the pixel, 8 bits (bad on purpose); with 80 only, +8: the second regrouping with no key.
(The brief's "0, 2, 4, 8 bits": 0, 1, 2, 3 and 8 are here; 4 bits of a material's number would mean nothing.)

The same picture in every arrangement: the same rays, the same random numbers in the same order, the same shading.
Where it cannot be the same to the bit: nowhere by intent. 48, 64 and 80 read the pixel's surface a second time
(the same textures, written by no pass meanwhile).

### Files (all in the copy)
- `native/pathtracing/shaders/pt_ser_study.hlsli` (new): the modes' keys, the ray traced without fetching the hit
  (`study_trace`, a copy of `trace_from_surface` up to the fetch: keep side by side), the rearranged pass
  (`gi_pixel_early`, a copy of `gi_pixel`: what differs is marked `DIFFERS`).
- `pt_gi.hlsl`: the include under `#ifdef PT_SER_STUDY`; at the call `#if ... == 2` the study's call, else the line
  as it was; the entry picks `gi_pixel_early` for 3 to 5. `pt_common.hlsli`: `PT_REORDER` is empty when
  `PT_SER_STUDY` is defined and not 2. `pt_shaders.hpp.in`: the six headers under `#ifdef TGPT_SER_STUDY`.
- `CMakeLists.txt`: the option, six `dxc` commands (`-D PT_SER_STUDY=<n>`), the tracer's definition. Refuses to be
  built with `TGPT_SPEED_MEASURE`, `TGPT_REGROUP_WATER` or `TGPT_LAMP_AROUND`.
- `native/pathtracing/src/tracer.cpp`, all under `#ifdef TGPT_SER_STUDY`: the six pipelines made after the others,
  two at a time, each in its own try (one that fails is logged and its number runs the pass that ships); the shader
  table grows to 8 records; `dispatch_pass` picks; the log lines. (One line outside the `#ifdef`: the table's size
  is a named constant, 2 as before.)
- `tests/ingame/scenarios.json`: recipes `ser-study` (the short one) and `ser-study-keys` (the sweep).
- `tests/source_checks.py`: the study's rules added to `test_path_tracing_speed_on_trial_default_off`; two counts
  of CMake lines moved by one (`${PT_SPEED_DEFINES}` 3 to 4, RTXDI's include path 5 to 6: the study's command).

### What the tracer logs (read these)
- At the start, once per pass: `Reordering study: <pass>: made in N s; the driver's stack for its ray generation
  shader X bytes, for the pipeline Y bytes a thread`. X is the driver's own word on what a thread carries. Compare
  "as it ships" (about 545 if the driver keeps values only; about 2,200 if it also keeps the unmarked arrays; far
  more if it keeps ray query state) with 48 / 64 / 80 (205 by our count) and with 16 (no call at all).
- When the number changes: `Speed on trial (N): ... the reordering study: arrangement A of the bounced light's pass
  ..., mode M`. "asked for, but it was not made" means that pipeline failed at the start: its step is void.
  The hit object variant (64) is the one most likely to be refused (it uses `HitObject::FromRayQuery`, which DXC
  1.9.2607 compiles; whether this driver takes it is not known).

### How to run
1. Deploy the study DLL (the coordinator's; the plugin is unchanged). Look at free memory first: the first start
   makes 10 large passes (the two that ship, the two plain ones, the study's six; the six two at a time after the
   others). Expect the first start to take 10 to 15 minutes and the driver to take many gigabytes meanwhile; later
   starts use the driver's cache.
2. `.\tools\harness\Run-PtHarness.ps1 -Scenario tavern-room -Recipe ser-study` first (the scene with the 4 x gap;
   11 traced steps of 25 s and 16 frames, about 7 minutes). Then `metal` and `water-day` with the same recipe.
3. Read each step's bounced light and reflections ms (`measure_read.py`, the 10 s lines inside a step), and the
   start-up lines above. The pictures should not differ between steps (`same_picture2.py`).
4. `ser-study-keys` only for a scene where the first run says the key matters (32, 1056 and 288 differ).
5. Put the normal build back.

## 5. What I expect of each step, written BEFORE the run

Known (run `20261010-100720`, bounced light's pass, ms): the tavern 56.0 as it ships, 13.9 plain; the town 60.9 and
27.8; the beach 42.6 and 70.8. My best guess of the cause, in this order: (1) the screen's order lost for the work
after the sort, (2) every thread moved with about 545 bytes, (3) the three calls. I hold it loosely: each line below
says what a number would prove.

| `Speed trial` | Step of `ser-study` | The tavern, bounced light, my guess | What it decides |
|---|---|---|---|
| 0 | as it ships | 56 | the base |
| 8 | yardstick | 14 | the target |
| 16 | no regrouping call | 14 to 18 | AT 14 TO 18: the ray generation form is free; the cost is the regrouping, and the steps below say which part. AT 45 OR MORE: the form itself is slow (a giant ray generation shader with inline ray queries); then no key and no cut will help, and the design's step 3 (rays through the pipeline, hit shaders) is the only way. In between: both. THE MOST IMPORTANT NUMBER OF THE RUN. |
| 32 | same place, same key, one call | 50 to 56 | the pass that ships with one call in place of three, nothing else changed. Far under 56: the three calls are a cost of their own. |
| 1056 | same place, no bits | 18 to 28 | the call with nothing to sort by. Near 16's time: a call that moves nobody is nearly free, so MOVING threads is the cost. Near 32's: the call itself costs, whatever the key. |
| 288 | same place, by what comes next | 18 to 30 | in a room nearly every thread is "plain": few move. Near 1056's: confirms "moving is the cost". At the beach this key should keep the gain (below). |
| 48 | early, little carried | 40 to 50 | today's key, 205 bytes in place of 540, the hit fetched after. 32's time less 48's is what the carried bytes cost. If it lands near 14: live state was the whole story. |
| 304 | early, by what comes next | 16 to 24 | the best inline arrangement by my guess: little carried, few moved in a room. |
| 64 | early, by hit object | 35 to 55 | the driver sorts by where the hit lies. I expect it NOT to fix the room, because the reuse still runs in sorted order. At 14 or under: hit objects alone are the answer. |
| 80 | early, then back by the screen | 25 to 35 | 48 with the screen's order put back before the reuse. 48's time less 80's is what the screen's order is worth, net of a second call. If it is large, the reuse belongs in a pass of its own (design, step 1). |
| 0 | as it ships again | 56 | the run's drift |

The reflections' pass changes only at 16. There: the tavern 11.6 as it ships and 15.0 plain; I expect about 15 (the
plain pass's time), which would say the ray generation form costs that pass nothing and the regrouping gains it 3
ms. The beach: 140 as it ships, 318 plain; I expect about 300.

The beach, bounced light (42.6 as it ships, 70.8 plain): 16 about 70 (no regrouping, no gain); 288 (2 bits: water
or not) 40 to 45, which would say 2 bits carry the whole gain there; 304 at or under 42; 64 unknown, perhaps the
best; 80 near 48. The town like the tavern, 28 the target.

What would surprise me, and what it would mean:
- 16 slow: see its line.
- 1056 as slow as 0: then even a call that sorts nothing costs 40 ms, and the cost is the driver's handling of a
  regrouping point in this shader (its size, the loop), not the moving.
- The driver's stack size in the log in the tens of kilobytes: then it keeps far more than the values (the arrays,
  or ray query state), and cutting THAT is the job.

## 6. The design: the bounced light's pass built for reordering

Target: with reordering on, the tavern at or under 14 ms, the town at or under 28, the beach no slower than 42.6.
Three steps, each measurable in the game against the yardstick, each keeping the picture. Steps 1 and 2 are inline
(no new kind of shader, no change to the acceleration structures). Step 3 is the pipeline's own way.

### What the pass does, sorted by what it needs
- A. Screen work, the same for every pixel: read the surface, draw the ray, the reuse across frames, the stores.
  Wants the screen's order. Never needs sorting.
- B. The ray. Wants threads of a warp alive together.
- C. Shading what the ray met: the material's textures (different data), plain / glass / water / subsurface
  (different code), more rays from the hit (different places), a loop that ends at different turns. This is what
  the feature is for.

### Step 1 (hours; decided by the run's 16, 48 and 80): the pass ends where its sorted work ends
- Split it. `pt_gi` keeps A's first half, B and C, and stores the frame's own sample as a GI reservoir (RTXDI's
  store, as the pass already does at its end) and the rays' mean. A new plain compute pass, `pt_gi_reuse`, loads
  that reservoir, does RTXDI's reuse across frames with its checking ray and stores the result. The sorted pass
  then ends with two writes a pixel, as the reflections' pass does. The reuse runs in the screen's order, always.
- No new memory: the sample travels in the reservoir page the pass writes anyway. One more dispatch (about 0.1 ms).
- If the run's 80 shows the second regrouping does as well, that is the smaller change; the split is the one that
  cannot regress, because the reuse then never sees a sorted order.
- One call, not three: the key is computed, so the ray's three outcomes join before the call (as in every study
  pass), and a source check counts the calls in the generated text.

### Step 2 (a day; decided by 288, 304 and 64): sort by the hit, with a small hint, carrying little
- The call right after the ray, before anything of the hit is fetched (the study's 48): the thread carries the hit
  as the query gave it, the direction, the random state and the loop's sums. Cut further: the transform (48 of the
  205 bytes) is read from the hit object after the call; the loop's sums (56 bytes) go when the pass traces one
  ray a pixel a dispatch (more rays: more dispatches, or a loop turn each, as today).
- The key: the hit object (`dx::HitObject::FromRayQuery`: the study's 64) with at most 3 hint bits, most important
  first: what comes next (water's path / glass's walk / plain / nothing), then "feeds the cache". Never the
  material's number.
- The water's path and the glass's walk: their own turns regroup too (the trial `TGPT_REGROUP_WATER` is the start),
  each turn by its own hit and a "last turn" bit, as the whitepaper's loop example.

### Step 3 (days; the redesign, needed for certain if the run's 16 says the ray generation form is slow)
Rays through the pipeline, shading in hit shaders shared by both big passes.
- The ray: `dx::HitObject::TraceRay`. Traversal is the driver's own; the inline alpha test becomes ONE any-hit
  shader for the whole scene. No acceleration structure changes: every instance already has hit group index 0 (every
  instance record starts zeroed and the tracer never sets `InstanceContributionToHitGroupIndex`), and the trace
  passes 0 as the geometry multiplier, so one hit group record serves every triangle. Opacity micromaps keep
  settling most alpha-tested triangles with no shader at all, as now; the pipeline is made with
  `D3D12_RAYTRACING_PIPELINE_CONFIG1` and its `ALLOW_OPACITY_MICROMAPS` flag (today's is `PIPELINE_CONFIG`: it
  traces by ray queries only, whose own flag says the same).
- The any-hit shader is `alpha_opaque` with its per-ray facts moved from static variables to the payload: the
  ray's kind (light, bounce, camera, mirror: 6 bits), its seed for the coverage coin (`ray_chance`), and the counts
  it gives back (cloth, glass, leaves met). 8 bytes. The rules that pass a near back face, the hero's own body and
  what the wind moves stay a re-trace from past the hit, as today (they concern opaque triangles, which no any-hit
  sees).
- The shading: closest-hit shaders BY CLASS, not by material: plain (HDRP Lit and what reads like it), the game's
  own shader programs (`pt_materials.hlsli`, `pt_decal_graphs.hlsli`), glass, water, subsurface; one miss (the
  sky). The class is read from our own material record after the trace and set with
  `HitObject::SetShaderTableIndex`: the whitepaper's "manually computed index". Then
  `MaybeReorderThread(hit, hint, bits)`: the driver sorts by shader first, then our hint, then the place. Then
  `Invoke`.
- The path as turns of the ray generation shader's loop: trace, set the class, regroup (hint: last turn, feeds),
  invoke; the payload carries the path between turns (throughput, radiance so far, the next ray, the random state,
  flags: about 48 bytes packed). The water's path becomes turns of the same loop (it can regroup only there:
  `MaybeReorderThread` exists in ray generation shaders alone). Shadow rays stay inline ray queries inside the hit
  shader: one-off rays the whitepaper says not to regroup for.
- What it needs of the tracer's C++: the state object with the ray generation shader, the any-hit hit group, one
  hit group a class, the miss shader, shader config (payload about 48 bytes, attributes 8), recursion 1; a hit
  group table of 1 + classes records and a miss table of 1, built once (never per scene); no local root
  signatures (everything is reached through the global root signature and the descriptor heap, as now).
- Both big passes use the same hit shaders (the reflections' "shade what is seen" is the path's first vertex), so
  the code exists once: as a state object collection the two pipelines share.
- What it costs to make: today each pass is one function of 300 to 420 thousand instructions with every helper
  inlined at every call (116 and 152 ray queries), and the driver takes minutes and gigabytes for each. Hit
  shaders hold each helper once, so the whole should be smaller and quicker to make. NOT MEASURED: the first build
  says. Video memory: the tables are a few hundred bytes; the driver's per-thread stack is the unknown, and the
  study's log line gives today's.
- What it risks: any-hit calls where triangles are not settled by a micromap cost more than the inline test (the
  town's plants); every `TraceRay` and `Invoke` is a place where live state is saved, so the same discipline holds
  there; static variables that today carry facts between helpers must become payload or arguments.

### The order
Run first. Then step 1 if 16 and 80 say so (expected: the tavern near the yardstick at once). Then step 2. Step 3
when 1 and 2 leave a gap to the target, or at once if the run's 16 is slow. Each with the yardstick's step in its
recipe, and Nsight Graphics' live state and "Active Threads Per Warp" views once, on the owner's PC, to see what
the driver keeps.

## 7. Workarounds and loose ends met
- `trace_from_surface`'s up to five queries a ray (what the wind moves, a near back face, the hero's body): a
  workaround in the tracing itself; left as it is (the design keeps it as a re-trace).
- The static variables that pass a ray's kind to the alpha test (`g_bounceRay`, `g_lightRay`, ...): they are why
  the alpha test cannot simply become an any-hit shader; named in the design, not touched.
- `v[i]` on 4-vectors makes 106 unmarked 16-byte arrays in the compiled pass (section 2): harmless if the driver
  looks at stores; to look at once the stack sizes are known.
- The beach's plain step was brighter than the reordered one in run `20261010-100720` (57.6 against 53.6): NOT
  looked at here. The study's 16 (the same ray generation pass with no regrouping) against 0 at the beach will say
  whether the regrouping or the pass's form goes with the difference.
- The tracer's memory lines at the start ("the two reordering passes ...") count the study's six pipelines in with
  the two that ship, in a study build.
- A slip of mine, undone: a first try of the "same source" check made DXC write `pt_gi.i` into the base copy's
  shader folder (10:52); removed at 10:58, the base has its 45 files again. Nothing was written to the main tree.

## 8. Tools left in the scratch folder (`scratchpad\ser1-tools`, not in the copy)
`build.ps1` (the one background job: configure, the main tree's 46 generated headers copied in and stamped, the six
new passes with `Build-PtShaders.ps1 -Throttle 3`, the DLL), `same-source.ps1` (the passes that ship get the same
text), `compare-shaders.ps1`, `changed.ps1`, `eol.ps1`, `frontend-check.ps1`, `syntax-check.ps1`, `ir_stats.py`,
`ir_live.py`, `ir_alloca_uses.py` (the counts of section 2), `hitobject_probe.hlsl` (what DXC 1.9.2607 takes of hit
objects: all that was tried; `GetAttributes` takes an out argument). The whitepaper's text:
`scratchpad\ser1-docs\ser-whitepaper.txt`.
