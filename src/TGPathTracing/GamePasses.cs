// SPDX-License-Identifier: MIT
using System;
using System.Reflection;
using HarmonyLib;
using UnityEngine;
using UnityEngine.Rendering.HighDefinition;

namespace TGPathTracing
{
    // PT-112 (A), THE RULE since 2026-10-11: while the traced light is on screen HDRP draws no opaque
    // object and no decal for the gameplay camera. Its opaque draw lists (its depth prepass, G-buffer,
    // motion vectors, forward opaque; HDRenderPipeline. CreateOpaqueRendererListDesc, both overloads)
    // are left with no layer, and its decals are left out of the camera's frame settings
    // (FrameSettings.AggregateFrameSettings, worked out anew every frame: the first frame after tracing
    // stops is the game's own again). The tracer writes the frame's own depth and motion into HDRP's
    // depth buffer, motion vectors and depth pyramid at the composite (TGPT_FRAME_OWN_DEPTH), which
    // HDRP's fog and transparents, the upscaler and post-processing read. HDRP still culls and prepares
    // the frame as before: the frame setting OpaqueObjects woke the game's ECS uploads instead (800 ms
    // frames, PROJECT-CONTEXT DEAD ENDS). Clean town run 20261011-011046: 87 to 90 ms a frame -> 81.
    // HDRP draws as the game does only where its G-buffer is what is looked at: the Yardstick view, the
    // old route (Own camera rays off), and the hidden "HDRP draws under the traced picture" (for tests:
    // the harness's yardstick recipe). HDRP's fog and its transparent effects stay HDRP's until the
    // tracer has them (PT-112 B and C).
    internal static class GamePasses
    {
        internal static bool Active; // the traced light replaces HDRP's on screen (HdrpPasses.SetIndirectOverride)
        // The game's own decal settings for the gameplay camera before they are left out here: what the tracer's decals follow
        // (DecalCapture), so that leaving HDRP's out does not leave the traced ones out too (round 19: it did, 4 ms "saved").
        internal static bool GameDecals = true, GameDecalLayers;
        // The game's own volumetric fog and shadow maps settings for the gameplay camera, before they are left out here
        // (HdrpPasses.FillFog reads the fog's albedo by the game's setting, whatever HDRP draws).
        internal static bool GameVolumetrics = true, GameShadowMaps = true;
        // PT-112, the rule since 2026-10-11: HDRP's lighting under the traced picture, left out of
        // the gameplay camera's frame settings while the tracer draws it. Its screen-space lighting
        // (read by nothing but HDRP's own lighting), its shadow maps and its volumetric fog grid
        // (read only by what HDRP still draws over the traced picture, its transparent effects,
        // until they are traced: they then take HDRP's plain fog and no shadow-map shadows). HDRP's
        // deferred lighting is skipped too (BeforeDeferredLighting): with no opaque object drawn it
        // lit nothing.
        private static readonly FrameSettingsField[] LeftOut = {
            FrameSettingsField.SSAO, FrameSettingsField.ContactShadows, FrameSettingsField.SSR, FrameSettingsField.TransparentSSR, FrameSettingsField.SSGI,
            FrameSettingsField.ScreenSpaceShadows, FrameSettingsField.SubsurfaceScattering, FrameSettingsField.Transmission,
            FrameSettingsField.ShadowMaps, FrameSettingsField.Volumetrics };
        private static int said = -1;

        // HDRP draws under the traced picture (true), or draws nothing opaque and the tracer gives the frame its depth (false).
        internal static bool HdrpDraws(Settings settings) =>
            settings == null || settings.HdrpDraws.Value || !settings.OwnCameraRays.Value || settings.CurrentView == DebugView.Yardstick;
        // The rule holds this frame: the traced frame writes its own depth and motion into HDRP's buffers (HdrpPasses).
        internal static bool OwnDepth => Active && !HdrpDraws(Session.Settings);
        // PT-112 (B), the rule since 2026-10-11: the game's fog traced, HDRP's opaque fog pass left out (HDRenderPipeline.
        // RenderOpaqueFog: the colour buffer goes on as it is; HDRP's transparents keep their own plain fog until they are
        // traced). A debug view shows no fog at all (the traced fog is drawn into the picture alone).
        internal static bool TracedFog => Active;

        internal static void Patch(Harmony harmony)
        {
            var method = typeof(FrameSettings).GetMethod("AggregateFrameSettings", BindingFlags.NonPublic | BindingFlags.Static, null,
                new[] { typeof(FrameSettings).MakeByRefType(), typeof(Camera), typeof(HDAdditionalCameraData), typeof(FrameSettings).MakeByRefType(), typeof(RenderPipelineSettings) }, null);
            if (method == null) throw new MissingMethodException("FrameSettings.AggregateFrameSettings");
            harmony.Patch(method, postfix: new HarmonyMethod(typeof(GamePasses), nameof(AfterAggregate)));
            int patched = 0;
            foreach (var m in typeof(HDRenderPipeline).GetMethods(BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Static))
                if (m.Name == "CreateOpaqueRendererListDesc" && m.ReturnType == typeof(UnityEngine.Rendering.RendererUtils.RendererListDesc))
                { harmony.Patch(m, postfix: new HarmonyMethod(typeof(GamePasses), nameof(AfterOpaqueList))); ++patched; }
            // PT-112 (C), on trial for one measuring run: the transparent draw lists too, behind the hidden "HDRP transparents
            // not drawn (trial)" (Settings), path tracing on or off.
            int transparent = 0;
            foreach (var m in typeof(HDRenderPipeline).GetMethods(BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Static))
                if (m.Name == "CreateTransparentRendererListDesc" && m.ReturnType == typeof(UnityEngine.Rendering.RendererUtils.RendererListDesc))
                { harmony.Patch(m, postfix: new HarmonyMethod(typeof(GamePasses), nameof(AfterTransparentList))); ++transparent; }
            if (transparent != 2) Session.Log.LogWarning("HDRP's transparent draw lists were found " + transparent + " times, not 2: the transparents' trial switch may not hold.");
            if (patched != 2) Session.Log.LogWarning("HDRP's opaque draw lists were found " + patched + " times, not 2: HDRP may still draw opaque objects under the traced picture.");
            var deferred = AccessTools.Method(typeof(HDRenderPipeline), "RenderDeferredLighting");
            if (deferred != null) harmony.Patch(deferred, prefix: new HarmonyMethod(typeof(GamePasses), nameof(BeforeDeferredLighting)));
            else Session.Log.LogWarning("HDRP's deferred lighting was not found: it may still run under the traced picture.");
            // HDRP's camera motion vectors (a pass over the screen from its depth, which is empty while the tracer draws: the
            // tracer writes the frame's own motion at the composite, TGPT_FRAME_OWN_DEPTH).
            var cameraMotion = AccessTools.Method(typeof(HDRenderPipeline), "RenderCameraMotionVectors");
            if (cameraMotion != null && cameraMotion.ReturnType == typeof(void)) harmony.Patch(cameraMotion, prefix: new HarmonyMethod(typeof(GamePasses), nameof(BeforeCameraMotionVectors)));
            else Session.Log.LogWarning("HDRP's camera motion vectors pass was not found: it may still run under the traced picture.");
            var fog = AccessTools.Method(typeof(HDRenderPipeline), "RenderOpaqueFog");
            if (fog != null && fog.ReturnType == typeof(UnityEngine.Rendering.RenderGraphModule.TextureHandle))
                harmony.Patch(fog, prefix: new HarmonyMethod(typeof(GamePasses), nameof(BeforeOpaqueFog)));
            else Session.Log.LogWarning("HDRP's opaque fog pass was not found: the traced fog's trial would show both fogs.");
            // The game's HDRP blurs the picture by its fog's opacity after the transparents (ScreenSpaceFogMultipleScattering:
            // the Fog's "multiple scattering intensity", 1 outdoors): a screen-space stand-in for light the fog scatters
            // more than once, which the traced fog now traces (its cache cells), and it reads HDRP's fog opacity, which is no
            // longer written. Left out while the fog is traced.
            var multiple = AccessTools.Method(typeof(HDRenderPipeline), "ScreenSpaceFogMultipleScattering");
            if (multiple != null && multiple.ReturnType == typeof(void)) harmony.Patch(multiple, prefix: new HarmonyMethod(typeof(GamePasses), nameof(BeforeFogMultipleScattering)));
            else Session.Log.LogWarning("HDRP's screen-space fog multiple scattering was not found: it may still blur the traced picture.");
        }
        // Its result is a struct HDRP returns empty itself when it has nothing to light (the original's own early return).
        private static bool BeforeDeferredLighting(HDCamera hdCamera) => !OwnDepth || hdCamera == null || !Gameplay(hdCamera.camera);
        private static bool BeforeCameraMotionVectors(HDCamera hdCamera) => !OwnDepth || hdCamera == null || !Gameplay(hdCamera.camera);
        private static bool BeforeFogMultipleScattering(HDCamera hdCamera) => !TracedFog || hdCamera == null || !Gameplay(hdCamera.camera);

        private static bool BeforeOpaqueFog(HDCamera hdCamera, UnityEngine.Rendering.RenderGraphModule.TextureHandle colorBuffer,
                                            ref UnityEngine.Rendering.RenderGraphModule.TextureHandle __result)
        {
            if (!TracedFog || hdCamera == null || !Gameplay(hdCamera.camera)) return true;
            __result = colorBuffer;
            return false;
        }

        private static bool Gameplay(Camera camera)
        {
            var main = Camera.main;
            return camera != null && main != null && main.GetInstanceID() == camera.GetInstanceID();
        }

        private static void AfterOpaqueList(ref UnityEngine.Rendering.RendererUtils.RendererListDesc __result, Camera camera)
        {
            if (!OwnDepth || !Gameplay(camera)) return;
            __result.layerMask = 0; __result.batchLayerMask = 0;
        }

        private static void AfterTransparentList(ref UnityEngine.Rendering.RendererUtils.RendererListDesc __result, Camera camera)
        {
            var settings = Session.Settings;
            if (settings == null || !settings.HdrpTransparentsOff.Value || !Gameplay(camera)) return;
            __result.layerMask = 0; __result.batchLayerMask = 0;
        }

        private static void AfterAggregate(ref FrameSettings aggregatedFrameSettings, Camera camera)
        {
            if (camera == null || camera.cameraType != CameraType.Game || camera.targetTexture != null || !Gameplay(camera)) return;
            GameDecals = aggregatedFrameSettings.IsEnabled(FrameSettingsField.Decals);
            GameDecalLayers = aggregatedFrameSettings.IsEnabled(FrameSettingsField.DecalLayers);
            GameVolumetrics = aggregatedFrameSettings.IsEnabled(FrameSettingsField.Volumetrics);
            GameShadowMaps = aggregatedFrameSettings.IsEnabled(FrameSettingsField.ShadowMaps);
            bool cut = OwnDepth;
            if (cut)
            {
                aggregatedFrameSettings.SetEnabled(FrameSettingsField.Decals, false);
                foreach (var field in LeftOut) aggregatedFrameSettings.SetEnabled(field, false);
            }
            int now = !Active ? 0 : cut ? 1 : 2;
            if (now == said) return;
            said = now;
            if (now == 1) Session.Log.LogInfo("HDRP under the traced picture: no opaque object, no decal and none of its lighting (ambient occlusion, contact shadows, " +
                "screen-space reflections and shadows, indirect diffuse, subsurface scattering, transmission, shadow maps, the volumetric fog grid, deferred lighting); " +
                "the path tracer gives the frame its depth and motion");
            else if (now == 2) Session.Log.LogInfo("HDRP under the traced picture: drawn as the game draws it (" +
                (Session.Settings == null ? "no settings" : Session.Settings.HdrpDraws.Value ? "HDRP draws under the traced picture is on" :
                 !Session.Settings.OwnCameraRays.Value ? "Own camera rays is off" : "the Yardstick view") + ")");
        }
    }
}
