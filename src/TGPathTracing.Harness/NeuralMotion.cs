// SPDX-License-Identifier: MIT
// Diagnostic only: compare the same NR motion with/without the game's motion blur. No saved settings change.
using System;
using System.Reflection;
using HarmonyLib;
using UnityEngine;
using UnityEngine.Rendering.HighDefinition;
namespace TGPathTracing.Harness
{
    internal static class NeuralMotion
    {
        internal static bool SuppressBlur;
        internal static bool SuppressLensEffects, LensEffectsSuppressed;
        internal static bool Active;
        internal static int Frame = -1;
        internal static int LensFrame = -1;
        internal static bool BeforeLensEffects;
        private static FieldInfo enabled, component, animated, reset;
        private static FieldInfo[] lensFlags;
        private static bool installed;
        internal static void Configure(Harmony harmony, bool suppress, bool suppressLens = false)
        {
            if (!installed)
            {
                var type = typeof(HDRenderPipeline);
                enabled = AccessTools.Field(type, "m_MotionBlurFS");
                component = AccessTools.Field(type, "m_MotionBlur");
                animated = AccessTools.Field(type, "m_AnimatedMaterialsEnabled");
                reset = AccessTools.Field(typeof(HDCamera), "resetPostProcessingHistory");
                var names = new[] { "m_DepthOfFieldFS", "m_MotionBlurFS", "m_PaniniProjectionFS", "m_BloomFS", "m_LensFlareDataDataDrivenFS",
                    "m_LensFlareScreenSpaceFS", "m_ChromaticAberrationFS", "m_LensDistortionFS", "m_VignetteFS", "m_FilmGrainFS", "m_DitheringFS" };
                lensFlags = Array.ConvertAll(names, name => AccessTools.Field(type, name));
                var method = AccessTools.Method(type, "BeginPostProcessFrame");
                if (enabled == null || component == null || animated == null || reset == null || method == null)
                    throw new MissingMemberException("HDRP motion-blur diagnostic fields");
                harmony.Patch(method, postfix: new HarmonyMethod(typeof(NeuralMotion), nameof(AfterBegin)));
                var lens = AccessTools.Method(type, "DepthOfFieldPass");
                if (lens == null) throw new MissingMemberException("HDRP pre-lens-effect diagnostic hook");
                harmony.Patch(lens, prefix: new HarmonyMethod(typeof(NeuralMotion), nameof(BeforeLens)));
                installed = true;
            }
            SuppressBlur = suppress;
            if (suppressLens && Array.Exists(lensFlags, field => field == null))
                throw new MissingMemberException("HDRP lens suppression diagnostic fields");
            SuppressLensEffects = suppressLens;
        }
        private static void BeforeLens(HDCamera hdCamera)
        {
            if (hdCamera?.camera != Camera.main) return;
            LensFrame = Time.frameCount;
            BeforeLensEffects = Neural.RecordedForFrame(LensFrame);
        }
        private static void AfterBegin(object __instance, object[] __args)
        {
            HDCamera camera = null;
            foreach (var argument in __args) if (argument is HDCamera value) camera = value;
            if (camera == null || camera.camera != Camera.main) return;
            // HDRP refreshes this field from the camera's frame settings every frame before this postfix.
            // Changing the cached flag for this frame leaves the camera, volume and game setting unchanged.
            if (SuppressBlur) enabled.SetValue(__instance, false);
            if (SuppressLensEffects) foreach (var field in lensFlags) field.SetValue(__instance, false);
            LensEffectsSuppressed = SuppressLensEffects && Array.TrueForAll(lensFlags, field => !(bool)field.GetValue(__instance));
            var blur = component.GetValue(__instance) as MotionBlur;
            Active = (bool)enabled.GetValue(__instance) && blur != null && blur.IsActive()
                && (bool)animated.GetValue(__instance) && !(bool)reset.GetValue(camera);
            Frame = Time.frameCount;
        }
    }
}
