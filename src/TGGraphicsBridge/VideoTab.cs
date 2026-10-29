// SPDX-License-Identifier: MIT
// The game's Video tab, as the mods built on the bridge use it: their options are real game
// Setting/PrefOption objects, so they render with the game's own row prefabs, respect Apply Changes /
// Undo / Restore Defaults, and nest exactly like the built-in Dynamic Resolution or Subsurface
// Scattering rows. Everything here works on both runtimes; under IL2CPP the classes a mod derives
// from the game's are registered with Il2CppInterop first (see Il2CppTypes).
using System;
using System.Collections.Generic;
using System.Linq;
using Awaken.TG.Main.Settings;
using Awaken.TG.Main.Settings.Graphics;
using Awaken.TG.Main.Settings.Options;
using Awaken.TG.Main.Settings.Options.Views;
using Awaken.TG.Main.Settings.Windows;
using Awaken.TG.MVC;
using UnityEngine;
#if BEPINEX_IL2CPP
using Il2CppInterop.Runtime;
using Il2CppInterop.Runtime.InteropTypes;
using Il2CppInterop.Runtime.InteropTypes.Arrays;
using SettingList = Il2CppSystem.Collections.Generic.List<Awaken.TG.Main.Settings.ISetting>;
#else
using System.Reflection;
using SettingList = System.Collections.Generic.List<Awaken.TG.Main.Settings.ISetting>;
#endif
namespace TGGraphicsBridge
{
    public static class VideoTab
    {
        public static bool IsSettingsOpen
        {
            get { try { return World.Any<AllSettingsUI>() != null; } catch { return false; } }
        }
        public static int IndexOf(SettingList list, Setting item)
        {
            for (int i = 0; i < list.Count; i++) if (Same(list[i], item)) return i;
            return -1;
        }
        // Moves item directly after the last setting whose type name matches one of the anchors; returns
        // whether it found one. A mod's rows follow the game's own upscaling row, and each other's.
        public static bool Reposition(SettingList list, Setting item, params string[] anchors)
        {
            int anchor = -1, own = -1;
            for (int i = 0; i < list.Count; i++)
            {
                if (Same(list[i], item)) { own = i; continue; }
                string name = TypeName(list[i]);
                foreach (var a in anchors)
                    if (name == a || (a.StartsWith("*") && name.IndexOf(a.Substring(1), StringComparison.OrdinalIgnoreCase) >= 0)) { anchor = i; break; }
            }
            if (anchor < 0 || own < 0) return false;
            var entry = list[own];
            list.RemoveAt(own);
            if (own < anchor) anchor--;
            list.Insert(Math.Min(anchor + 1, list.Count), entry);
            return true;
        }
        // Moves item directly before the first setting whose type name matches one of the anchors; returns
        // whether it moved. The mods' problems stand above the game's upscaling row and the mods' own rows.
        public static bool PositionBefore(SettingList list, Setting item, params string[] anchors)
        {
            int anchor = -1, own = -1;
            for (int i = 0; i < list.Count; i++)
            {
                if (Same(list[i], item)) { own = i; continue; }
                if (anchor >= 0) continue;
                string name = TypeName(list[i]);
                foreach (var a in anchors)
                    if (name == a || (a.StartsWith("*") && name.IndexOf(a.Substring(1), StringComparison.OrdinalIgnoreCase) >= 0)) { anchor = i; break; }
            }
            if (anchor < 0 || own < 0 || own == anchor - 1) return false;
            var entry = list[own];
            list.RemoveAt(own);
            if (own < anchor) anchor--;
            list.Insert(anchor, entry);
            return true;
        }
        // Closes the settings screen the way its Back prompt does: with unapplied changes, the game asks first.
        public static void CloseSettings() => World.Any<AllSettingsUI>()?.Close();
        // A row that runs onClick when selected, like the game's Gamma and Bug Reporting rows.
        public static ButtonOption Button(string id, string displayName, Action onClick)
        {
            Action guarded = () =>
            {
                try { onClick(); }
                catch (Exception ex) { BridgeLog.Log.LogError("TG Graphics Bridge: " + displayName + " failed: " + ex); }
            };
#if BEPINEX_IL2CPP
            return new ButtonOption(id, displayName, DelegateSupport.ConvertDelegate<Il2CppSystem.Action>(guarded));
#else
            return new ButtonOption(id, displayName, guarded);
#endif
        }
        // Moves item to the end of the list; returns whether it moved.
        public static bool MoveToEnd(SettingList list, Setting item)
        {
            int own = IndexOf(list, item);
            if (own < 0 || own == list.Count - 1) return false;
            var entry = list[own];
            list.RemoveAt(own);
            list.Add(entry);
            return true;
        }
        // The game checks a row's interactability when it builds the row. Like its anti-aliasing row does for
        // the upscaler, a mod refreshes its rows when what they depend on changes.
        public static void RefreshInteractability(IEnumerable<PrefOption> options)
        {
            try
            {
                var ui = World.Any<AllSettingsUI>();
                if (ui == null) return;
                var leaves = new HashSet<object>(options.Where(o => o != null).Select(o => Identity(o)));
                foreach (var view in FocusableViews(ui))
                    if (leaves.Contains(Identity(view.GenericOption))) view.UpdateInteractability();
            }
            catch { } // cosmetic only
        }
        // The game asks a row for its tooltip text when it shows the row's description.
        public static void Tooltip(PrefOption option, Func<string> text)
        {
#if BEPINEX_IL2CPP
            // Il2CppInterop's delegate bridge (DelegateSupport) turns a returned string into the address of the
            // managed string, not an IL2CPP string: it maps the return type to IntPtr before its string check.
            // The game then crashes in String.Trim while building the tooltip. The bridge compares only the
            // parameter types, so this function returns the IL2CPP string pointer itself.
            option.AddTooltip(DelegateSupport.ConvertDelegate<Il2CppSystem.Func<string>>(new Func<IntPtr>(() => IL2CPP.ManagedStringToIl2Cpp(text()))));
#else
            option.AddTooltip(text);
#endif
        }
        public static ToggleOption Choice(string id, string label) => new ToggleOption(id, label, false, false);
        // An arrows row of labelled choices; ids are prefix.id and prefix.id.index, the game's preference keys.
        public static EnumArrowsOption Enum(string prefix, string id, string displayName, int defaultIndex, out ToggleOption[] choices, params string[] labels)
        {
            choices = new ToggleOption[labels.Length];
            for (int i = 0; i < labels.Length; i++) choices[i] = Choice(prefix + id + "." + i, labels[i]);
            return new EnumArrowsOption(prefix + id, displayName, choices[Mathf.Clamp(defaultIndex, 0, choices.Length - 1)], false, choices);
        }
#if BEPINEX_IL2CPP
        // Every IL2CPP read returns a new wrapper object, so an object's identity is its IL2CPP pointer.
        public static object Identity(Il2CppObjectBase o) => o?.Pointer ?? IntPtr.Zero;
        public static bool Same(Il2CppObjectBase a, Il2CppObjectBase b) => a != null && b != null && a.Pointer == b.Pointer;
        public static SettingList GraphicSettings(SettingsMaster master) => master._graphicSettings;
        public static void AddGraphicSetting(SettingsMaster master, Setting item) => master.AddGraphicSetting(item);
        public static string TypeName(ISetting item) => IL2CPP.il2cpp_class_get_name_(item.ObjectClass);
        // The game's graphics setting of that class once its settings model has created it, else null.
        // Not World.Any<T>(): Il2CppInterop runs a game class's static constructor the first time a mod
        // uses the class at all, even as a type argument, where Mono waits for a static member. UpScaling's
        // translates its row labels; run before the game has loaded its localization, it throws, and the
        // class stays unusable for the rest of the session, for the game too. Looked up by class name, the
        // setting only exists once the game has run that constructor itself.
        public static T GraphicSetting<T>(string className) where T : Il2CppObjectBase
        {
            var master = World.Any<SettingsMaster>();
            var list = master != null ? GraphicSettings(master) : null;
            if (list == null) return null;
            for (int i = 0; i < list.Count; i++)
                if (TypeName(list[i]) == className) return list[i].TryCast<T>();
            return null;
        }
        // The game's upscaling row (Dynamic Resolution) once the game has created its setting, else null.
        public static EnumArrowsOption UpscalingRow() => GraphicSetting<UpScaling>("UpScaling")?.UpscalingTypeOption;
        // The settings screen's row views as Unity finds them in the scene, not from the screen's own set
        // (Model.Views, an IEnumerable over a HashSet). Walking that set through the interop's enumerator threw
        // "Collection was modified" the first time the settings opened, in every normal-version log on record
        // (2026-09-25, 2026-10-06): RowAlerts gave up for the session, and the rows' alert icons never showed.
        // A failure here gives no views this time and is logged once; the callers ask again while the screen is open.
        private static bool viewsFailed, viewsListed;
        public static List<VFocusableSetting> FocusableViews(AllSettingsUI ui)
        {
            var views = new List<VFocusableSetting>();
            try
            {
                var found = UnityEngine.Object.FindObjectsByType(Il2CppType.Of<VFocusableSetting>(), FindObjectsSortMode.None);
                if (found == null) return views;
                for (int i = 0; i < found.Length; i++)
                {
                    var view = found[i]?.TryCast<VFocusableSetting>();
                    if (view != null) views.Add(view);
                }
                if (!viewsListed && views.Count > 0)
                {
                    viewsListed = true;
                    BridgeLog.Log.LogInfo("The settings screen's rows are listed for the mods' alert icons (" + views.Count + " rows).");
                }
            }
            catch (Exception ex)
            {
                views.Clear();
                if (!viewsFailed)
                {
                    viewsFailed = true;
                    BridgeLog.Log.LogWarning("The settings screen's rows could not be listed, so the mods' alert icons and row states may lag; asked again while it is open: " + ex);
                }
            }
            return views;
        }
        public static void Forbid(EnumArrowsOption option, IEnumerable<ToggleOption> entries)
        {
            var array = entries.ToArray();
            option.SetForbiddenOptions(array.Length > 0 ? new Il2CppReferenceArray<ToggleOption>(array) : null);
        }
        public static ToggleOption[] Entries(EnumArrowsOption option)
        {
            var entries = option._options;
            if (entries == null) return new ToggleOption[0];
            var result = new ToggleOption[entries.Length];
            for (int i = 0; i < entries.Length; i++) result[i] = entries[i];
            return result;
        }
        public static void SetEntries(EnumArrowsOption option, ToggleOption[] entries) => option._options = new Il2CppReferenceArray<ToggleOption>(entries);
#else
        public static object Identity(object o) => o;
        public static bool Same(object a, object b) => ReferenceEquals(a, b);
        // AddGraphicSetting and the backing list are not public; resolved once, like the other Video-tab mods do.
        private static readonly FieldInfo GraphicList = typeof(SettingsMaster).GetField("_graphicSettings", BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Instance);
        private static readonly MethodInfo AddGraphic = typeof(SettingsMaster).GetMethod("AddGraphicSetting", BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Instance, null, new[] { typeof(Setting) }, null);
        private static readonly FieldInfo OptionsField = typeof(EnumArrowsOption).GetField("_options", BindingFlags.NonPublic | BindingFlags.Instance);
        public static SettingList GraphicSettings(SettingsMaster master) => AddGraphic != null ? GraphicList?.GetValue(master) as SettingList : null;
        public static void AddGraphicSetting(SettingsMaster master, Setting item) => AddGraphic.Invoke(master, new object[] { item });
        public static string TypeName(ISetting item) => item.GetType().Name;
        public static List<VFocusableSetting> FocusableViews(AllSettingsUI ui) => ui.Views.OfType<VFocusableSetting>().ToList();
        // The game's upscaling row (Dynamic Resolution) once the game has created its setting, else null.
        public static EnumArrowsOption UpscalingRow()
        {
            var master = World.Any<SettingsMaster>();
            var list = master != null ? GraphicSettings(master) : null;
            return list?.OfType<UpScaling>().FirstOrDefault()?.UpscalingTypeOption;
        }
        public static void Forbid(EnumArrowsOption option, IEnumerable<ToggleOption> entries)
        {
            var array = entries.ToArray();
            option.SetForbiddenOptions(array.Length > 0 ? array : null);
        }
        public static ToggleOption[] Entries(EnumArrowsOption option) => ((ToggleOption[])OptionsField?.GetValue(option))?.ToArray() ?? option.Options.ToArray();
        public static void SetEntries(EnumArrowsOption option, ToggleOption[] entries)
        {
            if (OptionsField == null || OptionsField.FieldType != typeof(ToggleOption[])) throw new MissingFieldException(nameof(EnumArrowsOption), "_options");
            OptionsField.SetValue(option, entries);
        }
#endif
    }
}
