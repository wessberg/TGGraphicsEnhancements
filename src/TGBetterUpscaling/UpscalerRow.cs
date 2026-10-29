// SPDX-License-Identifier: MIT
// FSR and XeSS are choices of the game's own Upscaling row, not rows of their own: one selector for
// one concept. The row has None, STP and DLSS, then FSR and XeSS. Game builds from October 2026 have an
// FSR entry of their own (HDRP's FSR 2) with the index and preference ID this mod's always had: this
// mod's FSR 3.1 or 4 takes that entry where AMD's runtime reports it, the game's FSR 2 keeps it where
// it doesn't, and XeSS follows it. Older builds get both entries from this mod. STP keeps the game's
// render-scale slider; DLSS, FSR and XeSS each get a Quality Mode row with the modes their runtime
// reports, and FSR its own sharpening below it. An upscaler whose runtime reports no modes keeps the
// slider. The game stores the row's index in its own preferences, so a choice of FSR or XeSS persists
// like any other, and without this mod the game falls back to its default for an index it does not know.
using System;
using System.Collections.Generic;
using System.Linq;
using Awaken.TG.Main.Memories;
using Awaken.TG.Main.Settings.Graphics;
using Awaken.TG.Main.Settings.Options;
using Awaken.TG.MVC;
using TGGraphicsBridge;
using UnityEngine;
#if !BEPINEX_IL2CPP
using System.Reflection;
#endif
namespace TGBetterUpscaling
{
    internal static class UpscalerRow
    {
        private const string IdPrefix = "TGUP.Video.";
        private const string TypeKey = "UpScalingType"; // the game's preference key for the row's index
        internal const int FsrIndex = (int)UpscalerSetting.FSR, XessIndex = (int)UpscalerSetting.XeSS;
        private const string FsrId = "UpScalingType_FSR", XessId = "UpScalingType_XeSS"; // the entries' preference IDs; FSR's is also the game's
        private static readonly PrefOption[] None = new PrefOption[0];
        private static UpScaling owner;
        private static EnumArrowsOption type;
        private static SliderOption sharpness;
        private static ToggleOption fsrChoice, xessChoice;
        private static readonly Dictionary<UpscalerSetting, EnumArrowsOption> modeRows = new Dictionary<UpscalerSetting, EnumArrowsOption>();
        private static Capabilities caps;
        private static bool failed, subscribed, configPending;
        private static float nextPoll, firstTick = -1;
        internal static string Status { get; private set; } = "Not installed yet.";
        internal static bool Installed => owner != null;
        // The row was left as the game has it (nothing to add, a row this mod does not know, or an error): the
        // game then sets its upscalers up itself, at its own slider.
        internal static bool Failed => failed;
        // A troubleshooting step selected this upscaler in the game's row: the game applies it as usual,
        // but the config keeps the player's choice, which EndTest puts back.
        private static bool testing;
#if !BEPINEX_IL2CPP
        private static readonly FieldInfo MainOption = typeof(UpScaling).GetField("_mainOption", BindingFlags.NonPublic | BindingFlags.Instance);
        private static readonly FieldInfo QualityField = typeof(UpScaling).GetField("_qualitySlider", BindingFlags.NonPublic | BindingFlags.Instance);
        private static SliderOption Quality(UpScaling up) => QualityField?.GetValue(up) as SliderOption;
        private static bool SetMain(UpScaling up, DependentOption option)
        {
            if (MainOption == null || MainOption.FieldType != typeof(DependentOption)) return false;
            MainOption.SetValue(up, option); return true;
        }
        private static readonly MethodInfo ForcedApply = typeof(Awaken.TG.Main.Settings.Setting).GetMethod("Apply", BindingFlags.NonPublic | BindingFlags.Instance, null,
            new[] { typeof(bool), typeof(bool).MakeByRefType() }, null);
        private static void ApplyRefreshing(UpScaling up)
        {
            if (ForcedApply != null) ForcedApply.Invoke(up, new object[] { true, false }); else up.Apply(out _);
        }
        // The game's Upscaling setting, once its settings model has created it.
        internal static UpScaling Game() => World.Any<UpScaling>();
        private static float? NoStep => null;
        private static readonly FieldInfo ForbiddenField = typeof(EnumArrowsOption).GetField("_forbiddenOptions", BindingFlags.NonPublic | BindingFlags.Instance);
        // Whether the game offers an entry of its row on this PC: it forbids the upscalers that can't run here.
        private static bool GameOffers(EnumArrowsOption row, ToggleOption entry) =>
            !(ForbiddenField?.GetValue(row) is ToggleOption[] forbidden) || Array.IndexOf(forbidden, entry) < 0;
#else
        private static bool GameOffers(EnumArrowsOption row, ToggleOption entry)
        {
            var forbidden = row._forbiddenOptions;
            if (forbidden == null) return true;
            for (int i = 0; i < forbidden.Length; i++) if (VideoTab.Same(forbidden[i], entry)) return false;
            return true;
        }
        private static SliderOption Quality(UpScaling up) => up._qualitySlider;
        private static bool SetMain(UpScaling up, DependentOption option) { up._mainOption = option; return true; }
        private static void ApplyRefreshing(UpScaling up) => up.Apply(true, out _);
        internal static UpScaling Game() => VideoTab.GraphicSetting<UpScaling>("UpScaling"); // not World.Any<UpScaling>(): see there
        private static Il2CppSystem.Nullable<float> NoStep => new Il2CppSystem.Nullable<float>(); // see PerformanceOverlayRows
#endif
        // The game's own FSR entry (HDRP's FSR 2, game builds from October 2026), as the game made its row: after None,
        // STP and DLSS, under this mod's index and preference ID for FSR. Null in older builds, and for a row this mod
        // has already extended.
        private static ToggleOption GameFsrEntry(ToggleOption[] entries) =>
            entries.Length == FsrIndex + 1 && entries[FsrIndex].ID == FsrId ? entries[FsrIndex] : null;
        internal static void Tick(float now)
        {
            if (failed || now < nextPoll) return;
            nextPoll = now + 0.25f;
            if (firstTick < 0) firstTick = now;
            try
            {
                var settings = Session.Settings;
                if (!settings.NativeMenu.Value) { Status = "Disabled by config (Advanced > Add upscalers to Video settings)."; return; }
                if (!subscribed)
                {
                    // A mod manager or the config file changed a value: the rows follow once the settings
                    // screen is closed, never under the player's hands.
                    subscribed = true;
                    settings.Changed += () => configPending = true;
                }
                var up = Game();
                if (up == null) { Status = "Waiting for the game's settings model."; return; }
                bool open = VideoTab.IsSettingsOpen;
                if (owner != null && VideoTab.Same(up, owner))
                {
                    if (configPending && !open) { configPending = false; ApplyConfig(); }
                    LogFsrVersion();
                    return;
                }
                if (open) return; // never change the row while the screen is open
                if (settings.Capabilities == null)
                {
                    // Only now, with the game's own Upscaling setting in the world: reading UpScaling's
                    // static members runs its static constructor, which translates the row's labels. Run
                    // before the game has loaded its localization, that constructor throws, the type stays
                    // unusable for the session, and the game's settings model (and with it loading) fails.
                    // Under IL2CPP any use of the type runs it, which is why Game() looks the setting up by name.
                    // FSR and XeSS once the native side has probed them on the bridge's D3D12 device; after
                    // 20 s without it, only what the game itself offers.
                    bool ready = Session.HaveStatus && Session.Status.DeviceReady != 0;
                    if (!ready && now - firstTick < 20f)
                    {
                        Status = Session.Upscaler.Connected ? "Waiting for the native capability report..." : Session.Upscaler.Detail;
                        return;
                    }
                    string dlssDetail = "";
                    var dlssModes = UpScaling.IsDLSSAvailable ? DlssModes.Query(out dlssDetail) : new QualityMode[0];
                    if (dlssDetail.Length > 0) Session.Log.LogWarning(dlssDetail + ".");
                    // The game's own FSR 2, where this game build has it and the game offers it on this PC (it
                    // forbids the entry otherwise), under the name the game gives it.
                    var gameRow = up.UpscalingTypeOption;
                    var gameFsr = GameFsrEntry(VideoTab.Entries(gameRow));
                    string gameFsrName = gameFsr == null || !GameOffers(gameRow, gameFsr) ? null : string.IsNullOrEmpty(gameFsr.DisplayName) ? "FSR" : gameFsr.DisplayName;
                    settings.ApplyCapabilities(new Capabilities(true, UpScaling.IsSTPAvailable, UpScaling.IsDLSSAvailable,
                        ready && Session.Status.Has(NativeFlags.BackendFsr), ready && Session.Status.Has(NativeFlags.BackendXess),
                        ready ? Session.Upscaler.FsrVersions(Session.Status.FsrVersionCount) : null, dlssModes,
                        ready ? Session.Upscaler.Modes(NativeFlags.BackendFsr) : null, ready ? Session.Upscaler.Modes(NativeFlags.BackendXess) : null, gameFsrName),
                        up.QualityScaling);
                }
                var reported = settings.Capabilities;
                // Without FSR and XeSS the row is still extended when DLSS has quality modes to offer: those
                // don't depend on either (Install greys out the entries this GPU can't run).
                if (!reported.Fsr && !reported.Xess && reported.Modes(UpscalerSetting.DLSS).Length == 0)
                {
                    Status = "Neither FSR nor XeSS is available on this GPU (" + Session.StatusText + "); the game's row is left as it is.";
                    if (!failed) BepInExLog(Status);
                    failed = true;
                    return;
                }
                Install(up, reported);
            }
            catch (Exception ex)
            {
                failed = true;
                // The game applies its upscaler through this row, so without it FSR and XeSS can't be chosen at all.
                Status = "Upscaling row failed, so FSR and XeSS can't be chosen this session (the game's own upscalers still work): " + ex.GetType().Name;
                Session.Log.LogWarning(Status + "\n" + ex);
            }
        }
        private static void BepInExLog(string text) => Session.Log.LogInfo(text);
        private static void Install(UpScaling up, Capabilities reported)
        {
            caps = reported;
            type = up.UpscalingTypeOption;
            var entries = VideoTab.Entries(type);
            var gameFsr = GameFsrEntry(entries);
            if (entries.Length == 3 || gameFsr != null)
            {
                // With FSR from AMD's runtime, this mod's entry stands at index 3 under the name of the version that
                // runs, in place of the game's own FSR 2 where the game has one. Without it the game's entry stays, and
                // the game runs its FSR 2 there (StpHost leaves it to the game's controller).
                fsrChoice = gameFsr != null && !caps.Fsr ? gameFsr : VideoTab.Choice(FsrId, Capabilities.Label(UpscalerSetting.FSR));
                xessChoice = VideoTab.Choice(XessId, Capabilities.Label(UpscalerSetting.XeSS));
                bool onGameFsr = gameFsr != null && VideoTab.Same(type.Option, gameFsr);
                VideoTab.SetEntries(type, entries.Take(FsrIndex).Concat(new[] { fsrChoice, xessChoice }).ToArray());
                // The row's current entry has to be one of its entries: the game reads and stores its index.
                if (onGameFsr && !VideoTab.Same(fsrChoice, gameFsr)) type.Option = fsrChoice;
            }
            else if (entries.Length == 5 && entries[FsrIndex].ID == FsrId && entries[XessIndex].ID == XessId)
            {
                fsrChoice = entries[FsrIndex]; xessChoice = entries[XessIndex]; // the game rebuilt its model around the same row
            }
            else
            {
                failed = true;
                Status = "The game's Upscaling row has " + entries.Length + " entries (" + string.Join(", ", entries.Select(e => e.ID).ToArray()) +
                    "), not the None, STP, DLSS and FSR this mod knows; left as it is, and the game sets its upscalers up itself.";
                Session.Log.LogWarning(Status);
                return;
            }
            // Unavailable entries are forbidden, the way the game forbids DLSS without an NVIDIA GPU.
            VideoTab.Forbid(type, new[]
            {
                caps.Stp ? null : VideoTab.Entries(type)[(int)UpscalerSetting.STP],
                caps.Dlss ? null : VideoTab.Entries(type)[(int)UpscalerSetting.DLSS],
                caps.Offers(UpscalerSetting.FSR) ? null : fsrChoice,
                caps.Xess ? null : xessChoice
            }.Where(o => o != null));

            // FSR's own sharpening, nested under the row like the quality slider. DLSS and XeSS have none:
            // NVIDIA dropped DLSS's sharpening, and XeSS never had one.
            sharpness = new SliderOption(IdPrefix + "FsrSharpness", "FSR Sharpness", 0f, 1f, false, "{0:P0}", Settings.DefaultSharpness, false, NoStep);
            VideoTab.Tooltip(sharpness, () => "Sharpening FSR applies after upscaling. 0% is off, 50% the default." + FsrVersionSentence());
            var quality = Quality(up);
            // Each upscaler's own quality modes in place of the slider, which STP keeps.
            modeRows.Clear();
            var dlssMode = ModeRow(UpscalerSetting.DLSS, "DlssMode");
            var fsrMode = ModeRow(UpscalerSetting.FSR, "FsrMode");
            var xessMode = ModeRow(UpscalerSetting.XeSS, "XessMode");
            var sliderRows = quality != null ? new PrefOption[] { quality } : None;
            var dlssRows = dlssMode != null ? new PrefOption[] { dlssMode } : sliderRows;
            // The game's own FSR 2 keeps the slider and has no sharpening of this mod's.
            var fsrRows = caps.Fsr ? new PrefOption[] { (PrefOption)fsrMode ?? quality, sharpness }.Where(o => o != null).ToArray() : sliderRows;
            var xessRows = xessMode != null ? new PrefOption[] { xessMode } : sliderRows;
            var every = new PrefOption[] { quality, dlssMode, fsrMode, xessMode, sharpness }.Where(o => o != null).ToArray();
#if BEPINEX_IL2CPP
            Il2CppTypes.Register<SwitchRowsOption>();
#endif
            var rows = quality != null ? SwitchRowsOption.Create(type, i => i <= 0 ? None : i == (int)UpscalerSetting.DLSS ? dlssRows
                : i == FsrIndex ? fsrRows : i == XessIndex ? xessRows : sliderRows, every) : null;
            bool nested = rows != null && SetMain(up, rows);
            owner = up;
            StpHost.HookPresets(); // the game's Upscaling setting exists now, so its type is initialized

            // The index a previous session stored: the game could not select FSR or XeSS before the row had them.
            int stored = PrefMemory.GetInt(TypeKey, -1);
            if ((stored == FsrIndex && caps.Offers(UpscalerSetting.FSR)) || (stored == XessIndex && caps.Xess)) type.OptionInt = stored;
            // The game closed during a troubleshooting test: its preferences hold the test's upscaler, the
            // config the player's.
            if (InterruptedTest())
            {
                if (caps.Offers(Session.Settings.Upscaler))
                {
                    type.OptionInt = (int)Session.Settings.Upscaler;
                    Session.Log.LogInfo("The last troubleshooting test didn't finish; upscaling is back to " + Session.Settings.Upscaler + ".");
                }
                Recovered();
            }
            SyncFsrRows();
            SyncModeRows();
            // The game's own apply: stores the rows' in-game values and raises the refresh its upscaling
            // controller acts on, even with nothing changed: the controller set the camera up before these
            // rows existed, and now hands it to this mod for FSR and XeSS, or sets DLSS to its quality mode
            // (see StpHost). The config mirrors the row from here on.
            ApplyRefreshing(up);
            OnGameApplied(up);
            Status = "FSR " + (caps.Fsr ? "available" : "unavailable") + ", XeSS " + (caps.Xess ? "available" : "unavailable") +
                " in the game's Upscaling row" + (nested ? "" : " (FSR Sharpness and the quality modes only in the config: this game build's row cannot nest them)") +
                (gameFsr == null ? "" : caps.Fsr ? "; this game build has an FSR entry of its own (FSR 2), which this mod's FSR takes the place of"
                    : caps.GameFsr != null ? "; the game's own FSR 2 keeps its entry (\"" + caps.GameFsr + "\"), at the game's slider"
                    : "; the game's own FSR entry stays greyed out, as the game has it on this PC") +
                "; quality modes " + string.Join("; ", Settings.WithModes.Select(u => u + ": " + (caps.Modes(u).Length > 0 ? string.Join(", ", caps.Modes(u).Select(m => m.ToString()).ToArray()) : "none, the slider")).ToArray()) +
                (caps.FsrVersions.Length > 0 ? "; FSR versions offered " + string.Join(", ", caps.FsrVersions) : "") +
                (caps.FsrVersion != null ? "; the row shows FSR as \"" + caps.LabelOf(UpscalerSetting.FSR) + "\" (" + caps.FsrVersion + ", AMD's default here)" : "") +
                (stored == FsrIndex || stored == XessIndex ? "; restored " + (UpscalerSetting)stored + " from the game's preferences" : "") + ".";
            Session.Log.LogInfo(Status);
        }
        private static void SyncFsrRows()
        {
            var settings = Session.Settings;
            if (sharpness != null) sharpness.Value = settings.Sharpness;
        }
        // A Quality Mode row with the modes the runtime reports, or null when it reports none. Restore
        // Defaults picks the vendor's "Quality".
        private static EnumArrowsOption ModeRow(UpscalerSetting upscaler, string id)
        {
            var offered = caps.Modes(upscaler);
            if (offered.Length == 0) return null;
            var names = offered.Select(m => m.Name).ToArray();
            var row = VideoTab.Enum(IdPrefix, id, "Quality Mode", Array.IndexOf(names, QualityModes.Fallback(offered).Name), out _, names);
            VideoTab.Tooltip(row, () => ModeTooltip(upscaler, row));
            modeRows[upscaler] = row;
            return row;
        }
        private static string ModeTooltip(UpscalerSetting upscaler, EnumArrowsOption row)
        {
            var offered = caps.Modes(upscaler);
            var mode = offered[Mathf.Clamp(row.OptionInt, 0, offered.Length - 1)];
            int width = Screen.width, height = Screen.height;
            string size = Mathf.RoundToInt(width * mode.Scale / 100f) + "x" + Mathf.RoundToInt(height * mode.Scale / 100f);
            string why = upscaler == UpscalerSetting.DLSS ? "NVIDIA's driver picks the DLSS model that works best for this mode."
                : upscaler == UpscalerSetting.FSR ? "AMD recommends this render resolution for this mode." + FsrVersionSentence() : "Intel recommends this render resolution for this preset.";
            return "Renders at " + mode.Scale.ToString("0.#") + "% (" + size + ") of " + width + "x" + height + ". " + why;
        }
        // The FSR version running on this GPU, in full, for the FSR rows' tooltips: the one AMD's runtime named when FSR
        // last started, else its default (the newest it offers here); "" while unknown.
        private static string FsrRunning() =>
            Session.HaveStatus && Session.Status.ActiveBackend == NativeFlags.BackendFsr ? Session.ActiveFsrVersion() : caps?.FsrVersion;
        private static string FsrVersionSentence()
        {
            string version = FsrRunning();
            return string.IsNullOrEmpty(version) ? "" : " This GPU runs FSR " + version + ", AMD's choice for it.";
        }
        // Once FSR has started: the version AMD's runtime chose, in the log, and a warning if the row names another.
        private static bool fsrVersionLogged;
        private static void LogFsrVersion()
        {
            if (fsrVersionLogged || caps == null || !Session.HaveStatus || Session.Status.ActiveBackend != NativeFlags.BackendFsr) return;
            fsrVersionLogged = true;
            string running = Session.ActiveFsrVersion(), shown = caps.LabelOf(UpscalerSetting.FSR);
            if (Capabilities.ShortVersion(running) == Capabilities.ShortVersion(caps.FsrVersion))
                Session.Log.LogInfo("FSR runs " + running + " on this GPU; the Upscaling row names it " + shown + ".");
            else
                Session.Log.LogWarning("FSR runs " + running + " on this GPU, though the Upscaling row names it " + shown + " (the newest AMD's runtime offers here); it keeps that name this session.");
        }
        private static void SyncModeRows()
        {
            foreach (var pair in modeRows)
            {
                var offered = caps.Modes(pair.Key);
                var mode = Session.Settings.Mode(pair.Key);
                int index = mode != null ? Array.FindIndex(offered, m => m.Name == mode.Name) : -1;
                if (index >= 0) pair.Value.OptionInt = index;
            }
        }
        private static Dictionary<UpscalerSetting, string> ChosenModes()
        {
            var chosen = new Dictionary<UpscalerSetting, string>();
            foreach (var pair in modeRows)
            {
                var offered = caps.Modes(pair.Key);
                chosen[pair.Key] = offered[Mathf.Clamp(pair.Value.OptionInt, 0, offered.Length - 1)].Name;
            }
            return chosen;
        }
        // The game's graphics preset set its render-scale slider (UpScaling.SetValueForPreset): each
        // upscaler's mode follows by the same rule as the first start, the highest-quality mode at or
        // below that scale. Like the slider, the rows change in the menu and take effect on Apply.
        internal static void OnPreset(UpScaling up)
        {
            if (owner == null || caps == null || !VideoTab.Same(up, owner)) return;
            float percent = up.QualityScaling;
            foreach (var pair in modeRows)
            {
                var offered = caps.Modes(pair.Key);
                var mode = QualityModes.Default(offered, percent);
                if (mode != null) pair.Value.OptionInt = Array.IndexOf(offered, mode);
            }
        }
        private static bool InterruptedTest()
        {
            try { return Troubleshooting.InterruptedRun; } catch { return false; } // a bridge from before the report
        }
        // The bridge keeps its marker of the interrupted run until this mod has put the upscaler back.
        private static void Recovered()
        {
            try { RecoveredWithBridge(); } catch { } // in a method of its own, for a bridge from before it
        }
        private static void RecoveredWithBridge() => Troubleshooting.InterruptedRunRecovered();
        // The upscaler running now: a troubleshooting step's, which the config doesn't hold, or the player's.
        internal static UpscalerSetting Current => testing && type != null ? (UpscalerSetting)Mathf.Clamp(type.OptionInt, 0, XessIndex) : Session.Settings.Upscaler;
        // The upscalers a troubleshooting test measures: every one the row offers, each at the player's mode.
        internal static UpscalerSetting[] Testable => owner != null && caps != null ? caps.Upscalers : new UpscalerSetting[0];
        internal static void Test(UpscalerSetting upscaler)
        {
            if (owner == null || !caps.Offers(upscaler)) return;
            testing = true;
            type.OptionInt = (int)upscaler;
            SyncModeRows();
            ApplyRefreshing(owner);
        }
        // Something the camera setup depends on changed outside the Video tab (TG Path Tracing starting or
        // stopping, which switches DLSS between Super Resolution and Ray Reconstruction): the game applies its
        // Upscaling setting again, through this mod's hook. Never while the settings screen is open.
        // false while the settings screen is open (the caller tries again after it closes).
        internal static bool Reapply()
        {
            if (owner == null || VideoTab.IsSettingsOpen) return false;
            ApplyRefreshing(owner);
            return true;
        }
        // The player's upscaler, from the config, after a troubleshooting test.
        internal static void EndTest()
        {
            if (!testing) return;
            testing = false;
            if (owner != null) ApplyConfig();
        }
        // Config to rows, then the game's own apply.
        private static void ApplyConfig()
        {
            var settings = Session.Settings;
            int wanted = (int)settings.Upscaler;
            var entries = VideoTab.Entries(type);
            if (wanted >= 0 && wanted < entries.Length && caps.Offers(settings.Upscaler)) type.OptionInt = wanted;
            SyncFsrRows();
            SyncModeRows();
            ApplyRefreshing(owner);
            var mode = settings.Mode(settings.Upscaler);
            Session.Log.LogInfo("Upscaling set from the config: " + settings.Upscaler + (mode != null ? " " + mode.Name : "") +
                (settings.Upscaler == UpscalerSetting.FSR ? " (sharpness " + settings.Sharpness.ToString("0.00") + ")" : "") + ".");
        }
        // The game applied its Upscaling setting (the Video tab, a preset, a resolution change, or this mod).
        internal static void OnGameApplied(UpScaling up)
        {
            if (owner == null || !VideoTab.Same(up, owner) || testing) return;
            var chosen = (UpscalerSetting)Mathf.Clamp(type.OptionInt, 0, XessIndex);
            Session.Settings.Store(chosen, sharpness != null ? sharpness.Value : Session.Settings.Sharpness, ChosenModes());
        }
    }
}
