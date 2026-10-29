// SPDX-License-Identifier: MIT
// Optional NR observation; no reference to TGNeuralRendering and no PT requirement.
using System;
using System.Reflection;
using BepInEx.Bootstrap;
namespace TGPathTracing.Harness
{
    internal static class Neural
    {
        internal const string PluginId = "com.wessberg.tgneuralrendering";
        private const BindingFlags Any = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;
        private static object latestStatus;
        private static string placement;
        private static FieldInfo recordedFrame;
        private static PropertyInfo postToneMap;
        internal static long Evaluations(out string reason)
        {
            reason = "TG Neural Rendering is not loaded.";
            latestStatus = null;
            placement = "";
            if (!Chainloader.PluginInfos.TryGetValue(PluginId, out var plugin) || plugin.Instance == null) return 0;
            var session = plugin.Instance.GetType().Assembly.GetType("TGNeuralRendering.Session");
            if (recordedFrame == null) recordedFrame = plugin.Instance.GetType().Assembly
                .GetType("TGNeuralRendering.UpscalePlacement")?.GetField("recordedFrame", Any);
            if (postToneMap == null) postToneMap = plugin.Instance.GetType().Assembly
                .GetType("TGNeuralRendering.UpscalePlacement")?.GetProperty("PostToneMapDiagnostic", Any);
            var native = session?.GetField("Native", Any)?.GetValue(null);
            if (native == null) return 0;
            placement = (bool)(session.GetProperty("BeforeUpscaling", Any)?.GetValue(null) ?? false) ? "Before Upscaling" : "After Upscaling";
            native.GetType().GetMethod("Poll", Any)?.Invoke(native, null);
            reason = Convert.ToString(session.GetProperty("Reason", Any)?.GetValue(null)) ?? "";
            var status = native.GetType().GetField("Status", Any)?.GetValue(native);
            latestStatus = status;
            return status == null ? 0 : Convert.ToInt64(status.GetType().GetField("Evaluated", Any)?.GetValue(status) ?? 0);
        }
        internal static void StageDiagnostics(string folder, float inputScale = 1, int toneMap = -1, bool afterToneMap = false)
        {
            if (!Chainloader.PluginInfos.TryGetValue(PluginId, out var plugin) || plugin.Instance == null)
            {
                if (folder != null) throw new InvalidOperationException("NR stage diagnostics require TG Neural Rendering.");
                return;
            }
            var session = plugin.Instance.GetType().Assembly.GetType("TGNeuralRendering.Session");
            var native = session?.GetField("Native", Any)?.GetValue(null);
            var method = native?.GetType().GetMethod("ConfigureDiagnostics", Any);
            if (method == null)
            {
                if (folder != null) throw new InvalidOperationException("This NR build lacks stage diagnostics.");
                return;
            }
            method.Invoke(native, new object[] { folder });
            if (folder != null && (inputScale != 1 || toneMap >= 0 || afterToneMap))
            {
                try
                {
                    if (inputScale != 1)
                    {
                        var scale = native.GetType().GetMethod("ConfigureInputScale", Any);
                        if (scale == null) throw new InvalidOperationException("This NR build lacks input conditioning diagnostics.");
                        scale.Invoke(native, new object[] { inputScale });
                    }
                    if (toneMap >= 0)
                    {
                        var mapper = native.GetType().GetMethod("ConfigureToneMap", Any);
                        if (mapper == null) throw new InvalidOperationException("This NR build lacks tone-map diagnostics.");
                        mapper.Invoke(native, new object[] { (uint)toneMap });
                    }
                    if (afterToneMap)
                    {
                        var late = native.GetType().GetMethod("ConfigurePostToneMap", Any);
                        if (late == null) throw new InvalidOperationException("This NR build lacks the tone-mapped input comparison.");
                        late.Invoke(native, new object[] { true });
                    }
                }
                catch { method.Invoke(native, new object[] { null }); throw; }
            }
        }
        internal static void Replay(bool reset, bool everyFrame = false)
        {
            if (!Chainloader.PluginInfos.TryGetValue(PluginId, out var plugin) || plugin.Instance == null)
                throw new InvalidOperationException("NR replay requires TG Neural Rendering.");
            var session = plugin.Instance.GetType().Assembly.GetType("TGNeuralRendering.Session");
            var native = session?.GetField("Native", Any)?.GetValue(null);
            var method = native?.GetType().GetMethod("ConfigureReplay", Any);
            if (method == null) throw new InvalidOperationException("This NR build lacks input replay.");
            if (everyFrame && !reset) throw new InvalidOperationException("Repeated reset requires replay reset enabled.");
            method.Invoke(native, new object[] { everyFrame ? 7u : reset ? 3u : 1u });
        }
        internal static void Check(Step step, long before, StepResult result)
        {
            bool? expected = step.neural;
            if (!expected.HasValue) return;
            long after = Evaluations(out string reason);
            result.neuralEvaluations = after - before; result.neuralReason = reason;
            result.neuralPlacement = placement;
            if (latestStatus != null)
            {
                result.neuralGpuMs = Convert.ToDouble(latestStatus.GetType().GetField("GpuMilliseconds", Any)?.GetValue(latestStatus) ?? 0);
                result.neuralTransferBytes = Convert.ToInt64(latestStatus.GetType().GetField("TransferBytes", Any)?.GetValue(latestStatus) ?? 0);
                result.neuralWidth = Convert.ToInt32(latestStatus.GetType().GetField("Width", Any)?.GetValue(latestStatus) ?? 0);
                result.neuralHeight = Convert.ToInt32(latestStatus.GetType().GetField("Height", Any)?.GetValue(latestStatus) ?? 0);
            }
            if (expected.Value && result.neuralEvaluations <= 0)
                result.error += " Neural Rendering did not evaluate during capture. " + reason;
            if (expected.Value && Array.Exists(result.frames, frame => frame.neuralBeforeLensEffects != !step.neuralPostToneMap))
                result.error += " Neural Rendering did not use the requested pre-lens or diagnostic post-tone-map placement.";
            if (expected.Value && step.neuralPostToneMap && Array.Exists(result.frames, frame => frame.neuralPostToneMap != true || frame.lensEffectsSuppressed != true))
                result.error += " Tone-mapped NR comparison did not run with the lens effects suppressed.";
            if (!expected.Value && result.neuralEvaluations != 0)
                result.error += " Neural Rendering evaluated while expected Off.";
            if (expected.Value) foreach (var pair in step.set)
                if (pair.plugin == PluginId && pair.key == "Placement" && pair.value != placement)
                    result.error += " NR placement did not take effect: requested " + pair.value + ", actual " + placement + ".";
        }
        internal static void Sample(FrameInfo frame)
        {
            frame.frameGeneration = NeuralCompatibility.FrameGeneration();
            frame.upscaling = NeuralCompatibility.Upscaling();
            frame.bridgePerformance = NeuralCompatibility.BridgePerformance();
            frame.neuralEvaluated = Evaluations(out _);
            if (latestStatus != null)
            {
                frame.neuralSkipped = ReadCounter("Skipped"); frame.neuralFailed = ReadCounter("Failed");
                frame.neuralResets = ReadCounter("Resets"); frame.neuralFrame = ReadCounter("LastFrame");
            }
            frame.motionBlurActive = NeuralMotion.Frame == frame.frame ? (bool?)NeuralMotion.Active : null;
            frame.neuralBeforeLensEffects = NeuralMotion.LensFrame == frame.frame ? (bool?)NeuralMotion.BeforeLensEffects : null;
            frame.neuralPostToneMap = postToneMap?.GetValue(null) is bool late ? late : false;
            frame.lensEffectsSuppressed = NeuralMotion.Frame == frame.frame ? (bool?)NeuralMotion.LensEffectsSuppressed : null;
        }
        internal static bool RecordedForFrame(int frame) => !(postToneMap?.GetValue(null) is bool late && late) && recordedFrame?.GetValue(null) is int value && value == frame;
        private static long ReadCounter(string name) => Convert.ToInt64(latestStatus.GetType().GetField(name, Any)?.GetValue(latestStatus) ?? -1);
    }
}
