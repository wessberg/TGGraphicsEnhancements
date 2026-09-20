// SPDX-License-Identifier: MIT
using System;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using Awaken.TG.Main.Settings.Options;
using TGFrameGeneration;

internal static class Program
{
    private static void Require(bool value, string message) { if (!value) throw new Exception(message); }
    public static void Main(string[] args)
    {
        AppDomain.CurrentDomain.AssemblyResolve += (_, e) =>
        {
            var path = Path.Combine(args[0], new AssemblyName(e.Name).Name + ".dll");
            return File.Exists(path) ? Assembly.LoadFrom(path) : null;
        };
        Run();
        Console.WriteLine("Option getter guard: native identity, owned/unowned routing, changing/nested conditions, exception isolation, GC retention, clear and unpatch/reinstall passed.");
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void Run()
    {
        var owned = new DependentOption(1) { ThrowInOriginal = true };
        var alias = new DependentOption(1) { ThrowInOriginal = true };
        var game = new DependentOption(2) { Value = true };
        bool show = false;
        Il2CppOptionConditions.Register(owned, () => show);
        Require(!owned.ShowDependent && owned.OriginalCalls == 0, "Owned option reached native callback.");
        show = true;
        Require(alias.ShowDependent && alias.OriginalCalls == 0, "Native wrapper alias missed guard.");
        Require(game.ShowDependent && game.OriginalCalls == 1, "Game option was intercepted.");
        game.Value = false;
        Require(!game.ShowDependent && game.OriginalCalls == 2, "Game condition lost dynamic behavior.");
        var child = new DependentOption(3) { ThrowInOriginal = true };
        Il2CppOptionConditions.Register(child, () => owned.ShowDependent);
        Require(child.ShowDependent, "Nested condition failed.");
        show = false;Require(!child.ShowDependent, "Nested condition failed to update.");
        var error = new DependentOption(4) { ThrowInOriginal = true };
        Il2CppOptionConditions.Register(error, () => throw new InvalidOperationException("test"));
        Require(!error.ShowDependent && !error.ShowDependent, "Failed managed condition was not contained.");
        Require(Session.Log.Warnings == 1 && error.OriginalCalls == 0, "Condition failure logged repeatedly or used native callback.");
        var weak = RegisterTemporary();
        GC.Collect();GC.WaitForPendingFinalizers();GC.Collect();
        Require(weak.IsAlive && new DependentOption(5).ShowDependent, "Registry failed to retain its native owner/condition.");
        Il2CppOptionConditions.Clear();owned.ThrowInOriginal = false;owned.Value = true;
        Require(owned.ShowDependent && owned.OriginalCalls == 1, "Clear kept stale pointer registrations.");
        Il2CppOptionConditions.Dispose();
        Require(game.ShowDependent == false && game.OriginalCalls == 3, "Dispose did not restore original getter.");
        Il2CppOptionConditions.Register(owned, () => false);
        Require(!owned.ShowDependent && owned.OriginalCalls == 1, "Reinstallation failed.");
        Il2CppOptionConditions.Dispose();
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference RegisterTemporary()
    {
        var owner = new DependentOption(5);
        Il2CppOptionConditions.Register(owner, () => true);
        return new WeakReference(owner);
    }
}
namespace Awaken.TG.Main.Settings.Options
{
    public sealed class DependentOption
    {
        public DependentOption(int pointer) { Pointer = new IntPtr(pointer); }
        public IntPtr Pointer { get; }
        public bool Value, ThrowInOriginal;
        public int OriginalCalls;
        public bool ShowDependent
        {
            [MethodImpl(MethodImplOptions.NoInlining)]
            get
            {
                ++OriginalCalls;
                if (ThrowInOriginal) throw new Exception("Unsafe native callback reached.");
                return Value;
            }
        }
    }
}
namespace TGFrameGeneration
{
    internal static class Plugin { internal const string Id = "tgfg.tests"; }
    internal static class Session { internal static TestLog Log = new TestLog(); }
    internal sealed class TestLog
    {
        internal int Warnings;
        internal void LogInfo(string text) { Console.WriteLine(text); }
        internal void LogWarning(string text) { ++Warnings; }
    }
}
