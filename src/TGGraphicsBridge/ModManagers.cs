// SPDX-License-Identifier: MIT
using System;
using System.Linq;
using System.Reflection;
using BepInEx.Configuration;
using BepInEx.Logging;
namespace TGGraphicsBridge
{
    // The settings of the mods built on the bridge are the game's Video-tab rows. FoA Mod Manager shows
    // the same rows from each plugin's config; these are the two points where they meet the installed
    // managers.
    public static class ModManagers
    {
        private static Type Find(string assembly, string type) =>
            AppDomain.CurrentDomain.GetAssemblies().FirstOrDefault(a => a.GetName().Name == assembly)?.GetType(type, false);
        // Avalon Mod Manager lists every plugin config in a MODS tab of the same Options screen and, on
        // Apply, writes back the values it staged when the screen opened, reverting what the Video tab
        // just applied. Its own BlacklistedMods setting is how a plugin stays out; each of these mods adds
        // itself for this session only, without saving that mod's file.
        public static void KeepOutOfAvalon(string guid, ManualLogSource log)
        {
            try
            {
                var field = Find("AvalonModManager", "AvalonModManager.Plugin")?.GetField("BlacklistedMods", BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static);
                if (!(field?.GetValue(null) is ConfigEntry<string> entry)) return;
                var ids = entry.Value.Split(',').Select(s => s.Trim()).Where(s => s.Length > 0).ToList();
                if (ids.Contains(guid)) return;
                ids.Add(guid);
                var file = entry.ConfigFile;
                bool save = file.SaveOnConfigSet;
                file.SaveOnConfigSet = false;
                try { entry.Value = string.Join(",", ids.ToArray()); }
                finally { file.SaveOnConfigSet = save; }
                log.LogInfo("Avalon Mod Manager: " + guid + " stays in the Video tab (listed in FoA Mod Manager), not in the MODS tab.");
            }
            catch (Exception ex) { log.LogWarning("Could not keep " + guid + " out of Avalon Mod Manager's MODS tab: " + ex.Message); }
        }
        private static PropertyInfo foaOpen;
        private static MethodInfo foaRefresh;
        private static bool foaResolved, foaPending;
        // FoA Mod Manager reads the rows (choices, visibility) when it builds its list, which it does when
        // opened. A change while it is open, such as a rule applied after its own edit, needs a rebuild. That
        // edit arrives inside FoA Mod Manager's UI code, so the rebuild waits for the next frame's Tick.
        public static void RequestFoARefresh() => foaPending = true;
        internal static void Tick()
        {
            if (!foaPending) return;
            foaPending = false;
            try
            {
                if (!foaResolved)
                {
                    foaResolved = true;
                    var api = Find("FoAModManager", "FoAModManager.FoAModManagerApi");
                    foaOpen = api?.GetProperty("IsOpen", BindingFlags.Public | BindingFlags.Static);
                    foaRefresh = api?.GetMethod("Refresh", BindingFlags.Public | BindingFlags.Static, null, Type.EmptyTypes, null);
                }
                if (foaOpen != null && foaRefresh != null && (bool)foaOpen.GetValue(null, null)) foaRefresh.Invoke(null, null);
            }
            catch { foaRefresh = null; } // an optional convenience only
        }
    }
}
