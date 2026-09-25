// SPDX-License-Identifier: MIT
// Runs the production Harmony hook against a stand-in skinning manager. This checks
// history timing and patch lifecycle, not Unity GPU scheduling or visual quality.
using System;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using Awaken.Kandra.Managers;
using TGGraphicsBridge;

internal static class Program
{
    private static void Require(bool value, string message)
    {
        if (!value) throw new Exception(message);
    }

    private static int Main(string[] args)
    {
        if (args.Length != 1) throw new ArgumentException("Pass the matching game's BepInEx/core directory.");
        AppDomain.CurrentDomain.AssemblyResolve += (_, e) =>
        {
            string path = Path.Combine(args[0], new AssemblyName(e.Name).Name + ".dll");
            return File.Exists(path) ? Assembly.LoadFrom(path) : null;
        };
        Run();
        Console.WriteLine("Kandra history: frame generation, upscaling and both, multiple cameras/managers, no request, failure recovery and unpatch/reinstall passed.");
        return 0;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void Run()
    {
        KandraMotionHistory.Initialize();
        Require(BridgeLog.Log.Installed, "Production Harmony hook failed to install.");
        // The mods that read motion vectors ask for the guard; any one request is enough.
        foreach (var owners in new[] { new[] { "TG Frame Generation" }, new[] { "TG Better Upscaling" }, new[] { "TG Frame Generation", "TG Better Upscaling" } })
        {
            string backend = string.Join("+", owners);
            foreach (var owner in owners) MotionVectors.Request(owner, true);
            // No minimap/extra camera: every frame must retain its ordinary copy.
            var single = new SkinningManager { Current = 10 };
            for (int frame = 0; frame < 8; ++frame)
            {
                ++UnityEngine.Time.frameCount;
                int previous = single.Current;
                single.RunCopyPrevious(null);
                single.RunSkinning(previous + 3);
                Require(single.Previous == previous && single.Copies == frame + 1,
                    backend + ": single-camera history changed without an extra render.");
            }
            ++UnityEngine.Time.frameCount;
            var a = new SkinningManager { Current = 100 };
            var b = new SkinningManager { Current = 200 };
            a.RunCopyPrevious(null); a.RunSkinning(110); // extra camera
            a.RunCopyPrevious(null); a.RunSkinning(120); // main camera
            b.RunCopyPrevious(null); b.RunSkinning(220); // different manager
            Require(a.Previous == 100 && a.Current == 120 && a.Copies == 1,
                backend + ": extra camera destroyed last frame's positions or suppressed skinning.");
            Require(b.Previous == 200 && b.Copies == 1, backend + ": different manager lost its first copy.");
            UnityEngine.Time.frameCount += 3;
            a.RunCopyPrevious(null);
            Require(a.Previous == 120 && a.Copies == 2, backend + ": history did not advance after a frame gap.");

            foreach (var owner in owners) MotionVectors.Request(owner, false);
            a.RunSkinning(130); a.RunCopyPrevious(null);
            a.RunSkinning(140); a.RunCopyPrevious(null);
            Require(a.Previous == 140 && a.Copies == 4, "No request changed original copy behavior.");
            foreach (var owner in owners) MotionVectors.Request(owner, true);
            a.RunSkinning(150); a.RunCopyPrevious(null);
            a.RunSkinning(160); a.RunCopyPrevious(null);
            Require(a.Previous == 150 && a.Copies == 5, "A new request after none reused the old guard state.");

            ++UnityEngine.Time.frameCount;
            a.Fail = true;
            bool threw = false;
            try { a.RunCopyPrevious(null); } catch (InvalidOperationException) { threw = true; }
            Require(threw, "Hook swallowed the original rendering exception.");
            a.Fail = false; a.RunCopyPrevious(null);
            Require(a.Previous == 160 && a.Copies == 6, "A failed copy prevented retry in a later context.");
#if BEPINEX_IL2CPP
            VerifyIl2CppDispatch(backend);
#endif
            foreach (var owner in owners) MotionVectors.Request(owner, false);
        }
        MotionVectors.Request("TG Frame Generation", true);
        KandraMotionHistory.Dispose();
        var plain = new SkinningManager { Current = 1 };
        plain.RunCopyPrevious(null); plain.RunSkinning(2); plain.RunCopyPrevious(null);
        Require(plain.Previous == 2 && plain.Copies == 2, "Dispose left a functional hook behind.");
        KandraMotionHistory.Initialize();
        plain.RunCopyPrevious(null); plain.RunSkinning(3); plain.RunCopyPrevious(null);
        Require(plain.Previous == 2 && plain.Copies == 3, "Reinstall did not reset patch state.");
        KandraMotionHistory.Dispose();
    }

#if BEPINEX_IL2CPP
    private static void VerifyIl2CppDispatch(string backend)
    {
        ++UnityEngine.Time.frameCount;
        var a = new SkinningManager { Current = 10, _copyKernel = 17, _skinningKernel = 9 };
        var alias = new SkinningManager(a); // a distinct CLR wrapper of the same native manager
        Require(!ReferenceEquals(a, alias) && a.Pointer == alias.Pointer, "Bad alias fixture.");
        a.RunSkinning(11); // skinning before the copy must not claim this frame's history
        a.RunComputeKernel(null, a._copyKernel); // the actual inlined IL2CPP call path
        alias.RunSkinning(12);
        alias.RunComputeKernel(null, alias._copyKernel);
        Require(a.Copies == 1 && a.Previous == 11 && a.Current == 12 && a.Skins == 2,
            backend + ": aliased native manager lost history or skipped skinning.");
        a.RunComputeKernel(null, 99);a.RunComputeKernel(null, 99);
        Require(a.OtherKernels == 2, "Unrelated kernels were filtered.");
        a.FailSkinning = true;
        bool threw = false;
        try { alias.RunSkinning(13); } catch (InvalidOperationException) { threw = true; }
        Require(threw, "Skinning exception was swallowed.");
        a.FailSkinning = false;alias.RunCopyPrevious(null);
        Require(a.Copies == 1, "A skinning exception cleared the history guard.");
        ++UnityEngine.Time.frameCount;
        a.Fail = true;
        try { alias.RunCopyPrevious(null); } catch (InvalidOperationException) { }
        a.Fail = false;a.RunCopyPrevious(null);
        Require(a.Copies == 2 && a.Previous == 12, "Copy failure did not permit retry through another wrapper.");
        ++UnityEngine.Time.frameCount;
        a._copyKernel = a._skinningKernel;
        a.RunSkinning(14);alias.RunSkinning(15);
        Require(a.Current == 15 && a.Skins == 4, "An overlapping kernel ID suppressed skinning.");
    }
#endif
}

namespace UnityEngine
{
    public static class Time { public static int frameCount; public static float unscaledTime; }
}
namespace UnityEngine.Rendering { public class CommandBuffer { } }
namespace Awaken.Kandra.Managers
{
    public class SkinningManager
    {
#if BEPINEX_IL2CPP
        private sealed class NativeState
        {
            internal int Current, Previous, Copies, Skins, OtherKernels, Pending;
            internal int CopyKernel = 7, SkinningKernel = 3;
            internal bool Fail, FailSkinning;
            internal IntPtr Pointer = new IntPtr(++nextPointer);
        }
        private static long nextPointer;
        private readonly NativeState state;
        public SkinningManager() { state = new NativeState(); }
        public SkinningManager(SkinningManager other) { state = other.state; }
        public IntPtr Pointer => state.Pointer;
        public int Current { get => state.Current; set => state.Current = value; }
        public int Previous => state.Previous;
        public int Copies => state.Copies;
        public int Skins => state.Skins;
        public int OtherKernels => state.OtherKernels;
        public int _copyKernel { get => state.CopyKernel; set => state.CopyKernel = value; }
        public int _skinningKernel { get => state.SkinningKernel; set => state.SkinningKernel = value; }
        public bool Fail { get => state.Fail; set => state.Fail = value; }
        public bool FailSkinning { get => state.FailSkinning; set => state.FailSkinning = value; }
        public void RunCopyPrevious(UnityEngine.Rendering.CommandBuffer commands) => RunComputeKernel(commands, _copyKernel);
        public void RunSkinning(int position) { state.Pending = position;RunComputeKernel(null, _skinningKernel); }
        [MethodImpl(MethodImplOptions.NoInlining)]
        public void RunComputeKernel(UnityEngine.Rendering.CommandBuffer commands, int kernel)
        {
            if (kernel == _skinningKernel)
            {
                if (FailSkinning) throw new InvalidOperationException("Simulated skinning failure.");
                state.Current = state.Pending;++state.Skins;
            }
            else if (kernel == _copyKernel)
            {
                if (Fail) throw new InvalidOperationException("Simulated command-recording failure.");
                state.Previous = Current;++state.Copies;
            }
            else ++state.OtherKernels;
        }
#else
        public int Current, Previous, Copies;
        public bool Fail;
        [MethodImpl(MethodImplOptions.NoInlining)]
        public void RunCopyPrevious(UnityEngine.Rendering.CommandBuffer commands)
        {
            if (Fail) throw new InvalidOperationException("Simulated command-recording failure.");
            Previous = Current; ++Copies;
        }
        public void RunSkinning(int position) { Current = position; }
#endif
    }
}
namespace TGGraphicsBridge
{
    internal static class BridgePlugin { internal const string Id = "tgb.tests"; }
    internal static class BridgeLog { internal static TestLog Log = new TestLog(); }
    internal sealed class TestLog
    {
        internal bool Installed;
        internal void LogInfo(string text) { Installed |= text.Contains("guard installed"); Console.WriteLine(text); }
        internal void LogWarning(string text) { throw new Exception(text); }
    }
}
