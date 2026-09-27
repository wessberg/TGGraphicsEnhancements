// SPDX-License-Identifier: MIT
// A 45 fps choice in the game's own Framerate Limit row (ScreenResolution.RefreshRateOption, "Target FPS"
// in the game's code), between 40 and 50: with 2x frame generation it shows 90 frames a second, which a
// player on an RTX 4060 laptop asked for. It is an entry of the game's row, not a row of the mod's, and
// the game applies it as it applies its own limits: ScreenResolution.Refresh sets Application.targetFrameRate
// to the entry's refresh rate over its VSync count, 45/1 here, the shape of the game's own 30 fps entry
// for screens without a 30 of their own.
//
// The game builds the row again, without the entry, at every start, which shapes the rest:
// - The row saves its choice as an index into its entries (PrefMemory "RefreshRate") and reads it back
//   into the game's own list before the entry exists. With the entry in the middle, every choice above it
//   would come back one entry higher, so after every change the index is written again as it is in the
//   game's list.
// - Whether 45 was chosen is kept by the entry itself: each entry is a ToggleOption whose on/off state the
//   game saves under its ID ("RefreshRate_45"), switched as the choice moves. At the next start the game
//   finds none of its own entries on and takes its default; the entry, added again, finds itself on and is
//   chosen again. Without the mod, the game's default stays.
// - The game forbids its plain limits while VSync is on (outside Exclusive Fullscreen); the entry is
//   forbidden with them. It is also forbidden in Exclusive Fullscreen, where Refresh would pass 45 Hz to
//   Screen.SetResolution as the display mode's refresh rate (the game's own limits are rates the display
//   has). Borderless Fullscreen and windowed don't use that refresh rate, and frame generation runs only
//   there.
// - The row is changed only while the Options screen is closed, as the game builds its views from it.
//   When the game builds the row again (the window moved to another monitor), the new row gets the entry.
using System;
using System.Globalization;
using Awaken.TG.Main.Memories;
using Awaken.TG.Main.Settings.Graphics;
using Awaken.TG.Main.Settings.Options;
using Awaken.TG.MVC;
using TGGraphicsBridge;
#if BEPINEX_IL2CPP
using Il2CppInterop.Runtime.InteropTypes.Arrays;
using RateMap = Il2CppSystem.Collections.Generic.Dictionary<Awaken.TG.Main.Settings.Options.ToggleOption, Awaken.TG.Main.Settings.Graphics.ScreenResolution.ARRefreshRate>;
#else
using System.Reflection;
using RateMap = System.Collections.Generic.Dictionary<Awaken.TG.Main.Settings.Options.ToggleOption, Awaken.TG.Main.Settings.Graphics.ScreenResolution.ARRefreshRate>;
#endif
namespace TGFrameGeneration
{
    internal static class ExtraFramerateLimit
    {
        internal const int Fps = 45;
        private const string EntryId = "RefreshRate_45";          // the game's naming: "RefreshRate_" + name
        private const string VSyncPrefix = "RefreshRate_VSync";   // the rates VSync offers, not limits
        private const string ExclusiveId = "Resolution_FullScreen";
        private static bool failed, constraining, callbackFailed;
        private static EnumArrowsOption row; // the last row looked at, extended or not
        // From the session's poll. Cheap until the game builds a row this hasn't seen.
        internal static void Tick()
        {
            if (failed) return;
            try
            {
                var screen = World.Any<ScreenResolution>();
                var rates = screen?.RefreshRateOption;
                if (rates == null || (row != null && VideoTab.Same(row, rates))) return;
                if (VideoTab.IsSettingsOpen) return;
                row = rates;
                Extend(screen, rates);
            }
            catch (Exception ex)
            {
                failed = true;
                Session.Log.LogWarning("Could not add " + Fps + " fps to the game's Framerate Limit (" + ex.GetType().Name + "); the row keeps the game's own choices.\n" + ex);
            }
        }
        private static void Extend(ScreenResolution screen, EnumArrowsOption rates)
        {
            var map = Rates(screen);
            var entries = Read(rates);
            if (map == null || entries == null)
            {
                Session.Log.LogWarning("The game's Framerate Limit row couldn't be read; it keeps the game's own choices.");
                return;
            }
            // Before the first entry that isn't a plain limit under 45: after 30 and 40, before 50.
            int at = -1;
            for (int i = 0; i < entries.Length; i++)
            {
                var entry = entries[i];
                if (entry == null) continue;
                if (entry.ID == EntryId) return; // this row has it already
                ScreenResolution.ARRefreshRate rate;
                bool limit = map.TryGetValue(entry, out rate) && !entry.ID.StartsWith(VSyncPrefix, StringComparison.Ordinal) && rate.refreshRate.numerator != 0;
                double fps = limit ? FpsOf(rate) : double.MaxValue;
                // Refresh truncates as this does.
                if (limit && (int)fps == Fps) return; // the game offers it already
                if (at < 0 && fps > Fps) at = i;
            }
            if (at < 0) at = entries.Length;
            var added = new ToggleOption(EntryId, Fps.ToString(CultureInfo.InvariantCulture), false, false);
            bool chosen = added.Enabled; // saved on: the player's choice when the game last built the row
            map.Add(added, new ScreenResolution.ARRefreshRate { refreshRate = new UnityEngine.RefreshRate { numerator = Fps, denominator = 1 }, vSyncCount = 1 });
            rates.onChange += (Action<ToggleOption>)((ToggleOption selected) => KeepIndex(rates, added, selected));
            rates.onForbiddenOptionsChange += (Action<ToggleOption>)((ToggleOption ignored) => Constrain(screen, rates, added));
            var grown = new ToggleOption[entries.Length + 1];
            Array.Copy(entries, 0, grown, 0, at);
            grown[at] = added;
            Array.Copy(entries, at, grown, at + 1, entries.Length - at);
            Write(rates, grown);
            Constrain(screen, rates, added);
            rates.EnsureCurrentOptionNotForbidden();
            string below = at > 0 ? grown[at - 1].DisplayName : null, above = at + 1 < grown.Length ? grown[at + 1].DisplayName : null;
            string between = below != null && above != null ? " between " + below + " and " + above : below != null ? " after " + below : above != null ? " before " + above : "";
            Session.Log.LogInfo("The game's Framerate Limit offers " + Fps + " fps" + between + ": " + 2 * Fps + " on screen with 2x frame generation.");
            if (!chosen) return;
            if (IndexOf(Forbidden(rates), added) >= 0)
            {
                // Chosen before, but VSync or Exclusive Fullscreen is on now: the game's choice stands.
                added.Enabled = false;
                return;
            }
            // The game took its default when it found none of its own entries on.
            rates.Option = added;
            // The applied value too, so the Options screen shows no change waiting to be applied.
            rates.Apply();
            // What Refresh does for this entry; the rest of Refresh (the display mode) is already in effect.
            if (!screen.VSyncOption.Enabled) UnityEngine.Application.targetFrameRate = Fps;
            Session.Log.LogInfo("Framerate Limit " + Fps + " chosen again, as you left it.");
        }
        // The row's saved index, as it is in the game's own list. For the entry itself, the one below it.
        private static void KeepIndex(EnumArrowsOption rates, ToggleOption added, ToggleOption selected)
        {
            try
            {
                var entries = Read(rates);
                int ours = IndexOf(entries, added), at = IndexOf(entries, selected);
                if (ours < 0 || at < 0) return;
                int index = at == ours ? Math.Max(0, ours - 1) : at > ours ? at - 1 : at;
                PrefMemory.Set(rates.ID, index, rates.Synchronize);
            }
            catch (Exception ex) { CallbackFailed(ex); }
        }
        // Runs inside the game's SetForbiddenOptions, before its EnsureCurrentOptionNotForbidden, so a 45 that
        // just became unavailable is left for the game's own nearest choice.
        private static void Constrain(ScreenResolution screen, EnumArrowsOption rates, ToggleOption added)
        {
            if (constraining) return;
            try
            {
                var mode = screen.FullscreenOption?.Option;
                bool forbid = screen.VSyncOption.Enabled || (mode != null && mode.ID == ExclusiveId);
                var list = Forbidden(rates) ?? new ToggleOption[0];
                int listed = IndexOf(list, added);
                if (forbid == (listed >= 0)) return;
                ToggleOption[] next;
                if (forbid)
                {
                    next = new ToggleOption[list.Length + 1];
                    Array.Copy(list, next, list.Length);
                    next[list.Length] = added;
                }
                else
                {
                    next = new ToggleOption[list.Length - 1];
                    Array.Copy(list, 0, next, 0, listed);
                    Array.Copy(list, listed + 1, next, listed, list.Length - listed - 1);
                }
                constraining = true;
                try { SetForbidden(rates, next); } finally { constraining = false; }
            }
            catch (Exception ex) { CallbackFailed(ex); }
        }
        // Called from the game's own code: never throws into it, and says so once.
        private static void CallbackFailed(Exception ex)
        {
            if (callbackFailed) return;
            callbackFailed = true;
            Session.Log.LogWarning("The " + Fps + " fps Framerate Limit entry hit an error; if the row misbehaves, choose another limit.\n" + ex);
        }
        private static double FpsOf(ScreenResolution.ARRefreshRate rate) =>
            rate.refreshRate.denominator == 0 ? 0 : (double)rate.refreshRate.numerator / rate.refreshRate.denominator / Math.Max(1, rate.vSyncCount);
        private static int IndexOf(ToggleOption[] entries, ToggleOption entry)
        {
            if (entries == null || entry == null) return -1;
            for (int i = 0; i < entries.Length; i++)
                if (entries[i] != null && VideoTab.Same(entries[i], entry)) return i;
            return -1;
        }
#if BEPINEX_IL2CPP
        // The interop assembly exposes the private fields. Every element read is a new wrapper, compared by
        // pointer (VideoTab.Same); the game's entries stay referenced by its dictionary, the added one by it too.
        private static RateMap Rates(ScreenResolution screen) => screen._refreshRates;
        private static ToggleOption[] Read(EnumArrowsOption rates) => Copy(rates._options);
        private static ToggleOption[] Forbidden(EnumArrowsOption rates) => Copy(rates._forbiddenOptions);
        private static void Write(EnumArrowsOption rates, ToggleOption[] entries) => rates._options = new Il2CppReferenceArray<ToggleOption>(entries);
        private static void SetForbidden(EnumArrowsOption rates, ToggleOption[] entries) =>
            rates.SetForbiddenOptions(new Il2CppReferenceArray<ToggleOption>(entries));
        private static ToggleOption[] Copy(Il2CppReferenceArray<ToggleOption> array)
        {
            if (array == null) return null;
            var copy = new ToggleOption[array.Length];
            for (int i = 0; i < copy.Length; i++) copy[i] = array[i];
            return copy;
        }
#else
        // Not public, like the display-mode dictionary DisplayMode reads; resolved once.
        private const BindingFlags Private = BindingFlags.NonPublic | BindingFlags.Instance;
        private static readonly FieldInfo RatesField = typeof(ScreenResolution).GetField("_refreshRates", Private);
        private static readonly FieldInfo OptionsField = typeof(EnumArrowsOption).GetField("_options", Private);
        private static readonly FieldInfo ForbiddenField = typeof(EnumArrowsOption).GetField("_forbiddenOptions", Private);
        private static RateMap Rates(ScreenResolution screen) => RatesField?.GetValue(screen) as RateMap;
        private static ToggleOption[] Read(EnumArrowsOption rates) => OptionsField?.GetValue(rates) as ToggleOption[];
        private static ToggleOption[] Forbidden(EnumArrowsOption rates) => ForbiddenField?.GetValue(rates) as ToggleOption[];
        private static void Write(EnumArrowsOption rates, ToggleOption[] entries)
        {
            if (OptionsField == null) throw new MissingFieldException(nameof(EnumArrowsOption), "_options");
            OptionsField.SetValue(rates, entries);
        }
        private static void SetForbidden(EnumArrowsOption rates, ToggleOption[] entries) => rates.SetForbiddenOptions(entries);
#endif
    }
}
