// SPDX-License-Identifier: MIT
// A run: wait for the title screen, list the saves, then for each scenario load its save, let the world settle,
// freeze it, and take each step's frames, state and frame times. Everything lands in the request's folder, which
// tools/harness/analyze.py reads; progress.log there says where a run is. The game is left as it was found: its
// settings and TG Path Tracing's are put back, nothing was saved, and the game quits when the request says so.
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Awaken.TG.Main.Saving.SaveSlots;
using BepInEx.Bootstrap;
using BepInEx.Configuration;
using BepInEx.Logging;
using HarmonyLib;
using Newtonsoft.Json;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
namespace TGPathTracing.Harness
{
    internal static class Runner
    {
        private static Request request;
        private static ManualLogSource log;
        private static string outDir = "";
        private static Harmony harmony;
        private static float startedAt;
        // The world stands still while this is set (HarnessBehaviour keeps the time scale at 0).
        internal static bool HoldTime;
        private static float timeScaleBefore = 1f;
        private static int vSyncBefore, targetBefore;
        private static bool uncapped;
        private static GameObject grainHost;
        private static List<KeyValuePair<ConfigDefinition, string>> configBefore;
        private static List<KeyValuePair<ConfigDefinition, string>> neuralBefore;
        private static int failed, ran;

        internal static void Start(MonoBehaviour host, Request r, ManualLogSource logger)
        {
            request = r; log = logger; outDir = r.outDir; startedAt = Time.realtimeSinceStartup;
            Directory.CreateDirectory(outDir);
            // No sound during a run (Sound.cs: FMOD's master and Unity's volume, kept every frame from here on).
            Try("Muting the game", () => Sound.Mute(true));
            harmony = new Harmony(HarnessPlugin.Id);
            host.StartCoroutine(Run());
        }
        internal static void Tick()
        {
            if (HoldTime && Time.timeScale != 0f) Time.timeScale = 0f;
            Sound.Keep();
        }
        // What HarnessBehaviour.OnGUI shows at the bottom left; empty while a step's pictures are taken.
        internal static string Banner = "";
        internal static void Note(string text)
        {
            try { if (log != null) log.LogInfo(text); } catch { }
            try { File.AppendAllText(Path.Combine(outDir, "progress.log"), DateTime.Now.ToString("HH:mm:ss.fff") + " " + text + "\n"); } catch { }
        }
        private static bool Try(string what, Action action)
        {
            try { action(); return true; }
            catch (Exception ex) { Note(what + " failed: " + ex.GetType().Name + ": " + ex.Message); return false; }
        }
        private static float Now => Time.realtimeSinceStartup;
        private static float Clean(double value) => double.IsNaN(value) || double.IsInfinity(value) ? 0f : (float)value;
        private static void WriteJson(string path, object value)
        {
            try { File.WriteAllText(path, JsonConvert.SerializeObject(value, Formatting.Indented), new UTF8Encoding(false)); }
            catch (Exception ex) { Note("Could not write " + path + ": " + ex.Message); }
        }

        private static IEnumerator Run()
        {
            Note("Run " + request.run + " (" + request.mode + ", " + request.scenarios.Length + " scenarios) waits for the title screen.");
            Application.runInBackground = true;
            float deadline = Now + request.startTimeout, nextSaid = Now + 45f;
            bool ready = false;
            while (!ready)
            {
                yield return new WaitForSecondsRealtime(0.5f);
                if (Now > deadline) { Finish("the title screen was not reached within " + request.startTimeout.ToString("0") + " s"); yield break; }
                try { if (Game.LeavePressStart()) Note("Left the game's \"press any button\" screen."); } catch (Exception ex) { Note("The \"press any button\" screen could not be left: " + ex.Message); }
                try { ready = Game.TitleReady(); } catch { ready = false; }
                if (!ready && Now > nextSaid)
                {
                    nextSaid = Now + 45f;
                    string blocking = "unknown";
                    try { blocking = Game.Blocking(); } catch { }
                    Note("Still no title screen menu after " + (Now - startedAt).ToString("0") + " s (showing: " + blocking + ").");
                }
            }
            Note("The title screen is up, " + (Now - startedAt).ToString("0") + " s after the plugin loaded.");
            yield return new WaitForSecondsRealtime(3f);
            Try("Stopping saves", () => Game.BlockSaves(harmony, Note));
            // PT-97: what is typed, clicked or pressed on a gamepad while the run lasts does not reach the game.
            Try("Keeping real input out of the game", KeepInputOut);
            Try("The environment report", WriteEnvironment);
            // The saves come from the disk or Steam's cloud: give them a moment to be all there.
            int known = -1;
            for (int i = 0; i < 40; i++)
            {
                int count = 0;
                try { count = Game.Slots().Length; } catch { }
                if (count > 0 && count == known) break;
                known = count;
                yield return new WaitForSecondsRealtime(0.5f);
            }
            Try("The list of saves", WriteSaves);
            if (request.uncap) Try("Lifting the frame cap", Uncap);
            if (request.noGrain) Try("Turning film grain and chromatic aberration off", NoGrain);
            Try("Reading TG Path Tracing's settings", () => configBefore = Traced.Snapshot(null));
            Try("Reading TG Neural Rendering's settings", () => neuralBefore = Traced.Snapshot(Neural.PluginId));
            if (request.mode == "scenarios" || request.mode == "calibrate")
            {
                foreach (var scenario in request.scenarios)
                {
                    var result = new ScenarioResult { key = scenario.key, save = scenario.save };
                    string dir = Path.Combine(outDir, Game.Safe(scenario.key));
                    Directory.CreateDirectory(dir);
                    yield return RunScenario(scenario, result, dir);
                    ++ran;
                    if (result.error.Length > 0) { ++failed; Note("Scenario " + scenario.key + " failed: " + result.error); }
                    WriteJson(Path.Combine(dir, "scenario.json"), result);
                }
            }
            Finish("");
        }

        private static void Finish(string error)
        {
            MissingInventory.Disarm();
            Try("Removing the mirror", Mirror.Remove);
            Try("Removing the lantern", Lantern.Remove);
            Try("Putting the lights' strengths back", RestoreLights);
            Try("Stopping the camera's slide", CameraSlide.Stop);
            Try("Restoring NR lifecycle controls", NeuralLifecycle.Restore);
            Try("Stopping NR stage diagnostics", () => Neural.StageDiagnostics(null));
            NeuralMotion.SuppressBlur = false;
            NeuralMotion.SuppressLensEffects = false;
            HoldTime = false;
            Try("Restoring the time scale", () => { if (Time.timeScale == 0f) Time.timeScale = timeScaleBefore > 0f ? timeScaleBefore : 1f; });
            Try("Restoring the sound", () => Sound.Mute(false));
            Try("Showing the HUD", () => Game.HideHud(false));
            Try("Restoring TG Path Tracing's settings", () => Traced.Restore(null, configBefore));
            Try("Restoring TG Neural Rendering's settings", () => Traced.Restore(Neural.PluginId, neuralBefore));
            if (uncapped) Try("Restoring the frame cap", () => { QualitySettings.vSyncCount = vSyncBefore; Application.targetFrameRate = targetBefore; });
            if (grainHost != null) Try("Removing the film grain override", () => UnityEngine.Object.Destroy(grainHost));
            Try("Giving real input back", () => { string line = RealInput.Keep(false); if (line != null) Note(line); });
            var done = new Done
            {
                run = request.run, finished = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss"), error = error, ok = error.Length == 0 && failed == 0,
                scenarios = ran, failed = failed, savesBlocked = Game.SavesBlocked, seconds = Now - startedAt
            };
            Note(error.Length > 0 ? "The run stopped: " + error : "The run is done: " + ran + " scenarios, " + failed + " failed, " + Game.SavesBlocked + " saves stopped.");
            WriteJson(Path.Combine(outDir, "done.json"), done);
            Banner = "";
            if (request.quit) { Note("Quitting the game."); Application.Quit(); }
        }

        // Again before every step: a device that came later (a gamepad switched on, the save's own input set-up) is caught.
        private static void KeepInputOut() { string line = RealInput.Keep(true); if (line != null) Note(line); }

        private static void WriteEnvironment()
        {
            var env = new Environment
            {
                run = request.run, started = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss"), game = Application.version, unity = Application.unityVersion,
                gpu = SystemInfo.graphicsDeviceName, gpuDriver = SystemInfo.graphicsDeviceVersion, graphicsApi = SystemInfo.graphicsDeviceType.ToString(),
                screenWidth = Screen.width, screenHeight = Screen.height, refreshRate = (int)Math.Round(Screen.currentResolution.refreshRateRatio.value),
                hdrOutput = HDROutputSettings.main != null && HDROutputSettings.main.active, pathTracingInstalled = Traced.Installed,
                vSyncCountBefore = QualitySettings.vSyncCount, targetFrameRateBefore = Application.targetFrameRate
            };
            var plugins = new List<PluginEntry>();
            foreach (var pair in Chainloader.PluginInfos)
                if (pair.Value != null && pair.Value.Metadata != null)
                    plugins.Add(new PluginEntry { id = pair.Key, name = pair.Value.Metadata.Name, version = pair.Value.Metadata.Version.ToString() });
            plugins.Sort((a, b) => string.CompareOrdinal(a.id, b.id));
            env.plugins = plugins.ToArray();
            var config = new List<string>();
            foreach (var pair in Traced.Snapshot(null)) config.Add(pair.Key.Section + "/" + pair.Key.Key + "=" + pair.Value);
            env.pathTracingConfig = config.ToArray();
            WriteJson(Path.Combine(outDir, "environment.json"), env);
        }

        private static void WriteSaves()
        {
            var list = new List<SaveInfo>();
            string folder = Path.Combine(outDir, "saves");
            foreach (var slot in Game.Slots())
            {
                if (slot == null) continue;
                try
                {
                    list.Add(Game.Describe(slot));
                    if (request.thumbnails) Game.WriteThumbnail(slot, folder);
                }
                catch (Exception ex) { Note("Save " + slot.ID + " could not be described: " + ex.Message); }
            }
            list.Sort((a, b) => string.CompareOrdinal(b.saved, a.saved));
            WriteJson(Path.Combine(outDir, "saves.json"), new SaveList { run = request.run, saves = list.ToArray() });
            Note("The game has " + list.Count + " saves (saves.json).");
        }

        // The frame rate is measured without the game's cap or VSync; both come back when the run ends.
        private static void Uncap()
        {
            vSyncBefore = QualitySettings.vSyncCount; targetBefore = Application.targetFrameRate;
            QualitySettings.vSyncCount = 0; Application.targetFrameRate = -1;
            uncapped = true;
        }
        // Film grain is new noise every frame, in the game's image and the traced one alike: it would hide what the
        // frames are compared for. A volume of the runner's own sets its intensity to 0 while the run lasts (only that
        // one parameter: Add without overrides, as HDRP's other parameters must stay the game's).
        private static void NoGrain()
        {
            grainHost = new GameObject("TG Path Tracing Harness volume") { hideFlags = HideFlags.HideAndDontSave };
            UnityEngine.Object.DontDestroyOnLoad(grainHost);
            var volume = grainHost.AddComponent<Volume>();
            volume.isGlobal = true; volume.priority = 100000f; volume.weight = 1f;
            var profile = ScriptableObject.CreateInstance<VolumeProfile>();
            profile.hideFlags = HideFlags.HideAndDontSave;
            volume.sharedProfile = profile;
            var grain = profile.Add<FilmGrain>();
            grain.intensity.Override(0f);
            // Chromatic aberration splits colors toward the screen's edges, in both pictures: off too, so two pictures are
            // easier to compare (the owner, 2026-10-06).
            var aberration = profile.Add<ChromaticAberration>();
            aberration.intensity.Override(0f);
        }

        private static IEnumerator RunScenario(Scenario s, ScenarioResult result, string dir)
        {
            Note("Scenario " + s.key + ": save \"" + s.save + "\".");
            Banner = s.key + ": loading the save";
            HoldTime = false;
            if (Time.timeScale == 0f) Time.timeScale = timeScaleBefore > 0f ? timeScaleBefore : 1f;
            Try("Showing the HUD", () => Game.HideHud(false));
            SaveSlot slot = null; string detail = ""; bool can = false;
            for (float waited = 0f; waited < 60f && !can; waited += 0.5f)
            {
                try
                {
                    slot = Game.Find(s.save, out detail);
                    if (slot == null && s.saveFallback.Length > 0)
                    {
                        slot = Game.Find(s.saveFallback, out string fallbackDetail);
                        if (slot != null) detail = "\"" + s.save + "\" does not exist yet: its stand-in " + s.saveFallback + ", " + fallbackDetail;
                    }
                    can = slot != null && Game.CanLoad(slot);
                }
                catch (Exception ex) { detail = ex.Message; }
                if (slot == null && waited >= 10f) break;
                if (!can) yield return new WaitForSecondsRealtime(0.5f);
            }
            if (slot == null) { result.error = "save \"" + s.save + "\" was not found (" + detail + ")"; yield break; }
            if (!can) { result.error = "the game does not let " + slot.ID + " load now"; yield break; }
            try
            {
                var info = Game.Describe(slot);
                result.slot = info.id; result.slotFile = info.file; result.slotName = info.displayName; result.location = info.location;
                result.slotSaved = info.saved;
                result.standIn = detail.Contains("stand-in");
            }
            catch { }
            Note("  loading " + result.slot + " \"" + result.slotName + "\" (" + result.location + "; found " + detail + ").");
            float t0 = Now;
            string loadError = "";
            try { Game.Load(slot); } catch (Exception ex) { loadError = ex.Message; }
            if (loadError.Length > 0) { result.error = "the load could not start: " + loadError; yield break; }
            yield return new WaitForSecondsRealtime(1.5f);
            // Half a second of play, not more: people walk off (a guard framed up close was gone after two seconds).
            float limit = Now + request.loadTimeout, steady = 0f;
            while (steady < 0.5f)
            {
                if (Now > limit) { result.error = "the load did not finish within " + request.loadTimeout.ToString("0") + " s"; yield break; }
                bool play = false;
                try { play = Game.InPlay(); } catch { }
                steady = play ? steady + Time.unscaledDeltaTime : 0f;
                yield return null;
            }
            result.loadSeconds = Now - t0;
            Note("  loaded in " + result.loadSeconds.ToString("0.0") + " s.");
            Try("Stopping saves", () => Game.BlockSaves(harmony, Note));
            // Game time, not real time: every run freezes the animations at the same moment after the load.
            float settled = 0f; limit = Now + s.settleAfterLoad * 4f + 30f;
            while (settled < s.settleAfterLoad && Now < limit) { settled += Time.deltaTime; yield return null; }
            if (s.hour >= 0)
            {
                Try("Setting the time of day", () => Game.SetTimeOfDay(s.hour, s.minute));
                settled = 0f; limit = Now + 30f;
                while (settled < 3f && Now < limit) { settled += Time.deltaTime; yield return null; }
            }
            if (request.mode == "calibrate")
            {
                if (request.hideHud) Try("Hiding the HUD", () => Game.HideHud(true));
                yield return Sweep(dir, result);
                Try("Showing the HUD", () => Game.HideHud(false));
                yield break;
            }
            if (s.pitch > -999f || s.yawOffset != 0f)
            {
                // Measured for one save: a save made anew under the same name stands elsewhere.
                if (s.cameraSaved.Length > 0 && s.cameraSaved != result.slotSaved)
                    Note("  the camera's calibration is for a save of " + s.cameraSaved + ", this one is of " + result.slotSaved + ": the view stays as loaded (run -Calibrate).");
                else
                {
                    bool turned = false;
                    Try("Turning the camera", () =>
                    {
                        turned = s.yawOffset == 0f || Game.SetYaw(Game.Yaw() + s.yawOffset);
                        if (s.pitch > -999f) turned &= Game.SetPitch(s.pitch);
                    });
                    if (!turned) Note("  the camera could not be turned to the calibrated view.");
                    // A few frames with the world running: the game's own camera code applies it.
                    for (int i = 0; i < 10; i++) yield return null;
                }
            }
            if (request.hideHud) Try("Hiding the HUD", () => Game.HideHud(true));
            if (Array.Exists(s.steps, step => step.effects)) yield return EffectsInventory.BeforeFreeze(s, dir);
            // A load can leave gameplay again after the initial readiness window (NR-18).
            // Do not freeze that transition or grade its backdrop as the scene.
            if (!GameplayReady(out string freezeFailure))
            {
                result.error = "the game left gameplay before the scene could be frozen: " + freezeFailure;
                Try("Showing the HUD", () => Game.HideHud(false));
                yield break;
            }
            if (s.freeze) { timeScaleBefore = Time.timeScale > 0f ? Time.timeScale : 1f; HoldTime = true; Time.timeScale = 0f; }
            yield return null;
            yield return null;
            Try("Reading the scene", () =>
            {
                result.scenes = Game.Scenes(); result.weatherTime = Game.WeatherTime();
                var camera = Camera.main;
                if (camera != null)
                {
                    var p = camera.transform.position; var e = camera.transform.eulerAngles;
                    result.px = p.x; result.py = p.y; result.pz = p.z; result.rx = e.x; result.ry = e.y; result.rz = e.z; result.fov = camera.fieldOfView;
                }
                result.interior = Traced.Flag("SceneLights", "Interior");
            });
            Note("  at (" + result.px.ToString("0.0") + ", " + result.py.ToString("0.0") + ", " + result.pz.ToString("0.0") + ") in " + result.scenes + ", " + result.weatherTime + (s.freeze ? "; the world is frozen." : "; the world runs."));
            var steps = new List<StepResult>();
            foreach (var step in s.steps)
            {
                var r = new StepResult { name = step.name };
                steps.Add(r);
                string stepDir = Path.Combine(dir, Game.Safe(step.name));
                Directory.CreateDirectory(stepDir);
                if (step.missing) MissingInventory.Arm(r);
                yield return RunStep(s, step, stepDir, r, result);
                if (step.missing) yield return MissingInventory.Run(stepDir, s, step, r);
                if (step.effects) yield return EffectsInventory.Run(stepDir, s.key, r.traced, step.traced);
                if (step.vfxCapture) yield return VfxCapture.Run(stepDir, r, step.vfxSimulation);
                WriteJson(Path.Combine(stepDir, "step.json"), r);
                result.steps = steps.ToArray();
                if (r.error.Length > 0)
                {
                    Note("  step " + step.name + ": " + r.error);
                    result.error += " " + step.name + ": " + r.error;
                }
                if (r.menuOpen) break; // Later takes would compare the same invalid scene.
            }
            Try("Removing the mirror", Mirror.Remove);
            Try("Removing the lantern", Lantern.Remove);
            Try("Putting the lights' strengths back", RestoreLights);
            HoldTime = false;
            if (Time.timeScale == 0f) Time.timeScale = timeScaleBefore;
            Try("Showing the HUD", () => Game.HideHud(false));
        }

        // A step's "lightName" and "lightCandela" (Request.cs): the game's own lights of that name near the camera at another
        // strength, and back.
        private static readonly Dictionary<Light, float> lightsChanged = new Dictionary<Light, float>();
        private static void LightStep(Step step)
        {
            var camera = Camera.main; int count = 0; string first = "";
            foreach (var light in UnityEngine.Object.FindObjectsByType<Light>(FindObjectsSortMode.None))
            {
                if (light == null || !light.isActiveAndEnabled) continue;
                if (camera != null && (light.transform.position - camera.transform.position).sqrMagnitude > 15f * 15f) continue;
                string path = light.name;
                for (var t = light.transform.parent; t != null; t = t.parent) path = t.name + "/" + path;
                if (path.IndexOf(step.lightName, StringComparison.OrdinalIgnoreCase) < 0) continue;
                if (!lightsChanged.ContainsKey(light)) lightsChanged[light] = light.intensity;
                float was = lightsChanged[light];
                light.intensity = step.lightCandela > 0f ? step.lightCandela : was;
                if (count++ == 0) first = path + ", " + was.ToString("0") + " cd as the game has it";
            }
            Note("    lights named \"" + step.lightName + "\" within 15 m of the camera: " + count + (count > 0 ? " (" + first + "); now " +
                (step.lightCandela > 0f ? step.lightCandela.ToString("0") + " cd" : "as the game has them") : "") + ".");
        }
        private static void RestoreLights()
        {
            foreach (var pair in lightsChanged) if (pair.Key != null) pair.Key.intensity = pair.Value;
            lightsChanged.Clear();
        }

        // A step's "lantern" (Step.lantern): stood the first time a step names it, over the water the scenario points at.
        private static void LanternStep(Scenario s, string what, ScenarioResult result)
        {
            if (what == "away") { Lantern.Remove(); Note("    the lantern is gone."); return; }
            float[] l = s.lantern != null && s.lantern.Length >= 3 ? s.lantern : new float[] { 0.5f, 0.6f, 0.35f };
            result.lanternText = Lantern.Place(l[0], l[1], l[2], what == "shade");
            Note("    " + result.lanternText + ".");
        }

        // A step's "mirror" (Step.mirror): the mirror is put up the first time a step names it, with the camera where the
        // scenario put it.
        private static void MirrorStep(Scenario s, string what, ScenarioResult result)
        {
            if (what == "away") { Mirror.Remove(); Note("    the mirror is gone."); return; }
            if (!Mirror.Stands)
            {
                float[] m = s.mirror != null && s.mirror.Length >= 4 ? s.mirror : new float[] { 1.6f, 1.2f, 0.9f, 0f };
                result.mirrorCorners = Mirror.Place(m[0], m[1], m[2], m[3], out string detail);
                result.mirrorText = detail;
                Note("    " + detail + ".");
                if (!Mirror.Stands) return;
            }
            Mirror.View(what == "behind");
            Note(what == "behind" ? "    the mirror is away and the camera stands at its mirror image." : "    the mirror stands, the camera is back.");
        }

        // The calibration's sweep: a save keeps where the hero stands and which way they face, not the look up or down,
        // and its picture in the load menu shows the view it was made with. Small frames of the camera looking up and
        // down, and a little to either side, for tools/harness/calibrate.py to find that view among them.
        private static IEnumerator Sweep(string dir, ScenarioResult result)
        {
            string folder = Path.Combine(dir, "sweep");
            Directory.CreateDirectory(folder);
            float yaw0 = 0f;
            Try("Reading the facing", () => yaw0 = Game.Yaw());
            var shots = new List<SweepShot>();
            foreach (float yaw in request.sweepYaw)
            {
                foreach (float pitch in request.sweepPitch)
                {
                    Try("Turning the camera", () => { if (request.sweepYaw.Length > 1) Game.SetYaw(yaw0 + yaw); Game.SetPitch(pitch); });
                    for (int i = 0; i < 5; i++) yield return null;
                    yield return new WaitForEndOfFrame();
                    var shot = new SweepShot { pitch = pitch, yawOffset = yaw, file = "y" + yaw.ToString("+0;-0;+0") + "_p" + pitch.ToString("+0;-0;+0") + ".raw" };
                    if (!FrameGrab.Grab(Path.Combine(folder, shot.file), 8, out shot.width, out shot.height, out shot.format, out string error))
                    {
                        Note("  a sweep frame could not be saved: " + error);
                        continue;
                    }
                    var camera = Camera.main;
                    if (camera != null) { var e = camera.transform.eulerAngles; shot.rx = e.x; shot.ry = e.y; }
                    shots.Add(shot);
                }
            }
            Try("Turning the camera back", () => { Game.SetYaw(yaw0); Game.SetPitch(0f); });
            result.sweep = shots.ToArray();
            Note("  swept " + shots.Count + " views around facing " + yaw0.ToString("0") + " degrees.");
        }

        private static bool SetMode(string value)
        {
            try { return Traced.Set(new Pair { key = "Mode", value = value }, out _); } catch { return false; }
        }

        internal static bool GameplayReady(out string detail)
        {
            detail = "";
            try
            {
                if (Game.InPlay()) return true;
                detail = Game.PlayState();
            }
            catch (Exception ex) { detail = "readiness query failed: " + ex.Message; }
            return false;
        }

        private static IEnumerator RunStep(Scenario s, Step step, string dir, StepResult r, ScenarioResult result)
        {
            Try("Keeping real input out of the game", KeepInputOut);
            Note("  step " + step.name + ".");
            string settling = s.key + ", step " + step.name + ": settling, the picture does not count yet";
            Banner = settling;
            if (!GameplayReady(out string takeFailure))
            { r.menuOpen = true; r.error = "the game is outside gameplay before the take: " + takeFailure; yield break; }
            // (Also for a path tracing step that asks for it: frames grabbed while the camera moves are judged without
            // the game's motion blur, the owner, 2026-10-12. The hook clears HDRP's own flag for the frame; nothing of
            // Neural Rendering runs. A step that asks leaves `motionBlurActive` false in each of its frames.)
            if (step.neural.HasValue || step.disableMotionBlur || step.disableLensEffects)
            {
                if (!Try("Observing motion blur", () => NeuralMotion.Configure(harmony, step.disableMotionBlur, step.disableLensEffects)))
                    r.error += " Motion-blur diagnostic hook could not be installed.";
            }
            else { NeuralMotion.SuppressBlur = false; NeuralMotion.SuppressLensEffects = false; }
            // The game puts its own frame cap back now and then (its Target FPS after a load: 60 in the castle scene of
            // 2026-10-06, which the state check then read as "not back": -1 -> 60). The run stays uncapped at every step.
            if (uncapped && (QualitySettings.vSyncCount != 0 || Application.targetFrameRate != -1)) { QualitySettings.vSyncCount = 0; Application.targetFrameRate = -1; }
            float t0 = Now;
            for (int i = 0; i < step.cycles; i++)
            {
                SetMode("On");
                yield return new WaitForSecondsRealtime(step.cycleSeconds);
                SetMode("Off");
                yield return new WaitForSecondsRealtime(step.cycleSeconds);
            }
            if (!string.IsNullOrEmpty(step.mirror)) Try("The mirror", () => MirrorStep(s, step.mirror, result));
            if (!string.IsNullOrEmpty(step.lantern)) Try("The lantern", () => LanternStep(s, step.lantern, result));
            if (!string.IsNullOrEmpty(step.lightName)) Try("The light's strength", () => LightStep(step));
            var applied = new List<string>();
            foreach (var pair in step.set)
            {
                string detail; bool took = false;
                try { took = Traced.Set(pair, out detail); } catch (Exception ex) { detail = pair.key + ": " + ex.Message; }
                applied.Add((took ? "" : "NOT APPLIED: ") + detail);
                if (!took && step.neural.HasValue) r.error += "Setting not applied: " + detail + ". ";
            }
            if (!string.IsNullOrEmpty(step.upscaler))
            {
                string detail; bool took = false;
                try { took = UpscalerStep.Set(step.upscaler, out detail); } catch (Exception ex) { detail = "upscaler " + step.upscaler + ": " + ex.Message; }
                applied.Add((took ? "" : "NOT APPLIED: ") + detail);
                if (!took) r.error += detail + ". ";
            }
            r.applied = applied.ToArray(); r.expectedTraced = step.traced;
            // The traced light on screen with nothing left to register, or gone: the native side counts the frames it
            // composited, read by the plugin four times a second.
            // A new build's first start compiles its passes for minutes (run 20261008-015158: 215 s, and the first
            // scenario's traced steps took the game's picture): that wait is not the step's.
            float compiling = Now + 600f; bool waitedForPasses = false, pictured = false;
            while (step.traced && Traced.Status("Starting") != 0 && Now < compiling)
            {
                waitedForPasses = true;
                Banner = s.key + ", step " + step.name + ": the tracer's shader passes are being made (" + (Now - t0).ToString("0") + " s)";
                // What the player sees during that wait, once, 5 s in: the game's own screen with its notices on it
                // (PT-97: the owner saw a gray screen and no "compiling shaders" notice). Never a picture of the
                // desktop: the owner works at this PC while a run goes.
                if (!pictured && Now - t0 > 5f) { pictured = true; Try("The waiting screen's picture", () => ScreenCapture.CaptureScreenshot(Path.Combine(dir, "waiting-for-passes.png"))); }
                yield return new WaitForSecondsRealtime(0.5f);
            }
            if (waitedForPasses) Note("    waited " + (Now - t0).ToString("0") + " s for the tracer's passes to compile.");
            // A running step lets the world go only now, with the passes made (the owner, 2026-10-11: "the time of day has also
            // shifted to the middle of the night at the point of time where the PT shaders are compiled. That makes comparison
            // with the vanilla frames flawed"): until then it was let go first, and a new build's minutes of pass making ran
            // the game's clock from the afternoon into the night (run 20261011-223913: 16:35 to 21:38 in 381 s); the walk
            // had also ended before the first traced frame. What is left: a running step itself moves the clock 25 to 45
            // game minutes (five of them two hours). Setting the clock back at each step was tried and taken out the same
            // hour: the game's own setter rolls the date on a day (run 20261011-235717), and with it maybe the weather.
            if ((step.live || step.perfSeconds > 0f) && HoldTime)
            {
                HoldTime = false;
                if (Time.timeScale == 0f) Time.timeScale = timeScaleBefore;
                Note("    the world runs from here on.");
            }
            if (step.slide > 0f || step.turn > 0f || step.cameraForward != 0f || step.cameraFov > 0f || step.walkForward != 0f)
                Try("Positioning the camera", () => Note("    " + CameraSlide.Start(step.slide, step.slidePeriod, step.turn, step.cameraForward, step.cameraFov, step.walkForward, step.walkSpeed) + "."));
            Banner = settling;
            float limit = Now + (step.traced ? step.waitTraced : 12f), steady = 0f;
            double last = Traced.Status("Composited");
            float lastAt = Now, settleStart = Now;
            bool on = false;
            // A traced step's pictures are taken when the traced world is whole (PT-98): the plugin's own word for it
            // (Arrival.Whole: no mesh to register, build or place, no listed instance waiting for its mesh, no character
            // being read, no texture kept back, loading or to bake), where the installed build has it. The native
            // queue alone, which an older build leaves as the only sign, is empty between two waves of meshes and knows
            // nothing of instances, textures or characters: run 20261009-150909 took npc-day's pictures with nothing
            // traced on 62 % of the screen.
            bool arrival = Traced.Static("Arrival", "Whole") is bool;
            while (Now < limit)
            {
                yield return new WaitForSecondsRealtime(0.5f);
                double now = Traced.Status("Composited");
                on = now != last; last = now;
                bool whole = arrival ? Traced.Flag("Arrival", "Whole") : Traced.Status("MeshesQueued") == 0;
                bool settled = step.traced ? on && whole : !on;
                steady = settled ? steady + (Now - lastAt) : 0f;
                lastAt = Now;
                if (step.traced && !whole) Banner = settling + " (" + (arrival ? Traced.Text("Arrival", "NoticeText") : Traced.Status("MeshesQueued").ToString("0") + " meshes to build") + ")";
                else Banner = settling;
                if (steady >= 1.5f) break;
            }
            r.traced = on;
            if (step.traced)
            {
                r.arrivalKnown = arrival; r.waitedForWorld = Now - settleStart;
                r.worldWhole = arrival ? Traced.Flag("Arrival", "Whole") : Traced.Status("MeshesQueued") == 0;
                if (arrival)
                {
                    r.toCome = Convert.ToInt32(Traced.Static("Arrival", "ToCome") ?? -1);
                    r.arrivalSeconds = Convert.ToSingle(Traced.Static("Arrival", "LastSeconds") ?? -1f);
                }
                if (!r.worldWhole) Note("    THE TRACED WORLD WAS NOT WHOLE after " + r.waitedForWorld.ToString("0.0") + " s (the step's limit, waitTraced " + step.waitTraced.ToString("0") +
                    " s): " + (arrival ? Traced.Text("Arrival", "NoticeText") : "meshes still to build") + ". Its pictures are of a world still arriving.");
                else Note("    the traced world is whole" + (r.arrivalSeconds >= 0f ? " (its last arrival took " + r.arrivalSeconds.ToString("0.0") + " s by the plugin's clock)" : "") +
                    "; this step waited " + r.waitedForWorld.ToString("0.0") + " s for it" + (arrival ? "" : " (an older build: by the native queue alone)") + ".");
            }
            int frame0 = Time.frameCount; float until = Now + step.settle;
            while (Now < until || Time.frameCount - frame0 < step.settleFrames) yield return null;
            r.waitedSeconds = Now - t0;
            Try("Reading the tracer's state", () => FillState(r));
            if (step.textures > 0f) Try("Counting the textures", () => Note("    textures.txt: " + Mirror.WriteTextures(Path.Combine(dir, "textures.txt"), step.textures) + " textures within " + step.textures.ToString("0") + " m."));
            if (step.audit)
            {
                var audits = new List<string>();
                Try("The state audit", () => { int n = Audit.Write(Path.Combine(dir, "audit-a.txt")); audits.Add("audit-a.txt"); Note("    audit: " + n + " lines."); });
                // A second one a moment later tells what changes by itself (a flame's flicker) from what was changed.
                yield return new WaitForSecondsRealtime(0.7f);
                Try("The second state audit", () => { Audit.Write(Path.Combine(dir, "audit-b.txt")); audits.Add("audit-b.txt"); });
                r.audits = audits.ToArray();
            }
            long neuralStart = step.neural.HasValue ? Neural.Evaluations(out _) : 0;
            if ((step.neuralInputScale != 1 || step.neuralToneMap >= 0) && (!step.neuralStages || step.neural != true))
            { r.error += " NR input conditioning requires stage diagnostics and NR On."; yield break; }
            if (step.neuralPostToneMap && (!step.neuralStages || step.neural != true || !step.disableLensEffects))
            { r.error += " Tone-mapped NR comparison requires stage diagnostics, NR On and lens effects suppressed."; yield break; }
            if (step.neuralReplay && (!step.neuralStages || step.neural != true || step.motionFrames < 1))
            { r.error += " NR replay requires stage diagnostics, NR On and a moving/held sequence."; yield break; }
            if (step.neuralReplayResetEveryFrame && (!step.neuralReplay || !step.neuralReplayReset))
            { r.error += " Repeated NR reset requires retained inputs and replay reset."; yield break; }
            if (step.neuralStages && !Try("Starting NR stage diagnostics", () => Neural.StageDiagnostics(Path.Combine(dir, "nr-stages"), step.neuralInputScale, step.neuralToneMap, step.neuralPostToneMap)))
            { r.error += " NR stage diagnostics could not start."; yield break; }
            Banner = ""; // the pictures are taken now: nothing of the runner's on screen
            try { if (step.frames > 0) yield return step.neuralHdr ? NeuralHdrCapture.Burst(dir, step, r) : Burst(dir, step, r); }
            finally { if (step.neuralStages) Try("Stopping NR stage diagnostics", () => Neural.StageDiagnostics(null)); }
            Neural.Check(step, neuralStart, r);
            if (r.menuOpen)
            {
                if (CameraSlide.Sliding) Try("Stopping the camera's slide", CameraSlide.Stop);
                yield break;
            }
            if (step.mipStreaming > 0)
            {
                // After the frames: it leaves textures at other mips than the camera asked for until Unity takes them back.
                yield return MipStreaming.Run(Path.Combine(dir, "mipstreaming.txt"), step.mipStreaming);
                Note("    mipstreaming.txt: " + MipStreaming.Summary + ".");
            }
            if (step.perfSeconds > 0f)
            {
                // Frame times are measured with the world running, as it is played (the step set it running above).
                yield return new WaitForSecondsRealtime(0.5f);
                yield return Perf(step.perfSeconds, r.perf, step.perSecond || step.walkForward != 0f);
                Note("    " + r.perf.fps.ToString("0.0") + " fps (1 % low " + r.perf.lowFps.ToString("0.0") + "), tracer GPU " + r.perf.tracerGpuMs.ToString("0.00") + " ms.");
            }
            if (CameraSlide.Sliding && step.walkForward != 0f)
            {
                // Where the walk ended, and what the camera sees there: a step's own pictures are taken as its walk starts,
                // and its frame times are mostly of the walk's end (2026-10-12: "a camp at 3 frames a second" was a camera
                // that had flown into a hill; nobody had seen the place).
                Note("    " + CameraSlide.Where() + ".");
                Try("The walk's end's picture", () => ScreenCapture.CaptureScreenshot(Path.Combine(dir, "walk-end.png")));
                yield return null; yield return new WaitForEndOfFrame(); yield return null;
            }
            if (CameraSlide.Sliding) Try("Stopping the camera's slide", CameraSlide.Stop);
            if (step.neuralManager) yield return NeuralManagerAudit.Run(dir, r);
            if (step.neuralLifecycle) yield return NeuralLifecycle.Run(dir, r);
            r.savesBlocked = Game.SavesBlocked;
        }

        private static void FillState(StepResult r)
        {
            r.frozen = HoldTime; r.timeScale = Time.timeScale; r.weatherTime = Game.WeatherTime();
            if (!Traced.Installed) { r.status = "TG Path Tracing is not loaded"; return; }
            r.rayReconstruction = Traced.RayReconstructionActive; r.nrd = Traced.NrdActive; r.rayRegeneration = Traced.RayRegenerationActive;
            r.exposure = Traced.Text("HdrpPasses", "ExposureText"); r.tracedGain = Clean(Convert.ToDouble(Traced.Static("HdrpPasses", "TracedGain") ?? 0f));
            r.scene = Traced.Text("SceneCapture", "Summary"); r.characters = Traced.Text("CharacterCapture", "Summary");
            r.lightsText = Traced.Text("SceneLights", "Count") + " traced lights" + (Traced.Flag("SceneLights", "Interior") ? ", interior" : "");
            r.gpuMs = Clean(Traced.Status("GpuMs")); r.renderThreadMs = Clean(Traced.Status("FrameMs"));
            r.renderWidth = (int)Traced.Status("RenderWidth"); r.renderHeight = (int)Traced.Status("RenderHeight");
            r.meshesQueued = (int)Traced.Status("MeshesQueued"); r.lastError = (int)Traced.Status("LastError");
            r.framesTraced = (long)Traced.Status("FramesTraced"); r.framesFailed = (long)Traced.Status("FramesFailed"); r.composited = (long)Traced.Status("Composited");
            r.status = "mode " + Traced.Get("Mode") + ", denoising " + Traced.Get("Denoising") + (Traced.Flag("HdrpPasses", "Failed") ? ", FAILED: " + Traced.Text("HdrpPasses", "Detail") : "") +
                (Traced.Status("Disabled") != 0 ? ", the native side turned itself off" : "") + (Traced.Status("Supported") == 0 ? ", unsupported" : "");
        }

        // The frames: each the screen as shown, one per rendered frame. In a frozen world the camera must not move
        // between them (a hand on the mouse): the burst is taken again, up to three times.
        private static IEnumerator Burst(string dir, Step step, StepResult r)
        {
            int scale = Mathf.Max(1, step.scale), stride = Mathf.Max(1, step.stride);
            if (CameraSlide.Sliding && step.motionFrames > 0)
            {
                CameraSlide.BeginCycle(step.motionFrames, step.motionHoldFrames);
                // Swings warm for one cycle. A forward walk starts here after stationary settling;
                // warming a walk then restarting it would introduce a camera jump into the comparison.
                if (step.walkForward == 0f)
                {
                    yield return null;
                    for (int i = 0; i < step.motionFrames + Mathf.Max(0, step.motionHoldFrames); i++) yield return new WaitForEndOfFrame();
                }
            }
            for (int attempt = 0; attempt < 3; attempt++)
            {
                var frames = new List<FrameInfo>();
                bool moved = false; string error = "";
                var first = Camera.main;
                Vector3 p0 = first != null ? first.transform.position : Vector3.zero;
                Quaternion q0 = first != null ? first.transform.rotation : Quaternion.identity;
                for (int i = 0; i < step.frames; i++)
                {
                    for (int k = 1; k < stride; k++) yield return null;
                    yield return new WaitForEndOfFrame();
                    if (!GameplayReady(out string frameFailure))
                    { r.menuOpen = true; error = "the game left gameplay during capture: " + frameFailure; break; }
                    var diagnosticError = Path.Combine(dir, "nr-stages", "error.txt");
                    if (step.neuralStages && File.Exists(diagnosticError))
                    { error = "NR input capture stopped: " + File.ReadAllText(diagnosticError); break; }
                    string file = "f" + i.ToString("00") + ".raw";
                    if (!FrameGrab.Grab(Path.Combine(dir, file), scale, out int w, out int h, out string format, out string grabError)) { error = grabError; break; }
                    r.width = w; r.height = h; r.format = format; r.scale = scale;
                    var info = new FrameInfo { file = file, frame = Time.frameCount, realtime = Now };
                    info.cameraMotionTime = CameraSlide.SampleTime;
                    info.scaledTime = Time.time;
                    info.windSpeed = Shader.GetGlobalFloat("_VegetationWindSpeed"); info.windDirection = Shader.GetGlobalFloat("_VegetationWindDirection");
                    Traced.SampleFrameCounters(info);
                    if (step.neural.HasValue) Neural.Sample(info);
                    var camera = Camera.main;
                    if (camera != null)
                    {
                        var jitter = HDCamera.GetOrCreate(camera).taaJitter; info.jitterX = jitter.x; info.jitterY = jitter.y;
                        var p = camera.transform.position; var e = camera.transform.eulerAngles;
                        info.px = p.x; info.py = p.y; info.pz = p.z; info.rx = e.x; info.ry = e.y; info.rz = e.z; info.fov = camera.fieldOfView;
                        if ((p - p0).sqrMagnitude > 1e-6f || Quaternion.Angle(camera.transform.rotation, q0) > 0.02f) moved = true;
                    }
                    frames.Add(info);
                    // End of frame: the NEXT frame is the first stationary input retained for NR.
                    if (step.neuralReplay && i == step.motionFrames &&
                        !Try("Starting identical-input NR replay", () => Neural.Replay(step.neuralReplayReset, step.neuralReplayResetEveryFrame)))
                    { error = "NR replay could not start"; break; }
                }
                r.frames = frames.ToArray(); r.cameraMoved = moved; r.retries = attempt;
                // Keep any earlier failure even if gameplay has returned by the end of the take.
                if (!GameplayReady(out string endFailure))
                {
                    r.menuOpen = true;
                    if (error.Length == 0) error = "the game left gameplay during capture: " + endFailure;
                }
                if (error.Length > 0) { r.error = "a frame could not be saved: " + error; yield break; }
                // A sliding camera moves on purpose (CameraSlide).
                if (!moved || !HoldTime || CameraSlide.Sliding) yield break;
                Note("    the camera moved during the frames: taking them again.");
                yield return new WaitForSecondsRealtime(1f);
            }
        }

        private static IEnumerator Perf(float seconds, PerfInfo perf, bool rows = false)
        {
            var times = new List<float>(8192);
            double gpu = 0, thread = 0, d3d11 = 0, unityCpu = 0, unityGpu = 0; int samples = 0, unitySamples = 0;
            var timings = new FrameTiming[1];
            float end = Now + seconds, nextSample = 0f, t0 = Now;
            // (PT-109, megageo-5: a row a second, when asked for.)
            var perSecond = new List<PerfSecond>(); var second = new List<float>(256); float secondStart = t0; double secondGpu = 0; int secondSamples = 0;
            while (Now < end)
            {
                yield return null;
                float ms = Time.unscaledDeltaTime * 1000f;
                times.Add(ms);
                FrameTimingManager.CaptureFrameTimings();
                if (FrameTimingManager.GetLatestTimings(1, timings) > 0 && timings[0].cpuFrameTime > 0)
                {
                    unityCpu += timings[0].cpuFrameTime; unityGpu += timings[0].gpuFrameTime; ++unitySamples;
                }
                if (Now >= nextSample)
                {
                    nextSample = Now + 0.25f;
                    double g = Traced.Status("GpuMs");
                    gpu += g; thread += Traced.Status("FrameMs"); d3d11 += Traced.Status("D3d11Ms"); ++samples;
                    secondGpu += g; ++secondSamples;
                }
                if (rows)
                {
                    second.Add(ms);
                    if (Now - secondStart >= 1f)
                    {
                        perSecond.Add(Second(secondStart - t0, second, secondSamples > 0 ? secondGpu / secondSamples : 0));
                        second.Clear(); secondStart = Now; secondGpu = 0; secondSamples = 0;
                    }
                }
            }
            if (rows && second.Count > 0) perSecond.Add(Second(secondStart - t0, second, secondSamples > 0 ? secondGpu / secondSamples : 0));
            perf.perSecond = perSecond.ToArray();
            perf.measured = times.Count > 0; perf.frames = times.Count; perf.seconds = Now - t0;
            if (times.Count == 0) yield break;
            foreach (float t in times) { if (t > 50f) ++perf.over50; if (t > 100f) ++perf.over100; }
            double sum = 0; foreach (float t in times) sum += t;
            var sorted = new List<float>(times); sorted.Sort();
            perf.frameMs = Clean(sum / times.Count); perf.fps = perf.frameMs > 0f ? 1000f / perf.frameMs : 0f;
            perf.medianMs = sorted[sorted.Count / 2]; perf.p95Ms = sorted[(int)(0.95f * (sorted.Count - 1))]; perf.p99Ms = sorted[(int)(0.99f * (sorted.Count - 1))];
            perf.worstMs = sorted[sorted.Count - 1]; perf.lowFps = perf.p99Ms > 0f ? 1000f / perf.p99Ms : 0f;
            if (samples > 0) { perf.tracerGpuMs = Clean(gpu / samples); perf.tracerRenderThreadMs = Clean(thread / samples); perf.tracerD3d11Ms = Clean(d3d11 / samples); }
            if (unitySamples > 0) { perf.unityCpuMs = Clean(unityCpu / unitySamples); perf.unityGpuMs = Clean(unityGpu / unitySamples); }
            perf.stages = Traced.StageTimes();
            perf.vSyncCount = QualitySettings.vSyncCount; perf.targetFrameRate = Application.targetFrameRate; perf.timeScale = Time.timeScale;
        }

        // PT-109 (megageo-5): one second of a measurement (Step.perSecond): its frames, and the tracer's numbers read now.
        private static PerfSecond Second(float at, List<float> frames, double tracerGpuMs)
        {
            var r = new PerfSecond { at = at, frames = frames.Count, tracerGpuMs = Clean(tracerGpuMs) };
            double sum = 0;
            foreach (float t in frames) { sum += t; if (t > r.worstMs) r.worstMs = t; if (t > 50f) ++r.over50; if (t > 100f) ++r.over100; }
            r.frameMs = frames.Count > 0 ? Clean(sum / frames.Count) : 0f;
            r.stages = Traced.StageTimes();
            r.instancesTraced = (int)Traced.Status("InstancesTraced"); r.instancesWaiting = (int)Traced.Status("InstancesWaiting");
            r.plantsWaiting = (int)Traced.Status("PlantsWaiting"); r.meshesQueued = (int)Traced.Status("MeshesQueued");
            r.micromapsWaiting = (int)Traced.Status("MicromapsWaiting"); r.tilesToBake = (int)Traced.Status("TilesToBake");
            r.accelerationMb = (float)(Traced.Diagnostic("Acceleration") / 1048576.0); r.plantsMb = (float)(Traced.Diagnostic("Plants") / 1048576.0);
            r.totalMb = (float)(Traced.Diagnostic("Total") / 1048576.0);
            var camera = Camera.main;
            if (camera != null) { var p = camera.transform.position; r.x = p.x; r.y = p.y; r.z = p.z; }
            return r;
        }
    }
}
