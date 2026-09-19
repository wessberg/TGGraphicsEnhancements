// SPDX-License-Identifier: MIT
// This is a PRELOADER assembly. Do not add Unity or TGFrameGeneration references.
#if BEPINEX_MONO
using System.Collections.Generic;
using Mono.Cecil;
namespace TGFG.Autoload
{
    public static class Patcher
    {
        // A valid BepInEx 5 patcher contract with no assembly modifications.
        public static IEnumerable<string> TargetDLLs { get { yield break; } }
        public static void Patch(AssemblyDefinition assembly) { }
        public static void Initialize() { Bootstrap.Run("Mono / BepInEx 5"); }
    }
}
#elif BEPINEX_IL2CPP
using BepInEx.Preloader.Core.Patching;
namespace TGFG.Autoload
{
    [PatcherPluginInfo("tgfg.autoload", "TGFG native autoload", "0.4.0")]
    public sealed class Patcher : BasePatcher
    {
        public override void Initialize() { Bootstrap.Run("IL2CPP / BepInEx 6"); }
    }
}
#else
#error Select the Mono or IL2CPP target.
#endif
