// SPDX-License-Identifier: MIT
// This is a PRELOADER assembly. Do not add Unity or gameplay-plugin references.
// Built once per mod that bundles the bridge (src/<Mod>.Autoload), each with its own assembly name,
// patcher ID and Identity, so that BepInEx loads every copy side by side (see Bootstrap).
#if BEPINEX_MONO
using System.Collections.Generic;
using Mono.Cecil;
namespace TGGraphicsBridge.Autoload
{
    public static class Patcher
    {
        // A valid BepInEx 5 patcher contract with no assembly modifications.
        public static IEnumerable<string> TargetDLLs { get { yield break; } }
        public static void Patch(AssemblyDefinition assembly) { }
        public static void Initialize() { Bootstrap.Run("Mono / BepInEx 5"); }
        public static void Finish() { Bootstrap.Finish(); }
    }
}
#elif BEPINEX_IL2CPP
using BepInEx.Preloader.Core.Patching;
namespace TGGraphicsBridge.Autoload
{
    [PatcherPluginInfo(Identity.PatcherGuid, Identity.Title + " graphics bridge autoload", Bootstrap.BridgeVersion)]
    public sealed class Patcher : BasePatcher
    {
        public override void Initialize() { Bootstrap.Run("IL2CPP / BepInEx 6"); }
        public override void Finalizer() { Bootstrap.Finish(); }
    }
}
#else
#error Select the Mono or IL2CPP target.
#endif
