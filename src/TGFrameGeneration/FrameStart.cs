// SPDX-License-Identifier: MIT
// Where each frame begins for the latency technologies: in Unity's PlayerLoop, right after
// TimeUpdate.WaitForLastPresentationAndUpdateTime, so after Unity's own wait for the previous frame's
// presentation and before any input is read (Unity's input in EarlyUpdate, Rewired, the game's input,
// in its MonoBehaviour's Update). Reflex, XeLL and Anti-Lag 2 wait there for the right moment to start
// the frame: their SDKs ask for that wait just before input is sampled, and Intel's XeLL guide for
// any other wait of the application to come before it. From a MonoBehaviour's Update it came after the
// input it was meant to precede, so the input a frame used was older by the wait. The entry is found
// by name; without TimeUpdate it goes first in the first phase. Should it go missing (another mod
// replacing the PlayerLoop), the frame begins in Update as it used to.
using System;
using UnityEngine.LowLevel;
#if BEPINEX_IL2CPP
using Il2CppInterop.Runtime;
using Il2CppInterop.Runtime.InteropTypes.Arrays;
#else
using System.Collections.Generic;
#endif
namespace TGFrameGeneration
{
    internal static class FrameStart
    {
        private const string TimePhase = "TimeUpdate", PresentationWait = "WaitForLastPresentationAndUpdateTime";
        private static bool tried, installed, begun, reportedMissing;
        private static int missed;
        private static string placement = "";
#if BEPINEX_IL2CPP
        // Il2CppInterop copies array elements of a value type as raw bytes, without the IL2CPP garbage
        // collector's write barrier, so the objects the new entry refers to are held here as well.
        private static PlayerLoopSystem.UpdateFunction callback;
        private static Il2CppReferenceArray<PlayerLoopSystem> phaseSystems;
        private static PlayerLoopSystem entry;
        private static Il2CppSystem.Type entryType;
#endif
        // From the plugin's Update, after Session.Tick: begins the frame here when the PlayerLoop entry
        // didn't. Installs the entry on the first call, when the game and UniTask have set up theirs.
        internal static void Update()
        {
            if (!tried)
            {
                tried = true;
                try { installed = Insert(); }
                catch (Exception ex) { Session.Log?.LogWarning("Could not add the frame start to Unity's PlayerLoop: " + ex.Message); }
                Session.Log?.LogInfo(installed
                    ? "Frames begin in Unity's PlayerLoop " + placement + ", before input: that is where Reflex, XeLL and Anti-Lag 2 wait"
                    : "Frames begin in Update; Reflex, XeLL and Anti-Lag 2 wait after the game has read input");
            }
            if (begun) { begun = false; missed = 0; return; }
            if (installed && ++missed == 30 && !reportedMissing)
            {
                reportedMissing = true;
                Session.Log?.LogWarning("The frame start left Unity's PlayerLoop (another mod replaced it); frames begin in Update from now on");
            }
            FrameBridge.BeginFrame();
        }
        private static void OnFrameStart()
        {
            try { begun = true; FrameBridge.BeginFrame(); }
            catch (Exception ex) { Session.Log?.LogError("Frame start failed: " + ex); }
        }
        // Where the entry goes: the phase, and the index in it. After the presentation wait when there is
        // one, else at the start of TimeUpdate, else at the start of the first phase.
        private static void Place(int phases, Func<int, string> phaseName, Func<int, int> count, Func<int, int, string> name, out int phase, out int at)
        {
            phase = 0; at = 0; placement = "at the start of its first phase";
            for (int p = 0; p < phases; ++p)
            {
                if (phaseName(p) != TimePhase) continue;
                phase = p; placement = "at the start of TimeUpdate";
                for (int i = 0; i < count(p); ++i)
                    if (name(p, i) == PresentationWait) { at = i + 1; placement = "after " + PresentationWait; break; }
                return;
            }
        }
#if BEPINEX_IL2CPP
        private static bool Insert()
        {
            var loop = PlayerLoop.GetCurrentPlayerLoop();
            var phases = loop.subSystemList;
            if (phases == null || phases.Length == 0) return false;
            // Value types in IL2CPP arrays: an element is read as a copy and written back.
            Place(phases.Length, p => phases[p].type?.Name, p => phases[p].subSystemList?.Length ?? 0,
                (p, i) => phases[p].subSystemList[i].type?.Name, out int target, out int at);
            var phase = phases[target];
            var systems = phase.subSystemList;
            int count = systems == null ? 0 : systems.Length;
            callback = DelegateSupport.ConvertDelegate<PlayerLoopSystem.UpdateFunction>(new Action(OnFrameStart));
            entryType = Il2CppType.Of<RuntimeBehaviour>();
            entry = new PlayerLoopSystem();
            entry.type = entryType;
            entry.updateDelegate = callback;
            phaseSystems = new Il2CppReferenceArray<PlayerLoopSystem>(count + 1);
            for (int i = 0, o = 0; o <= count; ++o) phaseSystems[o] = o == at ? entry : systems[i++];
            phase.subSystemList = phaseSystems;
            phases[target] = phase;
            loop.subSystemList = phases;
            PlayerLoop.SetPlayerLoop(loop);
            return true;
        }
#else
        private static bool Insert()
        {
            var loop = PlayerLoop.GetCurrentPlayerLoop();
            var phases = loop.subSystemList;
            if (phases == null || phases.Length == 0) return false;
            Place(phases.Length, p => phases[p].type?.Name, p => phases[p].subSystemList?.Length ?? 0,
                (p, i) => phases[p].subSystemList[i].type?.Name, out int target, out int at);
            var phase = phases[target];
            var systems = new List<PlayerLoopSystem>(phase.subSystemList ?? new PlayerLoopSystem[0]);
            systems.Insert(at, new PlayerLoopSystem { type = typeof(RuntimeBehaviour), updateDelegate = OnFrameStart });
            phase.subSystemList = systems.ToArray();
            phases[target] = phase;
            PlayerLoop.SetPlayerLoop(loop);
            return true;
        }
#endif
    }
}
