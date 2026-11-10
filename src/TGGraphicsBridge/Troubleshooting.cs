// SPDX-License-Identifier: MIT
// The troubleshooting report: one button at the bottom of the Video settings (nested under the performance
// overlay) that runs a short test and saves a text file in the BepInEx folder, for the player to paste
// into a bug report.
//
// The test run measures the game with the player's own settings first, then goes through what each mod
// built on the bridge registered with AddSweep: TG Better Upscaling each upscaler the game offers, TG
// Frame Generation each frame generation technology with each low latency technology it can run with
// (Off included where that is allowed). Every step is applied, left to settle for a moment and measured
// for a few seconds while the player keeps playing: the rendered frame rate and its 1% low, CPU, GPU
// and video memory use, and what each mod reports through AddMeasure (frames on screen, the presenting
// technology, the time the presentation bridge spends per frame). A mod's steps run while the other
// mods hold a neutral state (frame generation off during the upscalers); at the end every mod puts
// back what the player had. Each step is marked in the BepInEx log and in the native logs, so the
// native pipeline summaries line up with it.
//
// Menus pause the run, and while it is paused every mod puts the player's settings back: the settings
// screen is only reached through a menu, so its rows never show a test step's values, and whatever the
// player applies there is theirs. Back in the game, the current step is applied and measured again. A
// moment out of play without a menu (no camera, or the game's time stopped, as during an autosave) only
// starts the step's measurement over; it pauses the run once it has lasted a second.
// Selecting the button again stops the run and saves the report with the steps measured so far; before
// the run has started (it starts only in gameplay), it saves the logs without one.
//
// The file holds a summary (the system, the mods, problems found, the test results and what the native
// side knows), then this session's part of the logs of BepInEx, the bridge and both mods, their config
// values and where the mods' files are (ReportText). The Windows user folder is replaced by %USERPROFILE%.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using BepInEx;
using BepInEx.Configuration;
using UnityEngine;
namespace TGGraphicsBridge
{
    // One configuration of the test run.
    public sealed class SweepStep
    {
        public string Name;          // "FSR Frame Generation 2x + AMD Anti-Lag 2"
        public Action Apply;         // makes this step's configuration current
        public float Settle = 2.5f;  // seconds before measuring: swapchains and upscaler contexts are rebuilt
        public float Measure = 4f;   // seconds measured
    }
    public static class Troubleshooting
    {
        private sealed class Sweep { internal string Title, Covers; internal int Order; internal Func<IList<SweepStep>> Steps; internal Action Neutral, Restore; }
        private sealed class Part { internal string Title; internal int Order; internal Func<string> Text; internal Func<IEnumerable<string>> Files; }
        private sealed class Measurement { internal string Title; internal Action Begin; internal Func<string> End; }
        private sealed class Planned { internal SweepStep Step; internal Sweep Owner; }
        private sealed class Result { internal string Name, Line; internal float Fps, Low, Cpu, Gpu; internal double VramGb, BudgetGb; }
        private enum Phase { Idle, Waiting, Running, Writing }
        private static readonly List<Sweep> sweeps = new List<Sweep>();
        private static readonly List<Part> parts = new List<Part>();
        private static readonly List<Measurement> measurements = new List<Measurement>();
        private static Phase phase = Phase.Idle;
        private static List<Planned> plan;
        private static readonly List<Result> results = new List<Result>();
        private static int index;
        private static bool measuring, paused, requested, stopRequested, restored;
        private static string stopReason;
        private static float phaseTime, lastTick;
        private static Sweep active; // whose steps run now
        private static readonly List<float> frames = new List<float>();
        private static float nextSample;
        private static double cpuSum, gpuSum, vramMax, budget; private static int samples;
        private static ConfigEntry<bool> entry;
        private static bool storing;
        private static DateTime startedUtc;
        private static string lastReport = "";
        private const string ToastKey = "troubleshooting";
        // The report goes in the BepInEx folder, beside LogOutput.log, where players already look for logs.
        public static string ReportsFolder => Paths.BepInExRootPath;
        private static string Marker => Path.Combine(GraphicsBridge.Root, "troubleshooting-in-progress.txt");
        // The previous session's test run did not finish (the game closed or crashed during it): the mods
        // put their own persisted settings back when they read this.
        public static bool InterruptedRun { get; private set; }
        // TG Better Upscaling put the upscaler back after an interrupted run: the marker has done its job.
        public static void InterruptedRunRecovered()
        {
            try { if (File.Exists(Marker)) File.Delete(Marker); } catch (Exception ex) { BridgeLog.Log.LogWarning("Could not remove " + Marker + ": " + ex.Message); }
        }
        private static float firstTick = -1f;
        private static bool markerChecked;
        // Once, 30 s after startup: a marker nothing will recover from (TG Better Upscaling isn't running) goes;
        // one it should have recovered from stays, and is said to.
        private static void CheckMarker(float now)
        {
            if (markerChecked || !InterruptedRun) return;
            if (firstTick < 0f) firstTick = now;
            if (now - firstTick < 30f) return;
            markerChecked = true;
            if (!File.Exists(Marker)) return;
            if (InstallCheck.LoadedFrom("TGBetterUpscaling") == null) { InterruptedRunRecovered(); return; }
            BridgeLog.Log.LogWarning("TG Better Upscaling hasn't put your upscaler back after the interrupted troubleshooting run yet; check the Dynamic Resolution row.");
        }
        public static bool Running => phase == Phase.Running;
        public static bool Busy => phase != Phase.Idle;
        // A mod's part of the test run. steps: this mod's configurations to measure, read when the run
        // starts; neutral: the state it holds while another mod's steps run; restore: the player's own.
        public static void AddSweep(string title, int order, Func<IList<SweepStep>> steps, Action neutral, Action restore) =>
            AddSweep(title, null, order, steps, neutral, restore);
        // covers: what the steps try, as the button's tooltip and the start message name it ("each upscaler").
        public static void AddSweep(string title, string covers, int order, Func<IList<SweepStep>> steps, Action neutral, Action restore)
        {
            if (string.IsNullOrEmpty(title) || steps == null) return;
            lock (sweeps)
            {
                sweeps.RemoveAll(s => s.Title == title);
                sweeps.Add(new Sweep { Title = title, Covers = covers, Order = order, Steps = steps, Neutral = neutral, Restore = restore });
                sweeps.Sort((a, b) => a.Order.CompareTo(b.Order));
            }
        }
        // A mod's part of the report: text for the summary (called on the main thread) and files to include.
        public static void AddReport(string title, int order, Func<string> text, Func<IEnumerable<string>> files)
        {
            if (string.IsNullOrEmpty(title)) return;
            lock (parts)
            {
                parts.RemoveAll(p => p.Title == title);
                parts.Add(new Part { Title = title, Order = order, Text = text, Files = files });
                parts.Sort((a, b) => a.Order.CompareTo(b.Order));
            }
        }
        // What a mod adds to each step's measurement: begin when measuring starts, end returns its text.
        public static void AddMeasure(string title, Action begin, Func<string> end)
        {
            if (string.IsNullOrEmpty(title) || end == null) return;
            lock (measurements)
            {
                measurements.RemoveAll(m => m.Title == title);
                measurements.Add(new Measurement { Title = title, Begin = begin, End = end });
            }
        }
        // For mod managers, which read it once: what the button does with whichever mods are installed.
        internal const string Description = "Tries the upscalers, frame generation and low latency settings the installed mods offer, in play, " +
            "then puts yours back and saves a report in the BepInEx folder to paste into a bug report on Nexus.";
        // What the test run tries with the mods installed now, and for how long: only upscalers with TG Better
        // Upscaling alone, only frame generation with TG Frame Generation alone. Estimated from the steps each
        // mod would test, at most every 10 s (a mod may log while it lists them).
        private static string covers = "", duration = "";
        private static float coversAt = -100f;
        private static void Estimate()
        {
            float now = UnityEngine.Time.unscaledTime;
            if (now - coversAt < 10f && covers.Length > 0) return;
            coversAt = now;
            Sweep[] all;
            lock (sweeps) all = sweeps.ToArray();
            var what = all.Select(s => s.Covers ?? s.Title + "'s settings").ToArray();
            covers = what.Length == 0 ? "none" : what.Length == 1 ? what[0] : string.Join(", ", what.Take(what.Length - 1).ToArray()) + " and " + what[what.Length - 1];
            float seconds = 6f; // your own settings first
            foreach (var sweep in all)
            {
                try { var steps = sweep.Steps(); if (steps != null) foreach (var step in steps) if (step != null) seconds += step.Settle + step.Measure; }
                catch { }
            }
            int minutes = (int)Math.Round(seconds / 60f);
            duration = seconds < 90f ? ((int)Math.Max(10, Math.Round(seconds / 10f) * 10)) + " seconds" : minutes + " minutes";
        }
        // The button's tooltip, short enough for the game's narrow tooltip column.
        internal static string Describe()
        {
            Estimate();
            if (covers == "none") return "Saves a report of the mods' logs in the BepInEx folder, to paste into a bug report on Nexus.";
            return "Tries " + covers + " in play for about " + duration + ", then puts yours back and saves a report in the BepInEx folder to paste into a bug report on Nexus.";
        }
        internal static void Bind(ConfigFile config)
        {
            var row = new ConfigurationManagerAttributes
            {
                DisplayName = "Create Troubleshooting Report", DispName = "Create Troubleshooting Report", DisplaySection = "Video", Category = "Video",
                SortOrder = 13, Visible = true
            };
            entry = config.Bind("Video", "Create troubleshooting report", false, new ConfigDescription(Description, null, row));
            if (entry.Value) { storing = true; entry.Value = false; storing = false; }
            config.SettingChanged += OnSettingChanged;
            // Kept until TG Better Upscaling, whose test steps choose the game's upscaler, has put it back
            // (InterruptedRunRecovered); without that mod, nothing needs it and the first ticks remove it.
            try { InterruptedRun = File.Exists(Marker); } catch { }
            if (InterruptedRun) BridgeLog.Log.LogWarning("The last troubleshooting test run did not finish; the mods put your settings back.");
        }
        private static void OnSettingChanged(object sender, SettingChangedEventArgs e)
        {
            if (storing || entry == null || e.ChangedSetting != entry || !entry.Value) return;
            requested = true; // FoA Mod Manager's edit arrives inside its UI code: acted on in the next Tick
        }
        // The Video button or FoA Mod Manager: start, or (waiting) save the logs now, or (running) stop and save.
        public static void Request()
        {
            if (phase == Phase.Idle) Begin();
            else if (phase == Phase.Waiting) { BridgeLog.Log.LogInfo("Troubleshooting report: saving the logs without a test run."); Finish("saved without a test run"); }
            else if (phase == Phase.Running) Stop("stopped by you");
        }
        // From the Video settings: the screen closes, and the run starts back in the game.
        internal static void RequestFromMenu()
        {
            Request();
            if (phase != Phase.Waiting) return;
            try { VideoTab.CloseSettings(); } catch (Exception ex) { BridgeLog.Log.LogWarning("Could not close the settings: " + ex.Message); }
        }
        private static void Begin()
        {
            phase = Phase.Waiting; startedUtc = DateTime.UtcNow;
            results.Clear(); plan = null; index = 0; measuring = paused = stopRequested = restored = false; stopReason = null; awaySince = -1f;
            BridgeLog.Log.LogInfo("Troubleshooting report requested; the test run starts in gameplay.");
            Estimate();
            Notices.Show(ToastKey, "Troubleshooting report", "Go back to the game to start the test run of about " + duration + ". " +
                "It tries " + covers + ", then saves a report in the BepInEx folder. " +
                "Menus pause it. Select Create Troubleshooting Report again for a report of the logs only.", 600f);
        }
        // A mod had to stop what the test runs (frame generation's emergency key, a failure): the run ends at
        // the next frame, puts the settings back and saves what it measured.
        public static void Abort(string why) => Stop(why);
        private static void Stop(string why)
        {
            if (phase != Phase.Running) return;
            stopRequested = true; stopReason = why;
        }
        // Where the player is: in play (the game has focus, runs, locks the cursor and has a camera); in a
        // menu (the cursor is free, the settings are open, or the game is in the background); or away from
        // play without a menu: the game's time stopped or no camera, as while an autosave takes its picture
        // with a camera of its own.
        private enum Where { Playing, Menu, Away }
        private static Where Locate()
        {
            if (!Application.isFocused || Cursor.lockState != CursorLockMode.Locked || VideoTab.IsSettingsOpen) return Where.Menu;
            if (Time.timeScale <= 0f) return Where.Away;
            var camera = Camera.main;
            return camera != null && camera.isActiveAndEnabled ? Where.Playing : Where.Away;
        }
        private static bool Gameplay() => Locate() == Where.Playing;
        // How long the game may be away from play without a menu before the run pauses. An autosave took a
        // third of a second, and pausing for it rebuilt frame generation's swapchain twice for nothing.
        private const float AwayGrace = 1f;
        private static float awaySince = -1f;
        // Every frame, from the bridge's per-frame component.
        internal static void Tick(float now)
        {
            CheckMarker(now);
            if (requested) { requested = false; storing = true; try { if (entry != null) entry.Value = false; } finally { storing = false; } Request(); }
            float delta = lastTick > 0f ? now - lastTick : 0f; lastTick = now;
            if (phase == Phase.Waiting)
            {
                if (!Gameplay()) return;
                StartRun();
                return;
            }
            if (phase != Phase.Running) return;
            if (stopRequested) { Complete(stopReason ?? "stopped"); return; }
            var where = Locate();
            if (where != Where.Playing)
            {
                if (!paused)
                {
                    measuring = false; frames.Clear(); phaseTime = 0f; // this step is measured again from the start
                    if (awaySince < 0f) awaySince = now;
                    // The player's own settings in a menu, before it can show them: the settings screen is only
                    // reached through a menu, so its rows never hold a test step's values, and what the player
                    // applies there is theirs. Away from play without a menu, only after AwayGrace.
                    if (where == Where.Menu || now - awaySince >= AwayGrace)
                    {
                        paused = true;
                        RestoreAll();
                        Notices.Show(ToastKey, "Troubleshooting report: paused", "Your settings are back while the game is paused. Return to the game to " +
                            "continue the test run (step " + (index + 1) + " of " + plan.Count + "), or select Create Troubleshooting Report again to stop it and save the report.", 600f);
                    }
                }
                return;
            }
            awaySince = -1f;
            if (paused) { paused = false; ApplyStep(); return; }
            phaseTime += delta;
            var step = plan[index].Step;
            if (!measuring)
            {
                if (phaseTime < step.Settle) return;
                BeginMeasure();
                return;
            }
            frames.Add(Time.unscaledDeltaTime);
            if (now >= nextSample) { nextSample = now + 0.5f; Sample(); }
            if (phaseTime < step.Measure) return;
            EndMeasure();
            if (++index >= plan.Count) { Complete(null); return; }
            ApplyStep();
        }
        private static void StartRun()
        {
            var planned = new List<Planned> { new Planned { Step = new SweepStep { Name = "Your settings", Settle = 1f, Measure = 5f } } };
            Sweep[] all;
            lock (sweeps) all = sweeps.ToArray();
            foreach (var sweep in all)
            {
                try
                {
                    var steps = sweep.Steps();
                    if (steps == null) continue;
                    foreach (var step in steps) if (step != null) planned.Add(new Planned { Step = step, Owner = sweep });
                }
                catch (Exception ex) { BridgeLog.Log.LogWarning("Troubleshooting: " + sweep.Title + " has no test steps: " + ex.Message); }
            }
            // TG Better Upscaling's steps choose the game's upscaler, which the game stores: if the game closed
            // mid-run, the next start puts it back only if this marker is on disk. Without it, no test.
            try { Directory.CreateDirectory(GraphicsBridge.Root); File.WriteAllText(Marker, DateTime.UtcNow.ToString("o")); }
            catch (Exception ex)
            {
                BridgeLog.Log.LogWarning("Troubleshooting: could not write " + Marker + " (" + ex.Message + "); the test run doesn't start without it.");
                plan = planned;
                Finish("not run: its recovery file couldn't be written (" + ex.Message + ")");
                return;
            }
            plan = planned; index = 0; phase = Phase.Running; active = null;
            string line = "Troubleshooting test run: " + plan.Count + " steps: " + string.Join("; ", plan.Select(p => p.Step.Name).ToArray()) + ".";
            BridgeLog.Log.LogInfo(line);
            GraphicsBridge.LogNote(line);
            ApplyStep();
        }
        private static void ApplyStep()
        {
            while (true)
            {
                var planned = plan[index];
                // A mod's steps run while the others hold their neutral state; the first step is everyone's own.
                if (planned.Owner != active || restored)
                {
                    Sweep[] all;
                    lock (sweeps) all = sweeps.ToArray();
                    foreach (var sweep in all)
                        if (sweep != planned.Owner) Call(sweep.Title + "'s neutral state", planned.Owner == null ? sweep.Restore : sweep.Neutral);
                    active = planned.Owner; restored = false;
                }
                if (Call(planned.Step.Name, planned.Step.Apply, out string error)) break;
                results.Add(new Result { Name = planned.Step.Name, Line = "not applied: " + error, Fps = -1f });
                if (++index >= plan.Count) { Complete(null); return; }
            }
            var current = plan[index];
            measuring = false; phaseTime = 0f;
            string note = "Troubleshooting step " + (index + 1) + "/" + plan.Count + " \"" + current.Step.Name + "\": applied.";
            BridgeLog.Log.LogInfo(note);
            GraphicsBridge.LogNote(note);
            ShowStep();
        }
        private static void ShowStep()
        {
            var step = plan[index].Step;
            Notices.Show(ToastKey, "Troubleshooting report: step " + (index + 1) + " of " + plan.Count, step.Name + ". Keep playing; the test puts your settings back when it's done.",
                600f);
        }
        private static void BeginMeasure()
        {
            measuring = true; phaseTime = 0f; frames.Clear();
            cpuSum = gpuSum = vramMax = budget = 0; samples = 0; nextSample = 0f;
            Measurement[] all;
            lock (measurements) all = measurements.ToArray();
            foreach (var m in all) Call(m.Title + " measurement", m.Begin);
        }
        private static void Sample()
        {
            if (!GraphicsBridge.TryReadPerformance(out var p)) return;
            if ((p.Valid & 3u) == 3u) { cpuSum += p.CpuProcess; gpuSum += p.GpuProcess; samples++; }
            if ((p.Valid & 4u) != 0) { vramMax = Math.Max(vramMax, p.VramProcess / 1073741824.0); budget = p.VramBudget / 1073741824.0; }
        }
        private static void EndMeasure()
        {
            measuring = false;
            var step = plan[index].Step;
            var result = new Result { Name = step.Name };
            float total = frames.Sum();
            result.Fps = total > 0f ? frames.Count / total : 0f;
            if (frames.Count >= 20)
            {
                // As the performance overlay: the frame time 99% of the frames are faster than.
                var sorted = frames.ToArray(); Array.Sort(sorted);
                float slow = sorted[Math.Min(sorted.Length - 1, (int)(sorted.Length * 0.99f))];
                result.Low = slow > 0f ? 1f / slow : 0f;
            }
            result.Cpu = samples > 0 ? (float)(cpuSum / samples) : -1f;
            result.Gpu = samples > 0 ? (float)(gpuSum / samples) : -1f;
            result.VramGb = vramMax; result.BudgetGb = budget;
            var sb = new StringBuilder();
            sb.Append("rendered ").Append(result.Fps.ToString("0.0", CultureInfo.InvariantCulture)).Append(" fps");
            if (result.Low > 0f) sb.Append(" (1% low ").Append(result.Low.ToString("0", CultureInfo.InvariantCulture)).Append(')');
            if (result.Gpu >= 0f) sb.Append(", CPU ").Append(result.Cpu.ToString("0")).Append("%, GPU ").Append(result.Gpu.ToString("0")).Append('%');
            if (result.BudgetGb > 0) sb.Append(", VRAM ").Append(result.VramGb.ToString("0.0", CultureInfo.InvariantCulture)).Append(" of ").Append(result.BudgetGb.ToString("0.0", CultureInfo.InvariantCulture)).Append(" GB");
            Measurement[] all;
            lock (measurements) all = measurements.ToArray();
            foreach (var m in all)
            {
                string text = null;
                try { text = m.End(); } catch (Exception ex) { text = "failed: " + ex.GetType().Name; }
                if (!string.IsNullOrEmpty(text)) sb.Append(" | ").Append(m.Title).Append(": ").Append(text);
            }
            result.Line = sb.ToString();
            results.Add(result);
            string line = "Troubleshooting step " + (index + 1) + "/" + plan.Count + " \"" + step.Name + "\": " + result.Line;
            BridgeLog.Log.LogInfo(line);
            GraphicsBridge.LogNote(line);
        }
        // Retain the player settings across diagnostic steps. Each step applies its mod settings, then restores
        // the originals; report the mods whose restore failed, or an empty string when all succeeded.
        private static string restoreFailures = "";
        private static bool RestoreAll()
        {
            Sweep[] all;
            lock (sweeps) all = sweeps.ToArray();
            var failed = new List<string>();
            foreach (var sweep in Enumerable.Reverse(all))
                if (!Call(sweep.Title + "'s restore", sweep.Restore)) failed.Add(sweep.Title);
            active = null;
            restored = true;
            restoreFailures = string.Join(", ", failed.ToArray());
            return failed.Count == 0;
        }
        private static void Complete(string why)
        {
            bool back = RestoreAll();
            // The marker stays when a restore failed: the next start puts the upscaler back from the config.
            if (back) { try { if (File.Exists(Marker)) File.Delete(Marker); } catch { } }
            string outcome = (why == null ? "all " + plan.Count + " steps run" : why + " after " + results.Count + " of " + plan.Count + " steps") +
                (back ? "" : "; putting back the settings of " + restoreFailures + " failed");
            if (back) BridgeLog.Log.LogInfo("Troubleshooting test run " + (why == null ? "finished" : why) + "; your settings are back.");
            else BridgeLog.Log.LogWarning("Troubleshooting test run " + (why == null ? "finished" : why) + ", but " + restoreFailures +
                " couldn't put your settings back; the next start does, or choose them again in the Video settings.");
            Finish(outcome);
        }
        // The report's own code failed during a run: the settings go back before the report stops for the session.
        internal static void Abandon()
        {
            if (phase != Phase.Running) return;
            try { if (RestoreAll()) { try { if (File.Exists(Marker)) File.Delete(Marker); } catch { } } } catch { }
            phase = Phase.Idle;
            Notices.Hide(ToastKey);
        }
        private static bool Call(string what, Action action) => Call(what, action, out _);
        private static bool Call(string what, Action action, out string error)
        {
            error = null;
            if (action == null) return true;
            try { action(); return true; }
            catch (Exception ex) { error = ex.GetType().Name + ": " + ex.Message; BridgeLog.Log.LogWarning("Troubleshooting: " + what + " failed: " + ex); return false; }
        }
        // The summary on the main thread (the mods read Unity state), the file on a worker thread.
        private static void Finish(string outcome)
        {
            phase = Phase.Writing;
            Notices.Show(ToastKey, "Troubleshooting report", "Saving the report...", 60f);
            string summary;
            var files = new List<ReportEntry>();
            // The files first and on their own: a section of the summary that fails must not cost the logs.
            try { CollectFiles(files); } catch (Exception ex) { BridgeLog.Log.LogWarning("Troubleshooting: some files could not be listed: " + ex.Message); }
            try { summary = Summary(outcome); }
            catch (Exception ex) { summary = "The summary could not be written: " + ex; }
            string name = "TG Troubleshooting Report " + DateTime.Now.ToString("yyyy-MM-dd HH-mm-ss", CultureInfo.InvariantCulture) + ".txt";
            var thread = new Thread(() => Write(name, summary, files)) { IsBackground = true, Name = "TG troubleshooting report" };
            thread.Start();
        }
        // One text file, read and filtered off the main thread.
        private static void Write(string name, string summary, List<ReportEntry> files)
        {
            string path = null, error = null, text = null;
            try
            {
                text = ReportText.Build(summary, files);
                Directory.CreateDirectory(ReportsFolder);
                path = Path.Combine(ReportsFolder, name);
                // Written whole, then renamed: a full disk or the game closing leaves no half report.
                string partial = path + ".partial";
                File.WriteAllText(partial, text, new UTF8Encoding(false));
                if (File.Exists(path)) File.Delete(path);
                File.Move(partial, path);
            }
            catch (Exception ex) { error = ex.Message; path = null; BridgeLog.Log.LogError("Troubleshooting report failed: " + ex); }
            lastReport = path ?? "";
            MainThread.Post(() => Written(path, text, error));
        }
        // On the main thread: where the report is, to paste into a bug report.
        private static void Written(string path, string text, string error)
        {
            phase = Phase.Idle;
            if (path == null)
            {
                Notices.Show(ToastKey, "Troubleshooting report failed", error ?? "See BepInEx/LogOutput.log.", 20f);
                return;
            }
            string shown = path.StartsWith(Paths.GameRootPath, StringComparison.OrdinalIgnoreCase) ? path.Substring(Paths.GameRootPath.Length).TrimStart('\\', '/') : path;
            string size = (Encoding.UTF8.GetByteCount(text) / 1024) + " KB";
            BridgeLog.Log.LogInfo("Troubleshooting report saved (" + size + "): " + path);
            Notices.Show(ToastKey, "Troubleshooting report saved", (restoreFailures.Length > 0
                    ? "Some of your settings couldn't be put back (" + restoreFailures + "): check them in the Video settings. " : "") +
                "It's in " + shown.Replace('\\', '/') + " (" + size + "). Open it and paste its text into a bug report on the mod's Nexus page.", 30f);
        }
        // The last saved report, for the tooltip.
        internal static string LastReport => lastReport;
        internal static string Tooltip()
        {
            string state = phase == Phase.Waiting ? " Select again to save the report without the test."
                : phase == Phase.Running ? " Select again to stop the test and save the report."
                : lastReport.Length > 0 ? " The last report is in the BepInEx folder." : "";
            return Describe() + state;
        }
        private static string Summary(string outcome)
        {
            var sb = new StringBuilder();
            sb.AppendLine("TG troubleshooting report, " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture) +
                " (UTC " + DateTime.UtcNow.ToString("HH:mm", CultureInfo.InvariantCulture) + ")");
            sb.AppendLine("Test run: " + outcome + ".");
            Sweep[] took;
            lock (sweeps) took = sweeps.ToArray();
            sb.AppendLine("Mods in the test run: " + (took.Length == 0 ? "none" : string.Join(", ", took.Select(t => t.Title).ToArray())) + ".");
            Section(sb, null, SystemReport.Append);
            Section(sb, "Problems found", s =>
            {
                var notices = Notices.Current();
                if (notices.Count == 0) s.AppendLine("None.");
                foreach (var n in notices) s.AppendLine("[" + n.Level + "] " + n.Source + ": " + n.Label + ". " + n.Detail);
            });
            Section(sb, "Test run", s =>
            {
                if (results.Count == 0) { s.AppendLine("No step was measured."); return; }
                s.AppendLine("Each step: applied, left to settle, then measured. In the native logs, the Pipeline line after each step's note covers that step.");
                for (int i = 0; i < results.Count; i++) s.AppendLine((i + 1).ToString().PadLeft(2) + ". " + results[i].Name + ": " + results[i].Line);
                if (plan != null && results.Count < plan.Count)
                    s.AppendLine("Not measured: " + string.Join("; ", plan.Skip(results.Count).Select(p => p.Step.Name).ToArray()) + ".");
            });
            Section(sb, null, InstallCheck.Append);
            Section(sb, null, Compatibility.Append);
            Section(sb, "TG Graphics Bridge " + BridgePlugin.Version, s =>
            {
                s.AppendLine(GraphicsBridge.Describe());
                foreach (var fact in GraphicsBridge.Facts().OrderBy(f => f.Key)) s.AppendLine("  " + fact.Key + " = " + fact.Value);
            });
            Part[] snapshot;
            lock (parts) snapshot = parts.ToArray();
            foreach (var part in snapshot) Section(sb, part.Title, s => s.AppendLine(part.Text?.Invoke() ?? ""));
            return sb.ToString();
        }
        // One part of the summary; a part that fails says so, and the rest is still written.
        private static void Section(StringBuilder sb, string title, Action<StringBuilder> write)
        {
            sb.AppendLine();
            if (title != null) sb.AppendLine("== " + title + " ==");
            var part = new StringBuilder();
            try { write(part); sb.Append(part); }
            catch (Exception ex) { sb.Append(part).AppendLine("Unavailable: " + ex); }
        }
        // The logs in the order they are read: BepInEx's and the bridge's, each mod's, the game's, then the
        // mods' settings and where their files are.
        private static void CollectFiles(List<ReportEntry> files)
        {
            AddFile(files, Path.Combine(Paths.BepInExRootPath, "LogOutput.log"), "BepInEx/LogOutput.log (the mods' lines, warnings and errors)");
            AddFile(files, Path.Combine(GraphicsBridge.Root, "Autoload.log"));
            AddFile(files, Path.Combine(GraphicsBridge.Root, "TGGraphicsBridge.Native.log"));
            Part[] snapshot;
            lock (parts) snapshot = parts.ToArray();
            foreach (var part in snapshot)
                try { foreach (var file in part.Files?.Invoke() ?? Enumerable.Empty<string>()) AddFile(files, file); } catch { }
            try
            {
                string player = Application.consoleLogPath;
                if (!string.IsNullOrEmpty(player)) AddFile(files, player, "Unity's player log (errors and warnings)");
                var logs = Path.Combine(Application.persistentDataPath, "Logs");
                if (Directory.Exists(logs))
                {
                    var newest = new DirectoryInfo(logs).GetFiles("*.txt").OrderByDescending(f => f.LastWriteTimeUtc).FirstOrDefault();
                    if (newest != null) AddFile(files, newest.FullName, "The game's log (errors and warnings)");
                }
            }
            catch { }
            foreach (var id in new[] { "com.wessberg.tgframegeneration", "com.wessberg.tgbetterupscaling", "com.wessberg.tggraphicsbridge" })
                AddFile(files, Path.Combine(Paths.ConfigPath, id + ".cfg"), "BepInEx/config/" + id + ".cfg");
            try { files.Add(new ReportEntry { Name = "Where the mods' files are", Text = InstallCheck.CompactListing() }); } catch { }
        }
        private static void AddFile(List<ReportEntry> files, string path, string name = null)
        {
            if (string.IsNullOrEmpty(path) || !File.Exists(path) || files.Any(f => string.Equals(f.Path, path, StringComparison.OrdinalIgnoreCase))) return;
            files.Add(new ReportEntry { Name = name ?? ReportText.EntryName(path) + " (this session)", Path = path });
        }
    }
    // Work handed back to Unity's main thread from another one.
    internal static class MainThread
    {
        private static readonly Queue<Action> queue = new Queue<Action>();
        internal static void Post(Action action) { lock (queue) queue.Enqueue(action); }
        internal static void Run()
        {
            while (true)
            {
                Action next;
                lock (queue) { if (queue.Count == 0) return; next = queue.Dequeue(); }
                try { next(); } catch (Exception ex) { BridgeLog.Log.LogError("TG Graphics Bridge: " + ex); }
            }
        }
    }
}
