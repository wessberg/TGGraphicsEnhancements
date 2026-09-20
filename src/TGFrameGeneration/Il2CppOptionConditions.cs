// SPDX-License-Identifier: MIT
#if BEPINEX_IL2CPP
using System;
using System.Collections.Generic;
using Awaken.TG.Main.Settings.Options;
using HarmonyLib;

namespace TGFrameGeneration
{
    // Evaluate only TGFG's dependent-row conditions through Harmony's native method
    // bridge. Do not store a converted Func<bool> in the native option: the crash
    // dump's stack points through that callback path in DependentOption.ShowDependent.
    internal static class Il2CppOptionConditions
    {
        private sealed class Entry
        {
            internal DependentOption Owner; // retain the native object while its pointer is registered
            internal Func<bool> Evaluate;
            internal bool ReportedError;
        }
        private static readonly Dictionary<IntPtr, Entry> entries = new Dictionary<IntPtr, Entry>();
        private static Harmony harmony;
        private static bool observed;

        internal static void Register(DependentOption owner, Func<bool> evaluate)
        {
            if (owner == null || owner.Pointer == IntPtr.Zero || evaluate == null)
                throw new ArgumentException("Missing dependent option or condition.");
            if (harmony == null)
            {
                var method = AccessTools.PropertyGetter(typeof(DependentOption), "ShowDependent");
                if (method == null || method.ReturnType != typeof(bool))
                    throw new MissingMethodException("DependentOption.ShowDependent");
                var candidate = new Harmony(Plugin.Id + ".il2cpp-option-conditions");
                try
                {
                    candidate.Patch(method, prefix: new HarmonyMethod(typeof(Il2CppOptionConditions), nameof(BeforeShow)));
                    harmony = candidate;
                }
                catch { candidate.UnpatchSelf(); throw; }
                Session.Log.LogInfo("IL2CPP TGFG dependent-option condition hook installed.");
            }
            entries[owner.Pointer] = new Entry { Owner = owner, Evaluate = evaluate };
        }

        private static bool BeforeShow(DependentOption __instance, ref bool __result)
        {
            if (__instance == null || !entries.TryGetValue(__instance.Pointer, out var entry)) return true;
            try
            {
                __result = entry.Evaluate();
                if (!observed)
                {
                    observed = true;
                    Session.Log.LogInfo("IL2CPP TGFG dependent-option visibility evaluated through the native getter hook.");
                }
            }
            catch (Exception error)
            {
                __result = false;
                if (!entry.ReportedError)
                {
                    entry.ReportedError = true;
                    Session.Log.LogWarning("TGFG dependent-option condition failed: " + error.Message);
                }
            }
            return false;
        }

        internal static void Clear() { entries.Clear(); observed = false; }
        internal static void Dispose()
        {
            Clear();var installed = harmony;harmony = null;
            installed?.UnpatchSelf();
        }
    }
}
#endif
