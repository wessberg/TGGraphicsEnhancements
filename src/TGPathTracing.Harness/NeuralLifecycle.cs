// SPDX-License-Identifier: MIT
// Opt-in live validation. Use the installed upscaler's reversible diagnostic route.
// Neither normal PT recipes nor normal play enter this code.
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using Awaken.TG.Main.Settings.Options;
using Awaken.TG.Main.Settings.Options.Views;
using Awaken.TG.Main.Settings.Windows;
using Awaken.TG.MVC;
using BepInEx.Bootstrap;
using Newtonsoft.Json;
using UnityEngine;
namespace TGPathTracing.Harness
{
    internal static class NeuralLifecycle
    {
        private const BindingFlags Any = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;
        private static MethodInfo endTest;
        private static bool resized;
        private static int width, height;
        private static FullScreenMode screenMode;
        private static AllSettingsUI menu;
        private static object Get(Type type, object target, string name) => type.GetProperty(name, Any)?.GetValue(target)
            ?? type.GetField(name, Any)?.GetValue(target);
        private static Type Nr(string name) => Chainloader.PluginInfos[Neural.PluginId].Instance.GetType().Assembly.GetType("TGNeuralRendering." + name, true);
        private static EnumArrowsOption Mode()
        {
            var type = Nr("VideoRows");
            return (EnumArrowsOption)Get(type, Get(type, null, "rows"), "mode");
        }
        private static object Snapshot(string name)
        {
            long evaluated = Neural.Evaluations(out string reason);
            var native = Get(Nr("Session"), null, "Native");
            var status = Get(native.GetType(), native, "Status");
            var option = Mode();
            return new {
                name, evaluated, reason, frame = Time.frameCount,
                width = Screen.width, height = Screen.height, screenMode = Screen.fullScreenMode.ToString(),
                interactable = option.Interactable, selected = option.OptionInt,
                tooltip = option.TooltipConstructor?.Invoke(),
                enabled = (bool)Get(Nr("Session"), null, "Enabled"),
                status = status.GetType().GetFields(Any).Where(f => !f.IsStatic && f.FieldType.IsPrimitive)
                    .ToDictionary(f => f.Name, f => f.GetValue(status))
            };
        }
        internal static void Restore()
        {
            // Attempt every restoration even when one operation fails. Runner.Finish also calls this.
            var errors = new List<Exception>();
            try { if (menu != null && !menu.HasBeenDiscarded) menu.Discard(); menu = null; } catch (Exception ex) { errors.Add(ex); }
            try { CameraSlide.Stop(); } catch (Exception ex) { errors.Add(ex); }
            try { if (endTest != null) { endTest.Invoke(null, null); endTest = null; } } catch (Exception ex) { errors.Add(ex); }
            try { if (resized) { Screen.SetResolution(width, height, screenMode); resized = false; } } catch (Exception ex) { errors.Add(ex); }
            if (errors.Count > 0) throw new AggregateException("NR lifecycle restoration failed", errors);
        }
        internal static IEnumerator Run(string dir, StepResult result)
        {
            var records = new List<object>();
            var run = Audit(dir, records);
            try
            {
                while (true)
                {
                    bool more;
                    try { more = run.MoveNext(); }
                    catch (Exception ex) { result.error += " NR lifecycle: " + ex; break; }
                    if (!more) break;
                    yield return run.Current;
                }
            }
            finally
            {
                try { Restore(); }
                catch (Exception ex) { result.error += " " + ex; }
                File.WriteAllText(Path.Combine(dir, "nr-lifecycle.json"), JsonConvert.SerializeObject(records, Formatting.Indented));
            }
            // Screen.SetResolution completes at the end of the frame, including restoration.
            yield return new WaitForSecondsRealtime(2f);
        }
        private static IEnumerator Audit(string dir, List<object> records)
        {
            if (World.Any<AllSettingsUI>() != null) throw new InvalidOperationException("Close the existing settings screen before a lifecycle audit.");
            if (!(bool)Get(Nr("Session"), null, "Enabled")) throw new InvalidOperationException("Lifecycle audit requires active NR.");
            width = Screen.width; height = Screen.height; screenMode = Screen.fullScreenMode;
            var upscaler = Chainloader.PluginInfos["com.wessberg.tgbetterupscaling"].Instance.GetType().Assembly.GetType("TGBetterUpscaling.UpscalerRow", true);
            var choices = ((IEnumerable)Get(upscaler, null, "Testable")).Cast<object>().ToArray();
            var other = choices.FirstOrDefault(v => v.ToString() == "STP") ?? choices.FirstOrDefault(v => v.ToString() == "None");
            var dlss = choices.Single(v => v.ToString() == "DLSS");
            if (other == null) throw new InvalidOperationException("No supported non-DLSS choice for the gate check.");
            var test = upscaler.GetMethod("Test", Any);
            endTest = upscaler.GetMethod("EndTest", Any) ?? throw new MissingMethodException("Upscaler test restoration");
            int phase = 0;
            foreach (var choice in new[] { dlss, other, dlss })
            {
                test.Invoke(null, new[] { choice });
                yield return new WaitForSecondsRealtime(2f);
                records.Add(Snapshot(++phase + "-" + choice + "-begin"));
                yield return new WaitForSecondsRealtime(1f);
                records.Add(Snapshot(phase + "-" + choice + "-end"));
                // Open the actual Video tab and inspect its live row view, not a constructed test row.
                menu = World.Add(new AllSettingsUI());
                yield return new WaitForSecondsRealtime(.5f);
                var previousTab = menu.CurrentTabType;
                menu.ReplaceContent(SettingsTabType.GraphicSettings);
                yield return new WaitForSecondsRealtime(.5f);
                yield return new WaitForEndOfFrame();
                var view = menu.Views.OfType<VFocusableSetting>().Single(v => ReferenceEquals(v.GenericOption, Mode()));
                records.Add(new { name = phase + "-video-row", option = view.GenericOption.ID,
                    interactable = view.GenericOption.Interactable, active = view.gameObject.activeInHierarchy,
                    alpha = ((CanvasGroup)Get(typeof(VFocusableSetting), view, "content")).alpha });
                var texture = ScreenCapture.CaptureScreenshotAsTexture();
                try { File.WriteAllBytes(Path.Combine(dir, phase + "-video.png"), ImageConversion.EncodeToPNG(texture)); }
                finally { UnityEngine.Object.Destroy(texture); }
                menu.ReplaceContent(previousTab);
                menu.Discard(); menu = null;
                yield return new WaitForSecondsRealtime(.5f);
            }
            // Restore the player's upscaler before testing other discontinuities.
            endTest.Invoke(null, null); endTest = null;
            yield return new WaitForSecondsRealtime(2f);
            records.Add(Snapshot("4-before-resize"));
            resized = true;
            Screen.SetResolution(width == 2560 ? 1920 : 2560, width == 2560 ? 1080 : 1440, FullScreenMode.Windowed);
            yield return new WaitForSecondsRealtime(4f);
            records.Add(Snapshot("5-resized-begin"));
            yield return new WaitForSecondsRealtime(1f);
            records.Add(Snapshot("5-resized-end"));
            Screen.SetResolution(width, height, screenMode);
            yield return new WaitForSecondsRealtime(4f);
            if (Screen.width != width || Screen.height != height || Screen.fullScreenMode != screenMode)
                throw new InvalidOperationException("The original output size/mode was not restored.");
            resized = false;
            records.Add(Snapshot("6-restored-begin"));
            yield return new WaitForSecondsRealtime(1f);
            records.Add(Snapshot("6-restored-end"));
            CameraSlide.Start(0, 1, forward: 3);
            yield return new WaitForSecondsRealtime(1f);
            records.Add(Snapshot("7-cut-begin"));
            yield return new WaitForSecondsRealtime(1f);
            records.Add(Snapshot("7-cut-end"));
            CameraSlide.Stop();
            yield return new WaitForSecondsRealtime(1f);
            records.Add(Snapshot("8-return-begin"));
            yield return new WaitForSecondsRealtime(1f);
            records.Add(Snapshot("8-return-end"));
        }
    }
}
