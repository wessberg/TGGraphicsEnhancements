// SPDX-License-Identifier: MIT
// TG Path Tracing, when it is installed and tracing: whether it wants DLSS Ray Reconstruction to denoise
// and upscale its frames, and its guide textures for the frame being rendered. Found by name at run time
// (TGPathTracing.RayReconstruction); neither mod references the other, and without it nothing changes.
using System;
using System.Reflection;
using UnityEngine;
namespace TGBetterUpscaling
{
    internal static class PathTracingLink
    {
        private static PropertyInfo wanted;
        private static MethodInfo guides, ran, report;
        private static int reported = -1;
        private static float nextLookup;
        private static bool broken;

        private static bool Find()
        {
            if (wanted != null) return true;
            if (broken || Time.unscaledTime < nextLookup) return false;
            nextLookup = Time.unscaledTime + 5f;
            var type = Type.GetType("TGPathTracing.RayReconstruction, TGPathTracing", false);
            if (type == null) return false;
            var w = type.GetProperty("Wanted", BindingFlags.Public | BindingFlags.Static);
            var g = type.GetMethod("TryGetGuides", BindingFlags.Public | BindingFlags.Static);
            var r = type.GetMethod("Ran", BindingFlags.Public | BindingFlags.Static);
            if (w == null || g == null || r == null)
            {
                broken = true;
                Session.Log.LogWarning("TG Path Tracing is installed, but its Ray Reconstruction link is not the one this version expects; DLSS stays DLSS Super Resolution.");
                return false;
            }
            guides = g; ran = r; wanted = w;
            report = type.GetMethod("Report", BindingFlags.Public | BindingFlags.Static); // not in older versions
            Session.Log.LogInfo("TG Path Tracing found: while it traces and DLSS is the upscaler, DLSS Ray Reconstruction denoises and upscales its frames.");
            return true;
        }
        private static void Break(Exception ex)
        {
            broken = true; wanted = null;
            Session.Log.LogWarning("TG Path Tracing's Ray Reconstruction link failed; DLSS stays DLSS Super Resolution: " + ex.Message);
        }

        // TG Path Tracing has been found (without looking again).
        internal static bool Found => wanted != null;

        internal static bool Wanted
        {
            get
            {
                try { return Find() && (bool)wanted.GetValue(null, null); }
                catch (Exception ex) { Break(ex); return false; }
            }
        }

        // TG Path Tracing's guides for this frame (exactly the render size), or false when it wrote none.
        internal static bool TryGuides(uint width, uint height, out ulong diffuseAlbedo, out ulong specularAlbedo, out ulong normalRoughness)
        {
            diffuseAlbedo = specularAlbedo = normalRoughness = 0;
            try
            {
                if (!Find()) return false;
                var args = new object[] { (int)width, (int)height, 0ul, 0ul, 0ul };
                if (!(bool)guides.Invoke(null, args)) return false;
                diffuseAlbedo = (ulong)args[2]; specularAlbedo = (ulong)args[3]; normalRoughness = (ulong)args[4];
                return diffuseAlbedo != 0 && specularAlbedo != 0 && normalRoughness != 0;
            }
            catch (Exception ex) { Break(ex); return false; }
        }

        // Tells TG Path Tracing whether NGX runs Ray Reconstruction on this GPU, once (its Denoising row lists it only then).
        internal static void Report(bool available)
        {
            int value = available ? 1 : 0;
            if (reported == value) return;
            try { if (Find() && report != null) { report.Invoke(null, new object[] { available }); reported = value; } }
            catch (Exception ex) { Break(ex); }
        }

        // Tells TG Path Tracing that Ray Reconstruction ran for this frame: it hands over its light undenoised.
        internal static void Ran(int frame)
        {
            try { if (Find()) ran.Invoke(null, new object[] { frame }); }
            catch (Exception ex) { Break(ex); }
        }
    }
}
