// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using BepInEx.Configuration;
using TGGraphicsBridge;
namespace TGBetterUpscaling
{
    // The game's Upscaling row, by index: the game's own None, STP and DLSS, then the two this mod adds.
    public enum UpscalerSetting { None = 0, STP = 1, DLSS = 2, FSR = 3, XeSS = 4 }
    // What this GPU and game build offer, from the game (STP, DLSS) and the native side (FSR, XeSS).
    internal sealed class Capabilities
    {
        internal readonly bool Known, Stp, Dlss, Fsr, Xess;
        internal readonly string[] FsrVersions; // AMD's names, in the order the runtime reports them
        // The FSR version that runs on this GPU: AMD's default, the newest its runtime offers here (the native log
        // confirms it when FSR starts). Null without FSR.
        internal readonly string FsrVersion;
        // Each upscaler's quality modes as its runtime reports them, highest render scale first. Empty:
        // that upscaler keeps the game's render-scale slider.
        internal readonly QualityMode[] DlssModes, FsrModes, XessModes;
        internal Capabilities(bool known, bool stp, bool dlss, bool fsr, bool xess, IList<string> fsrVersions,
            IEnumerable<QualityMode> dlssModes = null, IEnumerable<QualityMode> fsrModes = null, IEnumerable<QualityMode> xessModes = null)
        {
            Known = known; Stp = stp; Dlss = dlss;
            // FSR and XeSS take HDRP's place for STP, so they need what STP needs.
            Fsr = fsr && stp; Xess = xess && stp;
            FsrVersions = (fsrVersions ?? new string[0]).ToArray();
            FsrVersion = Fsr ? Newest(FsrVersions) : null;
            DlssModes = Dlss ? QualityModes.Sorted(dlssModes) : new QualityMode[0];
            FsrModes = Fsr ? QualityModes.Sorted(fsrModes) : new QualityMode[0];
            XessModes = Xess ? QualityModes.Sorted(xessModes) : new QualityMode[0];
        }
        internal QualityMode[] Modes(UpscalerSetting upscaler) =>
            upscaler == UpscalerSetting.DLSS ? DlssModes : upscaler == UpscalerSetting.FSR ? FsrModes : upscaler == UpscalerSetting.XeSS ? XessModes : new QualityMode[0];
        internal bool Offers(UpscalerSetting upscaler)
        {
            switch (upscaler)
            {
                case UpscalerSetting.None: return true;
                case UpscalerSetting.STP: return Stp;
                case UpscalerSetting.DLSS: return Dlss;
                case UpscalerSetting.FSR: return Fsr;
                case UpscalerSetting.XeSS: return Xess;
            }
            return false;
        }
        internal UpscalerSetting[] Upscalers => ((UpscalerSetting[])Enum.GetValues(typeof(UpscalerSetting))).Where(Offers).ToArray();
        // What the Upscaling row and FoA Mod Manager call an upscaler: FSR by the short name of the version that runs
        // ("FSR 4", "FSR 3.1": the major and minor number of AMD's version name, a .0 minor dropped), its full version in the
        // FSR rows' tooltips and the logs; the others by name. The config keeps the plain name ("FSR"), so a stored choice
        // stays valid whatever version a later session runs.
        internal string LabelOf(UpscalerSetting upscaler)
        {
            string version = upscaler == UpscalerSetting.FSR ? ShortVersion(FsrVersion) : null;
            return version != null ? "FSR " + version : upscaler.ToString();
        }
        internal static string Label(UpscalerSetting upscaler) => Session.Settings?.Capabilities?.LabelOf(upscaler) ?? upscaler.ToString();
        // "4.0.2" -> "4", "3.1.5" -> "3.1"; null for a name without a number.
        internal static string ShortVersion(string name)
        {
            var parts = VersionNumbers(name);
            if (parts.Count == 0) return null;
            return parts.Count > 1 && parts[1] != 0 ? parts[0] + "." + parts[1] : parts[0].ToString();
        }
        // A version name's numbers ("4.0.2" -> 4, 0, 2), as the native side reads them (tgup_policy.hpp version_numbers).
        private static List<uint> VersionNumbers(string name)
        {
            var parts = new List<uint>();
            if (string.IsNullOrEmpty(name)) return parts;
            int i = 0;
            while (i < name.Length && !Digit(name[i])) ++i;
            while (i < name.Length && Digit(name[i]))
            {
                uint value = 0;
                while (i < name.Length && Digit(name[i])) { value = Math.Min(100000u, value * 10u + (uint)(name[i] - '0')); ++i; }
                parts.Add(value);
                if (i + 1 < name.Length && name[i] == '.' && Digit(name[i + 1])) ++i; else break;
            }
            return parts;
        }
        private static bool Digit(char c) => c >= '0' && c <= '9';
        // The newest of the versions, compared by their numbers (tgup_policy.hpp newest_version); null for none.
        private static string Newest(string[] names)
        {
            string best = null; List<uint> bestParts = null;
            foreach (var name in names)
            {
                var parts = VersionNumbers(name);
                if (best == null || Newer(parts, bestParts)) { best = name; bestParts = parts; }
            }
            return best;
        }
        private static bool Newer(List<uint> a, List<uint> b)
        {
            for (int i = 0; i < a.Count && i < b.Count; i++) if (a[i] != b[i]) return a[i] > b[i];
            return a.Count > b.Count;
        }
    }
    internal sealed class Settings
    {
        private const string VideoSection = "Video", AdvancedSection = "Advanced";
        private const string UpscalerKey = "Upscaler", SharpnessKey = "FSR sharpness";
        private const string DlssModeKey = "DLSS quality mode", FsrModeKey = "FSR quality mode", XessModeKey = "XeSS quality mode";
        internal static readonly UpscalerSetting[] WithModes = { UpscalerSetting.DLSS, UpscalerSetting.FSR, UpscalerSetting.XeSS };
        // FSR's sharpening at half strength: at a low render scale it recovers much of the detail FSR
        // loses, and at 100% scale it stays mild. A config that already has a value keeps it.
        internal const float DefaultSharpness = 0.5f;
        // Entries of earlier builds, dropped from the file so that no mod manager lists them: the FSR
        // version (FSR runs AMD's default for the GPU) and a test build's comparison key.
        private static readonly ConfigDefinition[] Retired =
        {
            new ConfigDefinition(VideoSection, "FSR version"), new ConfigDefinition(AdvancedSection, "Upscaler comparison key")
        };
        internal readonly ConfigFile File;
        // The Video tab's rows, in the plugin config so that mod managers list exactly these rows with the
        // Video tab's choices and rules. Upscaler mirrors the game's own row, which this mod extends.
        private ConfigEntry<string> upscaler;
        private readonly ConfigEntry<float> sharpness;
        // One Quality Mode row per upscaler, listed only for the selected one. Stored by the vendor's name,
        // since what a runtime offers can differ between sessions.
        private readonly Dictionary<UpscalerSetting, ConfigEntry<string>> modes = new Dictionary<UpscalerSetting, ConfigEntry<string>>();
        // Upscalers whose mode this config file did not have yet: they start from the game's render scale.
        private readonly HashSet<UpscalerSetting> unset = new HashSet<UpscalerSetting>();
        // Not in the Video tab; hidden from mod managers.
        internal readonly ConfigEntry<bool> NativeMenu, AutoExposure, FsrInputChecker, FsrDebugView, DumpNow;
        internal Capabilities Capabilities { get; private set; }
        // A Video setting changed in a mod manager or the config file (not by this mod's own sync).
        internal event Action Changed;
        private readonly Dictionary<string, ConfigurationManagerAttributes> rows = new Dictionary<string, ConfigurationManagerAttributes>();
        private int editing;
        internal Settings(ConfigFile file)
        {
            File = file;
            upscaler = Row(UpscalerKey, UpscalerSetting.DLSS.ToString(), "Upscaling", 0, Enum.GetNames(typeof(UpscalerSetting)),
                name => Capabilities.Label(Parse(name, UpscalerSetting.None)),
                "The game's Upscaling row, with AMD FSR and Intel XeSS added. They render at the game's upscaling quality and " +
                "upscale from the game's own depth and motion vectors where the game would run STP. FSR runs AMD's own choice " +
                "for this GPU: FSR 4 on an AMD Radeon RX 9000, FSR 3.1 elsewhere. XeSS runs on any GPU with Shader Model 6.4 " +
                "and fastest on Intel Arc.");
            var orphans = OrphanedEntries();
            ModeRow(UpscalerSetting.DLSS, DlssModeKey, QualityModes.DlssNames, orphans,
                "DLSS's quality modes, as NVIDIA's runtime reports them. Each renders at the resolution NVIDIA recommends for it, " +
                "and NVIDIA's driver picks the DLSS model that works best for each mode.");
            ModeRow(UpscalerSetting.FSR, FsrModeKey, QualityModes.FsrNames, orphans,
                "FSR's quality modes, as AMD's runtime reports them, each at the render resolution AMD recommends for it.");
            ModeRow(UpscalerSetting.XeSS, XessModeKey, QualityModes.XessNames, orphans,
                "XeSS's quality presets, as Intel's runtime reports them, each at the render resolution Intel recommends for it.");
            var sharpnessRow = new ConfigurationManagerAttributes
            {
                DisplayName = "FSR Sharpness", DispName = "FSR Sharpness", DisplaySection = VideoSection, Category = VideoSection, SortOrder = 2, Visible = true
            };
            rows[SharpnessKey] = sharpnessRow;
            sharpness = file.Bind(VideoSection, SharpnessKey, DefaultSharpness, new ConfigDescription(
                "FSR's own sharpening after upscaling. 0 is off, 0.5 the default. DLSS and XeSS have no sharpening of their own.",
                new AcceptableValueRange<float>(0f, 1f), sharpnessRow));
            NativeMenu = file.Bind(AdvancedSection, "Add upscalers to Video settings", true, Hidden(
                "Adds FSR and XeSS to the game's Upscaling row in Video settings. The game applies its upscaler through that row, so without " +
                "it only the game's own upscalers run, whatever Upscaling says here."));
            AutoExposure = file.Bind(AdvancedSection, "FSR auto exposure", false, Hidden(
                "FSR computes its own exposure. Off: the game's color is already exposed by HDRP, which FSR is told."));
            FsrInputChecker = file.Bind(AdvancedSection, "FSR input checker", false, Hidden(
                "AMD's check of FSR's inputs; its messages go to BepInEx/TGBetterUpscaling/Native/TGBetterUpscaling.Native.log. Rebuilds the FSR context."));
            FsrDebugView = file.Bind(AdvancedSection, "FSR debug view", false, Hidden(
                "Shows FSR's debug view (inputs and internal masks) instead of the upscaled image."));
            DumpNow = file.Bind(AdvancedSection, "Write diagnostic report now", false, Hidden(
                "Set to true to write a report to BepInEx/TGBetterUpscaling/diagnostics; resets to false afterwards. Review it before sharing."));
            RetireEntries();
            file.SettingChanged += OnSettingChanged;
            Settle(false);
        }
        internal UpscalerSetting Upscaler => Parse(upscaler.Value, UpscalerSetting.None);
        internal float Sharpness => Math.Max(0f, Math.Min(1f, sharpness.Value));
        // The selected mode of an upscaler, null when it has none (it then keeps the game's slider).
        internal QualityMode Mode(UpscalerSetting upscaler)
        {
            var offered = Capabilities?.Modes(upscaler);
            if (offered == null || offered.Length == 0 || !modes.TryGetValue(upscaler, out var entry)) return null;
            return QualityModes.Find(offered, entry.Value) ?? QualityModes.Fallback(offered);
        }
        // Once the game and the runtimes have reported. An upscaler this config had no mode for yet starts
        // on the mode that matches the game's render scale (gamePercent), rounded down.
        internal void ApplyCapabilities(Capabilities capabilities, float gamePercent)
        {
            if (capabilities == null || !capabilities.Known || Capabilities != null) return;
            Capabilities = capabilities;
            var started = new List<string>();
            Batch(() =>
            {
                foreach (var u in WithModes)
                {
                    var offered = capabilities.Modes(u);
                    if (!unset.Contains(u) || offered.Length == 0) continue;
                    var mode = QualityModes.Default(offered, gamePercent);
                    modes[u].Value = mode.Name;
                    unset.Remove(u);
                    started.Add(u + " " + mode.Name);
                }
            });
            Settle(false);
            if (started.Count > 0) Session.Log.LogInfo("Quality modes set from the game's " + gamePercent.ToString("0.#") + "% render scale, rounded down: " + string.Join(", ", started.ToArray()) + ".");
        }
        // The Video tab (or the game's own row) applied these values. A null mode is left as it is.
        internal void Store(UpscalerSetting upscaler, float sharpness, IDictionary<UpscalerSetting, string> chosenModes = null)
        {
            Batch(() =>
            {
                this.upscaler.Value = upscaler.ToString();
                Refresh();
                this.sharpness.Value = Math.Max(0f, Math.Min(1f, sharpness));
                if (chosenModes != null)
                    foreach (var pair in chosenModes)
                        if (pair.Value != null && modes.TryGetValue(pair.Key, out var entry)) entry.Value = pair.Value;
            });
            Settle(false);
        }
        private Dictionary<ConfigDefinition, string> OrphanedEntries()
        {
            try { return typeof(ConfigFile).GetProperty("OrphanedEntries", BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public)?.GetValue(File, null) as Dictionary<ConfigDefinition, string>; }
            catch { return null; }
        }
        private void ModeRow(UpscalerSetting upscaler, string key, string[] names, Dictionary<ConfigDefinition, string> orphans, string description)
        {
            // Read before binding: once bound, an entry the file lacked looks like one set to its default.
            if (orphans != null && !orphans.ContainsKey(new ConfigDefinition(VideoSection, key))) unset.Add(upscaler);
            modes[upscaler] = Row(key, QualityModes.DefaultName, "Quality Mode", 1, names, name => name, description);
            rows[key].Visible = false;
        }
        private void RetireEntries()
        {
            try
            {
                var orphans = OrphanedEntries();
                bool removed = false;
                if (orphans != null) foreach (var definition in Retired) removed |= orphans.Remove(definition);
                if (removed) Save();
            }
            catch { } // an older BepInEx keeps the unused lines, which nothing reads
        }
        // BepInEx's own saving is off for this file (Session.Start): every change is saved here.
        private void OnSettingChanged(object sender, SettingChangedEventArgs e)
        {
            if (editing != 0) return;
            if (e.ChangedSetting.Definition.Section == VideoSection) Settle(true);
            else Save();
        }
        // Through TG Graphics Bridge's SafeConfig: a file that can't be written keeps the values for this session
        // and shows a warning instead of throwing into whatever changed the setting.
        internal void Save()
        {
            try { SaveThroughBridge(); }
            catch { try { File.Save(); } catch { } } // a bridge from before SafeConfig
        }
        private void SaveThroughBridge() => TGGraphicsBridge.SafeConfig.Save(File, UpscalingPlugin.Name);

        private void Batch(Action change)
        {
            bool save = File.SaveOnConfigSet;
            ++editing; File.SaveOnConfigSet = false;
            try { change(); }
            finally { File.SaveOnConfigSet = save; --editing; }
        }
        private void Settle(bool external)
        {
            Batch(Refresh);
            Save();
            ModManagers.RequestFoARefresh();
            if (external) Changed?.Invoke();
        }
        // The Video tab's choices and visible rows, applied to the stored values whichever UI changed them.
        private void Refresh()
        {
            var caps = Capabilities;
            bool known = caps != null;
            var upscalers = known ? caps.Upscalers : (UpscalerSetting[])Enum.GetValues(typeof(UpscalerSetting));
            // Unknown until the game and the native side report; until then every value is kept.
            Func<string, string> label = v => known ? caps.LabelOf(Parse(v, UpscalerSetting.None)) : v;
            upscaler = Rebind(upscaler, upscalers.Select(u => u.ToString()).ToArray(),
                (known && caps.Dlss ? UpscalerSetting.DLSS : known && caps.Stp ? UpscalerSetting.STP : UpscalerSetting.None).ToString(), label);
            // FoA Mod Manager names the choices as the Video tab does (FSR with its version once AMD's runtime reported),
            // also when the offered upscalers stayed the same and the entry wasn't bound again.
            rows[UpscalerKey].ChoiceLabels = string.Join(";", upscalers.Select(u => u + "=" + label(u.ToString())).ToArray());
            rows[UpscalerKey].Visible = !known || upscalers.Length > 1;
            rows[SharpnessKey].Visible = Upscaler == UpscalerSetting.FSR;
            // Each upscaler's modes as its runtime reports them, listed for the selected upscaler only, as
            // in the Video tab. Until the runtimes report, every stored name is kept.
            foreach (var u in WithModes)
            {
                var offered = known ? caps.Modes(u) : new QualityMode[0];
                string key = u == UpscalerSetting.DLSS ? DlssModeKey : u == UpscalerSetting.FSR ? FsrModeKey : XessModeKey;
                if (offered.Length > 0)
                    modes[u] = Rebind(modes[u], offered.Select(m => m.Name).ToArray(), QualityModes.Fallback(offered).Name, name => name);
                rows[key].Visible = offered.Length > 0 && Upscaler == u;
            }
        }
        // Gives an entry new choices; a value outside them becomes the fallback. BepInEx fixes the
        // acceptable values per entry, so the entry is bound again.
        private ConfigEntry<string> Rebind(ConfigEntry<string> entry, string[] choices, string fallback, Func<string, string> label)
        {
            if (choices.Length == 0) return entry;
            string value = entry.Value;
            if (!choices.Contains(value)) value = choices.Contains(fallback) ? fallback : choices[0];
            if (!(entry.Description.AcceptableValues is AcceptableValueList<string> list && list.AcceptableValues.SequenceEqual(choices)))
            {
                var defaultValue = (string)entry.DefaultValue;
                if (!choices.Contains(defaultValue)) defaultValue = choices.Contains(fallback) ? fallback : choices[0];
                if (entry.Description.Tags.FirstOrDefault(t => t is ConfigurationManagerAttributes) is ConfigurationManagerAttributes row)
                    row.ChoiceLabels = string.Join(";", choices.Select(c => c + "=" + label(c)).ToArray());
                var description = new ConfigDescription(entry.Description.Description, new AcceptableValueList<string>(choices), entry.Description.Tags);
                File.Remove(entry.Definition);
                entry = File.Bind(entry.Definition, defaultValue, description);
            }
            if (entry.Value != value) entry.Value = value;
            return entry;
        }
        private ConfigEntry<string> Row(string key, string defaultValue, string displayName, int order, string[] choices, Func<string, string> label, string description)
        {
            var row = new ConfigurationManagerAttributes
            {
                DisplayName = displayName, DispName = displayName, DisplaySection = VideoSection, Category = VideoSection, SortOrder = order,
                ChoiceLabels = string.Join(";", choices.Select(c => c + "=" + label(c)).ToArray()),
                Visible = true
            };
            rows[key] = row;
            return File.Bind(VideoSection, key, defaultValue, new ConfigDescription(description, new AcceptableValueList<string>(choices), row));
        }
        private static ConfigDescription Hidden(string description) =>
            new ConfigDescription(description, null, new ConfigurationManagerAttributes { Visible = false });
        internal static T Parse<T>(string text, T fallback) where T : struct =>
            Enum.TryParse(text, out T value) && Enum.IsDefined(typeof(T), value) ? value : fallback;
    }
}
