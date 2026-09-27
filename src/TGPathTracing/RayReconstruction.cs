// SPDX-License-Identifier: MIT
// DLSS Ray Reconstruction for the traced light, run by TG Better Upscaling where HDRP would upscale: it
// denoises and upscales in one step. TG Better Upscaling finds this class by name (no reference either
// way): Wanted tells it to run Ray Reconstruction in place of DLSS Super Resolution, TryGetGuides hands it
// this frame's guide textures, and Ran tells this mod it ran, so the light goes out undenoised.
using UnityEngine;
namespace TGPathTracing
{
    public static class RayReconstruction
    {
        private static int lastRan = -100;
        // The frame whose capture asked the native side for guides (main thread, this frame's recording).
        internal static int GuidesFrame = -100;

        // Tracing, with Denoising on Ray Reconstruction (Session.WantsRayReconstruction).
        public static bool Wanted => Session.WantsRayReconstruction;

        // TG Better Upscaling reports whether NVIDIA's NGX runs Ray Reconstruction here, once it has started NGX
        // for it (when this mod first asked): -1 not yet, 0 no, 1 yes.
        internal static int Reported = -1;
        public static void Report(bool available)
        {
            int value = available ? 1 : 0;
            if (Reported == value) return;
            Reported = value;
            if (!available) Session.Log.LogWarning("DLSS Ray Reconstruction does not run on this GPU and driver (TG Better Upscaling's native log says why); Denoising no longer offers it.");
        }

        // Ray Reconstruction ran for one of the last two frames: this frame's light goes out as traced.
        internal static bool Active => Time.frameCount - lastRan <= 2;

        // The guides for the frame being rendered, exactly width x height (TG Better Upscaling's render size):
        // ID3D11Resource* of linear diffuse albedo, specular albedo, and world-space normals with roughness.
        // false when this frame asked for none (not tracing) or they are not at that size yet.
        public static bool TryGetGuides(int width, int height, out ulong diffuseAlbedo, out ulong specularAlbedo, out ulong normalRoughness)
        {
            diffuseAlbedo = specularAlbedo = normalRoughness = 0;
            var tracer = Session.Tracer;
            if (GuidesFrame != Time.frameCount || tracer == null || !tracer.ReadGuides(out var guides)) return false;
            if (guides.Width != (uint)width || guides.Height != (uint)height) return false;
            diffuseAlbedo = guides.DiffuseAlbedo; specularAlbedo = guides.SpecularAlbedo; normalRoughness = guides.NormalRoughness;
            return diffuseAlbedo != 0 && specularAlbedo != 0 && normalRoughness != 0;
        }

        // Ray Reconstruction's optional inputs for the frame being rendered, written by the composite as it applied the
        // traced light: the traced reflections' hit distance (world units) and the frame's color right after the traced
        // light, before HDRP's transparent surfaces and particles (ID3D11Resource*, exactly width x height). false when
        // the composite didn't write them this frame (TG Better Upscaling then leaves them out).
        public static bool TryGetExtraGuides(int width, int height, out ulong specularHitDistance, out ulong colorBeforeTransparency)
        {
            specularHitDistance = colorBeforeTransparency = 0;
            var tracer = Session.Tracer;
            if (GuidesFrame != Time.frameCount || tracer == null || !tracer.ReadGuides(out var guides)) return false;
            if (guides.Width != (uint)width || guides.Height != (uint)height || guides.ExtrasFrameIndex != guides.FrameIndex) return false;
            specularHitDistance = guides.SpecularHitDistance; colorBeforeTransparency = guides.ColorBeforeTransparency;
            return specularHitDistance != 0 && colorBeforeTransparency != 0;
        }

        public static void Ran(int frame) => lastRan = frame;

        // TG Better Upscaling runs in this process (its native DLL, which only TG Graphics Bridge loads).
        internal static bool UpscalingInstalled => RenoDxDlss.ModuleLoaded("TGBetterUpscaling.Native.dll");
    }
}
