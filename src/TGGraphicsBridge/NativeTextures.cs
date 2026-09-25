// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;
namespace TGGraphicsBridge
{
    // The ID3D11Resource* behind HDRP's render textures, for the native side of the mods built on the
    // bridge. GetNativeTexturePtr waits for the render thread, so it runs once per render texture and
    // again only when HDRP resizes that texture (which recreates its GPU resource). Keyed by instance ID:
    // under IL2CPP every property read returns a new wrapper object for the same texture. Main thread only.
    public static class NativeTextures
    {
        private struct Cached { internal IntPtr Pointer; internal int Width, Height; }
        private static readonly Dictionary<int, Cached> pointers = new Dictionary<int, Cached>();
        private static int screenWidth, screenHeight;
        public static IntPtr Pointer(RTHandle handle) => Pointer(handle?.rt);
        public static IntPtr Pointer(RenderTexture rt)
        {
            if (rt == null) return IntPtr.Zero;
            if (Screen.width != screenWidth || Screen.height != screenHeight)
            {
                pointers.Clear(); screenWidth = Screen.width; screenHeight = Screen.height;
            }
            int id = rt.GetInstanceID();
            if (pointers.TryGetValue(id, out var cached) && cached.Pointer != IntPtr.Zero && cached.Width == rt.width && cached.Height == rt.height)
                return cached.Pointer;
            if (pointers.Count >= 128) pointers.Clear(); // render graph pools cycle through a few textures
            cached = new Cached { Pointer = rt.GetNativeTexturePtr(), Width = rt.width, Height = rt.height };
            pointers[id] = cached;
            return cached.Pointer;
        }
    }
}
