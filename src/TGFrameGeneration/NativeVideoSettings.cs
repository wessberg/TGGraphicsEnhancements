// SPDX-License-Identifier: MIT
// Native Video-tab integration: the mod's options are real game Setting/PrefOption objects (see
// TG Graphics Bridge's VideoTab). Under IL2CPP the Setting and DependentOption subclasses are
// registered with Il2CppInterop (see Il2CppTypes); the rows and their rules live in VideoRows, a
// plain class, in both runtimes.
using System;
using System.Collections.Generic;
using System.Linq;
using Awaken.TG.Main.Settings;
using Awaken.TG.Main.Settings.Graphics;
using Awaken.TG.Main.Settings.Options;
using Awaken.TG.MVC;
using TGGraphicsBridge;
using UnityEngine;
#if BEPINEX_IL2CPP
using Il2CppInterop.Runtime.Injection;
#endif
namespace TGFrameGeneration
{
    // The game's Setting for TGFG's rows. It only forwards to VideoRows: under IL2CPP, every method of
    // this class is registered with IL2CPP, and only the overrides belong there.
    internal sealed class FrameGenerationVideoSetting : Setting
    {
        private readonly VideoRowsSlot slot;
#if BEPINEX_IL2CPP
        private readonly Il2CppSystem.Collections.Generic.IEnumerable<PrefOption> options;
        public FrameGenerationVideoSetting(IntPtr pointer) : base(pointer) { }
        internal FrameGenerationVideoSetting(VideoRowsSlot slot) : base(ClassInjector.DerivedConstructorPointer<FrameGenerationVideoSetting>())
        {
            ClassInjector.DerivedConstructorBody(this);
            Il2CppTypes.Construct(this, typeof(Setting), "NativeMethodInfoPtr__ctor_Protected_Void_0");
            this.slot = slot;
            options = slot.Options.Cast<Il2CppSystem.Collections.Generic.IEnumerable<PrefOption>>(); // the same list, refilled on a rebuild
        }
        public new string SettingName => "Frame Generation";
        public new Il2CppSystem.Collections.Generic.IEnumerable<PrefOption> Options => options;
        public new bool AutoApplyOnInit => false;
        public new void OnApply() => slot.Rows.Apply();
#else
        internal FrameGenerationVideoSetting(VideoRowsSlot slot) { this.slot = slot; }
        public override string SettingName => "Frame Generation";
        public override IEnumerable<PrefOption> Options => slot.Rows.Top;
        protected override bool AutoApplyOnInit => false;
        protected override void OnApply() => slot.Rows.Apply();
#endif
    }
    // The rows behind the setting. The game reads a setting's options each time it builds the screen,
    // applies or cancels, so new rows take the old ones' place without the setting changing.
    internal sealed class VideoRowsSlot
    {
        internal VideoRows Rows { get; private set; }
#if BEPINEX_IL2CPP
        internal readonly Il2CppSystem.Collections.Generic.List<PrefOption> Options = new Il2CppSystem.Collections.Generic.List<PrefOption>();
#endif
        internal VideoRowsSlot(VideoRows rows) { Set(rows); }
        internal void Set(VideoRows rows)
        {
            Rows = rows;
#if BEPINEX_IL2CPP
            Options.Clear();
            foreach (var option in rows.Top) Options.Add(option);
#endif
        }
    }
    internal sealed class VideoRows
    {
        private const string IdPrefix = "TGFG.Video.";
        private readonly Settings settings;
        private readonly VideoCapabilities caps;
        private readonly UpScaling upscaling; // the game's upscaler row
        private readonly EnumArrowsOption fg, mode, multiplier, maxMultiplierOption, target, latency;
        private readonly ToggleOption[] multiplierChoices, latencyChoices;
        private readonly FrameGenerationSetting[] backends; // Frame Generation row index -> technology
        private readonly LowLatencySetting[] latencies;     // Low Latency row index -> technology
        private readonly float[] targetValues;
        private readonly int multiplierRows, dlssMax;
        internal PrefOption[] Top { get; }
        internal string Layout { get; }
        internal bool Known => caps.Known; // built from the native side's report, not before it
        // The switching rows, so a rebuild can withdraw their IL2CPP visibility conditions.
        internal readonly List<DependentOption> Switches = new List<DependentOption>();
        internal VideoRows(Settings settings, VideoCapabilities caps, UpScaling upscaling)
        {
            this.settings = settings; this.caps = caps; this.upscaling = upscaling;
            backends = caps.Backends;
            // Without a report from the native side the row's only choice reads Off, greyed out, with the
            // bridge's alert icon and the reason as its tooltip; the stored value is kept.
            fg = Enum("FrameGeneration", "Frame Generation", 0, out _, caps.Known ? backends.Select(b => VideoCapabilities.Label(b)).ToArray() : new[] { "Off" });
            Tooltip(fg, () => FrameGenerationTooltip(Selected));
            fg.SetInteractabilityFunction((Func<bool>)Usable);
            RegisterAlerts(fg);

            // One multiplier shared by the technologies: rows up to the highest maximum any runtime reports;
            // higher values than the selected technology's own maximum are blocked and clamped on switching.
            multiplierRows = caps.HighestMultiplier;
            if (multiplierRows > 2)
            {
                multiplier = Enum("Multiplier", "Frame Multiplier", 0, out multiplierChoices, Enumerable.Range(2, multiplierRows - 1).Select(m => VideoCapabilities.MultiplierLabel(m)).ToArray());
                Tooltip(multiplier, () => "Frames shown per rendered frame; 2x is one generated frame per rendered frame. " +
                    VideoCapabilities.Label(Selected) + " supports up to " + caps.MaxMultiplier(Selected) + "x on this GPU.");
                multiplier.SetInteractabilityFunction((Func<bool>)Usable);
            }
            dlssMax = caps.MaxMultiplier(FrameGenerationSetting.DLSS);
            var dlssRows = new PrefOption[0];
            if (caps.HasMode)
            {
                mode = Enum("Mode", "Frame Generation Mode", 0, out _, "Fixed", "Dynamic");
                Tooltip(mode, () => "Fixed: a constant multiplier. Dynamic: NVIDIA Multi Frame Generation picks the number of generated frames each frame to reach the target framerate.");
                var upTo = Enumerable.Range(2, dlssMax - 1).Select(m => VideoCapabilities.MaxMultiplierLabel(m)).ToArray();
                maxMultiplierOption = Enum("MaxMultiplier", "Max Frame Multiplier", upTo.Length - 1, out _, upTo);
                Tooltip(maxMultiplierOption, () => "Upper limit for Dynamic mode. NVIDIA's runtime may ignore it in Dynamic mode.");
                targetValues = settings.Targets;
                target = Enum("TargetFps", "Target Framerate", 0, out _, targetValues.Select(v => VideoCapabilities.TargetLabel(v)).ToArray());
                Tooltip(target, () => "Framerate Dynamic mode aims for. Auto follows the display's refresh rate, as does VSync when it is on.");
                foreach (var row in new[] { mode, maxMultiplierOption, target }) row.SetInteractabilityFunction((Func<bool>)Usable);
                // Fixed shows only Frame Multiplier; Dynamic shows only its limit and target.
                var fixedRows = new PrefOption[] { multiplier };
                var dynamicRows = new PrefOption[] { maxMultiplierOption, target };
                var modeRows = SwitchRowsOption.Create(mode, i => i == 1 ? dynamicRows : fixedRows, fixedRows.Concat(dynamicRows));
                if (modeRows != null) Switches.Add(modeRows);
                dlssRows = modeRows != null ? new PrefOption[] { modeRows }
                    : new PrefOption[] { mode, new DependentOption(multiplier, (Func<bool>)(() => mode.OptionInt == 1), dynamicRows) }; // older game builds
            }
            else if (dlssMax > 2) dlssRows = new PrefOption[] { multiplier };
            var multiplierOnly = multiplier != null ? new PrefOption[] { multiplier } : new PrefOption[0];
            PrefOption[] RowsFor(int index)
            {
                var backend = backends[Mathf.Clamp(index, 0, backends.Length - 1)];
                if (backend == FrameGenerationSetting.DLSS) return dlssRows;
                return backend != FrameGenerationSetting.Off && caps.MaxMultiplier(backend) > 2 ? multiplierOnly : new PrefOption[0];
            }
            var nested = dlssRows.Concat(multiplierOnly).ToArray();
            var fgSwitch = SwitchRowsOption.Create(fg, RowsFor, nested);
            if (fgSwitch != null) Switches.Add(fgSwitch);
            PrefOption fgRow = fgSwitch;
            if (fgRow == null) fgRow = nested.Length > 0 ? new DependentOption(fg, (Func<bool>)(() => fg.OptionInt != 0), nested.Distinct().ToArray()) : (PrefOption)fg; // older game builds

            // One Low Latency selector with every technology this GPU offers. The frame generation choice
            // decides which of them may run; with a single one left, the row is greyed out.
            latencies = caps.Latencies;
            if (latencies.Length > 1)
            {
                latency = Enum("LowLatency", "Low Latency", 1, out latencyChoices, latencies.Select(l => VideoCapabilities.Label(l)).ToArray());
                Tooltip(latency, LatencyTooltip);
                latency.SetInteractabilityFunction((Func<bool>)(() => caps.AllowedLatencies(Selected).Length > 1));
            }
            fg.onChange += (Action<ToggleOption>)(_ => { UpdateConstraints(); RefreshInteractability(); });
            if (upscaling != null) upscaling.UpscalingTypeOption.onChange += (Action<ToggleOption>)(_ => RefreshInteractability());
            Top = latency != null ? new[] { fgRow, latency } : new[] { fgRow };
            string rows(FrameGenerationSetting b) => b == FrameGenerationSetting.DLSS
                ? (mode != null ? "Mode > [Fixed: Multiplier(2-" + dlssMax + "x) | Dynamic: MaxMultiplier + Target(" + targetValues.Length + ")]"
                    : dlssMax > 2 ? "Multiplier(2-" + dlssMax + "x)" : "2x only")
                : caps.MaxMultiplier(b) > 2 ? "Multiplier(2-" + caps.MaxMultiplier(b) + "x)" : "2x only";
            Layout = "FG[" + string.Join(",", backends.Select(b => VideoCapabilities.Label(b)).ToArray()) + "] > " +
                string.Join("; ", backends.Skip(1).Select(b => VideoCapabilities.Label(b) + ": " + rows(b)).ToArray()) +
                (latency != null ? " | Low Latency[" + string.Join(",", latencies.Select(l => VideoCapabilities.Label(l)).ToArray()) + "]" : "") +
                (upscaling != null ? "" : " (upscaler row not found)");
            SyncFromConfig();
        }
        private FrameGenerationSetting Selected => backends[Mathf.Clamp(fg.OptionInt, 0, backends.Length - 1)];
        // Without TGFG's HDRP custom pass, frame generation has only the DLSS upscaler's motion vectors and
        // depth, so its rows are usable while DLSS is selected in the upscaler row (also before Apply).
        private bool DlssSelected
        {
            get { try { return HdrpInputs.Available || upscaling == null || upscaling.IsDLSSEnabled; } catch { return true; } }
        }
        private bool Usable() => caps.Known && Session.Bridge != null && Session.Bridge.Connected && caps.InputSource && DlssSelected;
        // The game checks a row's interactability when it builds the row. Like its anti-aliasing row does for
        // the upscaler, refresh ours when what they depend on changes.
        private void RefreshInteractability() => VideoTab.RefreshInteractability(Leaves());
        // The bridge shows this row's problems as an alert icon after its name.
        private static void RegisterAlerts(PrefOption row)
        {
            // In a method of its own: the runtime resolves the bridge's members when it compiles the method that
            // uses them, so a bridge from before the alerts fails there, inside this try.
            try { Register(row); } catch { }
        }
        private static void Register(PrefOption row) => RowAlerts.Register(AlertRow.FrameGeneration, row);
        private static string Alert()
        {
            try { return AlertText(); } catch { return ""; }
        }
        private static string AlertText() => RowAlerts.Text(AlertRow.FrameGeneration);
        private string FrameGenerationTooltip(FrameGenerationSetting selected)
        {
            // A problem comes first, and alone: the game's tooltip is a narrow column.
            string alert = Alert();
            if (alert.Length > 0) return alert;
            if (!caps.Known)
                return Session.UnavailableTip(); // the full explanation is in the log and the troubleshooting report
            if (!caps.InputSource)
                return "Not available on this GPU: without HDRP inputs, frame generation uses the motion vectors and depth of DLSS upscaling, which needs an NVIDIA GPU.";
            if (!DlssSelected)
                return "Select DLSS as the upscaler to use frame generation. Without HDRP inputs it uses the motion vectors and depth of DLSS upscaling.";
            string text;
            switch (selected)
            {
                case FrameGenerationSetting.DLSS:
                    text = "NVIDIA DLSS Frame Generation, up to " + dlssMax + "x on this GPU" + (mode != null ? ", with Dynamic Multi Frame Generation" : "") + ". Requires NVIDIA Reflex.";
                    break;
                case FrameGenerationSetting.FSR:
                    text = "AMD FSR Frame Generation: one generated frame per rendered frame. Runs with NVIDIA Reflex or AMD Anti-Lag 2.";
                    break;
                case FrameGenerationSetting.XeSS:
                    text = "Intel XeSS Frame Generation, up to " + caps.MaxMultiplier(FrameGenerationSetting.XeSS) + "x on this GPU (more than 2x needs an Intel Arc GPU). Always runs with Intel XeLL.";
                    break;
                default:
                    text = "Generates extra frames between the rendered ones. Available here: " +
                        (backends.Length > 1 ? string.Join(", ", backends.Skip(1).Select(b => VideoCapabilities.Label(b)).ToArray()) : "none") + ".";
                    break;
            }
            // The game's tooltip is a narrow column: a finding takes the place of the general note, not a place after it.
            string findings = Findings();
            return text + (findings.Length > 0 ? findings : " Uses the game's own motion vectors and depth, from DLSS upscaling when it's on. Pauses in menus and while the cursor is unlocked.");
        }
        // Why a technology isn't offered on this PC (DLSS, XeSS); problems come from the bridge's alerts instead.
        private static string Findings()
        {
            // In a method of its own: the runtime resolves the bridge's members when it compiles the method
            // that uses them, so a bridge from before the notices fails there, inside this try.
            try { return FindingsFromBridge(); }
            catch { return ""; }
        }
        private static string FindingsFromBridge()
        {
            string dlss = Explain.DlssMissing(Session.HaveStatus ? Session.Status.GpuVendor : 0), xess = Explain.XessMissing();
            return (dlss.Length > 0 ? " " + dlss : "") + (xess.Length > 0 ? " " + xess : "");
        }
        private string LatencyTooltip()
        {
            var selected = Selected;
            string rule = selected == FrameGenerationSetting.DLSS ? "DLSS Frame Generation requires NVIDIA Reflex. "
                : selected == FrameGenerationSetting.XeSS ? "XeSS Frame Generation always runs with Intel XeLL. "
                : selected != FrameGenerationSetting.Off ? "Stays on while frame generation is on. " : "";
            return rule + "Cuts the delay between your input and the picture on screen by starting each frame just in time. " +
                "Reflex + Boost also keeps GPU clocks up when the CPU is the limit." +
                (caps.XeLL && !caps.XeLLStandalone ? " Intel XeLL works on this GPU only together with XeSS Frame Generation." : "");
        }
        private static void Tooltip(PrefOption option, Func<string> text) => VideoTab.Tooltip(option, text);
        private static EnumArrowsOption Enum(string id, string displayName, int defaultIndex, out ToggleOption[] choices, params string[] labels) =>
            VideoTab.Enum(IdPrefix, id, displayName, defaultIndex, out choices, labels);
        private IEnumerable<EnumArrowsOption> Leaves()
        {
            yield return fg;
            if (mode != null) yield return mode;
            if (multiplier != null) yield return multiplier;
            if (maxMultiplierOption != null) yield return maxMultiplierOption;
            if (target != null) yield return target;
            if (latency != null) yield return latency;
        }
        // The frame generation choice constrains the other rows; values it rules out are clamped.
        private void UpdateConstraints()
        {
            var selected = Selected;
            if (multiplier != null && selected != FrameGenerationSetting.Off)
            {
                int max = Math.Max(2, caps.MaxMultiplier(selected));
                if (multiplier.OptionInt + 2 > max) multiplier.OptionInt = max - 2;
                multiplier.SetForbiddenOptions(multiplierChoices.Skip(max - 1).ToArray());
            }
            if (latency != null)
            {
                var allowed = caps.AllowedLatencies(selected);
                var current = latencies[Mathf.Clamp(latency.OptionInt, 0, latencies.Length - 1)];
                if (!allowed.Contains(current)) latency.OptionInt = Math.Max(0, Array.IndexOf(latencies, caps.PreferredLatency(selected)));
                latency.SetForbiddenOptions(Enumerable.Range(0, latencies.Length).Where(i => !allowed.Contains(latencies[i])).Select(i => latencyChoices[i]).ToArray());
            }
        }
        internal void SyncFromConfig()
        {
            fg.OptionInt = Math.Max(0, Array.IndexOf(backends, settings.FrameGeneration));
            if (mode != null) mode.OptionInt = settings.Mode == FrameGenerationMode.Dynamic ? 1 : 0;
            if (multiplier != null) multiplier.OptionInt = Mathf.Clamp(settings.Multiplier - 2, 0, multiplierRows - 2);
            if (maxMultiplierOption != null) maxMultiplierOption.OptionInt = Mathf.Clamp(settings.MaxMultiplier - 2, 0, dlssMax - 2);
            if (target != null) target.OptionInt = Math.Max(0, Array.IndexOf(targetValues, VideoCapabilities.NearestTarget(targetValues, settings.TargetFps)));
            if (latency != null) latency.OptionInt = Math.Max(0, Array.IndexOf(latencies, settings.LowLatency));
            UpdateConstraints();
            // Applying stores the in-game value so the game does not report unsaved changes.
            foreach (var option in Leaves()) option.Apply();
        }
        internal void Apply()
        {
            var backend = Selected;
            var chosen = latency != null ? latencies[Mathf.Clamp(latency.OptionInt, 0, latencies.Length - 1)] : LowLatencySetting.Off;
            // The row already blocks other choices; this keeps a hand-edited state consistent as well.
            if (!caps.AllowedLatencies(backend).Contains(chosen)) chosen = caps.PreferredLatency(backend);
            settings.Apply(backend,
                mode != null && mode.OptionInt == 1 ? FrameGenerationMode.Dynamic : FrameGenerationMode.Fixed,
                multiplier != null ? multiplier.OptionInt + 2 : 2,
                maxMultiplierOption != null ? maxMultiplierOption.OptionInt + 2 : settings.MaxMultiplier,
                target != null ? targetValues[Mathf.Clamp(target.OptionInt, 0, targetValues.Length - 1)] : settings.TargetFps,
                chosen);
            DisplayMode.ArmFromVideoApply();
            HybridVsync.ArmFromVideoApply();
            Session.Log.LogInfo("Video settings applied: FG=" + settings.FrameGeneration + " mode=" + settings.Mode +
                " multiplier=" + settings.Multiplier + "x dynamicMax=" + settings.MaxMultiplier + "x target=" + settings.TargetFps +
                " lowLatency=" + settings.LowLatency);
        }
    }
    internal static class NativeVideoSettings
    {
        private static float nextPoll, firstTick = -1;
        private static bool failed, subscribed, syncPending;
        private static VideoRows rows;
        private static VideoRowsSlot slot;
        private static FrameGenerationVideoSetting setting;
        private static SettingsMaster owner;
        internal static string Status { get; private set; } = "Not registered yet.";
        internal static bool IsSettingsOpen => VideoTab.IsSettingsOpen;
        internal static void Tick(float now)
        {
            if (failed || now < nextPoll) return;
            nextPoll = now + 0.5f;
            if (firstTick < 0) firstTick = now;
            try
            {
                if (!Session.Settings.NativeMenu.Value) { Status = "Disabled by config (Advanced > Add rows to Video settings)."; return; }
                if (!subscribed)
                {
                    // A mod manager, the config file or the emergency key changed a value: the rows follow once the
                    // settings screen is closed, never under the player's hands.
                    subscribed = true;
                    Session.Settings.Changed += () => syncPending = true;
                }
                var master = World.Any<SettingsMaster>();
                if (master == null) { Status = "Waiting for the game's settings model."; return; }
                var list = VideoTab.GraphicSettings(master);
                if (list == null) { failed = true; Status = "Game settings API not found (SettingsMaster._graphicSettings, AddGraphicSetting); rows not added."; Session.Log.LogWarning(Status); return; }
                bool open = VideoTab.IsSettingsOpen;
                if (setting != null && VideoTab.Same(master, owner) && VideoTab.IndexOf(list, setting) >= 0)
                {
                    // Registered before the native side reported: once it has, its rows replace the empty ones.
                    if (!rows.Known && Session.Settings.Capabilities != null && !open)
                    {
                        Build(Session.Settings.Capabilities);
                        slot.Set(rows);
                        syncPending = false;
                        Status = "Video rows rebuilt with the reported capabilities: " + rows.Layout;
                        Session.Log.LogInfo(Status);
                        return;
                    }
                    if (syncPending && !open) { syncPending = false; rows.SyncFromConfig(); }
                    return;
                }
                if (open) return; // never mutate the list while the screen is open
                var caps = Session.Settings.Capabilities;
                // The GPU's report comes within seconds when the native part runs, so the rows wait for it and go in
                // complete. Without the native part it never comes: the row goes in at once, reading Off with the
                // alert icon and the reason, so a player who opens the settings right away still finds it.
                if (caps == null && NativeRunning() && now - firstTick < 20f) { Status = "Waiting for native capability report..."; return; }
#if BEPINEX_IL2CPP
                Il2CppTypes.Register<SwitchRowsOption>();
                Il2CppTypes.Register<FrameGenerationVideoSetting>(genericBase: true);
#endif
                Build(caps);
                slot = new VideoRowsSlot(rows);
                setting = new FrameGenerationVideoSetting(slot);
                owner = master;
                syncPending = false;
                VideoTab.AddGraphicSetting(master, setting);
                // Directly after the game's upscaling row (or a DLSS 5 block if another mod added one).
                VideoTab.Reposition(list, setting, "UpScaling", "*Dlss5");
                // Without a report the row offers Off only, greyed out, and its tooltip says why.
                Status = "Registered in Video settings: " + rows.Layout + (caps != null ? "" : "; frame generation unavailable. " + Session.UnavailableReason());
                Session.Log.LogInfo(Status);
            }
            catch (Exception ex)
            {
                failed = true;
                Status = "Video rows failed; change the settings in FoA Mod Manager or " + Session.Settings.File.ConfigFilePath + " instead: " + ex.GetType().Name;
                Session.Log.LogWarning(Status + "\n" + ex);
            }
        }
        // This mod's native part is loaded (the bridge loads it at startup, before any plugin runs), and the
        // bridge's with it. Only then can a report come; the bridge alone, for TG Better Upscaling, is not enough.
        private static bool NativeRunning()
        {
            if (!NativeBridge.IsLoaded("TGFG.Native.dll")) return false;
            try { return GraphicsBridge.Loaded; } catch { return true; }
        }
        private static void Build(VideoCapabilities caps)
        {
#if BEPINEX_IL2CPP
            if (rows != null) foreach (var old in rows.Switches) Il2CppOptionConditions.Remove(old);
#endif
#if BEPINEX_IL2CPP
            var upscaling = VideoTab.GraphicSetting<UpScaling>("UpScaling"); // not World.Any<UpScaling>(): see there
#else
            var upscaling = World.Any<UpScaling>();
#endif
            rows = new VideoRows(Session.Settings, caps ?? new VideoCapabilities(false, default(NativeStatus)), upscaling);
        }
    }
}
