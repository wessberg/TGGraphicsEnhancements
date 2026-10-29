// SPDX-License-Identifier: MIT
// Metadata diagnostic only. Live Off/On samples test culling and order against the actual trace event.
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using R = TGPathTracing.Harness.EffectsRead;
using UnityEngine;
using UnityEngine.Rendering;

namespace TGPathTracing.Harness
{
    internal static class VfxCapture
    {
        [DllImport("TGPathTracing.Native", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        private static extern int TGPT_VfxDiagnosticConfigure(string directory);
        [DllImport("TGPathTracing.Native", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr TGPT_VfxDiagnosticGetRenderEvent();
        [DllImport("TGPathTracing.Native", CallingConvention = CallingConvention.Cdecl)]
        private static extern int TGPT_VfxDiagnosticCompletedFrame();

        private static string error = "";
        private static Camera held;
        private static Vector3 position;
        private static Quaternion rotation;
        private static bool away;
        private static float[] Matrix(Matrix4x4 matrix)
        {
            var values = new float[16];
            for (int i = 0; i < 16; i++) values[i] = matrix[i];
            return values;
        }
        private static void Hold(ScriptableRenderContext context, List<Camera> cameras)
        {
            if (held == null) return;
            held.transform.position = position;
            held.transform.rotation = rotation * Quaternion.Euler(0, away ? 180 : 0, 0);
        }
        private static void Write(string path, object value) => File.WriteAllText(path, JsonConvert.SerializeObject(value, Formatting.Indented));

        // All loaded VFX components, not just visible ones. No buffer-to-component mapping is inferred.
        private static JArray Systems()
        {
            var result = new JArray(); var type = R.Type("UnityEngine.VFX.VisualEffect");
            if (type == null) { result.Add(R.Unknown("VisualEffect type not loaded")); return result; }
            foreach (var obj in Resources.FindObjectsOfTypeAll(type)) if (obj is Component c && R.Loaded(c))
            {
                var renderer = c.GetComponent<Renderer>();
                var entry = new JObject { ["id"] = c.GetInstanceID(), ["name"] = c.name,
                    ["active"] = c.gameObject.activeInHierarchy,
                    ["state"] = R.Members(c, "enabled", "culled", "pause", "playRate", "aliveParticleCount", "visualEffectAsset"),
                    ["renderer"] = renderer == null ? null : R.Members(renderer, "enabled", "isVisible"),
                    ["cullingPolicy"] = R.Unknown("The shipped managed API has no graph culling-policy getter; culled is observed state."),
                    ["bufferMapping"] = R.Unknown("GPU allocations may batch several systems; no component identity is assumed.") };
                entry["localToWorld"] = new JArray(Matrix(c.transform.localToWorldMatrix));
                entry["graph"] = R.Try(() => EffectsInventory.Vfx(c, renderer));
                entry["systems"] = R.Try(() => {
                    var names = new List<string>(); R.Call(c, "GetParticleSystemNames", names); var list = new JArray();
                    foreach (string name in names) list.Add(new JObject { ["name"] = name,
                        ["info"] = R.Try(() => R.Public(R.Call(c, "GetParticleSystemInfo", name))) });
                    return list;
                });
                result.Add(entry);
            }
            return result;
        }

        private static IEnumerator Sample(string directory, int count, bool expectedTraced)
        {
            Directory.CreateDirectory(directory);
            error = "";
            IntPtr callback = IntPtr.Zero;
            try
            {
                int result = TGPT_VfxDiagnosticConfigure(directory);
                if (result != 0) error = "Native diagnostic configure failed: " + result + ". Was vfx-observer.enable present at game startup?";
                else callback = TGPT_VfxDiagnosticGetRenderEvent();
            }
            catch (Exception ex) { error = ex.Message; }
            if (callback == IntPtr.Zero || error.Length > 0)
            {
                Write(Path.Combine(directory, "capture-error.json"), new { error });
                yield break;
            }
            try
            {
                // The first boundary is explicitly a primer. Only subsequent consecutive boundaries
                // claim a complete rendered frame; draw recording can happen on worker threads earlier.
                for (int i = -1; i < count; i++)
                {
                    yield return new WaitForEndOfFrame();
                    int frame = Time.frameCount;
                    GL.IssuePluginEvent(callback, frame);
                    var camera = Camera.main;
                    var clock = new { timeScale = Time.timeScale, time = Time.time, unscaledTime = Time.unscaledTime,
                        realtime = Time.realtimeSinceStartup, deltaTime = Time.deltaTime };
                    string image = "frame-" + frame + ".raw";
                    bool grabbed = FrameGrab.Grab(Path.Combine(directory, image), 1, out int width, out int height, out string format, out string grabError);
                    // Screenshot readback normally drains the queued plugin event. Record the actual
                    // acknowledgement; a delayed callback is not declared matched to this screenshot.
                    int completed = TGPT_VfxDiagnosticCompletedFrame();
                    Write(Path.Combine(directory, "frame-" + frame + "-image.json"), new {
                        frame, primer = i < 0, nativeCompletedAtScreenshot = completed, grabbed, grabError,
                        image, width, height, format, clock, expectedTraced,
                        mode = Traced.Get("Mode"), systems = Systems(),
                        shaderTimes = R.Try(() => R.Members(R.Get(RenderPipelineManager.currentPipeline, "m_ShaderVariablesGlobalCB"),
                            "_Time", "_LastTime", "unity_DeltaTime", "_TimeParameters", "_LastTimeParameters", "_FrameCount")),
                        shaderTimeCaution = "HDRP CPU mirror; GPU graph/custom material clocks and particle attributes are not read by this diagnostic.",
                        exposure = Traced.Text("HdrpPasses", "ExposureText"),
                        gain = Traced.Static("HdrpPasses", "TracedGain"),
                        tracer = new { frames = Traced.Status("FramesTraced"), failed = Traced.Status("FramesFailed"),
                            disabled = Traced.Status("Disabled"), composited = Traced.Status("Composited") },
                        camera = camera == null ? null : new {
                            position = new[] { camera.transform.position.x, camera.transform.position.y, camera.transform.position.z },
                            rotation = new[] { camera.transform.eulerAngles.x, camera.transform.eulerAngles.y, camera.transform.eulerAngles.z },
                            worldToCamera = Matrix(camera.worldToCameraMatrix),
                            gpuProjection = Matrix(GL.GetGPUProjectionMatrix(camera.projectionMatrix, true))
                        }
                    });
                    if (!grabbed) error = grabError;
                    if (completed != frame) error = "Native frame acknowledgement did not match screenshot: " + completed + " vs " + frame;
                    if (error.Length > 0)
                    {
                        Write(Path.Combine(directory, "capture-error.json"), new { error });
                        // Let an already queued callback finish before deactivating its destination.
                        float deadline = Time.realtimeSinceStartup + 10;
                        while (TGPT_VfxDiagnosticCompletedFrame() != frame && Time.realtimeSinceStartup < deadline) yield return null;
                        break;
                    }
                }
            }
            finally { TGPT_VfxDiagnosticConfigure(null); }
        }

        internal static IEnumerator Run(string directory, StepResult result, bool simulation = false)
        {
            if (simulation && !(Traced.Static("Session", "Settings") is object))
            { result.error += "Particle diagnostic settings unavailable. "; yield break; }
            held = Camera.main;
            if (held == null) { result.error += "No camera for VFX diagnostic. "; yield break; }
            position = held.transform.position; rotation = held.transform.rotation; away = false;
            bool heldTime = Runner.HoldTime; float timeScale = Time.timeScale;
            RenderPipelineManager.beginContextRendering += Hold;
            try
            {
                if (Time.timeScale <= 0) { result.error += "Live VFX diagnostic requires the recipe's live flag. "; yield break; }
                yield return Sample(Path.Combine(directory, "vfx-live-facing"), simulation ? 2 : 4, result.expectedTraced);
                if (error.Length > 0) result.error += error + ". ";
                away = true;
                for (int i = 0; i < 4; i++) yield return null;
                yield return Sample(Path.Combine(directory, "vfx-live-away"), simulation ? 2 : 8, result.expectedTraced);
                if (error.Length > 0) result.error += error + ". ";
                away = false;
                for (int i = 0; i < 4; i++) yield return null;
                yield return Sample(Path.Combine(directory, "vfx-live-returned"), simulation ? 2 : 4, result.expectedTraced);
                if (error.Length > 0) result.error += error + ". ";
                Runner.HoldTime = true; Time.timeScale = 0;
                for (int i = 0; i < 4; i++) yield return null;
                yield return Sample(Path.Combine(directory, "vfx-frozen"), simulation ? 2 : 4, result.expectedTraced);
                if (error.Length > 0) result.error += error + ". ";
            }
            finally
            {
                Runner.HoldTime = heldTime; Time.timeScale = timeScale;
                RenderPipelineManager.beginContextRendering -= Hold;
                if (held != null) { held.transform.position = position; held.transform.rotation = rotation; }
                held = null;
            }
        }
    }
}
