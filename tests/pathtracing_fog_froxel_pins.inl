// SPDX-License-Identifier: MIT
// The fog check's cases (pathtracing_fog_froxel_checks.inl FogCase: name, lamp, candle, no sun, place, mist's cache, pin).
// A PIN is the five regions' means a case is held to where nothing is known exactly (all 0: printed, not judged; the
// open cases are judged against what is known exactly and carry none).
//   The room's cases and the outdoor ones with the light scattered once: THE PER-PIXEL ROUTE'S numbers (pt_fog.hlsl, the
//   last build that had it, 2026-10-12, each from an empty mist cache). A row is filled only from a run in which the
//   cells' fog and that route were within the check's 3 %; a row of 0 is printed, not judged. They pin the fog's
//   light to what one point a pixel converged to: a way of tracing the same light that shares little code with the
//   cells' walk.
//   FILLED 2026-10-12 11:20 (docs/agents/fog-3.md has both columns of each): the run of trial 5's build (the last
//   with both routes, %TEMP%\tgb\fogrule2-t5: native C05BB7040E17), in which the cells read x0.995 to x1.006 of the
//   per-pixel route in every region of these cases, and x1.013 to x1.016 with the lamp 0.02 m ahead.
//   The outdoor cases with the mist's cache: THE CELLS' OWN numbers. The two routes fed the mist's cache at different
//   points, so the per-pixel route is no yardstick for these; nothing else is. These rows only hold the light where
//   it was on the day they were taken: a change of them is a change of the mist's light, to be explained.
//   FILLED 2026-10-12 11:20 from fog-3's build (the coordinator's run of 09:37, main 18784FDDA14D, read the same six
//   digits; the per-pixel route read them within 1.1 % in trial 5's run).
    {"no lamp near the camera", 0.f, false, false, 0, false, {0.02895, 0.04366, 0.03665, 0.04376, 0.06755019}},
    {"a lamp 2 m ahead", 2.f, false, false, 0, false, {0.06184, 0.11431, 0.25570, 0.15831, 0.10653602}},
    {"a lamp 0.3 m ahead", 0.3f, false, false, 0, false, {0.25148, 0.43289, 0.79449, 0.55943, 0.26463578}},
    {"a lamp 0.02 m ahead", 0.02f, false, false, 0, false, {0.51215, 0.55113, 0.56195, 0.55037, 0.57728868}},
    {"a candle-like emitting quad 4 m ahead", 0.f, true, false, 0, false, {0.02895, 1.02160, 0.03656, 5.05183, 0.18096623}},
    {"no sun in the frame, a lamp 2 m ahead", 2.f, false, true, 0, false, {0.06076, 0.11407, 0.25589, 0.15812, 0.10625012}},
    {"no sun in the frame, the candle-like emitting quad", 0.f, true, true, 0, false, {0.02788, 1.02121, 0.03629, 5.05153, 0.18061670}},
    {"open: a uniform fog, sun and sky, the light scattered once", 0.f, false, false, 1, false, {0, 0, 0, 0, 0}},
    {"open: no sun, no sky, a lamp 6 m ahead", 6.f, false, true, 5, false, {0, 0, 0, 0, 0}},
    {"open: no sun, no sky, a lamp 60 m ahead", 60.f, false, true, 5, false, {0, 0, 0, 0, 0}},
    {"open, no ground: the mist's cache", 0.f, false, false, 2, true, {0, 0, 0, 0, 0}},
    {"outdoors, the fog scene's fog, the light scattered once", 0.f, false, false, 3, false, {0.06610, 0.05018, 0.14353, 0.07206, 0.07848232}},
    {"outdoors, the fog scene's fog, the mist's cache", 0.f, false, false, 3, true, {0.14981, 0.12576, 0.16390, 0.07628, 0.12120797}},
    {"outdoors, the vista's fog, the light scattered once", 0.f, false, false, 4, false, {0.15563, 0.09970, 0.36088, 0.13235, 0.17684160}},
    {"outdoors, the vista's fog, the mist's cache", 0.f, false, false, 4, true, {0.17049, 0.13436, 0.36318, 0.13361, 0.18746442}},
//   The night's fog (place 6; docs/agents/fog-3.md): judged against what is known exactly, no pin.
    {"outdoors, the night's fog: its moon alone, the light scattered once", 0.f, false, false, 6, false, {0, 0, 0, 0, 0}},
    {"outdoors, the night's fog: its sky alone, the light scattered once", 0.f, false, true, 6, false, {0, 0, 0, 0, 0}},
