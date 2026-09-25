// SPDX-License-Identifier: MIT
// DLSS Frame Generation with VSync on a screen connected to another GPU than the one the game renders on
// (a laptop's panel on its integrated GPU, or a monitor in the motherboard's port). NVIDIA's DLSS-G guide
// (22.6): VSync with frame generation relies on independent flip, and "If VSync is enabled with Frame
// Generation on a system that does not support IFLIP, high latency is expected. On such systems it is
// better to not enable VSync." Frames shown on another GPU's screen are copied to it and usually composed
// by Windows; that screen is the one case the mod can recognise.
//
// So while DLSS Frame Generation is the player's choice and available, and the screen is on the other
// GPU, the game's own VSync setting is turned off, and its toggle is greyed out with a tooltip saying why.
// It is the game's toggle, changed the way the player would change it. The game shares one row between
// VSync rates and frame limits (Target FPS) and moves it to the nearest plain limit when VSync goes off;
// the mod sets that row to Unlimited instead, so turning VSync off doesn't bring in a cap the player never
// chose. Both happen only at the moment VSync is turned off: a limit chosen afterwards stays. As with
// DisplayMode, the value is changed once rather than held down; Restore Defaults (which ignores a greyed
// row) and Cancel re-arm the check. When the condition ends the toggle is offered again and VSync stays
// off until the player turns it back on. Driver-forced VSync can't be changed here; the "High latency"
// row (FrameRateCheck) says where to change it.
//
// A screen on another GPU doesn't prove Windows composes the frames: hybrid laptops can flip them straight
// to the screen too (the two-copy hybrid path, Windows 11's cross-adapter scan-out), and there VSync off
// would tear. While VSync is held off, the bridge therefore presents without the tearing flag
// (TGFG_SetNoTearing): the newest frame shows at the next refresh, with no VSync wait and no tear.
using System;
using Awaken.TG.Main.Settings.Graphics;
using Awaken.TG.Main.Settings.Options;
using Awaken.TG.MVC;
using TGGraphicsBridge;
namespace TGFrameGeneration
{
    internal static class HybridVsync
    {
        internal static string Status = "";
        private static volatile bool pending = true, fromVideoApply;
        private static bool failed, holding, applyOwed, announced, unlimitedSet;
        private static ToggleOption hooked; // the VSync row the callbacks are on
        private const string UnlimitedId = "RefreshRate_Unlimited"; // ScreenResolution: "RefreshRate_" + "Unlimited"
        private static float nextCheck;
        private static ToggleOption vsync;
        private static string screen = "";
        internal static void Arm() { pending = true; }
        internal static void ArmFromVideoApply() { fromVideoApply = true; pending = true; }
        // From the session's poll: when something changed, and once a second for the display, which the
        // bridge re-checks by itself.
        internal static void Tick(float now)
        {
            if (!pending && now < nextCheck) return;
            nextCheck = now + 1f;
            pending = false;
            if (!Reconcile()) pending = true;
            // While VSync is held off, the bridge presents without the tearing flag: the frames on this screen
            // may be flipped straight to it (hybrid independent flip, cross-adapter scan-out), where turning
            // VSync off would otherwise tear. Sent every second, so a bridge that connects later gets it too.
            try { Session.Bridge?.SetNoTearing(holding); } catch { }
        }
        private static bool Wanted()
        {
            if (failed || !Session.Settings.VsyncOffForHybridDlss.Value || Session.Settings.FrameGeneration != FrameGenerationSetting.DLSS) return false;
            if (!Session.HaveStatus || !Session.Status.CapsKnown || !Session.Status.Has(FrameGenerationSetting.DLSS)) return false;
            try
            {
                if (GraphicsBridge.Fact("display.crossAdapter") != "1") return false;
                screen = GraphicsBridge.Fact("display.adapter");
                return true;
            }
            catch { return false; } // a bridge from before the facts
        }
        // False while there is more to do that cannot be done yet, so the next tick tries again.
        private static bool Reconcile()
        {
            // The Apply press counts for this reconcile only (see DisplayMode).
            bool videoApply = fromVideoApply;
            fromVideoApply = false;
            try
            {
                var resolution = World.Any<ScreenResolution>();
                if (resolution == null || resolution.VSyncOption == null) return false;
                vsync = resolution.VSyncOption;
                Hook(vsync);
                if (!Wanted()) { Release(); return true; }
                Hold(vsync);
                if (vsync.Enabled)
                {
                    // The game's own toggle, as the player would flip it: stores the choice, and the game's
                    // onChange moves the Target FPS row to the nearest limit without VSync. Nothing in effect
                    // changes yet, so this is safe with the Options screen open.
                    vsync.Enabled = false;
                    // The game has just moved Target FPS to the nearest plain limit; Unlimited instead.
                    var unlimited = Unlimited(resolution.RefreshRateOption);
                    if (unlimited != null) resolution.RefreshRateOption.Option = unlimited;
                    else Session.Log.LogWarning("The game's Target FPS row has no Unlimited entry; it keeps the limit the game chose when VSync went off.");
                    unlimitedSet = unlimited != null;
                    applyOwed = true;
                }
                if (!applyOwed) return true;
                // Applying the resolution setting applies its display, resolution and refresh rows with it,
                // so it waits for the Options screen to close, unless the player just applied the Video tab.
                if (VideoTab.IsSettingsOpen && !videoApply) return false;
                applyOwed = false;
                bool needRestart;
                resolution.Apply(out needRestart);
                Status = "VSync turned off for DLSS Frame Generation on " + screen;
                Session.Log.LogInfo("VSync turned off: DLSS Frame Generation runs and the game's screen is connected to " + screen +
                    ", not to the GPU the game renders on. NVIDIA's DLSS-G guide expects high latency with VSync there. The game's own " +
                    "VSync setting now reads Off" + (unlimitedSet ? " and Target FPS Unlimited" : "") + (needRestart ? "; the game asks for a restart" : "") + ".");
                if (!announced)
                {
                    announced = true;
                    Notices.Show("fg.hybridvsync.off", "VSync turned off",
                        "DLSS Frame Generation has a lot of input delay with VSync on this screen, so the mod turned VSync off" +
                        (unlimitedSet ? " and set Target FPS to Unlimited. " : ". ") +
                        "It stays available with FSR or XeSS frame generation.", 12f);
                }
                return true;
            }
            catch (Exception ex)
            {
                failed = true;
                Release();
                Status = "Could not turn VSync off: " + ex.GetType().Name;
                Session.Log.LogWarning(Status + ". With DLSS Frame Generation on this screen, turn VSync off in the Video settings.\n" + ex);
                return true;
            }
        }
        // Greys the toggle out with the reason as its tooltip. The game sets neither on this row.
        private static void Hold(ToggleOption option)
        {
            if (holding) return;
            holding = true;
            VideoTab.Tooltip(option, () => "Off while DLSS Frame Generation runs: this screen is connected to " + screen +
                ", not to the GPU the game renders on, and NVIDIA's frame generation has a lot of input delay with VSync there. " +
                "Choose FSR or XeSS frame generation to use VSync.");
            VideoTab.RefreshInteractability(new PrefOption[] { option });
            Session.Log.LogInfo("VSync is held off while DLSS Frame Generation runs on this screen; the toggle is greyed out.");
        }
        // The Target FPS row's Unlimited entry, or null. Under IL2CPP the row's backing array is read directly,
        // as DisplayMode does: enumerating the IEnumerable property found nothing there.
        private static ToggleOption Unlimited(EnumArrowsOption row)
        {
            if (row == null) return null;
#if BEPINEX_IL2CPP
            var entries = row._options;
            if (entries == null) return null;
            for (int i = 0; i < entries.Length; i++)
                if (entries[i] != null && entries[i].ID == UnlimitedId) return entries[i];
#else
            foreach (var entry in row.Options)
                if (entry != null && entry.ID == UnlimitedId) return entry;
#endif
            return null;
        }
        // Offers the toggle again. Safe to call when it isn't held.
        internal static void Release()
        {
            applyOwed = false;
            if (!holding) return;
            holding = false;
            try
            {
                vsync?.AddTooltip(null);
                if (vsync != null) VideoTab.RefreshInteractability(new PrefOption[] { vsync });
                Status = "";
                Session.Log.LogInfo("VSync can be turned on again in the Video settings.");
            }
            catch (Exception ex) { Session.Log.LogWarning("Could not offer VSync again: " + ex.GetType().Name + ". Restart the game to restore it.\n" + ex); }
        }
        // Once per VSync row: the interactability follows the hold, and every change of the toggle (the player,
        // Cancel, Restore Defaults) re-arms the check. Interop converts the managed delegates in both runtimes.
        // The game builds its VSync row again when the window moves to another monitor
        // (ScreenResolution.ResetScreenResolution calls Init): the new row gets the callbacks, and the hold
        // is put on it again, with its grey state and tooltip.
        private static void Hook(ToggleOption option)
        {
            if (hooked != null && VideoTab.Same(hooked, option)) return;
            if (hooked != null) holding = false; // the hold was on the discarded row
            hooked = option;
            option.SetInteractabilityFunction((Func<bool>)(() => !holding));
            option.onChange += (Action<bool>)((bool ignored) => pending = true);
        }
    }
}
