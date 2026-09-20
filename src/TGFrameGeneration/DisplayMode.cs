// SPDX-License-Identifier: MIT
// Frame generation presents through TGFG's own swapchain, and DXGI will not let that swapchain exist
// on a window that already holds the display exclusively. A cold start in Exclusive Fullscreen fails
// to create the bridge at all; switching into it later keeps the bridge but leaves Unity's window
// deactivated, so the game reports itself unfocused, pauses, and frame generation suspends with it.
// Borderless Fullscreen at the same resolution avoids both, and Windows presents a flip-model
// swapchain the same way in either mode.
//
// TGFG therefore changes the game's own display setting rather than overriding the live mode behind
// its back: an override would leave Options reading "Fullscreen" while the game ran borderless, and
// the game would write the exclusive mode back whenever the player applied anything else. Going
// through the same Setting/PrefOption API the Video rows use means the setting says what is true and
// persists like any other choice.
//
// The mode is changed once, not held down. Choosing Exclusive Fullscreen again is prevented the way
// the game prevents any other unavailable choice - the entry is marked forbidden, so the row's arrows
// skip it - rather than by writing the value back under the player, which would make the two settings
// fight. Turning frame generation off offers the entry again.
//
// Two operations here are not the same and are not gated the same way. Marking the entry forbidden
// and correcting the row's value change nothing that is in effect, so they happen as soon as they are
// needed, including while the Options screen is open. Making the corrected value take effect is the
// game's own Setting.Apply, which applies that setting's resolution, refresh rate and VSync rows as
// well, so it waits until the player is not part way through editing them.
using System;
using Awaken.TG.Main.Settings.Graphics;
using Awaken.TG.Main.Settings.Options;
using Awaken.TG.MVC;
using UnityEngine;
#if BEPINEX_IL2CPP
using Il2CppInterop.Runtime.InteropTypes;
using Il2CppInterop.Runtime.InteropTypes.Arrays;
using ModeMap = Il2CppSystem.Collections.Generic.Dictionary<Awaken.TG.Main.Settings.Options.ToggleOption, UnityEngine.FullScreenMode>;
#else
using System.Reflection;
using ModeMap = System.Collections.Generic.Dictionary<Awaken.TG.Main.Settings.Options.ToggleOption, UnityEngine.FullScreenMode>;
#endif
namespace TGFrameGeneration
{
    internal static class DisplayMode
    {
        internal static string Status = "";
        // Armed at startup so a session that begins with frame generation already selected is checked
        // once; after that only when something changes the selection or the display row. Written from
        // whichever thread raised the change, read and cleared on the main thread.
        private static volatile bool pending = true;
        private static bool failed, hooked, applyOwed, noBorderless;
        // Set when the player pressed Apply in the Video tab, the one moment when applying the
        // display setting cannot disturb anything: the same press has just applied its sibling rows.
        private static volatile bool fromVideoApply;
        // The option TGFG currently forbids an entry on, and nothing else: the restriction is released
        // through this, so it can be lifted after a failure and at shutdown without finding it again.
        private static EnumArrowsOption blocked;
        internal static void Arm() { pending = true; }
        // The player just applied the Video tab. Waiting for the Options screen to close would make
        // them press Apply a second time before the display change took effect.
        internal static void ArmFromVideoApply() { fromVideoApply = true; pending = true; }
        internal static void Tick()
        {
            if (!pending) return;
            // Consumed before the work, not after, so a change raised while reconciling is not lost.
            pending = false;
            if (!Reconcile()) pending = true;
        }
        // Lifts the restriction and forgets it. Safe to call when there is none.
        internal static void Release()
        {
            var option = blocked;
            blocked = null;
            applyOwed = false;
            if (option == null) return;
            try
            {
                // Null rather than an empty array: the game never forbids anything on this row itself,
                // so this leaves it exactly as it was found.
                Unforbid(option);
                Session.Log.LogInfo("Exclusive Fullscreen is offered again in the game's display settings.");
            }
            catch (Exception ex)
            {
                Session.Log.LogWarning("Could not offer Exclusive Fullscreen again: " + ex.GetType().Name + ". " +
                    "Restart the game to restore the choice.\n" + ex);
            }
        }
        // False while there is more to do that cannot be done yet, so the next tick tries again.
        private static bool Reconcile()
        {
            try
            {
                var screen = World.Any<ScreenResolution>();
                if (screen == null) return false;
                var option = screen.FullscreenOption;
                if (option == null) return false;
                var modes = Modes(screen);
                if (modes == null) return false;
                Hook(option);
                var exclusive = Find(option, modes, FullScreenMode.ExclusiveFullScreen);
                var borderless = Find(option, modes, FullScreenMode.FullScreenWindow);
                bool wanted = !failed && Session.Settings.BorderlessForFg.Value &&
                              Session.Settings.FrameGeneration != FrameGenerationSetting.Off;
                // Without somewhere to move to, the entry has to stay selectable: a row whose every
                // choice is forbidden cannot be operated at all.
                if (!wanted || exclusive == null || borderless == null)
                {
                    Release();
                    if (wanted && borderless == null && !noBorderless)
                    {
                        noBorderless = true;
                        Status = "The game offers no Borderless Fullscreen option";
                        Session.Log.LogWarning(Status + "; leaving the display setting alone. Frame generation cannot run " +
                            "in Exclusive Fullscreen: choose Windowed, or turn off the borderless option in TGFG's advanced settings.");
                    }
                    return true;
                }
                Block(option, exclusive);
                FullScreenMode current;
                // Windowed and Borderless Fullscreen both work; only the exclusive mode is changed.
                if (TryMode(modes, option.Option, out current) && current == FullScreenMode.ExclusiveFullScreen)
                {
                    // Stores the choice in the game's own preferences and updates the row. Nothing that
                    // is in effect changes yet, so this is safe with the Options screen open, and it
                    // keeps the player from applying Exclusive Fullscreen from there.
                    option.Option = borderless;
                    applyOwed = true;
                }
                if (!applyOwed) return true;
                // Applying the display setting applies its resolution, refresh rate and VSync rows
                // with it, so it waits for the Options screen to close - unless the player just
                // pressed Apply there, which applied those same rows and asked for exactly this.
                if (NativeVideoSettings.IsSettingsOpen && !fromVideoApply) return false;
                applyOwed = false;
                fromVideoApply = false;
                // The game's own apply, which drives Screen.SetResolution with the resolution and
                // refresh rate this setting already holds.
                bool needRestart;
                screen.Apply(out needRestart);
                Session.NoteDisplayChange();
                Status = "Display setting changed to Borderless Fullscreen for frame generation";
                Session.Log.LogInfo("Display setting changed from Exclusive Fullscreen to Borderless Fullscreen: frame " +
                    "generation presents through its own swapchain, which cannot take the display exclusively. The game's " +
                    "own setting now reads Borderless Fullscreen and stays there" +
                    (needRestart ? "; the game asks for a restart" : "") + ".");
                return true;
            }
            catch (Exception ex)
            {
                // Never try again, but do not leave the player without the choice either.
                failed = true;
                Release();
                Status = "Could not change the display setting: " + ex.GetType().Name;
                Session.Log.LogWarning(Status + ". Choose Borderless Fullscreen in the game's display settings if frame " +
                    "generation does not start.\n" + ex);
                return true;
            }
        }
        // Bookkeeping follows the mutation, so a throw cannot leave the flag claiming a restriction the
        // option does not have, or hide one it does.
        private static void Block(EnumArrowsOption option, ToggleOption exclusive)
        {
            if (blocked != null) return;
            Forbid(option, exclusive);
            blocked = option;
            Session.Log.LogInfo("Exclusive Fullscreen is no longer offered in the game's display settings: frame generation " +
                "cannot run in it. Turn frame generation off to get the choice back.");
        }
        // Every path that changes the row - the arrows, Cancel, Restore Defaults - goes through the
        // setter, which invokes this, and Restore Defaults is the one that bypasses the forbidden list.
        // Re-arming rather than acting keeps the work out of the game's own call stack, at the cost of
        // one tick. Il2CppSystem.Action converts from the managed delegate and combines, so this is the
        // same code in both runtimes.
        private static void Hook(EnumArrowsOption option)
        {
            if (hooked) return;
            hooked = true;
            option.onChange += (Action<ToggleOption>)((ToggleOption ignored) => pending = true);
        }
        // Once per session. A row this cannot read makes every decision below it a no-op, so the log
        // has to say whether it was read at all rather than only what was decided.
        private static bool described;
        private static void Describe(int entries, int resolved)
        {
            if (described) return;
            described = true;
            Session.Log.LogInfo("The game's display-mode row has " + entries + " entries, " + resolved +
                                " of them with a mode TGFG can identify.");
        }
        private static bool TryMode(ModeMap modes, ToggleOption option, out FullScreenMode mode)
        {
            mode = default(FullScreenMode);
            return option != null && modes.TryGetValue(option, out mode);
        }
#if BEPINEX_IL2CPP
        private static void Forbid(EnumArrowsOption option, ToggleOption entry) =>
            option.SetForbiddenOptions(new Il2CppReferenceArray<ToggleOption>(new[] { entry }));
        private static void Unforbid(EnumArrowsOption option) =>
            option.SetForbiddenOptions((Il2CppReferenceArray<ToggleOption>)null);
        private static ModeMap Modes(ScreenResolution screen) => screen._fullScreenModes;
        // Read the row's backing array, which the interop assembly exposes directly. Enumerating the
        // IEnumerable property instead found nothing in game, and because a row with no Borderless
        // entry is treated as one that cannot be helped, the whole feature went quiet for the session.
        private static ToggleOption Find(EnumArrowsOption option, ModeMap modes, FullScreenMode wanted)
        {
            var entries = option._options;
            if (entries == null) { Describe(0, 0); return null; }
            ToggleOption found = null;
            int resolved = 0;
            for (int i = 0; i < entries.Length; i++)
            {
                FullScreenMode mode;
                if (!TryMode(modes, entries[i], out mode)) continue;
                ++resolved;
                if (mode == wanted && found == null) found = entries[i];
            }
            Describe(entries.Length, resolved);
            return found;
        }
#else
        private static void Forbid(EnumArrowsOption option, ToggleOption entry) => option.SetForbiddenOptions(new[] { entry });
        private static void Unforbid(EnumArrowsOption option) => option.SetForbiddenOptions((ToggleOption[])null);
        // Not public, like SettingsMaster's setting lists; resolved once.
        private static readonly FieldInfo ModesField =
            typeof(ScreenResolution).GetField("_fullScreenModes", BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Instance);
        private static ModeMap Modes(ScreenResolution screen) => ModesField != null ? ModesField.GetValue(screen) as ModeMap : null;
        private static ToggleOption Find(EnumArrowsOption option, ModeMap modes, FullScreenMode wanted)
        {
            ToggleOption found = null;
            int entries = 0, resolved = 0;
            foreach (var toggle in option.Options)
            {
                ++entries;
                FullScreenMode mode;
                if (!TryMode(modes, toggle, out mode)) continue;
                ++resolved;
                if (mode == wanted && found == null) found = toggle;
            }
            Describe(entries, resolved);
            return found;
        }
#endif
    }
}
