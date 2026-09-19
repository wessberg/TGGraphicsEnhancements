// SPDX-License-Identifier: MIT
#if BEPINEX_IL2CPP
using System;
using System.Reflection;
using HarmonyLib;
using Il2CppInterop.Runtime;
using Il2CppInterop.Runtime.Injection;
using Il2CppInterop.Runtime.InteropTypes;
namespace TGFrameGeneration
{
    // IL2CPP only. TGFG derives from three IL2CPP classes: the game's Setting and DependentOption (the
    // Video rows) and HDRP's CustomPass (the HDRP inputs). Il2CppInterop registers such a class with
    // IL2CPP and sends the base class's virtual calls to the managed methods with the same name and
    // parameter types, so those classes declare their overrides as `new` members.
    internal static class Il2CppTypes
    {
        internal static void Register<T>(bool genericBase = false) where T : class
        {
            if (ClassInjector.IsTypeRegisteredInIl2Cpp<T>()) return;
            if (!genericBase) { ClassInjector.RegisterTypeInIl2Cpp<T>(); return; }
            var harmony = new Harmony(Plugin.Id + ".il2cpp-types");
            harmony.Patch(AccessTools.Method(typeof(ClassInjector), "SystemTypeFromIl2CppType"),
                finalizer: new HarmonyMethod(typeof(Il2CppTypes), nameof(UnresolvedParameterType)));
            try { ClassInjector.RegisterTypeInIl2Cpp<T>(); }
            finally { harmony.UnpatchSelf(); }
        }
        // To find a class's overrides, registration looks up a managed type for each parameter of every
        // inherited virtual method. The game's Setting inherits Element<SettingsMaster>, whose methods take
        // its type parameter T, which has no managed type, so the lookup throws and registration fails.
        // TGFG overrides none of those methods: while registering such a class (genericBase), a failed
        // lookup yields Il2CppSystem.Object, which matches no override and keeps the inherited method.
        private static Exception UnresolvedParameterType(Exception __exception, ref Type __result)
        {
            if (__exception != null) __result = typeof(Il2CppSystem.Object);
            return null;
        }
        // ClassInjector.DerivedConstructorPointer allocates the object without running an IL2CPP
        // constructor, so the base class's field initializers would never run (CustomPass would start
        // disabled, a Setting without its model state). This runs the base constructor that the interop
        // type exposes as a NativeMethodInfoPtr field, as a C# base(...) call would.
        internal static unsafe void Construct(Il2CppObjectBase target, Type baseType, string constructor, params Il2CppObjectBase[] arguments)
        {
            var field = baseType.GetField(constructor, BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Static);
            var method = field != null ? (IntPtr)field.GetValue(null) : IntPtr.Zero;
            if (method == IntPtr.Zero) throw new MissingMethodException(baseType.FullName, constructor);
            void** args = stackalloc void*[Math.Max(1, arguments.Length)];
            for (int i = 0; i < arguments.Length; i++) args[i] = (void*)IL2CPP.Il2CppObjectBaseToPtr(arguments[i]);
            IntPtr exception = IntPtr.Zero;
            IL2CPP.il2cpp_runtime_invoke(method, target.Pointer, args, ref exception);
            Il2CppException.RaiseExceptionIfNecessary(exception);
        }
    }
}
#endif
