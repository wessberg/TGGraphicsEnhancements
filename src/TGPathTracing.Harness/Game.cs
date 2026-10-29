// SPDX-License-Identifier: MIT
// Everything the runner asks of the game itself (Tainted Grail's own code, TG.Main.dll, read from its decompiled
// source after the 2026-10-06 update): the saves and how one is loaded, when a load has finished, the game's time, its
// HUD, and that nothing is saved while a run lasts. Nothing here is touched before the title screen exists: the game's
// own types are set up by then.
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using Awaken.TG.Main.Heroes;
using Awaken.TG.Main.Saving;
using Awaken.TG.Main.Saving.Models;
using Awaken.TG.Main.Saving.SaveSlots;
using Awaken.TG.Main.Timing;
using Awaken.TG.Main.UI.Helpers;
using Awaken.TG.Main.UI.RoguePreloader;
using Awaken.TG.Main.UI.TitleScreen;
using Awaken.TG.Main.UI.TitleScreen.Loading;
using Awaken.TG.MVC;
using Awaken.TG.MVC.UI.Handlers.States;
using HarmonyLib;
using UnityEngine;
using UnityEngine.SceneManagement;
namespace TGPathTracing.Harness
{
    internal static class Game
    {
        private const string BlockerId = "TG Path Tracing harness";
        internal static int SavesBlocked { get; private set; }
        private static bool savePatched, hudHidden;
        private static readonly FieldInfo ScreenshotBytes = AccessTools.Field(typeof(SaveSlot), "_screenshotBytes");
        private static readonly MethodInfo ShowHud = AccessTools.Method(typeof(Awaken.TG.Main.UI.HUD.HUD), "ShowHUD", new[] { typeof(bool) });
        private static readonly MethodInfo HudVisibilityChanged = AccessTools.Method(typeof(Awaken.TG.Main.UI.HUD.HUD), "OnHudVisibilityChanged");

        // The game starts on "press any button" and stays there until someone does (TitleScreenUI.CreateFocusPopup waits
        // for that screen's model to be discarded, which is what a key press ends in: VPressStartUI.Hide). Found through
        // the view, so none of the game's own types is touched before the game has made them.
        internal static bool LeavePressStart()
        {
            var view = UnityEngine.Object.FindAnyObjectByType<VPressStartUI>();
            if (view == null || view.Target == null || view.Target.HasBeenDiscarded) return false;
            view.Target.Discard();
            return true;
        }
        // What stands between the start and the title screen's menu, for the log: the popups the game shows after an
        // update (patch notes, a graphics preset, a font choice) each wait for the player.
        internal static string Blocking()
        {
            var names = new List<string>();
            foreach (var popup in World.All<Awaken.TG.Main.UI.Popup.PopupUI>().ToArraySlow())
                if (popup != null) names.Add(popup.GetType().Name);
            return names.Count > 0 ? string.Join(", ", names.ToArray()) : "no popup";
        }
        // The title screen's menu is up and nothing is loading: a save can be loaded, as its Continue button does.
        internal static bool TitleReady()
        {
            if (UnityEngine.Object.FindAnyObjectByType<VTitleScreenUI>() == null) return false;
            return World.Services != null && World.HasAny<TitleScreenUI>() && !LoadingScreenUI.IsLoading && ScenePreloader.IsLoadingCompleted;
        }
        internal static bool Loading => !ScenePreloader.IsLoadingCompleted || LoadingScreenUI.IsLoading;
        // The loading screen is gone and the player could walk: the state the game itself saves and loads in.
        internal static bool InPlay()
        {
            if (Loading || !World.HasAny<Hero>() || Hero.Current == null) return false;
            var stack = UIStateStack.Instance;
            return stack != null && stack.State != null && stack.State.IsMapInteractive && Camera.main != null;
        }
        // Only queried after readiness failed. Record evidence instead of attributing it to keyboard input.
        internal static string PlayState()
        {
            var stack = UIStateStack.Instance;
            var camera = Camera.main;
            return "loading=" + Loading + ", hero=" + (World.HasAny<Hero>() && Hero.Current != null) +
                ", UI=" + (stack?.State == null ? "missing" : stack.State.GetType().Name) +
                ", interactive=" + (stack?.State != null && stack.State.IsMapInteractive) +
                ", camera=" + (camera == null ? "missing" : camera.name) + ", timeScale=" + Time.timeScale +
                ", focused=" + Application.isFocused + ", cursor=" + Cursor.lockState + ", popups=" + Blocking();
        }

        internal static SaveSlot[] Slots() => World.All<SaveSlot>().ToArraySlow();

        // A save by its ID or file name, or by the name the player gave it; of several with one name, the newest.
        internal static SaveSlot Find(string wanted, out string detail)
        {
            detail = "";
            if (string.IsNullOrEmpty(wanted)) { detail = "no save named"; return null; }
            wanted = wanted.Trim();
            SaveSlot best = null; int matches = 0;
            var slots = Slots();
            foreach (var slot in slots)
            {
                if (slot == null) continue;
                if (string.Equals(slot.ID, wanted, StringComparison.OrdinalIgnoreCase) || string.Equals(slot.SaveFileName, wanted, StringComparison.OrdinalIgnoreCase)) { detail = "by ID"; return slot; }
            }
            foreach (var slot in slots)
            {
                if (slot == null) continue;
                string name = Text(slot.Name).Trim(), shown = Text(slot.DisplayName).Trim();
                if (!string.Equals(name, wanted, StringComparison.OrdinalIgnoreCase) && !string.Equals(shown, wanted, StringComparison.OrdinalIgnoreCase)) continue;
                ++matches;
                if (best == null || (slot.LastSavedTime ?? DateTime.MinValue) > (best.LastSavedTime ?? DateTime.MinValue)) best = slot;
            }
            detail = best != null ? "by name" + (matches > 1 ? ", the newest of " + matches : "") : "none of the " + slots.Length + " saves has that ID or name";
            return best;
        }

        internal static SaveInfo Describe(SaveSlot slot)
        {
            var info = new SaveInfo();
            info.id = slot.ID ?? ""; info.file = slot.SaveFileName ?? "";
            info.name = Text(slot.Name); info.displayName = Text(slot.DisplayName); info.customName = slot.IsCustomName;
            info.location = Text(slot.HeroLocation); info.hero = Text(slot.HeroName); info.heroLevel = slot.HeroLevel;
            try { info.scene = slot.SceneRef != null ? slot.SceneRef.Name ?? "" : ""; } catch { }
            try { info.additiveScene = slot.AdditiveSceneRef != null ? slot.AdditiveSceneRef.Name ?? "" : ""; } catch { }
            info.saved = slot.LastSavedTime.HasValue ? slot.LastSavedTime.Value.ToString("yyyy-MM-dd HH:mm:ss") : "";
            try { info.playTime = slot.PlayRealTime.TotalHours.ToString("0.0", System.Globalization.CultureInfo.InvariantCulture) + " h"; } catch { }
            info.quickSave = slot.IsQuickSave; info.autoSave = slot.IsAutoSave;
            try { info.canLoad = slot.CanLoad(); } catch { }
            var bytes = Thumbnail(slot);
            info.thumbnailBytes = bytes != null ? bytes.Length : 0;
            return info;
        }
        // The picture the game keeps with a save (a 320x180 JPEG), as its load menu shows it.
        internal static byte[] Thumbnail(SaveSlot slot)
        {
            try { return ScreenshotBytes != null ? ScreenshotBytes.GetValue(slot) as byte[] : null; } catch { return null; }
        }
        private static string Text(object value)
        {
            try { return value == null ? "" : value.ToString() ?? ""; } catch { return ""; }
        }

        internal static bool CanLoad(SaveSlot slot) => slot != null && !slot.HasBeenDiscarded && slot.CanLoad();
        internal static void Load(SaveSlot slot) => LoadSave.Get.Load(slot, "TG Path Tracing harness");

        // Nothing is saved while a run lasts: the game's own blocker stops its automatic saves before they pick a slot
        // (the timer, doors and rest ask LoadSave.CanAutoSave; a quest's forced one asks CanForceAutoSave: both look for
        // a SaveBlocker), and LoadSave.Save itself returns without writing, whoever calls it. The blocker lives in the
        // gameplay domain, which a load drops: it is added again after each.
        internal static void BlockSaves(Harmony harmony, Action<string> note)
        {
            if (!savePatched)
            {
                var save = AccessTools.Method(typeof(LoadSave), "Save", new[] { typeof(SaveSlot), typeof(bool) });
                if (save != null)
                {
                    harmony.Patch(save, prefix: new HarmonyMethod(typeof(Game), nameof(SkipSave)));
                    savePatched = true;
                }
                else note("LoadSave.Save was not found: saves are stopped by the game's SaveBlocker alone.");
            }
            if (!World.HasAny<Hero>()) return; // the title screen has no gameplay domain to hold a blocker
            foreach (var blocker in World.All<SaveBlocker>().ToArraySlow())
                if (blocker != null && blocker.SourceID == BlockerId) return;
            World.Add(new SaveBlocker(BlockerId));
        }
        private static bool SkipSave(SaveSlot saveSlot)
        {
            ++SavesBlocked;
            Runner.Note("A save was stopped (" + (saveSlot != null ? saveSlot.ID : "no slot") + "): nothing is saved during a run.");
            return false;
        }

        internal static string WeatherTime()
        {
            var time = World.Any<GameRealTime>();
            return time != null ? ((DateTime)time.WeatherTime).ToString("yyyy-MM-dd HH:mm:ss") : "";
        }
        internal static void SetTimeOfDay(int hour, int minute)
        {
            var time = World.Any<GameRealTime>();
            if (time != null) time.SetWeatherTime(hour, minute, 0, 0);
        }

        // The camera's look up or down, through the hero's own camera (HeroCamera.SetPitch, which the game's look input
        // adds to every frame): a save does not keep it.
        internal static bool SetPitch(float degrees)
        {
            var hero = Hero.Current;
            var controller = hero != null ? hero.VHeroController : null;
            if (controller == null || controller.HeroCamera == null) return false;
            controller.HeroCamera.SetPitch(degrees);
            return true;
        }

        // The hero turned to a facing (degrees round the vertical): the hero's view turns its own transform when the player
        // looks sideways, and the hero's saved rotation is read from it.
        internal static bool SetYaw(float degrees)
        {
            var hero = Hero.Current;
            var controller = hero != null ? hero.VHeroController : null;
            if (controller == null) return false;
            controller.transform.rotation = Quaternion.Euler(0f, degrees, 0f);
            return true;
        }
        internal static float Yaw()
        {
            var hero = Hero.Current;
            var controller = hero != null ? hero.VHeroController : null;
            return controller != null ? controller.transform.eulerAngles.y : 0f;
        }

        internal static string Scenes()
        {
            var names = new List<string>();
            for (int i = 0; i < SceneManager.sceneCount; i++) names.Add(SceneManager.GetSceneAt(i).name);
            return string.Join(", ", names.ToArray());
        }

        // The HUD and every other canvas off, as the game's photo mode does it, and back. The loading screen is a
        // canvas too: the HUD is shown again before the next load.
        internal static void HideHud(bool hide)
        {
            if (hide == hudHidden) return;
            hudHidden = hide;
            if (hide)
            {
                var hud = World.Any<Awaken.TG.Main.UI.HUD.HUD>();
                if (hud != null && ShowHud != null) ShowHud.Invoke(hud, new object[] { false });
                UIUtils.HideUI();
            }
            else
            {
                UIUtils.ShowUI();
                var hud = World.Any<Awaken.TG.Main.UI.HUD.HUD>();
                if (hud != null && HudVisibilityChanged != null) HudVisibilityChanged.Invoke(hud, null);
            }
        }

        internal static void WriteThumbnail(SaveSlot slot, string folder)
        {
            var bytes = Thumbnail(slot);
            if (bytes == null || bytes.Length == 0) return;
            Directory.CreateDirectory(folder);
            File.WriteAllBytes(Path.Combine(folder, Safe(slot.SaveFileName ?? slot.ID) + ".jpg"), bytes);
        }
        internal static string Safe(string name)
        {
            if (string.IsNullOrEmpty(name)) return "unnamed";
            var chars = name.ToCharArray();
            for (int i = 0; i < chars.Length; i++)
                if (!(char.IsLetterOrDigit(chars[i]) || chars[i] == '-' || chars[i] == '_' || chars[i] == '.')) chars[i] = '-';
            return new string(chars);
        }
    }
}
