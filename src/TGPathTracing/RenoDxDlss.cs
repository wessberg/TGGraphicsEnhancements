// SPDX-License-Identifier: MIT
// RenoDX's DLSS 5 (the ReShade add-on renodx-dlss.addon64) runs NVIDIA's neural rendering on the game's own
// DLSS: it requires the game's DLSS Super Resolution and reuses its resources, at every hook stage (Render,
// Upscaled, Present). It hooks Ray Reconstruction's calls as well; the two work together (the player's test), so
// this only reports that it is loaded.
using System;
using System.Runtime.InteropServices;
namespace TGPathTracing
{
    internal static class RenoDxDlss
    {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)] private static extern IntPtr GetModuleHandleW(string name);
        private static bool logged;
        internal static bool ModuleLoaded(string name) => GetModuleHandleW(name) != IntPtr.Zero;
        // ReShade loads its add-ons when the game creates its device, before the first frame; asked again while it is not.
        internal static bool Loaded
        {
            get
            {
                if (logged) return true;
                if (GetModuleHandleW("renodx-dlss.addon64") == IntPtr.Zero) return false;
                logged = true;
                Session.Log.LogInfo("RenoDX DLSS 5 is loaded; DLSS Ray Reconstruction works alongside it.");
                return true;
            }
        }
    }
}
