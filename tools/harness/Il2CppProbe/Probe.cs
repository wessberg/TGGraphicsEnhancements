// SPDX-License-Identifier: MIT
// The normal version's test: the Mono harness cannot run there. Loads one save with saving blocked, walks the released
// mods' technologies one by one and writes what each did, then quits. In no release zip (Run-Il2CppProbe.ps1 installs and
// removes it). Frame rates here are not quoted unless the log says focused=True.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using Awaken.TG.Main.Heroes;
using Awaken.TG.Main.Saving;
using Awaken.TG.Main.Saving.Models;
using Awaken.TG.Main.Saving.SaveSlots;
using Awaken.TG.Main.Settings;
using Awaken.TG.Main.Settings.Windows;
using Awaken.TG.Main.UI.Menu;
using Awaken.TG.Main.UI.RoguePreloader;
using Awaken.TG.Main.UI.TitleScreen;
using Awaken.TG.Main.UI.TitleScreen.Loading;
using Awaken.TG.MVC;
using Awaken.TG.MVC.UI.Handlers.States;
using BepInEx;
using BepInEx.Configuration;
using BepInEx.Unity.IL2CPP;
using HarmonyLib;
using UnityEngine;

[BepInPlugin("com.wessberg.il2cpp-probe", "TG normal-version probe", "1.0.0")]
[BepInDependency("com.wessberg.tgneuralrendering")]
[BepInDependency("com.wessberg.tgframegeneration")]
[BepInDependency("com.wessberg.tgbetterupscaling")]
public class Probe : BasePlugin
{
    internal static string Folder;
    internal static readonly BindingFlags Any = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.Instance;
    internal static void Write(string text) => File.AppendAllText(Path.Combine(Folder, "run.log"), DateTime.Now.ToString("HH:mm:ss.fff") + " " + text + Environment.NewLine);
    public override void Load()
    {
        Folder = Path.Combine(Paths.BepInExRootPath, "TGIl2CppProbe");
        Directory.CreateDirectory(Folder);
        File.WriteAllText(Path.Combine(Folder, "run.log"), "Normal-version probe. Saves blocked.\n");
        new Harmony("com.wessberg.il2cpp-probe").Patch(AccessTools.Method(typeof(LoadSave), "Save", new[] { typeof(SaveSlot), typeof(bool) }), prefix: new HarmonyMethod(typeof(Probe), nameof(NoSave)));
        AddComponent<ProbeBehaviour>();
    }
    private static bool NoSave() { Write("SAVE blocked"); return false; }
}

public class ProbeBehaviour : MonoBehaviour
{
    private readonly Queue<(string name, Action run, float wait)> steps = new Queue<(string, Action, float)>();
    private float next = 20f;
    private bool loading, started;
    private Type nr, fg, up;
    private MenuUI pause;
    private AllSettingsUI menu;
    private long frames; private float startedAt; private ulong sourceAt, outputAt, evaluatedAt, upscaledAt;
    public ProbeBehaviour(IntPtr ptr) : base(ptr) { }
    private void Update()
    {
        ++frames;
        if (Time.realtimeSinceStartup < next) return;
        try
        {
            if (!started) { Begin(); return; }
            if (steps.Count == 0) { Probe.Write("DONE"); next = float.MaxValue; Application.Quit(); return; }
            var step = steps.Dequeue();
            next = Time.realtimeSinceStartup + step.wait;
            try { step.run(); }
            catch (Exception ex) { Probe.Write("ERROR in " + step.name + ": " + (ex.InnerException ?? ex)); }
        }
        catch (Exception ex) { Probe.Write("FATAL " + ex); next = float.MaxValue; Application.Quit(); }
    }
    private void Begin()
    {
        next = Time.realtimeSinceStartup + .5f;
        if (World.Any<SettingsMaster>() == null) return;
        if (!loading)
        {
            var press = UnityEngine.Object.FindObjectOfType<VPressStartUI>();
            if (press != null && press.Target != null) { press.Target.Discard(); return; }
            if (World.Any<TitleScreenUI>() == null || LoadingScreenUI.IsLoading || !ScenePreloader.IsLoadingCompleted) return;
            var slots = World.All<SaveSlot>().ToArraySlow(); SaveSlot chosen = null;
            foreach (var wanted in new[] { "PT npc-day", "PT tavern-room" })
                for (int i = 0; chosen == null && i < slots.Length; ++i) if (slots[i].Name.ToString() == wanted && slots[i].CanLoad()) chosen = slots[i];
            foreach (var wanted in new[] { "Slot_50", "QuickSave_3" })
                for (int i = 0; chosen == null && i < slots.Length; ++i) if (slots[i].ID == wanted && slots[i].CanLoad()) chosen = slots[i];
            for (int i = 0; chosen == null && i < slots.Length; ++i) if (slots[i].CanLoad()) chosen = slots[i];
            if (chosen == null) throw new Exception("No save can be loaded.");
            Probe.Write("LOAD " + chosen.ID + " " + chosen.Name);
            LoadSave.Get.Load(chosen, "normal-version probe"); loading = true; next += 2; return;
        }
        if (Hero.Current == null || LoadingScreenUI.IsLoading || !ScenePreloader.IsLoadingCompleted || UIStateStack.Instance?.State?.IsMapInteractive != true || Camera.main == null) return;
        World.Add(new SaveBlocker("normal-version probe"));
        Assembly Of(string name) => AppDomain.CurrentDomain.GetAssemblies().Single(a => a.GetName().Name == name);
        nr = Of("TGNeuralRendering").GetType("TGNeuralRendering.Session", true);
        fg = Of("TGFrameGeneration").GetType("TGFrameGeneration.Session", true);
        up = Of("TGBetterUpscaling").GetType("TGBetterUpscaling.UpscalerRow", true);
        Probe.Write("PLAY focused=" + Application.isFocused + " | FG: " + fg.GetProperty("StatusText", Probe.Any).GetValue(null));
        Phase("as-launched", () => { });
        Phase("up-dlss", () => Upscaler("DLSS"));
        Phase("fg-dlss-quality", () => Fg("DLSS", "Quality"));
        Phase("fg-dlss-performance", () => Fg("DLSS", "Performance"));
        Phase("fg-dlss-quality-again", () => Fg("DLSS", "Quality"));
        Phase("fg-fsr", () => Fg("FSR", "Quality"));
        Phase("fg-xess", () => Fg("XeSS", "Quality"));
        Phase("fg-dlss-for-the-row", () => Fg("DLSS", "Quality"));
        steps.Enqueue(("pause", () => pause = World.Add(new MenuUI()), 1f));
        steps.Enqueue(("options", () => MenuUI.OpenSettingUI(UnityEngine.Object.FindObjectOfType<VMenuUI>()), 1.5f));
        steps.Enqueue(("video tab", () => { menu = World.Any<AllSettingsUI>(); menu.ReplaceContent(SettingsTabType.GraphicSettings); }, 2f));
        steps.Enqueue(("video picture", () => ScreenCapture.CaptureScreenshot(Path.Combine(Probe.Folder, "video-tab.png")), 2f));
        steps.Enqueue(("close", () => { menu.CancelAndClose(); menu = null; pause.Close(); pause = null; Probe.Write("Video tab opened and closed with the DLSS rows"); }, 2f));
        Phase("fg-off", () => Fg("Off", "Quality"));
        Phase("up-fsr", () => Upscaler("FSR"));
        Phase("up-xess", () => Upscaler("XeSS"));
        Phase("up-dlss-back", () => Upscaler("DLSS"));
        Phase("nr-on", () => Nr("On"));
        Phase("nr-off", () => Nr("Off"));
        steps.Enqueue(("end", () => up.GetMethod("EndTest", Probe.Any).Invoke(null, null), 2f));
        started = true;
    }
    private void Phase(string name, Action apply)
    {
        steps.Enqueue((name, () => { apply(); Probe.Write("SET " + name); }, 6f));
        steps.Enqueue((name + " start", () => { frames = 0; startedAt = Time.realtimeSinceStartup; sourceAt = FgStatus("SourcePresents"); outputAt = FgStatus("OutputFramesReported"); evaluatedAt = NrStatus("Evaluated"); upscaledAt = UpStatus("FramesUpscaled"); }, 8f));
        steps.Enqueue((name + " end", () =>
        {
            float seconds = Time.realtimeSinceStartup - startedAt;
            Probe.Write("PHASE " + name + ": " + (frames / seconds).ToString("0.0") + " fps, focused=" + Application.isFocused +
                ", FG source +" + (FgStatus("SourcePresents") - sourceAt) + " output +" + (FgStatus("OutputFramesReported") - outputAt) + " status=" + FgStatus("DlssgStatus") + " failedMask=" + FgStatus("BackendFailedMask") +
                ", NR +" + (NrStatus("Evaluated") - evaluatedAt) + " failed=" + NrStatus("Failed") + ", upscaled +" + (UpStatus("FramesUpscaled") - upscaledAt) + " failed=" + UpStatus("FramesFailed") + " fallback=" + UpStatus("FramesFallback") +
                " | " + Text(Up(), "StatusText"));
            ScreenCapture.CaptureScreenshot(Path.Combine(Probe.Folder, name + ".png"));
        }, 1f));
    }
    private Type Up() => up.Assembly.GetType("TGBetterUpscaling.Session", true);
    private static string Text(Type session, string property) => session.GetProperty(property, Probe.Any)?.GetValue(null)?.ToString();
    private static ulong Number(object holder, string name) => holder == null ? 0UL : Convert.ToUInt64(holder.GetType().GetField(name, Probe.Any)?.GetValue(holder) ?? 0);
    private ulong FgStatus(string name) => Number(fg.GetField("Status", Probe.Any).GetValue(null), name);
    private ulong UpStatus(string name) => Number(Up().GetField("Status", Probe.Any).GetValue(null), name);
    private ulong NrStatus(string name)
    {
        var native = nr.GetField("Native", Probe.Any).GetValue(null);
        return Number(native?.GetType().GetField("Status", Probe.Any)?.GetValue(native), name);
    }
    private void Upscaler(string name)
    {
        var test = up.GetMethod("Test", Probe.Any);
        test.Invoke(null, new[] { Enum.Parse(test.GetParameters()[0].ParameterType, name) });
    }
    private void Fg(string backend, string quality)
    {
        var settings = fg.GetField("Settings", Probe.Any).GetValue(null);
        var apply = settings.GetType().GetMethod("Apply", Probe.Any);
        var types = apply.GetParameters();
        apply.Invoke(settings, new object[] { Enum.Parse(types[0].ParameterType, backend), Enum.Parse(types[1].ParameterType, quality), Enum.Parse(types[2].ParameterType, "Fixed"), 2, 2, 120f, Enum.Parse(types[6].ParameterType, "Reflex") });
    }
    private void Nr(string value) => ((ConfigEntry<string>)nr.GetField("Mode", Probe.Any).GetValue(null)).Value = value;
}
