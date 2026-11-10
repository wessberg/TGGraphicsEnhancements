// SPDX-License-Identifier: MIT
// A mod's config file on a machine that may not let it be written: read-only, locked by an antivirus
// scan, a full disk. BepInEx saves on every change and throws where the change was made: inside a
// mod's start, before it logs anything, or inside the game's settings screen, where the change's own
// notification never comes. Saved through here instead, the values stay in effect for the session and
// the player is told once, as a "Can't save" warning on the mod's row, cleared by the next save that works.
using System;
using BepInEx.Configuration;
namespace TGGraphicsBridge
{
    public static class SafeConfig
    {
        public static bool Save(ConfigFile file, string owner)
        {
            if (file == null) return false;
            string id = "config." + owner;
            try
            {
                file.Save();
                if (Notices.Has(id)) Notices.Clear(id);
                return true;
            }
            catch (Exception ex)
            {
                Notices.Set(id, NoticeLevel.Warning, owner, "Can't save",
                    "Its settings file can't be written (read-only, locked or disk full). Changes apply now but aren't kept.",
                    owner + "'s settings file, " + file.ConfigFilePath + ", can't be written: " + ex.Message + ". Changes apply for this session but " +
                    "are lost when the game closes. Check that the file isn't read-only and the disk has space, then change any setting to save again.");
                return false;
            }
        }
        // Disable BepInEx automatic saving for the session and route changes through Save, including
        // configuration users that do not save their own changes.
        public static void Take(ConfigFile file, string owner)
        {
            file.SaveOnConfigSet = false;
            file.SettingChanged += new Saver(file, owner).OnChanged; // a method, not a lambda: the IL2CPP build can't compile one here
        }
        private sealed class Saver
        {
            private readonly ConfigFile file;
            private readonly string owner;
            internal Saver(ConfigFile file, string owner) { this.file = file; this.owner = owner; }
            internal void OnChanged(object sender, SettingChangedEventArgs args) => Save(file, owner);
        }
    }
}
