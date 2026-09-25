// SPDX-License-Identifier: MIT
// The performance overlay's row in the game's Video settings, the last one there: Performance Overlay
// (Off, On), and nested under it Overlay Position and Overlay Size while it is on, and always the button
// that creates a troubleshooting report (Troubleshooting). A real game Setting, so it
// follows Apply Changes, Undo and Restore Defaults like the game's own rows, and it stores its values in
// TG Graphics Bridge's config, whose Video entries FoA Mod Manager lists. The bridge's plugin loads once
// however many mods bundle it, so the row is added once; a Performance Overlay already in the list (from
// another copy of the bridge) is left to be the only one.
using System;
using System.Collections.Generic;
using System.Linq;
using Awaken.TG.Main.Settings;
using Awaken.TG.Main.Settings.Options;
using Awaken.TG.MVC;
using UnityEngine;
#if BEPINEX_IL2CPP
using Il2CppInterop.Runtime.Injection;
#endif
namespace TGGraphicsBridge
{
    internal sealed class PerformanceOverlaySetting : Setting
    {
        internal const string Name = "Performance Overlay";
        private readonly PerformanceOverlayRows rows;
#if BEPINEX_IL2CPP
        private readonly Il2CppSystem.Collections.Generic.List<PrefOption> list = new Il2CppSystem.Collections.Generic.List<PrefOption>();
        private readonly Il2CppSystem.Collections.Generic.IEnumerable<PrefOption> options;
        public PerformanceOverlaySetting(IntPtr pointer) : base(pointer) { }
        internal PerformanceOverlaySetting(PerformanceOverlayRows rows) : base(ClassInjector.DerivedConstructorPointer<PerformanceOverlaySetting>())
        {
            ClassInjector.DerivedConstructorBody(this);
            Il2CppTypes.Construct(this, typeof(Setting), "NativeMethodInfoPtr__ctor_Protected_Void_0");
            this.rows = rows;
            foreach (var option in rows.Top) list.Add(option);
            options = list.Cast<Il2CppSystem.Collections.Generic.IEnumerable<PrefOption>>();
        }
        public new string SettingName => Name;
        public new Il2CppSystem.Collections.Generic.IEnumerable<PrefOption> Options => options;
        public new bool AutoApplyOnInit => false;
        public new void OnApply() => rows.Apply();
#else
        internal PerformanceOverlaySetting(PerformanceOverlayRows rows) { this.rows = rows; }
        public override string SettingName => Name;
        public override IEnumerable<PrefOption> Options => rows.Top;
        protected override bool AutoApplyOnInit => false;
        protected override void OnApply() => rows.Apply();
#endif
    }
    internal sealed class PerformanceOverlayRows
    {
        private const string IdPrefix = "TGGB.Video.";
        private readonly EnumArrowsOption shown, corner;
        private readonly SliderOption size;
        private readonly ButtonOption report;
        internal PrefOption[] Top { get; }
        internal PerformanceOverlayRows()
        {
            shown = VideoTab.Enum(IdPrefix, "PerformanceOverlay", PerformanceOverlaySetting.Name, 0, out _, "Off", "On");
            VideoTab.Tooltip(shown, () => PerformanceOverlay.Description);
            corner = VideoTab.Enum(IdPrefix, "OverlayPosition", "Overlay Position", 0, out _, "Top Left", "Top Right", "Bottom Left", "Bottom Right");
            VideoTab.Tooltip(corner, () => PerformanceOverlay.PositionDescription);
#if BEPINEX_IL2CPP
            // The step is a Nullable<float>, a boxed object in the interop that may not be null itself; an
            // empty one is the game's default step, as null is on Mono.
            var step = new Il2CppSystem.Nullable<float>();
#else
            float? step = null;
#endif
            size = new SliderOption(IdPrefix + "OverlaySize", "Overlay Size", 0.5f, 2f, false, "{0:P0}", 1f, false, step);
            VideoTab.Tooltip(size, () => PerformanceOverlay.SizeDescription);
            // The troubleshooting report, nested under the overlay with its position and size: the two things a
            // bug report needs. It stays there with the overlay off.
            report = VideoTab.Button(IdPrefix + "TroubleshootingReport", "Create Troubleshooting Report", Troubleshooting.RequestFromMenu);
            VideoTab.Tooltip(report, Troubleshooting.Tooltip);
            var whenOn = new PrefOption[] { corner, size, report };
            var whenOff = new PrefOption[] { report };
            var rows = SwitchRowsOption.Create(shown, i => i == 1 ? whenOn : whenOff, whenOn);
            Top = rows != null ? new PrefOption[] { rows } : new PrefOption[] { shown, corner, size, report }; // older game builds: all rows, always
            SyncFromConfig();
        }
        // Config to rows; applying stores the in-game values, so the game reports no unsaved changes.
        internal void SyncFromConfig()
        {
            shown.OptionInt = PerformanceOverlay.Visible ? 1 : 0;
            corner.OptionInt = Mathf.Clamp((int)PerformanceOverlay.Corner, 0, 3);
            size.Value = PerformanceOverlay.Size;
            shown.Apply(); corner.Apply(); size.Apply();
        }
        internal void Apply() => PerformanceOverlay.Store(shown.OptionInt == 1, (OverlayCorner)Mathf.Clamp(corner.OptionInt, 0, 3), size.Value);
    }
    internal static class PerformanceOverlayRow
    {
        private static PerformanceOverlaySetting setting;
        private static PerformanceOverlayRows rows;
        private static SettingsMaster owner;
        private static bool failed, subscribed, syncPending, sawOther;
        private static float nextPoll;
        internal static void Tick(float now)
        {
            if (failed || now < nextPoll) return;
            nextPoll = now + 0.5f;
            try
            {
                if (!subscribed)
                {
                    // FoA Mod Manager or the config file changed a value: the row follows once the settings
                    // screen is closed, never under the player's hands.
                    subscribed = true;
                    PerformanceOverlay.Changed += () => syncPending = true;
                }
                var master = World.Any<SettingsMaster>();
                if (master == null) return;
                var list = VideoTab.GraphicSettings(master);
                if (list == null) { failed = true; BridgeLog.Log.LogWarning("Game settings API not found; the Performance Overlay stays in FoA Mod Manager and the config."); return; }
                bool open = VideoTab.IsSettingsOpen;
                if (setting != null && VideoTab.Same(master, owner) && VideoTab.IndexOf(list, setting) >= 0)
                {
                    if (open) return;
                    if (syncPending) { syncPending = false; rows.SyncFromConfig(); }
                    VideoTab.MoveToEnd(list, setting); // the last row, also after rows other mods add later
                    return;
                }
                if (open) return; // never change the list while the screen is open
                for (int i = 0; i < list.Count; i++)
                {
                    if (VideoTab.TypeName(list[i]) != nameof(PerformanceOverlaySetting)) continue;
                    if (!sawOther) { sawOther = true; BridgeLog.Log.LogInfo("The Video settings already have a Performance Overlay row; not adding a second one."); }
                    return;
                }
#if BEPINEX_IL2CPP
                Il2CppTypes.Register<SwitchRowsOption>();
                Il2CppTypes.Register<PerformanceOverlaySetting>(genericBase: true);
#endif
                rows = new PerformanceOverlayRows();
                setting = new PerformanceOverlaySetting(rows);
                owner = master;
                syncPending = false;
                VideoTab.AddGraphicSetting(master, setting);
                VideoTab.MoveToEnd(list, setting);
                BridgeLog.Log.LogInfo("Performance Overlay added as the last row of the Video settings (" + (PerformanceOverlay.Visible ? "on" : "off") + ").");
            }
            catch (Exception ex)
            {
                failed = true;
                BridgeLog.Log.LogWarning("The Performance Overlay row could not be added to the Video settings; it stays in FoA Mod Manager and the config: " + ex);
            }
        }
    }
}
