// SPDX-License-Identifier: MIT
// PT-45: does Unity's mip streaming make a new Direct3D 11 texture when it loads or drops mips? The tracer opens a texture
// of the game's by its D3D11 object: a new object is a new texture to open (and the old one's death notice lets go of the
// old), the same object needs nothing. For streamed textures without their top mip loaded: the native pointer and D3D11's
// own size, mips and flags; then with every mip asked for; then with only the smallest few; then given back to Unity.
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using UnityEngine;
using UnityEngine.Rendering;
namespace TGPathTracing.Harness
{
    internal static class MipStreaming
    {
        internal static string Summary = "";

        [StructLayout(LayoutKind.Sequential)]
        private struct Desc { public uint Width, Height, MipLevels, ArraySize, Format, SampleCount, SampleQuality, Usage, BindFlags, CPUAccessFlags, MiscFlags; }
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate void GetDescFn(IntPtr self, out Desc desc);

        private struct Seen { public IntPtr Pointer; public uint Width, Height, Mips, Misc; public int Loaded; public bool Read; }

        private static Seen Look(Texture2D t)
        {
            var s = new Seen { Loaded = t.loadedMipmapLevel };
            try { s.Pointer = t.GetNativeTexturePtr(); } catch { }
            if (s.Pointer == IntPtr.Zero || SystemInfo.graphicsDeviceType != GraphicsDeviceType.Direct3D11) return s;
            try
            {
                // ID3D11Texture2D::GetDesc, vtable slot 10 (Mirror.Describe reads it the same way).
                var getDesc = (GetDescFn)Marshal.GetDelegateForFunctionPointer(Marshal.ReadIntPtr(Marshal.ReadIntPtr(s.Pointer), 10 * IntPtr.Size), typeof(GetDescFn));
                getDesc(s.Pointer, out var d);
                s.Width = d.Width; s.Height = d.Height; s.Mips = d.MipLevels; s.Misc = d.MiscFlags; s.Read = true;
            }
            catch { }
            return s;
        }
        private static string Text(Seen s) =>
            "0x" + s.Pointer.ToInt64().ToString("X") + (s.Read ? " " + s.Width + "x" + s.Height + ", " + s.Mips + " mips, misc 0x" + s.Misc.ToString("X") : " (not read)") +
            ", loaded from mip " + s.Loaded;

        private static IEnumerator Wait(List<Texture2D> picked, Func<Texture2D, bool> done, float seconds)
        {
            float until = Time.realtimeSinceStartup + seconds;
            while (Time.realtimeSinceStartup < until && picked.Exists(t => t != null && !done(t))) yield return null;
            for (int i = 0; i < 4; i++) yield return null; // a few frames more: the swap, if any, reaches the render thread
        }

        internal static IEnumerator Run(string path, int count)
        {
            var picked = new List<Texture2D>();
            foreach (var t in Resources.FindObjectsOfTypeAll<Texture2D>())
                if (t != null && t.streamingMipmaps && t.loadedMipmapLevel > 0 && t.width >= 256 && t.height >= 256 && t.mipmapCount > 4 && picked.Count < count) picked.Add(t);
            var text = new StringBuilder();
            text.AppendLine("PT-45: does Unity's mip streaming make a new D3D11 texture when mips load or drop? Mip streaming " +
                (QualitySettings.streamingMipmapsActive ? "on, budget " + QualitySettings.streamingMipmapsMemoryBudget.ToString("0") + " MB" : "off") + "; " +
                picked.Count + " streamed textures without their top mip loaded (" + count + " asked for). misc 0x2: made shareable by TG Path Tracing's hook.");
            var before = picked.ConvertAll(Look);
            foreach (var t in picked) t.requestedMipmapLevel = 0;
            yield return Wait(picked, t => t.loadedMipmapLevel == 0, 10f);
            var all = picked.ConvertAll(Look);
            foreach (var t in picked) t.requestedMipmapLevel = Mathf.Max(0, t.mipmapCount - 3);
            yield return Wait(picked, t => t.loadedMipmapLevel >= t.mipmapCount - 3, 10f);
            var few = picked.ConvertAll(Look);
            foreach (var t in picked) if (t != null) t.ClearRequestedMipmapLevel();
            int newOnLoad = 0, newOnDrop = 0, loaded = 0, dropped = 0;
            for (int i = 0; i < picked.Count; i++)
            {
                bool load = all[i].Loaded < before[i].Loaded, drop = few[i].Loaded > all[i].Loaded;
                if (load) { ++loaded; if (all[i].Pointer != before[i].Pointer) ++newOnLoad; }
                if (drop) { ++dropped; if (few[i].Pointer != all[i].Pointer) ++newOnDrop; }
                text.AppendLine();
                text.AppendLine(picked[i].name + " (" + picked[i].width + "x" + picked[i].height + ", " + picked[i].mipmapCount + " mips, " + picked[i].format + ")");
                text.AppendLine("  as found:      " + Text(before[i]));
                text.AppendLine("  every mip:     " + Text(all[i]) + (load ? (all[i].Pointer != before[i].Pointer ? "  NEW OBJECT" : "  same object") : "  (did not load)"));
                text.AppendLine("  smallest few:  " + Text(few[i]) + (drop ? (few[i].Pointer != all[i].Pointer ? "  NEW OBJECT" : "  same object") : "  (did not drop)"));
            }
            Summary = picked.Count + " streamed textures: " + loaded + " loaded their top mips (" + newOnLoad + " as a new D3D11 object), " + dropped +
                " dropped them (" + newOnDrop + " as a new object)";
            text.Insert(0, Summary + "\n\n");
            File.WriteAllText(path, text.ToString(), new UTF8Encoding(false));
        }
    }
}
