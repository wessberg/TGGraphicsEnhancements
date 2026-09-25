// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using Awaken.Kandra.Managers;
using HarmonyLib;
using UnityEngine;
using UnityEngine.Rendering;
#if BEPINEX_IL2CPP
using ManagerIdentity = System.IntPtr;
#else
using ManagerIdentity = Awaken.Kandra.Managers.SkinningManager;
#endif

namespace TGGraphicsBridge
{
    // Kandra copies current world-space vertices to its previous-position buffer in
    // beginContextRendering, then skins again. An additional Camera.Render in the same
    // Unity frame can replace last frame's positions with this frame's positions.
    // Preserve one history copy per manager per frame; all skinning still runs.
    internal static class KandraMotionHistory
    {
        private static Harmony harmony;
        private static readonly HashSet<ManagerIdentity> copied = new HashSet<ManagerIdentity>();
        private static int copyFrame = -1;
        private static long suppressed;
        private static float nextReport;

        internal static void Initialize()
        {
            if (harmony != null) return;
            try
            {
#if BEPINEX_IL2CPP
                // The installed IL2CPP OnBeginRendering inlines RunCopyPrevious and
                // RunSkinning. Both call this dispatch method with different kernels.
                // Hook the dispatch and filter by the manager's live copy-kernel ID.
                var method = AccessTools.DeclaredMethod(typeof(SkinningManager), "RunComputeKernel",
                    new[] { typeof(CommandBuffer), typeof(int) });
                const string prefix = nameof(BeforeKernel), finalizer = nameof(KernelFinished);
#else
                var method = AccessTools.DeclaredMethod(typeof(SkinningManager), "RunCopyPrevious",
                    new[] { typeof(CommandBuffer) });
                const string prefix = nameof(BeforeCopy), finalizer = nameof(CopyFinished);
#endif
                if (method == null || method.ReturnType != typeof(void))
                    throw new MissingMethodException("Kandra history-copy dispatch");
                harmony = new Harmony(BridgePlugin.Id + ".kandra-motion-history");
                harmony.Patch(method,
                    prefix: new HarmonyMethod(typeof(KandraMotionHistory), prefix),
                    finalizer: new HarmonyMethod(typeof(KandraMotionHistory), finalizer));
                BridgeLog.Log.LogInfo("Kandra motion history guard installed (one previous-position copy per Unity frame; " + method.Name + ").");
            }
            catch (Exception error)
            {
                Dispose();
                BridgeLog.Log?.LogWarning("Kandra motion history guard unavailable: " + error.Message);
            }
        }

#if BEPINEX_IL2CPP
        private static bool BeforeKernel(SkinningManager __instance, int __1, out bool __state)
        {
            __state = false;
            // Never suppress skinning, including a malformed/changed kernel layout.
            if (__1 != __instance._copyKernel || __1 == __instance._skinningKernel) return true;
            __state = BeforeCopy(__instance);
            return __state;
        }

        private static Exception KernelFinished(SkinningManager __instance, bool __state, Exception __exception)
        {
            // A skinning failure must not clear an already recorded history copy.
            return __state ? CopyFinished(__instance, __exception) : __exception;
        }
#endif

        private static ManagerIdentity Identity(SkinningManager manager)
        {
#if BEPINEX_IL2CPP
            // Interop can create different managed wrappers for the same native object.
            return manager.Pointer;
#else
            return manager;
#endif
        }

        private static bool BeforeCopy(SkinningManager __instance)
        {
            if (harmony == null || !MotionVectors.Requested)
            {
                copied.Clear(); copyFrame = -1;
                return true;
            }
            int frame = Time.frameCount;
            if (frame != copyFrame) { copied.Clear(); copyFrame = frame; }
            if (copied.Add(Identity(__instance))) return true;

            ++suppressed;
            float now = Time.unscaledTime;
            if (suppressed == 1 || now >= nextReport)
            {
                nextReport = now + 10f;
                BridgeLog.Log?.LogInfo("Kandra motion history: prevented " + suppressed +
                    " duplicate previous-position copies; Unity frame " + frame + ".");
            }
            return false;
        }

        // If recording the original copy fails, allow a later context to try again.
        // Preserve the game's exception and do not swallow rendering failures.
        private static Exception CopyFinished(SkinningManager __instance, Exception __exception)
        {
            if (__exception != null) copied.Remove(Identity(__instance));
            return __exception;
        }

        internal static void Dispose()
        {
            var installed = harmony;
            harmony = null;
            copied.Clear(); copyFrame = -1; suppressed = 0; nextReport = 0;
            try { installed?.UnpatchSelf(); }
            catch (Exception error) { BridgeLog.Log?.LogWarning("Kandra history unpatch failed: " + error.Message); }
        }
    }
}
