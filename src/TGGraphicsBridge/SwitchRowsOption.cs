// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.Linq;
using Awaken.TG.Main.Settings.Options;
using UnityEngine;
#if BEPINEX_IL2CPP
using Il2CppInterop.Runtime.Injection;
using Il2CppInterop.Runtime.InteropTypes.Arrays;
#else
using System.Reflection;
#endif
namespace TGGraphicsBridge
{
    // A row whose nested rows depend on its own value (a technology, a mode). The game's dependent view
    // spawns OtherOptions when the condition turns true and removes them when it turns false, so a
    // change swaps the list and keeps the condition false for that frame to rebuild the rows. Apply,
    // Cancel and Restore Defaults cover every value's rows, shown or not. Under IL2CPP, register it with
    // Il2CppTypes.Register<SwitchRowsOption>() before the first Create.
    public sealed class SwitchRowsOption : DependentOption
    {
#if !BEPINEX_IL2CPP
        private static readonly FieldInfo OthersField = typeof(DependentOption).GetField("<OtherOptions>k__BackingField", BindingFlags.NonPublic | BindingFlags.Instance);
#endif
        private static readonly PrefOption[] None = new PrefOption[0];
        private sealed class Switch
        {
            internal EnumArrowsOption Selector;
            internal Func<int, PrefOption[]> Rows;
            internal SwitchRowsOption Owner;
            internal PrefOption[] Current;
            internal int Shown, HiddenFrame = -1;
            internal bool Show()
            {
                int wanted = Selector.OptionInt;
                if (wanted != Shown && Owner != null)
                {
                    Shown = wanted;
                    Current = Rows(wanted) ?? None;
#if BEPINEX_IL2CPP
                    Owner.nativeRows = new Il2CppReferenceArray<PrefOption>(Current);
                    Owner._OtherOptions_k__BackingField = Owner.nativeRows;
#else
                    OthersField.SetValue(Owner, Current);
#endif
                    HiddenFrame = Time.frameCount;
                }
                return Current.Length > 0 && Time.frameCount != HiddenFrame;
            }
        }
        private readonly PrefOption[] all; // the selector and every row any of its values shows
#if BEPINEX_IL2CPP
        // Keep the native array's managed wrapper alive as long as this option owns it.
        // be.755 does not retain this inherited reference through the injected class:
        // a temporary array wrapper can be finalized while OtherOptions still points
        // to it. A later menu then reads recycled memory as PrefOption objects (NR-37).
        private Il2CppReferenceArray<PrefOption> nativeRows;
        public SwitchRowsOption(IntPtr pointer) : base(pointer) { }
        private SwitchRowsOption(EnumArrowsOption selector, Switch rows, PrefOption[] every) : base(ClassInjector.DerivedConstructorPointer<SwitchRowsOption>())
        {
            ClassInjector.DerivedConstructorBody(this);
            nativeRows = new Il2CppReferenceArray<PrefOption>(rows.Current);
            Il2CppTypes.Construct(this, typeof(DependentOption), "NativeMethodInfoPtr__ctor_Public_Void_PrefOption_Il2CppReferenceArray_1_PrefOption_0",
                selector, nativeRows);
            all = new PrefOption[] { selector }.Concat(every).ToArray();
            Il2CppOptionConditions.Register(this, rows.Show);
        }
        public new bool WasChanged => all.Any(o => o.WasChanged);
        public new bool IsSetToDefault => all.All(o => o.IsSetToDefault);
        public new void ForceChange() { foreach (var o in all) o.ForceChange(); }
        public new void Apply() { foreach (var o in all) o.Apply(); }
        public new void Cancel() { foreach (var o in all) o.Cancel(); }
        public new void RestoreDefault() { foreach (var o in all) o.RestoreDefault(); }
#else
        private SwitchRowsOption(EnumArrowsOption selector, Switch rows, PrefOption[] every) : base(selector, rows.Show, rows.Current)
        {
            all = new PrefOption[] { selector }.Concat(every).ToArray();
        }
        public override bool WasChanged => all.Any(o => o.WasChanged);
        public override bool IsSetToDefault => all.All(o => o.IsSetToDefault);
        public override void ForceChange() { foreach (var o in all) o.ForceChange(); }
        public override void Apply() { foreach (var o in all) o.Apply(); }
        public override void Cancel() { foreach (var o in all) o.Cancel(); }
        public override void RestoreDefault() { foreach (var o in all) o.RestoreDefault(); }
#endif
        // Null when this game build lacks what the switch relies on; callers fall back to a DependentOption.
        public static SwitchRowsOption Create(EnumArrowsOption selector, Func<int, PrefOption[]> rows, IEnumerable<PrefOption> every)
        {
#if !BEPINEX_IL2CPP
            if (OthersField == null || OthersField.FieldType != typeof(PrefOption[])) return null;
#endif
            int initial = selector.OptionInt;
            var sw = new Switch { Selector = selector, Rows = rows, Shown = initial, Current = rows(initial) ?? None };
            var option = new SwitchRowsOption(selector, sw, every.Where(o => o != null).Distinct().ToArray());
            sw.Owner = option;
            return option;
        }
    }
}
