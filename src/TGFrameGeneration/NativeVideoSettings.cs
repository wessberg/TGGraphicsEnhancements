// SPDX-License-Identifier: MIT
// Native Video-tab integration: the mod's options are real game Setting/PrefOption objects,
// so they render with the game's own row prefabs, respect Apply Changes / Undo / Restore
// Defaults, and nest exactly like the built-in Dynamic Resolution or Subsurface Scattering rows.
// Under IL2CPP the Setting and DependentOption subclasses are registered with Il2CppInterop (see
// Il2CppTypes); the rows and their rules live in VideoRows, a plain class, in both runtimes.
using System;
using System.Collections.Generic;
using System.Linq;
using Awaken.TG.Main.Settings;
using Awaken.TG.Main.Settings.Graphics;
using Awaken.TG.Main.Settings.Options;
using Awaken.TG.Main.Settings.Options.Views;
using Awaken.TG.Main.Settings.Windows;
using Awaken.TG.MVC;
using UnityEngine;
#if BEPINEX_IL2CPP
using Il2CppInterop.Runtime;
using Il2CppInterop.Runtime.Injection;
using Il2CppInterop.Runtime.InteropTypes;
using Il2CppInterop.Runtime.InteropTypes.Arrays;
using SettingList = Il2CppSystem.Collections.Generic.List<Awaken.TG.Main.Settings.ISetting>;
#else
using System.Reflection;
using SettingList = System.Collections.Generic.List<Awaken.TG.Main.Settings.ISetting>;
#endif
namespace TGFrameGeneration
{
    // The game's Setting for TGFG's rows. It only forwards to VideoRows: under IL2CPP, every method of
    // this class is registered with IL2CPP, and only the overrides belong there.
    internal sealed class FrameGenerationVideoSetting : Setting
    {
        private readonly VideoRows rows;
#if BEPINEX_IL2CPP
        private readonly Il2CppSystem.Collections.Generic.IEnumerable<PrefOption> options;
        public FrameGenerationVideoSetting(IntPtr pointer) : base(pointer) { }
        internal FrameGenerationVideoSetting(VideoRows rows) : base(ClassInjector.DerivedConstructorPointer<FrameGenerationVideoSetting>())
        {
            ClassInjector.DerivedConstructorBody(this);
            Il2CppTypes.Construct(this, typeof(Setting), "NativeMethodInfoPtr__ctor_Protected_Void_0");
            this.rows = rows;
            var list = new Il2CppSystem.Collections.Generic.List<PrefOption>();
            foreach (var option in rows.Top) list.Add(option);
            options = list.Cast<Il2CppSystem.Collections.Generic.IEnumerable<PrefOption>>();
        }
        public new string SettingName => "Frame Generation";
        public new Il2CppSystem.Collections.Generic.IEnumerable<PrefOption> Options => options;
        public new bool AutoApplyOnInit => false;
        public new void OnApply() => rows.Apply();
#else
        internal FrameGenerationVideoSetting(VideoRows rows) { this.rows = rows; }
        public override string SettingName => "Frame Generation";
        public override IEnumerable<PrefOption> Options => rows.Top;
        protected override bool AutoApplyOnInit => false;
        protected override void OnApply() => rows.Apply();
#endif
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
        internal VideoRows(Settings settings, VideoCapabilities caps, UpScaling upscaling)
        {
            this.settings = settings; this.caps = caps; this.upscaling = upscaling;
            backends = caps.Backends;
            fg = Enum("FrameGeneration", "Frame Generation", 0, out _, backends.Select(b => VideoCapabilities.Label(b)).ToArray());
            Tooltip(fg, () => FrameGenerationTooltip(Selected));
            fg.SetInteractabilityFunction((Func<bool>)Usable);

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
            PrefOption fgRow = SwitchRowsOption.Create(fg, RowsFor, nested);
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
        private bool Usable() => Session.Bridge != null && Session.Bridge.Connected && caps.InputSource && DlssSelected;
        // The game checks a row's interactability when it builds the row. Like its anti-aliasing row does for
        // the upscaler, refresh ours when what they depend on changes.
        private void RefreshInteractability()
        {
            try
            {
                var ui = World.Any<AllSettingsUI>();
                if (ui == null) return;
                var leaves = new HashSet<object>(Leaves().Select(o => NativeVideoSettings.Identity(o)));
                foreach (var view in NativeVideoSettings.FocusableViews(ui))
                    if (leaves.Contains(NativeVideoSettings.Identity(view.GenericOption))) view.UpdateInteractability();
            }
            catch { } // cosmetic only
        }
        private string FrameGenerationTooltip(FrameGenerationSetting selected)
        {
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
            return text + " Uses the game's own motion vectors and depth, from DLSS upscaling when it's on. Pauses in menus and while the cursor is unlocked." +
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
        // The game asks a row for its tooltip text when it shows the row's description.
        private static void Tooltip(PrefOption option, Func<string> text)
        {
#if BEPINEX_IL2CPP
            // Il2CppInterop's delegate bridge (DelegateSupport) turns a returned string into the address of the
            // managed string, not an IL2CPP string: it maps the return type to IntPtr before its string check.
            // The game then crashes in String.Trim while building the tooltip. The bridge compares only the
            // parameter types, so this function returns the IL2CPP string pointer itself.
            option.AddTooltip(DelegateSupport.ConvertDelegate<Il2CppSystem.Func<string>>(new Func<IntPtr>(() => IL2CPP.ManagedStringToIl2Cpp(text()))));
#else
            option.AddTooltip(text);
#endif
        }
        private static ToggleOption Choice(string id, string label) => new ToggleOption(IdPrefix + id, label, false, false);
        private static EnumArrowsOption Enum(string id, string displayName, int defaultIndex, out ToggleOption[] choices, params string[] labels)
        {
            choices = new ToggleOption[labels.Length];
            for (int i = 0; i < labels.Length; i++) choices[i] = Choice(id + "." + i, labels[i]);
            return new EnumArrowsOption(IdPrefix + id, displayName, choices[Mathf.Clamp(defaultIndex, 0, choices.Length - 1)], false, choices);
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
#if !BEPINEX_IL2CPP
        private static readonly FieldInfo OthersField = typeof(DependentOption).GetField("<OtherOptions>k__BackingField", BindingFlags.NonPublic | BindingFlags.Instance);
#endif
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
#if BEPINEX_IL2CPP
                    Owner._OtherOptions_k__BackingField = new Il2CppReferenceArray<PrefOption>(Current);
#else
                    OthersField.SetValue(Owner, Current);
#endif
                    HiddenFrame = Time.frameCount;
                }
                return Current.Length > 0 && Time.frameCount != HiddenFrame;
            }
        }
        private readonly PrefOption[] all; // the selector and every row any of its values shows
#if BEPINEX_IL2CPP
        public SwitchRowsOption(IntPtr pointer) : base(pointer) { }
        private SwitchRowsOption(EnumArrowsOption selector, Switch rows, PrefOption[] every) : base(ClassInjector.DerivedConstructorPointer<SwitchRowsOption>())
        {
            ClassInjector.DerivedConstructorBody(this);
            Il2CppTypes.Construct(this, typeof(DependentOption), "NativeMethodInfoPtr__ctor_Public_Void_PrefOption_Func_1_Boolean_Il2CppReferenceArray_1_PrefOption_0",
                selector, (Il2CppSystem.Func<bool>)(Func<bool>)rows.Show, new Il2CppReferenceArray<PrefOption>(rows.Current));
            all = new PrefOption[] { selector }.Concat(every).ToArray();
        }
        public new bool WasChanged => all.Any(o => o.WasChanged);
        public new bool IsSetToDefault => all.All(o => o.IsSetToDefault);
        public new void ForceChange() { foreach (var o in all) o.ForceChange(); }
        public new void Apply() { foreach (var o in all) o.Apply(); }
        public new void Cancel() { foreach (var o in all) o.Cancel(); }
        public new void RestoreDefault() { foreach (var o in all) o.RestoreDefault(); }
#else
        private SwitchRowsOption(EnumArrowsOption selector, Switch rows, PrefOption[] every) : base(selector, rows.Show, rows.Current)
        {
            all = new PrefOption[] { selector }.Concat(every).ToArray();
        }
        public override bool WasChanged => all.Any(o => o.WasChanged);
        public override bool IsSetToDefault => all.All(o => o.IsSetToDefault);
        public override void ForceChange() { foreach (var o in all) o.ForceChange(); }
        public override void Apply() { foreach (var o in all) o.Apply(); }
        public override void Cancel() { foreach (var o in all) o.Cancel(); }
        public override void RestoreDefault() { foreach (var o in all) o.RestoreDefault(); }
#endif
        internal static SwitchRowsOption Create(EnumArrowsOption selector, Func<int, PrefOption[]> rows, IEnumerable<PrefOption> every)
        {
#if !BEPINEX_IL2CPP
            if (OthersField == null || OthersField.FieldType != typeof(PrefOption[])) return null;
#endif
            int initial = selector.OptionInt;
            var sw = new Switch { Selector = selector, Rows = rows, Shown = initial, Current = rows(initial) ?? None };
            var option = new SwitchRowsOption(selector, sw, every.Where(o => o != null).Distinct().ToArray());
            sw.Owner = option;
            return option;
        }
    }
    internal static class NativeVideoSettings
    {
        private static float nextPoll, firstTick = -1;
        private static bool failed, subscribed, syncPending;
        private static VideoRows rows;
        private static FrameGenerationVideoSetting setting;
        private static SettingsMaster owner;
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
                var list = GraphicSettings(master);
                if (list == null) { failed = true; Status = "Game settings API not found (SettingsMaster._graphicSettings, AddGraphicSetting); rows not added."; Session.Log.LogWarning(Status); return; }
                bool open = World.Any<AllSettingsUI>() != null;
                if (setting != null && Same(master, owner) && IndexOf(list, setting) >= 0)
                {
                    if (syncPending && !open) { syncPending = false; rows.SyncFromConfig(); }
                    return;
                }
                if (open) return; // never mutate the list while the screen is open
                var caps = Session.Settings.Capabilities;
                if (caps == null && now - firstTick < 20f) { Status = "Waiting for native capability report..."; return; }
#if BEPINEX_IL2CPP
                Il2CppTypes.Register<SwitchRowsOption>();
                Il2CppTypes.Register<FrameGenerationVideoSetting>(genericBase: true);
#endif
                rows = new VideoRows(Session.Settings, caps ?? new VideoCapabilities(false, default(NativeStatus)), World.Any<UpScaling>());
                setting = new FrameGenerationVideoSetting(rows);
                owner = master;
                syncPending = false;
                AddGraphicSetting(master, setting);
                Reposition(list, setting);
                Status = "Registered in Video settings: " + rows.Layout + (caps != null ? "" : " (capabilities unknown at registration)");
                Session.Log.LogInfo(Status);
            }
            catch (Exception ex)
            {
                failed = true;
                Status = "Video rows failed; change the settings in FoA Mod Manager or " + Session.Settings.File.ConfigFilePath + " instead: " + ex.GetType().Name;
                Session.Log.LogWarning(Status + "\n" + ex);
            }
        }
        private static int IndexOf(SettingList list, Setting item)
        {
            for (int i = 0; i < list.Count; i++) if (Same(list[i], item)) return i;
            return -1;
        }
        private static void Reposition(SettingList list, Setting item)
        {
            // Directly after the game's upscaling row (or a DLSS 5 block if another mod added one).
            int anchor = -1, own = -1;
            for (int i = 0; i < list.Count; i++)
            {
                if (Same(list[i], item)) { own = i; continue; }
                string name = TypeName(list[i]);
                if (name == "UpScaling" || name.IndexOf("Dlss5", StringComparison.OrdinalIgnoreCase) >= 0) anchor = i;
            }
            if (anchor < 0 || own < 0) return;
            var entry = list[own];
            list.RemoveAt(own);
            if (own < anchor) anchor--;
            list.Insert(Math.Min(anchor + 1, list.Count), entry);
        }
#if BEPINEX_IL2CPP
        // Every IL2CPP read returns a new wrapper object, so an object's identity is its IL2CPP pointer.
        internal static object Identity(Il2CppObjectBase o) => o?.Pointer ?? IntPtr.Zero;
        private static bool Same(Il2CppObjectBase a, Il2CppObjectBase b) => a != null && b != null && a.Pointer == b.Pointer;
        private static SettingList GraphicSettings(SettingsMaster master) => master._graphicSettings;
        private static void AddGraphicSetting(SettingsMaster master, Setting item) => master.AddGraphicSetting(item);
        private static string TypeName(ISetting item) => IL2CPP.il2cpp_class_get_name_(item.ObjectClass);
        internal static List<VFocusableSetting> FocusableViews(AllSettingsUI ui)
        {
            var views = new List<VFocusableSetting>();
            var enumerator = ui.Views.GetEnumerator();
            var cursor = enumerator.Cast<Il2CppSystem.Collections.IEnumerator>();
            while (cursor.MoveNext())
            {
                var view = enumerator.Current?.TryCast<VFocusableSetting>();
                if (view != null) views.Add(view);
            }
            return views;
        }
#else
        internal static object Identity(object o) => o;
        private static bool Same(object a, object b) => ReferenceEquals(a, b);
        // AddGraphicSetting and the backing list are not public; resolved once, like the other Video-tab mods do.
        private static readonly FieldInfo GraphicList = typeof(SettingsMaster).GetField("_graphicSettings", BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Instance);
        private static readonly MethodInfo AddGraphic = typeof(SettingsMaster).GetMethod("AddGraphicSetting", BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Instance, null, new[] { typeof(Setting) }, null);
        private static SettingList GraphicSettings(SettingsMaster master) => AddGraphic != null ? GraphicList?.GetValue(master) as SettingList : null;
        private static void AddGraphicSetting(SettingsMaster master, Setting item) => AddGraphic.Invoke(master, new object[] { item });
        private static string TypeName(ISetting item) => item.GetType().Name;
        internal static List<VFocusableSetting> FocusableViews(AllSettingsUI ui) => ui.Views.OfType<VFocusableSetting>().ToList();
#endif
    }
}
