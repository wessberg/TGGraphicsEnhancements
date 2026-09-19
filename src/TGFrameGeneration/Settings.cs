// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using BepInEx;
using BepInEx.Configuration;
using UnityEngine;
namespace TGFrameGeneration
{
    // Native request mode. Kept for status display and the ABI mapping.
    public enum GenerationMode { Off = 0, Fixed = 1, Dynamic = 2 }
    // Values are the native backend identifiers.
    public enum FrameGenerationSetting { Off = 0, DLSS = 1, FSR = 2, XeSS = 3 }
    public enum FrameGenerationMode { Fixed = 0, Dynamic = 1 }
    // Values are the native latency identifiers.
    public enum LowLatencySetting { Off = 0, Reflex = 1, ReflexBoost = 2, XeLL = 3, AntiLag2 = 4 }
    // Row hints for settings managers: FoA Mod Manager reads DisplayName, DisplaySection, SortOrder,
    // ChoiceLabels and Hidden; BepInEx ConfigurationManager reads DispName, Category and Browsable. FoA Mod
    // Manager sorts Order ascending and ConfigurationManager descending, so only SortOrder is set.
    internal sealed class ConfigurationManagerAttributes
    {
        public string DisplayName, DispName, DisplaySection, Category, ChoiceLabels;
        public int? SortOrder;
        public bool? Hidden, Browsable;
        internal bool Visible { set { Hidden = !value; Browsable = value; } }
    }
    internal sealed class Settings
    {
        // ABI sanity bound only; the rows offer what the selected runtime reports.
        internal const int MaxMultiplierBound = 16;
        private const string VideoSection = "Video", AdvancedSection = "Advanced";
        private const string FrameGenerationKey = "Frame generation", ModeKey = "Frame generation mode", MultiplierKey = "Frame multiplier",
            MaxMultiplierKey = "Dynamic max multiplier", TargetKey = "Target framerate", LowLatencyKey = "Low latency";
        internal readonly ConfigFile File;
        // The Video tab's rows, in the plugin config so that mod managers list exactly these rows with the
        // Video tab's choices and rules. Choices are enum names; entries whose choices depend on another
        // row are rebound when those choices change (BepInEx fixes them per entry).
        private ConfigEntry<string> frameGeneration, lowLatency;
        private readonly ConfigEntry<string> mode;
        private ConfigEntry<int> multiplier, maxMultiplier;
        private readonly ConfigEntry<float> targetFps;
        // Not in the Video tab; hidden from mod managers.
        internal readonly ConfigEntry<float> BaseLimit;
        internal readonly ConfigEntry<bool> NativeMenu, SuspendOnCursor, RequireCamera, DumpNow;
        internal readonly ConfigEntry<KeyCode> EmergencyKey;
        // Null until the native side reports what this GPU supports; until then every value is kept.
        internal VideoCapabilities Capabilities { get; private set; }
        // A Video setting changed: in the Video tab, a mod manager, the config file or by a rule.
        internal event Action Changed;
        internal float[] Targets { get; } = VideoCapabilities.Targets();
        private readonly Dictionary<string, ConfigurationManagerAttributes> rows = new Dictionary<string, ConfigurationManagerAttributes>();
        private int editing;
        // Up to 0.5.0 the Video rows were kept in BepInEx/TGFrameGeneration/VideoSettings.cfg and the rest
        // under the plugin ID tgfoa.experimental.dlss-frame-generation.
        private static string LegacyVideoPath => Path.Combine(Path.Combine(Paths.BepInExRootPath, "TGFrameGeneration"), "VideoSettings.cfg");
        private static string LegacyMainPath => Path.Combine(Paths.ConfigPath, "tgfoa.experimental.dlss-frame-generation.cfg");
        internal Settings(ConfigFile file)
        {
            File = file;
            bool existing = System.IO.File.Exists(file.ConfigFilePath);
            var stored = ReadValues(existing ? new[] { file.ConfigFilePath } : new[] { LegacyMainPath, LegacyVideoPath });
            frameGeneration = Row(FrameGenerationKey, FrameGenerationSetting.Off.ToString(), "Frame Generation", 0,
                Enum.GetNames(typeof(FrameGenerationSetting)), null,
                "Generates extra frames between the rendered ones with NVIDIA DLSS, AMD FSR or Intel XeSS, as supported by this GPU, " +
                "from the game's own motion vectors and depth.");
            mode = Row(ModeKey, FrameGenerationMode.Fixed.ToString(), "Frame Generation Mode", 1,
                Enum.GetNames(typeof(FrameGenerationMode)), null,
                "Fixed: a constant multiplier. Dynamic: NVIDIA Multi Frame Generation picks the number of generated frames each frame to reach the target framerate.");
            multiplier = Row(MultiplierKey, 2, "Frame Multiplier", 2, Range(MaxMultiplierBound), VideoCapabilities.MultiplierLabel,
                "Frames shown per rendered frame; 2x is one generated frame per rendered frame. Limited to what the selected technology supports on this GPU.");
            maxMultiplier = Row(MaxMultiplierKey, MaxMultiplierBound, "Max Frame Multiplier", 3, Range(MaxMultiplierBound), VideoCapabilities.MaxMultiplierLabel,
                "Upper limit for Dynamic mode. NVIDIA's runtime may ignore it in Dynamic mode.");
            targetFps = Row(TargetKey, 0f, "Target Framerate", 4, Targets, VideoCapabilities.TargetLabel,
                "Framerate Dynamic mode aims for. Auto follows the display's refresh rate, as does VSync when it is on.");
            lowLatency = Row(LowLatencyKey, LowLatencySetting.Reflex.ToString(), "Low Latency", 5,
                Enum.GetNames(typeof(LowLatencySetting)), name => VideoCapabilities.Label(Parse(name, LowLatencySetting.Off)),
                "Starts each frame just in time, which cuts input delay. Stays on while frame generation is on: DLSS needs NVIDIA Reflex and XeSS always uses Intel XeLL. " +
                "XeLL on its own needs an Intel GPU, AMD Anti-Lag 2 an AMD GPU.");
            BaseLimit = file.Bind(AdvancedSection, "Base FPS limit", 0f, Hidden(
                "Optional cap on rendered frames through the active low-latency technology (Reflex, XeLL or Anti-Lag 2). 0 = no cap.",
                new AcceptableValueRange<float>(0, 1000)));
            NativeMenu = file.Bind(AdvancedSection, "Add rows to Video settings", true, Hidden(
                "Adds the Frame Generation and Low Latency rows to the game's Video settings."));
            SuspendOnCursor = file.Bind(AdvancedSection, "Suspend when cursor unlocked", true, Hidden(
                "Pauses frame generation while the cursor is unlocked (menus, loading)."));
            RequireCamera = file.Bind(AdvancedSection, "Require active main camera", true, Hidden(
                "Pauses frame generation while the game has no active main camera."));
            EmergencyKey = file.Bind(AdvancedSection, "Emergency off key", KeyCode.F8, Hidden(
                "Sets Frame Generation to Off. None disables the key."));
            DumpNow = file.Bind(AdvancedSection, "Write diagnostic report now", false, Hidden(
                "Set to true to write a report to BepInEx/TGFrameGeneration/diagnostics; resets to false afterwards. Review it before sharing."));
            if (stored != null) Restore(stored, !existing);
            file.SettingChanged += OnSettingChanged;
            Settle();
        }
        internal FrameGenerationSetting FrameGeneration
        {
            get => Parse(frameGeneration.Value, FrameGenerationSetting.Off);
            set => frameGeneration.Value = value.ToString();
        }
        internal FrameGenerationMode Mode
        {
            get => Parse(mode.Value, FrameGenerationMode.Fixed);
            set => mode.Value = value.ToString();
        }
        internal int Multiplier { get => multiplier.Value; set => multiplier.Value = value; }
        internal int MaxMultiplier { get => maxMultiplier.Value; set => maxMultiplier.Value = value; }
        internal float TargetFps { get => targetFps.Value; set => targetFps.Value = VideoCapabilities.NearestTarget(Targets, value); }
        internal LowLatencySetting LowLatency
        {
            get => Parse(lowLatency.Value, LowLatencySetting.Off);
            set => lowLatency.Value = value.ToString();
        }
        internal GenerationMode RequestedMode =>
            FrameGeneration == FrameGenerationSetting.Off ? GenerationMode.Off :
            FrameGeneration == FrameGenerationSetting.DLSS && Mode == FrameGenerationMode.Dynamic && (Capabilities == null || Capabilities.HasMode)
                ? GenerationMode.Dynamic : GenerationMode.Fixed;
        internal void ApplyCapabilities(VideoCapabilities capabilities)
        {
            if (capabilities == null || !capabilities.Known || Capabilities != null) return;
            Capabilities = capabilities;
            Settle();
        }
        // The Video tab's Apply. The technology goes first: the choices of the rows after it depend on it,
        // and BepInEx replaces a value outside an entry's choices with the first choice.
        internal void Apply(FrameGenerationSetting frameGeneration, FrameGenerationMode mode, int multiplier, int maxMultiplier, float targetFps,
            LowLatencySetting lowLatency)
        {
            Batch(() =>
            {
                FrameGeneration = frameGeneration;
                Mode = mode;
                Refresh();
                Multiplier = multiplier;
                MaxMultiplier = maxMultiplier;
                TargetFps = targetFps;
                LowLatency = lowLatency;
            });
            Settle();
        }
        internal void SetFrameGenerationOff()
        {
            if (FrameGeneration == FrameGenerationSetting.Off) return;
            Batch(() => FrameGeneration = FrameGenerationSetting.Off);
            Settle();
        }
        private void OnSettingChanged(object sender, SettingChangedEventArgs e)
        {
            if (editing == 0 && e.ChangedSetting.Definition.Section == VideoSection) Settle();
        }
        private void Batch(Action change)
        {
            bool save = File.SaveOnConfigSet;
            ++editing; File.SaveOnConfigSet = false;
            try { change(); }
            finally { File.SaveOnConfigSet = save; --editing; }
        }
        private void Settle()
        {
            Batch(Refresh);
            File.Save();
            ModManagers.RequestFoARefresh();
            Changed?.Invoke();
        }
        // The Video tab's choices, rules and visible rows, applied to the stored values whichever UI changed them.
        private void Refresh()
        {
            var caps = Capabilities;
            bool known = caps != null;
            var backends = known ? caps.Backends : (FrameGenerationSetting[])Enum.GetValues(typeof(FrameGenerationSetting));
            frameGeneration = Rebind(frameGeneration, backends.Select(b => b.ToString()).ToArray(), FrameGenerationSetting.Off.ToString());
            var backend = FrameGeneration;
            // DLSS needs Reflex, XeSS runs with XeLL only, and low latency stays on while frame generation is on.
            var latencies = known ? caps.AllowedLatencies(backend) : (LowLatencySetting[])Enum.GetValues(typeof(LowLatencySetting));
            lowLatency = Rebind(lowLatency, latencies.Select(l => l.ToString()).ToArray(),
                (known ? caps.PreferredLatency(backend) : LowLatencySetting.Reflex).ToString());
            // A technology offers up to its own maximum; switching clamps the multiplier to it.
            int max = !known ? MaxMultiplierBound : Math.Max(2, backend == FrameGenerationSetting.Off ? caps.HighestMultiplier : caps.MaxMultiplier(backend));
            multiplier = Rebind(multiplier, Range(max), 2, v => Mathf.Clamp(v, 2, max));
            int dlssMax = known ? Math.Max(2, caps.MaxMultiplier(FrameGenerationSetting.DLSS)) : MaxMultiplierBound;
            maxMultiplier = Rebind(maxMultiplier, Range(dlssMax), dlssMax, v => Mathf.Clamp(v, 2, dlssMax));
            // Rows shown as in the Video tab. Rows the Video tab shows greyed out (no NVIDIA GPU for the
            // DLSS inputs) have no greyed-out state in a mod manager, so they are left out.
            bool usable = !known || caps.InputSource;
            bool hasMode = !known || caps.HasMode;
            bool dynamic = backend == FrameGenerationSetting.DLSS && hasMode && Mode == FrameGenerationMode.Dynamic;
            rows[FrameGenerationKey].Visible = usable && backends.Length > 1;
            rows[ModeKey].Visible = usable && backend == FrameGenerationSetting.DLSS && hasMode;
            rows[MultiplierKey].Visible = usable && backend != FrameGenerationSetting.Off && max > 2 && !dynamic;
            rows[MaxMultiplierKey].Visible = usable && dynamic;
            rows[TargetKey].Visible = usable && dynamic;
            rows[LowLatencyKey].Visible = !known || caps.Latencies.Length > 1;
        }
        // Gives an entry new choices. A value outside them becomes the nearest (numbers) or the fallback;
        // BepInEx would otherwise fall back to the first choice.
        private ConfigEntry<T> Rebind<T>(ConfigEntry<T> entry, T[] choices, T fallback, Func<T, T> nearest = null) where T : IEquatable<T>
        {
            if (choices.Length == 0) return entry;
            T value = entry.Value;
            if (!choices.Contains(value) && nearest != null) value = nearest(value);
            if (!choices.Contains(value)) value = choices.Contains(fallback) ? fallback : choices[0];
            if (!(entry.Description.AcceptableValues is AcceptableValueList<T> list && list.AcceptableValues.SequenceEqual(choices)))
            {
                var defaultValue = (T)entry.DefaultValue;
                if (!choices.Contains(defaultValue)) defaultValue = choices.Contains(fallback) ? fallback : choices[0];
                var description = new ConfigDescription(entry.Description.Description, new AcceptableValueList<T>(choices), entry.Description.Tags);
                File.Remove(entry.Definition);
                entry = File.Bind(entry.Definition, defaultValue, description);
            }
            if (!entry.Value.Equals(value)) entry.Value = value;
            return entry;
        }
        private ConfigEntry<T> Row<T>(string key, T defaultValue, string displayName, int order, T[] choices, Func<T, string> label, string description)
            where T : IEquatable<T>
        {
            var row = new ConfigurationManagerAttributes
            {
                DisplayName = displayName, DispName = displayName, DisplaySection = VideoSection, Category = VideoSection, SortOrder = order,
                ChoiceLabels = label == null ? null : string.Join(";", choices.Select(c => Serialize(c) + "=" + label(c)).ToArray()),
                Visible = true
            };
            rows[key] = row;
            return File.Bind(VideoSection, key, defaultValue, new ConfigDescription(description, new AcceptableValueList<T>(choices), row));
        }
        private static ConfigDescription Hidden(string description, AcceptableValueBase values = null) =>
            new ConfigDescription(description, values, new ConfigurationManagerAttributes { Visible = false });
        private static string Serialize<T>(T value) => TomlTypeConverter.ConvertToString(value, typeof(T));
        private static int[] Range(int max) => Enumerable.Range(2, Math.Max(1, max - 1)).ToArray();
        private static T Parse<T>(string text, T fallback) where T : struct =>
            Enum.TryParse(text, out T value) && Enum.IsDefined(typeof(T), value) ? value : fallback;
        // Key = value lines of BepInEx config files; later files win.
        private static Dictionary<string, string> ReadValues(string[] paths)
        {
            var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var path in paths)
            {
                try
                {
                    if (!System.IO.File.Exists(path)) continue;
                    foreach (var raw in System.IO.File.ReadAllLines(path))
                    {
                        var line = raw.Trim();
                        int equals = line.IndexOf('=');
                        if (line.Length == 0 || line[0] == '#' || line[0] == '[' || equals <= 0) continue;
                        values[line.Substring(0, equals).Trim()] = line.Substring(equals + 1).Trim();
                    }
                }
                catch { } // unreadable: defaults
            }
            return values.Count > 0 ? values : null;
        }
        // Values BepInEx would reset on binding, and on the first start the settings of 0.5.0 and earlier.
        private void Restore(Dictionary<string, string> stored, bool legacy)
        {
            Batch(() =>
            {
                // A target from a display with other refresh rates snaps to the nearest one offered here.
                if (stored.TryGetValue(TargetKey, out var text) && float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var target))
                    TargetFps = target;
                if (!legacy) return;
                if (stored.TryGetValue(FrameGenerationKey, out text)) FrameGeneration = Parse(text, FrameGenerationSetting.Off);
                if (stored.TryGetValue(ModeKey, out text)) Mode = Parse(text, FrameGenerationMode.Fixed);
                if (stored.TryGetValue(LowLatencyKey, out text)) LowLatency = Parse(text, LowLatencySetting.Reflex);
                else if (stored.TryGetValue("NVIDIA Reflex", out text)) // 0.4.0: Off, On, OnPlusBoost
                    LowLatency = text == "OnPlusBoost" ? LowLatencySetting.ReflexBoost : text == "Off" ? LowLatencySetting.Off : LowLatencySetting.Reflex;
                if (stored.TryGetValue(MultiplierKey, out text) && int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out var value))
                    Multiplier = Mathf.Clamp(value, 2, MaxMultiplierBound);
                if (stored.TryGetValue(MaxMultiplierKey, out text) && int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out value))
                    MaxMultiplier = Mathf.Clamp(value, 2, MaxMultiplierBound);
                if (stored.TryGetValue("Reflex base FPS limit", out text) && float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var limit))
                    BaseLimit.Value = Mathf.Clamp(limit, 0, 1000);
                if (stored.TryGetValue(NativeMenu.Definition.Key, out text) && bool.TryParse(text, out var flag)) NativeMenu.Value = flag;
                if (stored.TryGetValue(SuspendOnCursor.Definition.Key, out text) && bool.TryParse(text, out flag)) SuspendOnCursor.Value = flag;
                if (stored.TryGetValue(RequireCamera.Definition.Key, out text) && bool.TryParse(text, out flag)) RequireCamera.Value = flag;
                if (stored.TryGetValue(EmergencyKey.Definition.Key, out text) && Enum.TryParse(text, out KeyCode key)) EmergencyKey.Value = key;
            });
            if (!legacy) return;
            // Renamed rather than deleted: a backup, and no longer a config file of its own.
            foreach (var path in new[] { LegacyMainPath, LegacyVideoPath })
            {
                try
                {
                    if (!System.IO.File.Exists(path)) continue;
                    if (System.IO.File.Exists(path + ".migrated")) System.IO.File.Delete(path + ".migrated");
                    System.IO.File.Move(path, path + ".migrated");
                }
                catch { } // a file in use stays; its values were read already
            }
        }
    }
}
