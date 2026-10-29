// SPDX-License-Identifier: MIT
// Shared source for the upscaling and neural-rendering hosts. Recording through HDRP's own
// CustomPass keeps texture lifetimes/dependencies in its render graph on both game runtimes.
using System;
using System.Reflection;
using HarmonyLib;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
using UnityEngine.Rendering.RenderGraphModule;
#if BEPINEX_IL2CPP
using Il2CppInterop.Runtime;
#endif
namespace TGGraphicsBridge.Internal
{
    internal static class CustomPassRecorder
    {
#if !BEPINEX_IL2CPP
        private static readonly FieldInfo CustomColor = AccessTools.Field(typeof(HDRenderPipeline), "m_CustomPassColorBuffer");
        private static readonly FieldInfo CustomDepth = AccessTools.Field(typeof(HDRenderPipeline), "m_CustomPassDepthBuffer");
        private static readonly MethodInfo ExecuteInternal = AccessTools.Method(typeof(CustomPass), "ExecuteInternal");
        internal static bool Ready => ExecuteInternal != null && CustomColor != null && CustomDepth != null;
        internal static void Record(CustomPass pass, CustomPassVolume volume, HDRenderPipeline pipeline,
            RenderGraph graph, HDCamera camera, CustomPass.RenderTargets targets)
        {
            targets.customColorBuffer = CustomColor.GetValue(pipeline) as Lazy<RTHandle>;
            targets.customDepthBuffer = CustomDepth.GetValue(pipeline) as Lazy<RTHandle>;
            ExecuteInternal.Invoke(pass, new object[] { graph, camera, default(CullingResults), default(CullingResults), targets, volume });
        }
#else
        private static readonly IntPtr ExecuteInternal = FindExecuteInternal();
        internal static bool Ready => ExecuteInternal != IntPtr.Zero;
        private static IntPtr FindExecuteInternal()
        {
            foreach (var field in typeof(CustomPass).GetFields(BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Static))
                if (field.Name.StartsWith("NativeMethodInfoPtr_ExecuteInternal_", StringComparison.Ordinal) && field.FieldType == typeof(IntPtr))
                    return (IntPtr)field.GetValue(null);
            return IntPtr.Zero;
        }
        internal static unsafe void Record(CustomPass pass, CustomPassVolume volume, HDRenderPipeline pipeline,
            RenderGraph graph, HDCamera camera, CustomPass.RenderTargets targets)
        {
            targets.customColorBuffer = pipeline.m_CustomPassColorBuffer;
            targets.customDepthBuffer = pipeline.m_CustomPassDepthBuffer;
            var culling = default(CullingResults);
            // The generated interop passes the boxed `in RenderTargets` header instead of its data.
            // That corrupts graph handles and leaks textures. Keep BU's tested unboxed invocation.
            void** args = stackalloc void*[6];
            args[0] = (void*)IL2CPP.Il2CppObjectBaseToPtr(graph);
            args[1] = (void*)IL2CPP.Il2CppObjectBaseToPtr(camera);
            args[2] = &culling;
            args[3] = &culling;
            args[4] = (void*)IL2CPP.il2cpp_object_unbox(IL2CPP.Il2CppObjectBaseToPtrNotNull(targets));
            args[5] = (void*)IL2CPP.Il2CppObjectBaseToPtr(volume);
            IntPtr exception = IntPtr.Zero;
            IL2CPP.il2cpp_runtime_invoke(ExecuteInternal, IL2CPP.Il2CppObjectBaseToPtrNotNull(pass), args, ref exception);
            Il2CppInterop.Runtime.Il2CppException.RaiseExceptionIfNecessary(exception);
        }
#endif
    }
}
