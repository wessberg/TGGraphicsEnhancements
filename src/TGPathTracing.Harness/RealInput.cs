// SPDX-License-Identifier: MIT Real input kept out of the game while a harness run lasts (PT-97). The game's
// window takes the focus when it starts, and what was typed then, and a gamepad's sticks and buttons with or
// without the focus, steered the frozen game: two scenes failed with "the game left gameplay during
// capture". The game reads its devices through Rewired: every controller it knows (keyboard, mouse,
// gamepads, custom ones) is disabled for the run, and those this turned off are turned on again at its end.
// By reflection: the harness links no input assembly. The mods' own hotkeys (Unity's legacy Input) are not
// touched, and the runner itself steers the game through its code, never through input.
using System;
using System.Collections;
using System.Collections.Generic;
using HarmonyLib;
namespace TGPathTracing.Harness
{
    internal static class RealInput
    {
        private static readonly List<object> off = new List<object>();
        private static int said = -1;

        // on: disables every enabled controller (call it again at any time: a device plugged in later is caught then).
        // off: gives back the ones this turned off. Returns a line for the log when something changed, else null.
        internal static string Keep(bool on)
        {
            var reInput = AccessTools.TypeByName("Rewired.ReInput");
            if (reInput == null) return Say(-2, "Real input: Rewired was not found; the game still takes the keyboard, the mouse and gamepads.");
            if (!on)
            {
                int back = 0;
                foreach (var c in off) { try { Enabled(c)?.SetValue(c, true, null); ++back; } catch { } }
                off.Clear(); said = -1;
                return back > 0 ? "Real input is the game's again (" + back + " devices)." : null;
            }
            if (AccessTools.Property(reInput, "isReady")?.GetValue(null, null) is bool ready && !ready) return null;
            var helper = AccessTools.Property(reInput, "controllers")?.GetValue(null, null);
            if (helper == null) return Say(-3, "Real input: Rewired's controllers could not be read; the game still takes input.");
            int before = off.Count;
            foreach (string list in new[] { "Controllers", "Joysticks", "CustomControllers", "Keyboard", "Mouse" })
            {
                object value;
                try { value = AccessTools.Property(helper.GetType(), list)?.GetValue(helper, null); } catch { continue; }
                if (value is IEnumerable many) { foreach (var c in many) Disable(c); }
                else Disable(value);
            }
            if (off.Count == 0) return Say(-4, "Real input: Rewired names no enabled device; nothing to keep out.");
            if (off.Count == before && said == off.Count) return null;
            var names = new List<string>();
            foreach (var c in off) { try { names.Add(Convert.ToString(AccessTools.Property(c.GetType(), "name")?.GetValue(c, null))); } catch { names.Add("?"); } }
            return Say(off.Count, "Real input is kept from the game for the run: " + off.Count + " devices off (" + string.Join(", ", names.ToArray()) + ").");
        }

        private static System.Reflection.PropertyInfo Enabled(object c) => c == null ? null : AccessTools.Property(c.GetType(), "enabled");
        private static void Disable(object c)
        {
            var enabled = Enabled(c);
            if (enabled == null || off.Contains(c)) return;
            try { if ((bool)enabled.GetValue(c, null)) { enabled.SetValue(c, false, null); off.Add(c); } } catch { }
        }
        // The same thing is said once.
        private static string Say(int state, string text) { if (said == state) return null; said = state; return text; }
    }
}
