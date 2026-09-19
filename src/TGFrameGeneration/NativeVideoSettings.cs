// SPDX-License-Identifier: MIT
// Native Video-tab integration: the mod's options are real game Setting/PrefOption objects,
// so they render with the game's own row prefabs, respect Apply Changes / Undo / Restore
// Defaults, and nest exactly like the built-in Dynamic Resolution or Subsurface Scattering rows.
#if BEPINEX_MONO
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using Awaken.TG.Main.Settings;
using Awaken.TG.Main.Settings.Graphics;
using Awaken.TG.Main.Settings.Options;
using Awaken.TG.Main.Settings.Options.Views;
using Awaken.TG.Main.Settings.Windows;
using Awaken.TG.MVC;
using UnityEngine;
namespace TGFrameGeneration
{
    internal sealed class FrameGenerationVideoSetting : Setting
    {
        private const string IdPrefix = "TGFG.Video.";
        private readonly Settings settings;
        private readonly VideoCapabilities caps;
        private readonly UpScaling upscaling; // the game's upscaler row
        private readonly PrefOption[] top;
        private readonly EnumArrowsOption fg, mode, multiplier, maxMultiplierOption, target, latency;
        private readonly FrameGenerationSetting[] backends; // Frame Generation row index -> technology
        private readonly LowLatencySetting[] latencies;     // Low Latency row index -> technology
        private readonly float[] targetValues;
        private readonly int multiplierRows, dlssMax;
        internal string Layout { get; }
        internal FrameGenerationVideoSetting(Settings settings, VideoCapabilities caps, UpScaling upscaling)
        {
            this.settings = settings; this.caps = caps; this.upscaling = upscaling;
            backends = caps.Backends;
            fg = Enum("FrameGeneration", "Frame Generation", 0, backends.Select(b => VideoCapabilities.Label(b)).ToArray());
            fg.AddTooltip(() => FrameGenerationTooltip(Selected));
            fg.SetInteractabilityFunction(Usable);

            // One multiplier shared by the technologies: rows up to the highest maximum any runtime reports;
            // higher values than the selected technology's own maximum are blocked and clamped on switching.
            multiplierRows = caps.HighestMultiplier;
            if (multiplierRows > 2)
            {
                multiplier = Enum("Multiplier", "Frame Multiplier", 0, Enumerable.Range(2, multiplierRows - 1).Select(m => VideoCapabilities.MultiplierLabel(m)).ToArray());
                multiplier.AddTooltip(() => "Frames shown per rendered frame; 2x is one generated frame per rendered frame. " +
                    VideoCapabilities.Label(Selected) + " supports up to " + caps.MaxMultiplier(Selected) + "x on this GPU.");
                multiplier.SetInteractabilityFunction(Usable);
            }
            dlssMax = caps.MaxMultiplier(FrameGenerationSetting.DLSS);
            var dlssRows = new PrefOption[0];
            if (caps.HasMode)
            {
                mode = Enum("Mode", "Frame Generation Mode", 0, "Fixed", "Dynamic");
                mode.AddTooltip(() => "Fixed: a constant multiplier. Dynamic: NVIDIA Multi Frame Generation picks the number of generated frames each frame to reach the target framerate.");
                var upTo = Enumerable.Range(2, dlssMax - 1).Select(m => VideoCapabilities.MaxMultiplierLabel(m)).ToArray();
                maxMultiplierOption = Enum("MaxMultiplier", "Max Frame Multiplier", upTo.Length - 1, upTo);
                maxMultiplierOption.AddTooltip(() => "Upper limit for Dynamic mode. NVIDIA's runtime may ignore it in Dynamic mode.");
                targetValues = settings.Targets;
                target = Enum("TargetFps", "Target Framerate", 0, targetValues.Select(v => VideoCapabilities.TargetLabel(v)).ToArray());
                target.AddTooltip(() => "Framerate Dynamic mode aims for. Auto follows the display's refresh rate, as does VSync when it is on.");
                foreach (var row in new[] { mode, maxMultiplierOption, target }) row.SetInteractabilityFunction(Usable);
                // Fixed shows only Frame Multiplier; Dynamic shows only its limit and target.
                var fixedRows = new PrefOption[] { multiplier };
                var dynamicRows = new PrefOption[] { maxMultiplierOption, target };
                var modeRows = SwitchRowsOption.Create(mode, i => i == 1 ? dynamicRows : fixedRows, fixedRows.Concat(dynamicRows));
                dlssRows = modeRows != null ? new PrefOption[] { modeRows }
                    : new PrefOption[] { mode, new DependentOption(multiplier, () => mode.OptionInt == 1, dynamicRows) }; // older game builds
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
            PrefOption fgRow = SwitchRowsOption.Create(fg, RowsFor, nested);
            if (fgRow == null) fgRow = nested.Length > 0 ? new DependentOption(fg, () => fg.OptionInt != 0, nested.Distinct().ToArray()) : (PrefOption)fg; // older game builds

            // One Low Latency selector with every technology this GPU offers. The frame generation choice
            // decides which of them may run; with a single one left, the row is greyed out.
            latencies = caps.Latencies;
            if (latencies.Length > 1)
            {
                latency = Enum("LowLatency", "Low Latency", 1, latencies.Select(l => VideoCapabilities.Label(l)).ToArray());
                latency.AddTooltip(LatencyTooltip);
                latency.SetInteractabilityFunction(() => caps.AllowedLatencies(Selected).Length > 1);
            }
            fg.onChange += _ => { UpdateConstraints(); RefreshInteractability(); };
            if (upscaling != null) upscaling.UpscalingTypeOption.onChange += _ => RefreshInteractability();
            top = latency != null ? new[] { fgRow, latency } : new[] { fgRow };
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
        public override string SettingName => "Frame Generation";
        public override IEnumerable<PrefOption> Options => top;
        protected override bool AutoApplyOnInit => false;
        private FrameGenerationSetting Selected => backends[Mathf.Clamp(fg.OptionInt, 0, backends.Length - 1)];
        // Frame generation uses the motion vectors and depth of DLSS upscaling, so its rows are usable while
        // DLSS is selected in the game's upscaler row (also before that choice is applied).
        private bool DlssSelected
        {
            get { try { return upscaling == null || upscaling.IsDLSSEnabled; } catch { return true; } }
        }
        private bool Usable() => Session.Bridge != null && Session.Bridge.Connected && caps.InputSource && DlssSelected;
        // The game checks a row's interactability when it builds the row. Like its anti-aliasing row does for
        // the upscaler, refresh ours when what they depend on changes.
        private void RefreshInteractability()
        {
            try
            {
                var ui = World.Any<AllSettingsUI>();
                if (ui == null) return;
                var leaves = new HashSet<PrefOption>(Leaves());
                foreach (var view in ui.Views.OfType<VFocusableSetting>().ToArray())
                    if (leaves.Contains(view.GenericOption)) view.UpdateInteractability();
            }
            catch { } // cosmetic only
        }
        private string FrameGenerationTooltip(FrameGenerationSetting selected)
        {
            if (!caps.InputSource)
                return "Not available on this GPU: frame generation uses the motion vectors and depth of the game's DLSS upscaling, which needs an NVIDIA GPU.";
            if (!DlssSelected)
                return "Select DLSS as the upscaler to use frame generation. It uses the motion vectors and depth of DLSS upscaling.";
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
            return text + " Uses the motion vectors and depth of DLSS upscaling. Pauses in menus and while the cursor is unlocked." +
                (Session.Bridge != null && Session.Bridge.Connected ? "" : " The TGFG native backend is not loaded.");
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
        private static ToggleOption Choice(string id, string label) => new ToggleOption(IdPrefix + id, label, false, false);
        private static EnumArrowsOption Enum(string id, string displayName, int defaultIndex, params string[] labels)
        {
            var toggles = new ToggleOption[labels.Length];
            for (int i = 0; i < labels.Length; i++) toggles[i] = Choice(id + "." + i, labels[i]);
            return new EnumArrowsOption(IdPrefix + id, displayName, toggles[Mathf.Clamp(defaultIndex, 0, toggles.Length - 1)], false, toggles);
        }
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
                multiplier.SetForbiddenOptions(multiplier.Options.Skip(max - 1).ToArray());
            }
            if (latency != null)
            {
                var allowed = caps.AllowedLatencies(selected);
                var current = latencies[Mathf.Clamp(latency.OptionInt, 0, latencies.Length - 1)];
                if (!allowed.Contains(current)) latency.OptionInt = Math.Max(0, Array.IndexOf(latencies, caps.PreferredLatency(selected)));
                var toggles = latency.Options.ToArray();
                latency.SetForbiddenOptions(Enumerable.Range(0, latencies.Length).Where(i => !allowed.Contains(latencies[i])).Select(i => toggles[i]).ToArray());
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
        protected override void OnApply()
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
            Session.Log.LogInfo("Video settings applied: FG=" + settings.FrameGeneration + " mode=" + settings.Mode +
                " multiplier=" + settings.Multiplier + "x dynamicMax=" + settings.MaxMultiplier + "x target=" + settings.TargetFps +
                " lowLatency=" + settings.LowLatency);
        }
    }
    // A row whose nested rows depend on its own value (the frame generation technology, or the DLSS
    // mode). The game's dependent view spawns OtherOptions when the condition turns true and removes
    // them when it turns false, so a change swaps the list and keeps the condition false for that
    // frame to rebuild the rows. Apply, Cancel and Restore Defaults cover every value's rows, shown or not.
    internal sealed class SwitchRowsOption : DependentOption
    {
        private static readonly FieldInfo OthersField = typeof(DependentOption).GetField("<OtherOptions>k__BackingField", BindingFlags.NonPublic | BindingFlags.Instance);
        private static readonly PrefOption[] None = new PrefOption[0];
        private sealed class Switch
        {
            internal EnumArrowsOption Selector;
            internal Func<int, PrefOption[]> Rows;
            internal SwitchRowsOption Owner;
            internal PrefOption[] Current;
            internal int Shown, HiddenFrame = -1;
            internal bool Show()
            {
                int wanted = Selector.OptionInt;
                if (wanted != Shown && Owner != null)
                {
                    Shown = wanted;
                    Current = Rows(wanted) ?? None;
                    OthersField.SetValue(Owner, Current);
                    HiddenFrame = Time.frameCount;
                }
                return Current.Length > 0 && Time.frameCount != HiddenFrame;
            }
        }
        private readonly Switch state;
        private readonly PrefOption[] every;
        private SwitchRowsOption(EnumArrowsOption selector, Switch rows, PrefOption[] every) : base(selector, rows.Show, rows.Current)
        {
            state = rows; this.every = every;
        }
        internal static SwitchRowsOption Create(EnumArrowsOption selector, Func<int, PrefOption[]> rows, IEnumerable<PrefOption> every)
        {
            if (OthersField == null || OthersField.FieldType != typeof(PrefOption[])) return null;
            int initial = selector.OptionInt;
            var sw = new Switch { Selector = selector, Rows = rows, Shown = initial, Current = rows(initial) ?? None };
            var option = new SwitchRowsOption(selector, sw, every.Where(o => o != null).Distinct().ToArray());
            sw.Owner = option;
            return option;
        }
        private IEnumerable<PrefOption> Every() => new[] { BaseOption }.Concat(every);
        public override bool WasChanged => Every().Any(o => o.WasChanged);
        public override bool IsSetToDefault => Every().All(o => o.IsSetToDefault);
        public override void ForceChange() { foreach (var o in Every()) o.ForceChange(); }
        public override void Apply() { foreach (var o in Every()) o.Apply(); }
        public override void Cancel() { foreach (var o in Every()) o.Cancel(); }
        public override void RestoreDefault() { foreach (var o in Every()) o.RestoreDefault(); }
    }
    internal static class NativeVideoSettings
    {
        private static float nextPoll, firstTick = -1;
        private static bool failed, subscribed, syncPending;
        private static FrameGenerationVideoSetting setting;
        private static SettingsMaster owner;
        // AddGraphicSetting and the backing list are not public; resolved once, like the other Video-tab mods do.
        private static readonly FieldInfo GraphicList = typeof(SettingsMaster).GetField("_graphicSettings", BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Instance);
        private static readonly MethodInfo AddGraphic = typeof(SettingsMaster).GetMethod("AddGraphicSetting", BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Instance, null, new[] { typeof(Setting) }, null);
        internal static string Status { get; private set; } = "Not registered yet.";
        internal static bool IsSettingsOpen
        {
            get { try { return World.Any<AllSettingsUI>() != null; } catch { return false; } }
        }
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
                var list = GraphicList != null ? GraphicList.GetValue(master) as List<ISetting> : null;
                if (list == null || AddGraphic == null) { failed = true; Status = "Game settings API not found (list=" + (list != null) + ", add=" + (AddGraphic != null) + "); rows not added."; Session.Log.LogWarning(Status); return; }
                bool open = World.Any<AllSettingsUI>() != null;
                if (setting != null && ReferenceEquals(master, owner) && list.Contains(setting))
                {
                    if (syncPending && !open) { syncPending = false; setting.SyncFromConfig(); }
                    return;
                }
                if (open) return; // never mutate the list while the screen is open
                var caps = Session.Settings.Capabilities;
                if (caps == null && now - firstTick < 20f) { Status = "Waiting for native capability report..."; return; }
                setting = new FrameGenerationVideoSetting(Session.Settings, caps ?? new VideoCapabilities(false, default(NativeStatus)), World.Any<UpScaling>());
                owner = master;
                syncPending = false;
                AddGraphic.Invoke(master, new object[] { setting });
                Reposition(list, setting);
                Status = "Registered in Video settings: " + setting.Layout + (caps != null ? "" : " (capabilities unknown at registration)");
                Session.Log.LogInfo(Status);
            }
            catch (Exception ex)
            {
                failed = true;
                Status = "Video rows failed; change the settings in FoA Mod Manager or " + Session.Settings.File.ConfigFilePath + " instead: " + ex.GetType().Name;
                Session.Log.LogWarning(Status + "\n" + ex);
            }
        }
        private static void Reposition(List<ISetting> list, ISetting setting)
        {
            // Directly after the game's upscaling row (or a DLSS 5 block if another mod added one).
            int anchor = -1;
            for (int i = 0; i < list.Count; i++)
            {
                string name = list[i].GetType().Name;
                if (name == "UpScaling" || name.IndexOf("Dlss5", StringComparison.OrdinalIgnoreCase) >= 0) anchor = i;
            }
            if (anchor < 0) return;
            list.Remove(setting);
            list.Insert(Math.Min(anchor + 1, list.Count), setting);
        }
    }
}
#else
namespace TGFrameGeneration
{
    internal static class NativeVideoSettings
    {
        internal static string Status => "Native Video rows are only implemented for the Mono runtime; change the settings in " + Session.Settings?.File.ConfigFilePath + ".";
        internal static bool IsSettingsOpen => false;
        internal static void Tick(float now) { }
    }
}
#endif
