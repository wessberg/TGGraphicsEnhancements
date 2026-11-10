// SPDX-License-Identifier: MIT No sound during a run. The game's sound is FMOD's (FMODUnity.dll), not
// Unity's, so Unity's own master volume, the first try, muted nothing. FMOD's master channel group is muted
// through the game's FMOD integration (RuntimeManager.MuteAllEvents), by name, so the runner needs no
// reference to it; Unity's volume goes to 0 too (videos). Neither is a setting of the game's: nothing the
// player set is touched, and both come back at the end.
using System;
using System.Reflection;
using UnityEngine;
namespace TGPathTracing.Harness
{
    internal static class Sound
    {
        private static bool wanted, looked, said;
        private static float unityBefore = 1f;
        private static MethodInfo muteAll;
        private static PropertyInfo initialized, muted;
        private static int frames;

        internal static void Mute(bool on)
        {
            if (on && !wanted) unityBefore = AudioListener.volume;
            wanted = on;
            if (on) { frames = 0; Keep(); return; }
            AudioListener.volume = unityBefore;
            try { if (Ready()) muteAll.Invoke(null, new object[] { false }); } catch { }
        }

        // Every frame while a run lasts: FMOD starts after the plugins do, and the mute must hold from its first sound on.
        internal static void Keep()
        {
            if (!wanted) return;
            // Each frame until FMOD is muted once, then twice a second (should anything take the mute off).
            if (said && ++frames % 30 != 0) return;
            if (AudioListener.volume != 0f) AudioListener.volume = 0f;
            try
            {
                if (!Ready()) return;
                if (muted != null && (bool)muted.GetValue(null, null)) { said = true; return; }
                muteAll.Invoke(null, new object[] { true });
                if (!said) { said = true; Runner.Note("The game's sound (FMOD) is muted for the run."); }
            }
            catch (Exception ex)
            {
                if (!said) { said = true; Runner.Note("The game's sound could not be muted: " + (ex.InnerException ?? ex).Message); }
            }
        }

        // FMOD's integration, once it has started (asking it earlier would start it).
        private static bool Ready()
        {
            if (!looked)
            {
                looked = true;
                var type = Type.GetType("FMODUnity.RuntimeManager, FMODUnity");
                if (type != null)
                {
                    muteAll = type.GetMethod("MuteAllEvents", BindingFlags.Public | BindingFlags.Static, null, new[] { typeof(bool) }, null);
                    initialized = type.GetProperty("IsInitialized", BindingFlags.Public | BindingFlags.Static);
                    muted = type.GetProperty("IsMuted", BindingFlags.Public | BindingFlags.Static);
                }
                if (muteAll == null) Runner.Note("The game's FMOD integration has no RuntimeManager.MuteAllEvents: its sound stays on.");
            }
            if (muteAll == null) return false;
            return initialized == null || (bool)initialized.GetValue(null, null);
        }
    }
}
