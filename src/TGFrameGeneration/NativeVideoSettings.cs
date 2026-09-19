// SPDX-License-Identifier: MIT
// Native Video-tab integration: the mod's options are real game Setting/PrefOption objects,
// so they render with the game's own row prefabs, respect Apply Changes / Undo / Restore
// Defaults, and nest exactly like the built-in Dynamic Resolution or Subsurface Scattering rows.
#if BEPINEX_MONO
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;
using Awaken.TG.Main.Settings;
using Awaken.TG.Main.Settings.Options;
using Awaken.TG.Main.Settings.Windows;
using Awaken.TG.MVC;
using UnityEngine;
namespace TGFrameGeneration
{
    internal sealed class FrameGenerationVideoSetting : Setting
    {
        private const string IdPrefix = "TGFG.Video.";
        private readonly Settings settings;
        private readonly PrefOption[] top;
        private readonly EnumArrowsOption fg, mode, multiplier, maxMultiplierOption, target, reflex;
        private readonly ToggleOption reflexOff;
        private readonly float[] targetValues;
        private readonly int maxMultiplier;
        private readonly bool hasMode, hasMultiplier;
        internal string Layout { get; }
        internal FrameGenerationVideoSetting(Settings settings, int maxMultiplier, bool dynamicSupported)
        {
            this.settings = settings;
            this.maxMultiplier = Mathf.Clamp(maxMultiplier, 2, 6);
            hasMultiplier = this.maxMultiplier > 2;
            hasMode = hasMultiplier && dynamicSupported;

            fg = Enum("FrameGeneration", "Frame Generation", 0, "Off", "DLSS");
            fg.AddTooltip(() => "NVIDIA DLSS Frame Generation through the TGFG Streamline bridge. Generates frames between rendered frames. " +
                "Pauses automatically in menus and while the cursor is unlocked. Requires a GeForce RTX GPU with Frame Generation support" +
                (Session.Bridge != null && Session.Bridge.Connected ? "." : " and the TGFG native backend, which is not loaded."));
            fg.SetInteractabilityFunction(() => Session.Bridge != null && Session.Bridge.Connected);

            var dependents = new List<PrefOption>();
            if (hasMode)
            {
                mode = Enum("Mode", "Frame Generation Mode", 0, "Fixed", "Dynamic");
                mode.AddTooltip(() => "Fixed: a constant multiplier. Dynamic: NVIDIA Multi Frame Generation picks the number of generated frames each frame to hit the target framerate.");
                dependents.Add(mode);
            }
            if (hasMultiplier)
            {
                var labels = new string[this.maxMultiplier - 1];
                for (int i = 0; i < labels.Length; i++) labels[i] = (i + 2) + "x";
                multiplier = Enum("Multiplier", "Frame Multiplier", 0, labels);
                multiplier.AddTooltip(() => "Output frames per rendered frame. 2x means one generated frame per rendered frame.");
            }
            if (hasMode)
            {
                var upTo = new string[this.maxMultiplier - 1];
                for (int i = 0; i < upTo.Length; i++) upTo[i] = "Up to " + (i + 2) + "x";
                maxMultiplierOption = Enum("MaxMultiplier", "Max Frame Multiplier", upTo.Length - 1, upTo);
                maxMultiplierOption.AddTooltip(() => "Upper bound for Dynamic mode. Passed to the runtime as its frame count; the Streamline 2.14.1 guide documents this value as ignored in Dynamic mode, so it only takes effect if the installed runtime honours it.");
            }
            targetValues = BuildTargets();
            var targetLabels = targetValues.Select(v => v <= 0 ? "Auto" : v.ToString("0", CultureInfo.InvariantCulture)).ToArray();
            target = Enum("TargetFps", "Target Framerate", 0, targetLabels);
            target.AddTooltip(() => "Dynamic mode output target. Auto follows the display refresh rate; with VSync on, the runtime targets the refresh rate regardless.");
            if (hasMode)
            {
                // Fixed shows only Frame Multiplier; Dynamic shows only its limit and target.
                var dynamicRows = new PrefOption[] { maxMultiplierOption, target };
                var modeRows = ModeRowsOption.Create(mode, new PrefOption[] { multiplier }, dynamicRows);
                if (modeRows != null) dependents[dependents.IndexOf(mode)] = modeRows;
                else dependents.Add(new DependentOption(multiplier, () => mode.OptionInt == 1, dynamicRows)); // older game builds
            }
            else if (hasMultiplier) dependents.Add(multiplier);

            reflex = Enum("Reflex", "NVIDIA Reflex", 1, "Off", "On", "On + Boost");
            reflexOff = reflex.Options.First();
            reflex.AddTooltip(() => fg.OptionInt == 1
                ? "Reflex low-latency mode is required while Frame Generation is on."
                : "Reduces render latency. Boost also keeps GPU clocks high when CPU-bound.");
            fg.onChange += _ => UpdateReflexConstraint();

            top = dependents.Count > 0
                ? new PrefOption[] { new DependentOption(fg, () => fg.OptionInt == 1, dependents.ToArray()), reflex }
                : new PrefOption[] { fg, reflex };
            Layout = "FG" + (hasMode ? " > Mode > [Fixed: Multiplier(2-" + this.maxMultiplier + "x) | Dynamic: MaxMultiplier + Target(" + targetValues.Length + ")]"
                : hasMultiplier ? " > Multiplier(2-" + this.maxMultiplier + "x)" : " (2x only)") + " | Reflex";
            SyncFromConfig();
        }
        public override string SettingName => "Frame Generation";
        public override IEnumerable<PrefOption> Options => top;
        protected override bool AutoApplyOnInit => false;
        private static ToggleOption Choice(string id, string label) => new ToggleOption(IdPrefix + id, label, false, false);
        private static EnumArrowsOption Enum(string id, string displayName, int defaultIndex, params string[] labels)
        {
            var toggles = new ToggleOption[labels.Length];
            for (int i = 0; i < labels.Length; i++) toggles[i] = Choice(id + "." + i, labels[i]);
            return new EnumArrowsOption(IdPrefix + id, displayName, toggles[Mathf.Clamp(defaultIndex, 0, toggles.Length - 1)], false, toggles);
        }
        private static float[] BuildTargets()
        {
            float max = 0;
            try
            {
                foreach (var r in Screen.resolutions) max = Mathf.Max(max, (float)r.refreshRateRatio.value);
                if (max <= 0) max = (float)Screen.currentResolution.refreshRateRatio.value;
            }
            catch { }
            if (max <= 0 || float.IsNaN(max)) max = 240;
            var list = new List<float> { 0 };
            foreach (var c in new[] { 60f, 72f, 90f, 100f, 120f, 144f, 165f, 175f, 180f, 200f, 240f, 280f, 300f, 360f, 480f, 500f })
                if (c <= max + 0.5f) list.Add(c);
            float rounded = Mathf.Round(max);
            if (!list.Any(v => Mathf.Abs(v - rounded) < 0.6f)) list.Add(rounded);
            list.Sort();
            return list.ToArray();
        }
        private int NearestTargetIndex(float value)
        {
            if (value <= 0) return 0;
            int best = 0; float distance = float.MaxValue;
            for (int i = 1; i < targetValues.Length; i++)
            {
                float d = Mathf.Abs(targetValues[i] - value);
                if (d < distance) { distance = d; best = i; }
            }
            return best;
        }
        private IEnumerable<EnumArrowsOption> Leaves()
        {
            yield return fg;
            if (mode != null) yield return mode;
            if (multiplier != null) yield return multiplier;
            if (maxMultiplierOption != null) yield return maxMultiplierOption;
            yield return target;
            yield return reflex;
        }
        private void UpdateReflexConstraint()
        {
            reflex.SetForbiddenOptions(fg.OptionInt == 1 ? new[] { reflexOff } : new ToggleOption[0]);
            reflex.EnsureCurrentOptionNotForbidden();
        }
        internal void SyncFromConfig()
        {
            fg.OptionInt = settings.FrameGeneration.Value == FrameGenerationSetting.DLSS ? 1 : 0;
            if (mode != null) mode.OptionInt = settings.Mode.Value == FrameGenerationMode.Dynamic ? 1 : 0;
            if (multiplier != null) multiplier.OptionInt = Mathf.Clamp(settings.Multiplier.Value - 2, 0, maxMultiplier - 2);
            if (maxMultiplierOption != null) maxMultiplierOption.OptionInt = Mathf.Clamp(settings.MaxMultiplier.Value - 2, 0, maxMultiplier - 2);
            target.OptionInt = NearestTargetIndex(settings.TargetFps.Value);
            reflex.OptionInt = Mathf.Clamp((int)settings.Reflex.Value, 0, 2);
            UpdateReflexConstraint();
            // Applying stores the in-game value so the game does not report unsaved changes.
            foreach (var option in Leaves()) option.Apply();
        }
        protected override void OnApply()
        {
            settings.FrameGeneration.Value = fg.OptionInt == 1 ? FrameGenerationSetting.DLSS : FrameGenerationSetting.Off;
            if (mode != null) settings.Mode.Value = mode.OptionInt == 1 ? FrameGenerationMode.Dynamic : FrameGenerationMode.Fixed;
            else settings.Mode.Value = FrameGenerationMode.Fixed;
            settings.Multiplier.Value = multiplier != null ? multiplier.OptionInt + 2 : 2;
            settings.MaxMultiplier.Value = maxMultiplierOption != null ? maxMultiplierOption.OptionInt + 2 : maxMultiplier;
            settings.TargetFps.Value = targetValues[Mathf.Clamp(target.OptionInt, 0, targetValues.Length - 1)];
            var reflexValue = (ReflexSetting)Mathf.Clamp(reflex.OptionInt, 0, 2);
            if (settings.FrameGeneration.Value == FrameGenerationSetting.DLSS && reflexValue == ReflexSetting.Off) reflexValue = ReflexSetting.On;
            settings.Reflex.Value = reflexValue;
            settings.VideoFile.Save();
            Session.Log.LogInfo("Video settings applied: FG=" + settings.FrameGeneration.Value + " mode=" + settings.Mode.Value +
                " multiplier=" + settings.Multiplier.Value + "x dynamicMax=" + settings.MaxMultiplier.Value + "x target=" + settings.TargetFps.Value + " reflex=" + settings.Reflex.Value);
        }
    }
    // Mode row whose nested rows depend on the selected mode. The game's dependent view spawns
    // OtherOptions when the condition turns true and removes them when it turns false, so a mode
    // change swaps the list and keeps the condition false for that frame to rebuild the rows.
    // Apply, Cancel and Restore Defaults cover the rows of both modes, shown or not.
    internal sealed class ModeRowsOption : DependentOption
    {
        private static readonly FieldInfo OthersField = typeof(DependentOption).GetField("<OtherOptions>k__BackingField", BindingFlags.NonPublic | BindingFlags.Instance);
        private sealed class Switch
        {
            internal EnumArrowsOption Mode;
            internal PrefOption[] Fixed, Dynamic;
            internal ModeRowsOption Owner;
            internal int Shown, HiddenFrame = -1;
            internal bool Show()
            {
                int wanted = Mode.OptionInt == 1 ? 1 : 0;
                if (wanted != Shown && Owner != null)
                {
                    Shown = wanted;
                    OthersField.SetValue(Owner, wanted == 1 ? Dynamic : Fixed);
                    HiddenFrame = Time.frameCount;
                }
                return Time.frameCount != HiddenFrame;
            }
        }
        private readonly Switch state;
        private ModeRowsOption(EnumArrowsOption mode, Switch rows) : base(mode, rows.Show, rows.Fixed) { state = rows; }
        internal static ModeRowsOption Create(EnumArrowsOption mode, PrefOption[] fixedRows, PrefOption[] dynamicRows)
        {
            if (OthersField == null || OthersField.FieldType != typeof(PrefOption[])) return null;
            var rows = new Switch { Mode = mode, Fixed = fixedRows, Dynamic = dynamicRows };
            var option = new ModeRowsOption(mode, rows);
            rows.Owner = option;
            return option;
        }
        private IEnumerable<PrefOption> Every() => new[] { BaseOption }.Concat(state.Fixed).Concat(state.Dynamic);
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
        private static bool failed;
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
                if (!Session.Settings.NativeMenu.Value) { Status = "Disabled by config (02 Menu)."; return; }
                var master = World.Any<SettingsMaster>();
                if (master == null) { Status = "Waiting for the game's settings model."; return; }
                var list = GraphicList != null ? GraphicList.GetValue(master) as List<ISetting> : null;
                if (list == null || AddGraphic == null) { failed = true; Status = "Game settings API not found (list=" + (list != null) + ", add=" + (AddGraphic != null) + "); rows not added."; Session.Log.LogWarning(Status); return; }
                if (setting != null && ReferenceEquals(master, owner) && list.Contains(setting)) return;
                if (World.Any<AllSettingsUI>() != null) return; // never mutate the list while the screen is open
                bool caps = Session.HaveStatus && Session.Status.CapsKnown;
                if (!caps && now - firstTick < 20f) { Status = "Waiting for native capability report..."; return; }
                int maxMultiplier = caps ? Math.Min(6, (int)Session.Status.MaxGeneratedFrames + 1) : 6;
                bool dynamic = !caps || Session.Status.DynamicSupported != 0;
                setting = new FrameGenerationVideoSetting(Session.Settings, maxMultiplier, dynamic);
                owner = master;
                AddGraphic.Invoke(master, new object[] { setting });
                Reposition(list, setting);
                Status = "Registered in Video settings: " + setting.Layout + (caps ? "" : " (capabilities unknown at registration)");
                Session.Log.LogInfo(Status);
            }
            catch (Exception ex)
            {
                failed = true;
                Status = "Video rows failed; edit BepInEx/TGFrameGeneration/VideoSettings.cfg instead: " + ex.GetType().Name;
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
        internal static string Status => "Native Video rows are only implemented for the Mono runtime; edit BepInEx/TGFrameGeneration/VideoSettings.cfg.";
        internal static bool IsSettingsOpen => false;
        internal static void Tick(float now) { }
    }
}
#endif
