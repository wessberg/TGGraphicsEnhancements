// SPDX-License-Identifier: MIT
#if BEPINEX_IL2CPP
using System;
using System.Reflection;
using System.Runtime.InteropServices;
using Il2CppInterop.Common;
using Il2CppInterop.Runtime;
using Il2CppInterop.Runtime.Injection;
using Il2CppInterop.Runtime.Runtime;
using Il2CppInterop.Runtime.Startup;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
using UnityEngine.Rendering.RenderGraphModule;

namespace TGNeuralRendering
{
    internal static partial class UpscalePlacement
    {
        // BepInEx be.755's Harmony trampoline treats `in PrepassOutput` as a pointer
        // to an object reference. It is actually a pointer to unboxed struct data.
        // That conversion crashes before any prefix can run, even with NR Off or
        // its model absent. Keep that argument opaque and forward the native call.
        // All other arguments and the return value retain their generated ABI.
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate TextureHandle PostProcessNative(IntPtr pipeline, IntPtr graph, IntPtr prepass,
            TextureHandle input, TextureHandle backBuffer, TextureHandle ui, TextureHandle afterPost,
            TextureHandle fog, CullingResults culling, IntPtr camera, CubemapFace face, byte finalPass,
            IntPtr methodInfo);

        private static IDetour postProcessDetour;
        private static PostProcessNative postProcessCallback, originalPostProcess;
        private static bool postProcessObserved;

        private static unsafe void StartPostProcessHook(MethodInfo method)
        {
            var args = method.GetParameters();
            var expected = new[] { typeof(RenderGraph), typeof(HDRenderPipeline.PrepassOutput).MakeByRefType(),
                typeof(TextureHandle), typeof(TextureHandle), typeof(TextureHandle), typeof(TextureHandle),
                typeof(TextureHandle), typeof(CullingResults), typeof(HDCamera), typeof(CubemapFace), typeof(bool) };
            if (method.ReturnType != typeof(TextureHandle) || args.Length != expected.Length)
                throw new NotSupportedException("HDRP post-processing signature changed.");
            for (int i = 0; i < expected.Length; ++i)
                if (args[i].ParameterType != expected[i])
                    throw new NotSupportedException("HDRP post-processing argument changed: " + args[i].Name);
            var field = Il2CppInteropUtils.GetIl2CppMethodInfoPointerFieldForGeneratedMethod(method);
            var info = field == null ? IntPtr.Zero : (IntPtr)field.GetValue(null);
            if (info == IntPtr.Zero) throw new MissingMethodException("HDRP native post-processing entry point.");
            var address = UnityVersionHandler.Wrap((Il2CppMethodInfo*)info).MethodPointer;
            postProcessCallback = InvokePostProcess;
            postProcessObserved = false;
            postProcessDetour = Il2CppInteropRuntime.Instance.DetourProvider.Create(address, postProcessCallback);
            // Root the original delegate before applying: the next render can call us immediately.
            originalPostProcess = postProcessDetour.GenerateTrampoline<PostProcessNative>();
            postProcessDetour.Apply();
            Session.Log.LogInfo("IL2CPP post-processing hook: unboxed prepass data forwarded unchanged.");
        }

        private static TextureHandle InvokePostProcess(IntPtr pipeline, IntPtr graph, IntPtr prepass,
            TextureHandle input, TextureHandle backBuffer, TextureHandle ui, TextureHandle afterPost,
            TextureHandle fog, CullingResults culling, IntPtr camera, CubemapFace face, byte finalPass,
            IntPtr methodInfo)
        {
            ScheduleState state = null;
            try
            {
                if (camera != IntPtr.Zero) BeginPost(new HDCamera(camera), out state);
            }
            catch (Exception ex) { SceneCapture.Fail(ex); }
            try
            {
                var result = originalPostProcess(pipeline, graph, prepass, input, backBuffer, ui, afterPost,
                    fog, culling, camera, face, finalPass, methodInfo);
                if (!postProcessObserved)
                {
                    postProcessObserved = true;
                    Session.Log.LogInfo("IL2CPP post-processing hook completed its first frame.");
                }
                return result;
            }
            finally
            {
                try { EndPost(null, state); }
                catch (Exception ex) { SceneCapture.Fail(ex); }
            }
        }

        private static void StopPostProcessHook()
        {
            postProcessDetour?.Dispose();
            postProcessDetour = null;
            // Keep delegates rooted for an in-flight render during shutdown.
        }
    }
}
#endif
