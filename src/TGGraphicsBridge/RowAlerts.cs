// SPDX-License-Identifier: MIT
// The mods' problems and warnings (Notices) on the Video rows they concern, instead of rows of their
// own: the Frame Generation row and the game's upscaling row (Dynamic Resolution, which TG Better
// Upscaling extends). Such a row gets an alert icon after its name (AlertIcon) and the most serious
// problem, with what to do, as its tooltip; the performance overlay lists all of them. TG Frame
// Generation registers its row and builds its own tooltip from Text; the game's upscaling row has no
// tooltip of its own, so the bridge gives it one while there is something to say. Each mod claims its
// row once it has started; one that BepInEx loaded but that never claimed it stopped while starting,
// and gets a "Didn't start" problem. When TG Frame Generation has a problem and no row of its own (it
// can't load, or didn't start), the bridge shows a stand-in Frame Generation row that reads Off,
// greyed out, with the same icon and tooltip.
using System;
using System.Collections.Generic;
using System.Linq;
using Awaken.TG.Main.Settings;
using Awaken.TG.Main.Settings.Options;
using Awaken.TG.Main.Settings.Windows;
using Awaken.TG.MVC;
#if BEPINEX_IL2CPP
using Il2CppInterop.Runtime.Injection;
#endif
namespace TGGraphicsBridge
{
    public static class RowAlerts
    {
        // The rows a mod registered, by row; the stand-in and the game's upscaling row are the bridge's own.
        private static readonly Dictionary<AlertRow, PrefOption> registered = new Dictionary<AlertRow, PrefOption>();
        private static readonly HashSet<AlertRow> claimed = new HashSet<AlertRow>();
        private static float firstTick = -1f;
        private static bool startsChecked;
        // A mod that started: TG Frame Generation and TG Better Upscaling at the end of their start.
        public static void Claim(AlertRow row) { lock (claimed) claimed.Add(row); }
        internal static bool Claimed(AlertRow row) { lock (claimed) return claimed.Contains(row); }
        private static PrefOption upscaling;
        private static bool upscalingTip, failed;
        private static float nextTick;
        // TG Frame Generation's Frame Generation row, each time it builds its rows.
        public static void Register(AlertRow row, PrefOption option)
        {
            if (option == null) return;
            lock (registered) registered[row] = option;
        }
        public static bool Has(AlertRow row) => Notices.For(row).Count > 0;
        // The tooltip of a row with problems: the most serious one, as "Source: Label. Summary", and how many
        // more the performance overlay lists; "" when the row has none.
        public static string Text(AlertRow row)
        {
            var list = Notices.For(row);
            if (list.Count == 0) return "";
            var n = list[0];
            string text = n.Source + ": " + n.Label + ". " + (n.Summary ?? n.Detail);
            if (list.Count > 1) text += " (" + (list.Count - 1) + " more in the performance overlay.)";
            return text;
        }
        // From the bridge's per-frame component, four times a second.
        internal static void Tick(float now)
        {
            if (failed || now < nextTick) return;
            nextTick = now + 0.25f;
            if (firstTick < 0f) firstTick = now;
            try
            {
                if (!startsChecked && now - firstTick > 3f) { startsChecked = true; CheckStarts(); }
                UpscalingTooltip();
                StandIn.Tick();
                if (!VideoTab.IsSettingsOpen) { AlertIcon.Clear(); return; }
                var ui = World.Any<AllSettingsUI>();
                if (ui == null) return;
                var alerted = new HashSet<object>();
                if (Has(AlertRow.NeuralRendering))
                {
                    lock (registered) if (registered.TryGetValue(AlertRow.NeuralRendering, out var nr)) alerted.Add(VideoTab.Identity(nr));
                }
                if (Has(AlertRow.FrameGeneration))
                {
                    lock (registered) if (registered.TryGetValue(AlertRow.FrameGeneration, out var fg)) alerted.Add(VideoTab.Identity(fg));
                    if (StandIn.Row != null) alerted.Add(VideoTab.Identity(StandIn.Row));
                }
                if (Has(AlertRow.Upscaling) && upscaling != null) alerted.Add(VideoTab.Identity(upscaling));
                AlertIcon.Show(VideoTab.FocusableViews(ui).Where(v => v.GenericOption != null && alerted.Contains(VideoTab.Identity(v.GenericOption))).ToList());
            }
            catch (Exception ex)
            {
                failed = true;
                AlertIcon.Clear();
                BridgeLog.Log.LogWarning("The mods' problems could not be shown on the Video rows; they are in the log and the performance overlay: " + ex);
            }
        }
        // Once, after every plugin had the chance to start: a mod whose assembly BepInEx loaded but that never
        // claimed its row threw while starting. Its own log line never came, so this is all anyone sees of it.
        private static void CheckStarts()
        {
            foreach (var mod in new[] { new { Folder = "TGFrameGeneration", Title = "TG Frame Generation", Row = AlertRow.FrameGeneration },
                                        new { Folder = "TGBetterUpscaling", Title = "TG Better Upscaling", Row = AlertRow.Upscaling },
                                        new { Folder = "TGNeuralRendering", Title = "TG Neural Rendering", Row = AlertRow.NeuralRendering } })
            {
                if (InstallCheck.LoadedFrom(mod.Folder) == null || Claimed(mod.Row)) continue;
                Notices.Set("start." + mod.Folder, NoticeLevel.Problem, mod.Title, "Didn't start",
                    "BepInEx loaded it, but it stopped while starting. Reinstall it; if that doesn't help, please report it with a troubleshooting report.",
                    "BepInEx loaded " + mod.Title + " (" + InstallCheck.LoadedFrom(mod.Folder) + "), but it stopped with an error while starting, before it " +
                    "logged anything. This game doesn't write Unity's log, where the error would be. Reinstall the mod; if it still doesn't start, " +
                    "please report it with a troubleshooting report.");
            }
        }
        // The game's upscaling row gets the bridge's tooltip while it has problems, and none otherwise, as
        // the game leaves it.
        private static void UpscalingTooltip()
        {
            var row = VideoTab.UpscalingRow();
            if (row == null) return;
            if (upscaling == null || !VideoTab.Same(row, upscaling)) { upscaling = row; upscalingTip = false; }
            bool want = Has(AlertRow.Upscaling);
            if (want == upscalingTip) return;
            upscalingTip = want;
            if (want) VideoTab.Tooltip(upscaling, () => Text(AlertRow.Upscaling));
            else upscaling.AddTooltip(null);
        }
    }
    // The stand-in Frame Generation row: only while TG Frame Generation has a problem and never started, so
    // it has no row of its own. Changed only while the settings screen is closed.
    internal static class StandIn
    {
        private const string Source = "TG Frame Generation";
        internal static EnumArrowsOption Row;
        private static StandInSetting setting;
        private static StandInSlot slot;
        private static SettingsMaster owner;
        internal static void Tick()
        {
            if (VideoTab.IsSettingsOpen) return;
            var master = World.Any<SettingsMaster>();
            if (master == null) return;
            var list = VideoTab.GraphicSettings(master);
            if (list == null) return;
            bool wanted = !RowAlerts.Claimed(AlertRow.FrameGeneration) && Notices.For(AlertRow.FrameGeneration).Any(n => n.Source == Source);
            if (setting != null && !VideoTab.Same(master, owner)) setting = null; // a new settings model: build again
            if (setting == null)
            {
                if (!wanted) return;
#if BEPINEX_IL2CPP
                Il2CppTypes.Register<StandInSetting>(genericBase: true);
#endif
                slot = new StandInSlot();
                Row = VideoTab.Enum("TGGB.StandIn.", "FrameGeneration", "Frame Generation", 0, out _, "Off");
                Row.SetInteractabilityFunction((Func<bool>)(() => false));
                VideoTab.Tooltip(Row, () => RowAlerts.Text(AlertRow.FrameGeneration));
                setting = new StandInSetting(slot);
                owner = master;
                VideoTab.AddGraphicSetting(master, setting);
                BridgeLog.Log.LogInfo("TG Frame Generation is installed but didn't load; its Frame Generation row stands in, reading Off.");
            }
            slot.Set(wanted ? new PrefOption[] { Row } : new PrefOption[0]);
            if (VideoTab.IndexOf(list, setting) >= 0) VideoTab.Reposition(list, setting, "UpScaling", "*Dlss5");
        }
    }
    internal sealed class StandInSetting : Setting
    {
        private readonly StandInSlot slot;
#if BEPINEX_IL2CPP
        private readonly Il2CppSystem.Collections.Generic.IEnumerable<PrefOption> options;
        public StandInSetting(IntPtr pointer) : base(pointer) { }
        internal StandInSetting(StandInSlot slot) : base(ClassInjector.DerivedConstructorPointer<StandInSetting>())
        {
            ClassInjector.DerivedConstructorBody(this);
            Il2CppTypes.Construct(this, typeof(Setting), "NativeMethodInfoPtr__ctor_Protected_Void_0");
            this.slot = slot;
            options = slot.Options.Cast<Il2CppSystem.Collections.Generic.IEnumerable<PrefOption>>(); // the same list, refilled
        }
        public new string SettingName => "Frame Generation";
        public new Il2CppSystem.Collections.Generic.IEnumerable<PrefOption> Options => options;
        public new bool AutoApplyOnInit => false;
#else
        internal StandInSetting(StandInSlot slot) { this.slot = slot; }
        public override string SettingName => "Frame Generation";
        public override IEnumerable<PrefOption> Options => slot.Rows;
        protected override bool AutoApplyOnInit => false;
#endif
    }
    internal sealed class StandInSlot
    {
        internal PrefOption[] Rows { get; private set; } = new PrefOption[0];
#if BEPINEX_IL2CPP
        internal readonly Il2CppSystem.Collections.Generic.List<PrefOption> Options = new Il2CppSystem.Collections.Generic.List<PrefOption>();
#endif
        internal void Set(PrefOption[] rows)
        {
            if (rows.Length == Rows.Length) return;
            Rows = rows;
#if BEPINEX_IL2CPP
            Options.Clear();
            foreach (var row in rows) Options.Add(row);
#endif
        }
    }
}
