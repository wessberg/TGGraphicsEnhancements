// SPDX-License-Identifier: MIT
// What a run is asked to do (BepInEx/TGPathTracing/Harness/request.json, written by tools/harness/Run-PtHarness.ps1 from
// tests/ingame/scenarios.json) and what it reports back, read and written with the game's Json.NET: public fields
// and arrays. A field the request leaves out keeps the value it has here.
using System;
namespace TGPathTracing.Harness
{
    // One config entry of a BepInEx plugin, set as its config file would hold it. Plugin and section may be left out:
    // TG Path Tracing, and the first section that has the key.
    [Serializable]
    internal sealed class Pair
    {
        public string plugin = "";
        public string section = "";
        public string key = "";
        public string value = "";
    }

    // One state of the renderer to measure in a scenario: the settings, then how long to let it settle, then what to
    // take (frames, the state of HDRP, the frame rate).
    [Serializable]
    internal sealed class Step
    {
        // null leaves existing PT scenarios unchanged; true/false checks real NR evaluations during capture.
        public bool? neural = null;
        public bool neuralManager = false; // Inspect the optional manager after the gameplay/performance capture.
        public bool neuralLifecycle = false; // Reversible upscaler/resize/cut and native menu audit, after capture.
        public bool neuralHdr = false; // Raw final HDR output through a temporary ReShade capture add-on.
        public bool neuralReplay = false, neuralReplayReset = false; // Retain NR inputs after motion stops; optional one-time history reset.
        public bool neuralReplayResetEveryFrame = false; // Determinism diagnostic; never a player setting.
        public float neuralInputScale = 1; // Diagnostic HDR conditioning, inverted on output; never a player setting.
        public int neuralToneMap = -1; // -1: unchanged; 0/1: peak/per-channel comparison with matched history reset.
        public bool neuralPostToneMap = false; // Compare actual graded SDR colour with unchanged DLSS guides. Harness only.
        public bool neuralStages = false; // Synchronous NR input/output/exposure capture; invalidates performance timings.
        public string name = "";
        public Pair[] set = new Pair[0];
        public string upscaler = ""; // TG Better Upscaling's choice for this step (UpscalerStep); "end" puts the player's back.
        // Path tracing goes on and off this many times before the settings above are applied, cycleSeconds each way: what
        // a player does with the row or the hotkey, before the state after it is measured.
        public int cycles = 0;
        public float cycleSeconds = 2f;
        // The step expects the traced light on screen (or expects it gone): the runner waits for that, at most this long.
        public bool traced = false;
        public float waitTraced = 45f;
        // Real seconds and rendered frames to wait after that before anything is measured.
        public float settle = 4f;
        public int settleFrames = 90;
        // Frames to save, every stride-th one (1: consecutive frames), at 1/scale of the screen.
        public int frames = 8;
        public int stride = 1;
        public int scale = 1;
        // The state of HDRP's lights, volumes, camera and materials, written out for comparison (Audit.cs).
        public bool audit = false;
        // Loaded-world effects/fog census and three real-time-spaced freeze probes (EffectsInventory.cs).
        public bool effects = false;
        public bool missing = false; // PT-83: census within the scenario's missingBoxes, no rendering changes.
        // Metadata diagnostic for dispatch/draw buffer links, live view turns, and the actual trace boundary.
        public bool vfxCapture = false;
        public bool vfxSimulation = false; // Compute-buffer layout diagnostic; not decoded particles.
        // The world runs from this step on (a frozen world stands so still that a denoiser has it easy: what boils in play
        // shows with the world running, the camera's idle sway and the flames' flicker included). Such steps come last in
        // a scenario: the world cannot be frozen at the same instant again.
        public bool live = false;
        // Seconds of frame times to measure, with the world running (the step is live); 0 measures none.
        public float perfSeconds = 0f;
        // PT-109 (megageo-5): those seconds also one row a second (PerfSecond: the frames' mean and worst, the tracer's stage
        // times with its structures by kind, what it still waits for, its memory, where the camera stood). A walk
        // (walkForward) always has them: its stutter is when something new comes into reach.
        public bool perSecond = false;
        // PT-46's mirror (Mirror.cs), before the settings above: "stand" puts the scenario's mirror up (once) with the camera
        // where the scenario put it; "behind" takes it away and stands the camera at its mirror image (the reference: the
        // game's own picture of what a perfect mirror shows); "away" removes it. "" leaves it as it is.
        public string mirror = "";
        // PT-14's lantern (Lantern.cs), before the settings above: "open" stands the scenario's lantern over the water (once),
        // a light with its glowing glass; "shade" puts its metal shade on, so no light of its own reaches what is above it;
        // "away" removes it. "" leaves it as it is.
        public string lantern = "";
        // A light of the game's at another strength for the step: every enabled Light within 15 m of
        // the camera whose path holds lightName takes lightCandela (Light.intensity: candela for
        // HDRP's point lights); 0 puts it back as the game has it. All are put back when the
        // scenario ends; nothing is saved.
        public string lightName = "";
        public float lightCandela = 0f;
        // PT-53 (CameraSlide.cs): metres the camera swings to either side, as a player strafing, from the settings on to
        // the step's end, once every slidePeriod seconds. 0: it stands. Frames taken meanwhile show what moves with it.
        public float slide = 0f;
        public float slidePeriod = 3f;
        public float turn = 0f;
        // NR face comparisons: move the actual camera closer, then restore its pose/FOV after the step.
        public float cameraForward = 0f;
        public float cameraFov = 0f;
        public float walkForward = 0f; // Metres forward during motionFrames, then hold there; no loop or warm-up walk.
        public float walkSpeed = 0f; // Optional metres per real second. Such takes do not share per-frame poses.
        public int motionFrames = 0;
        public int motionHoldFrames = 0; // Hold the starting pose after each moving cycle, to see history settle.
        public bool disableMotionBlur = false; // Diagnostic comparison only; the game's saved setting stays intact.
        public bool disableLensEffects = false; // Suppress lens/bloom/grain passes identically in colour-contract comparisons.
        // Metres: write textures.txt, what copies of the textures within this reach would take in video memory (PT-45).
        public float textures = 0f;
        // PT-45: this many streamed textures, loaded to their top mip and dropped to their smallest few, after the frames:
        // whether Unity makes a new D3D11 texture each time (mipstreaming.txt, MipStreaming.cs). 0 runs none.
        public int mipStreaming = 0;
    }

    [Serializable]
    internal sealed class Scenario
    {
        public string key = "";
        // A save's ID or file name ("Slot_83"), or the name the player gave it in the game's save menu ("PT tavern").
        public string save = "";
        // The save to load while the one above does not exist yet (an older save of the same place).
        public string saveFallback = "";
        // Seconds of game time to let the world run after the load before it is frozen, so every run freezes the same
        // moment of every animation.
        public float settleAfterLoad = 2f;
        // The time of day to set after the load; -1 keeps the save's.
        public int hour = -1;
        public int minute = 0;
        // How far the camera looks down (positive) or up (negative), in degrees. A save keeps where the hero stands and
        // which way they face, not this: every load comes back looking level. -1000 leaves it level.
        public float pitch = -1000f;
        // Degrees added to the facing the save came back with (0: as saved).
        public float yawOffset = 0f;
        // The save's own time when pitch and yawOffset were measured for it (tools/harness/calibrate.py matches the
        // save's picture): they are applied only to that very save, so a save made anew is not tilted by old numbers.
        public string cameraSaved = "";
        // The world stands still (time scale 0) from then on, so every step renders the same world: what differs between
        // two steps' frames is the renderer's doing.
        public bool freeze = true;
        // The mirror of steps with "mirror": metres ahead along the view, width, height, and degrees turned round the
        // camera's up (0: it faces the camera and shows what is behind it).
        public float[] mirror = new float[] { 1.6f, 1.2f, 0.9f, 0f };
        // The lantern of steps with "lantern": the point of the screen (across, down, as shares from its top left) whose
        // water it hangs over, and metres above that water.
        public float[] lantern = new float[] { 0.5f, 0.6f, 0.35f };
        public Newtonsoft.Json.Linq.JArray missingBoxes = new Newtonsoft.Json.Linq.JArray();
        public Step[] steps = new Step[0];
    }

    [Serializable]
    internal sealed class Request
    {
        public string run = "";
        public string outDir = "";
        // "inventory": list the saves and stop. "scenarios": that, then the scenarios. "calibrate": that, then each
        // scenario's save loaded and the camera swept up and down (and a little to the sides), a small frame each, for
        // tools/harness/calibrate.py to find the view the save was made with.
        public string mode = "scenarios";
        public bool quit = true;
        public bool uncap = true;
        public bool hideHud = true;
        public bool noGrain = true;
        public bool thumbnails = true;
        public float startTimeout = 420f;
        public float loadTimeout = 420f;
        // The calibration's sweep: the camera's look down (positive) or up, and degrees to either side of the save's facing.
        public float[] sweepPitch = new float[] { -40f, -30f, -20f, -10f, 0f, 10f, 20f, 30f, 40f, 50f, 60f, 70f };
        // The facing is kept by a save (measured 2026-10-06: within 2 degrees wherever the scene has clear shapes), so the
        // sweep leaves it alone; give several values here to sweep it too.
        public float[] sweepYaw = new float[] { 0f };
        public Scenario[] scenarios = new Scenario[0];
    }

    [Serializable]
    internal sealed class SaveInfo
    {
        public string id = "", file = "", name = "", displayName = "", location = "", scene = "", additiveScene = "", hero = "", saved = "", playTime = "";
        public int heroLevel;
        public bool customName, quickSave, autoSave, canLoad;
        public int thumbnailBytes;
    }
    [Serializable]
    internal sealed class SaveList
    {
        public string run = "";
        public SaveInfo[] saves = new SaveInfo[0];
    }

    [Serializable]
    internal sealed class FrameInfo
    {
        public float cameraMotionTime;
        public string file = "";
        public int frame;             // Unity's frame count: consecutive numbers are consecutive frames
        public float realtime;
        public float scaledTime, windSpeed, windDirection, jitterX, jitterY;
        public long ptTraced, ptComposited;
        public float px, py, pz, rx, ry, rz, fov;
        public long neuralEvaluated = -1, neuralSkipped = -1, neuralFailed = -1, neuralResets = -1, neuralFrame = -1;
        public bool? motionBlurActive;
        public bool? neuralBeforeLensEffects;
        public bool? neuralPostToneMap, lensEffectsSuppressed;
        // Cached FG runtime counters, without querying or resetting its presentation counters.
        public System.Collections.Generic.Dictionary<string, object> frameGeneration;
        public System.Collections.Generic.Dictionary<string, object> upscaling;
        public System.Collections.Generic.Dictionary<string, object> bridgePerformance;
    }
    [Serializable]
    internal sealed class PerfInfo
    {
        public bool measured;
        public int frames;
        public float seconds;
        public float fps, frameMs, medianMs, p95Ms, p99Ms, worstMs, lowFps;   // lowFps: 1000 / p99Ms, the "1 % low"
        public float tracerGpuMs, tracerRenderThreadMs, tracerD3d11Ms;        // TG Path Tracing's own timers, averaged
        public float unityCpuMs, unityGpuMs;                                  // Unity's FrameTimingManager, 0 where it reports none
        public string stages = "";
        public int vSyncCount, targetFrameRate;
        public float timeScale;
        public int over50, over100;                                           // frames that took longer than 50 and 100 ms (the stutter)
        public PerfSecond[] perSecond = new PerfSecond[0];                    // Step.perSecond (and every walk): a row a second
    }
    // PT-109 (megageo-5): one second of a measured step (Step.perSecond). The tracer's numbers are its own, read once a second:
    // its stage times over about the last second (TGPT_GetStageTimes, with the scene stage's structures by kind), its GPU time
    // averaged over the second's samples, what the traced world still waits for (TGPT_Status), its memory (TGPT_Diagnostics).
    [Serializable]
    internal sealed class PerfSecond
    {
        public float at;                                   // seconds into the measurement
        public int frames, over50, over100;
        public float frameMs, worstMs, tracerGpuMs;
        public string stages = "";
        public int instancesTraced, instancesWaiting, plantsWaiting, meshesQueued, micromapsWaiting, tilesToBake;
        public float accelerationMb, plantsMb, totalMb;
        public float x, y, z;                              // the camera
    }
    [Serializable]
    internal sealed class StepResult
    {
        public long neuralEvaluations;
        public double neuralGpuMs;
        public long neuralTransferBytes;
        public string neuralReason = "";
        public string neuralPlacement = "";
        public int neuralWidth, neuralHeight;
        public string name = "", error = "";
        public string[] applied = new string[0];      // "key: before -> after", or why a setting did not take
        public bool expectedTraced, traced, rayReconstruction, nrd, rayRegeneration;
        public float waitedSeconds;
        // PT-98, the traced world's arrival (TG Path Tracing's Arrival, where the installed build has it): whether it was
        // whole when the pictures were taken, the things still to come then, how long the wait for it lasted in this
        // step, and how long the last arrival took by the plugin's own clock (negative: none ended yet, or an older build).
        public bool arrivalKnown, worldWhole;
        public int toCome = -1;
        public float waitedForWorld, arrivalSeconds = -1f;
        public string status = "", exposure = "", scene = "", characters = "", lightsText = "";
        public float tracedGain, gpuMs, renderThreadMs;
        public int renderWidth, renderHeight, meshesQueued, lastError;
        public long framesTraced, framesFailed, composited;
        public int width, height, scale;
        public string format = "";
        public bool frozen, cameraMoved;
        public bool menuOpen;         // a menu, a dialogue or a loading screen was up when the frames were taken: not the scene
        public int retries;
        public float timeScale;
        public string weatherTime = "";
        public FrameInfo[] frames = new FrameInfo[0];
        public string[] audits = new string[0];
        public PerfInfo perf = new PerfInfo();
        public int savesBlocked;
    }
    [Serializable]
    internal sealed class ScenarioResult
    {
        public string key = "", save = "", error = "";
        public bool standIn;          // the scenario's own save does not exist yet: its stand-in was loaded
        public string slot = "", slotFile = "", slotName = "", slotSaved = "", location = "", scenes = "";
        public SweepShot[] sweep = new SweepShot[0];
        public float loadSeconds;
        public string weatherTime = "";
        public float px, py, pz, rx, ry, rz, fov;
        public bool interior;
        // The mirror's corners on the screen (x, y as shares from the top left: bottom left, top left, top right, bottom
        // right), as the camera saw them when it was put up, and what was done.
        public float[] mirrorCorners = new float[0];
        public string mirrorText = "";
        // The lantern: where it stands and over which water, as it was last set.
        public string lanternText = "";
        public StepResult[] steps = new StepResult[0];
    }
    // One small frame of the calibration's sweep: the view asked for, and the one the camera then had.
    [Serializable]
    internal sealed class SweepShot
    {
        public string file = "";
        public float pitch, yawOffset;
        public float rx, ry;
        public int width, height;
        public string format = "";
    }
    [Serializable]
    internal sealed class PluginEntry { public string id = "", name = "", version = ""; }
    [Serializable]
    internal sealed class Environment
    {
        public string run = "", started = "", game = "", unity = "", gpu = "", gpuDriver = "", graphicsApi = "";
        public int screenWidth, screenHeight, refreshRate;
        public bool hdrOutput, pathTracingInstalled;
        public int vSyncCountBefore, targetFrameRateBefore;
        public PluginEntry[] plugins = new PluginEntry[0];
        public string[] pathTracingConfig = new string[0];
    }
    [Serializable]
    internal sealed class Done
    {
        public string run = "", finished = "", error = "";
        public bool ok;
        public int scenarios, failed, savesBlocked;
        public float seconds;
    }
}
