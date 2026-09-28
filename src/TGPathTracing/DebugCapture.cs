// SPDX-License-Identifier: MIT
// A capture for finding out why the traced image looks wrong without having to describe it: the probe key holds the
// view for a few seconds while it saves the image, what changes from frame to frame and what of it is left without each of
// ReSTIR's reuses (what boils), each debug view, the image with the sky's light off and with rays
// from the G-buffer's surfaces stopping at near back faces (the two ways light could leak along a seam), and the game's
// own rendering (path tracing off), with a report: the exposure, every light near the camera and what the tracer makes
// of it (traced or left out, and why), HDRP's diffusion profiles, and the probe's findings under the crosshair. Written
// to BepInEx/TGPathTracing/Captures/<time>/, one folder per press.
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Text;
using BepInEx;
using TGGraphicsBridge;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
using UnityEngine.SceneManagement;
namespace TGPathTracing
{
    internal static class DebugCapture
    {
        internal static bool Running { get; private set; }
        // Read by the frame the capture pass hands to the native side: the sky's light off, and the pass-through off.
        internal static bool SkyOff;
        internal static uint ExtraOptions;
        // Read by the same frame: the rays per pixel the comparison of what boils traces (0: the row's), and the frames
        // ReSTIR DI and GI reuse a sample over (0: the defaults or the config's).
        internal static uint Rays, DiHistory, GiHistory;
        // Read by the water reflections' hook: HDRP's own reflections on water, for the shot that compares them.
        internal static bool WaterOff;
        private const int ImageWidth = 1920;
        private const float LightDistance = 40f;

        internal static void Start(MonoBehaviour host)
        {
            if (Running || host == null) return;
            string dir = Path.Combine(Paths.BepInExRootPath, "TGPathTracing", "Captures", DateTime.Now.ToString("yyyyMMdd-HHmmss"));
            Directory.CreateDirectory(dir);
            host.StartCoroutine(Run(dir));
        }

        private static IEnumerator Run(string dir)
        {
            Running = true;
            var settings = Session.Settings;
            var view = settings.CurrentView;
            var mode = settings.CurrentMode;
            var log = new StringBuilder();
            try
            {
                Notices.Show("tgpt.capture", PathTracingPlugin.Name, "Capturing for about 22 seconds: hold still", 22f);
                string probe = "";
                try { probe = Probe.Text(24); Session.Log.LogInfo(probe); } catch (Exception ex) { probe = "The probe failed: " + ex; }
                try
                {
                    var cam = Camera.main;
                    if (cam != null) probe += "\n\n== Characters along the crosshair" + CharacterCapture.DescribeAlongRay(cam.transform.position, cam.transform.forward);
                }
                catch (Exception ex) { probe += "\n\n== Characters along the crosshair: failed: " + ex.Message; }
                string report = "";
                try { report = Report(probe); } catch (Exception ex) { report = "The report failed: " + ex; }
                File.WriteAllText(Path.Combine(dir, "report.txt"), report);
                if (mode == TracingMode.Off)
                {
                    yield return Shot(dir, "game", log);
                    yield break;
                }
                settings.CurrentView = DebugView.None;
                // The same frame a quarter of a second later, and the difference between the two, four times over: what
                // flickers or boils stands out where the image holds still.
                keep = true;
                yield return Shot(dir, "01-final", log);
                var first = kept;
                yield return new WaitForSecondsRealtime(0.25f);
                yield return Shot(dir, "01b-final-again", log);
                keep = false;
                if (first != null && kept != null && first.Length == kept.Length) SaveDifference(dir, "01c-difference", first, kept, keptWidth, keptHeight);
                // What boils: the same pair of frames with the bounced light from each pixel's own 16 rays alone (ReSTIR GI's
                // reuse, which shares one sample between neighbours and frames, left out), with the direct light without
                // ReSTIR DI's reuse, and with neither. Whatever change a pair loses against the first came from the reuse left
                // out; what none loses comes from elsewhere (the radiance cache, the denoiser).
                var change = new StringBuilder("Boiling (frame-to-frame change of the image's middle, in 8x8-pixel blocks of 8-bit levels, a quarter " +
                                               "of a second apart; the crosshair's 128 pixels around in brackets): as traced " + BlockChange(first, kept, keptWidth, keptHeight));
                yield return Pair(dir, "01d-bounced-light-own-rays", Tgpt.OptionGiOwnRays, 16u, 0u, log, change, "the bounced light from each pixel's own 16 rays alone");
                yield return Pair(dir, "01e-direct-light-no-reuse", Tgpt.OptionDiNoReuse, 0u, 0u, log, change, "the direct light without its reuse");
                yield return Pair(dir, "01f-neither-reused", Tgpt.OptionGiOwnRays | Tgpt.OptionDiNoReuse, 16u, 0u, log, change, "neither");
                // The bounced light's reuse over 4 frames instead of 8 (the direct light's is 4 now: over RTXDI's 20 a cage's bars
                // boiled), and the image as traced again once it has settled like the others: the first pair is taken right
                // after the report, and the HUD and the image around it can still be changing.
                yield return Pair(dir, "01g-bounced-light-history-4", 0u, 0u, 0u, log, change, "the bounced light's reuse over 4 frames", 4u);
                yield return Pair(dir, "01h-as-traced-settled", 0u, 0u, 0u, log, change, "as traced again, settled");
                log.Insert(0, change.Append("\n").ToString());
                var views = new[] { DebugView.Direct, DebugView.Indirect, DebugView.IndirectRaw, DebugView.Specular, DebugView.Albedo, DebugView.Normals,
                                    DebugView.Coverage, DebugView.Materials, DebugView.Surfaces, DebugView.Emissive };
                int n = 2;
                foreach (var v in views)
                {
                    settings.CurrentView = v;
                    yield return new WaitForSecondsRealtime(0.5f);
                    yield return Shot(dir, (n++).ToString("00") + "-" + Settings.ViewNames[(int)v].ToLowerInvariant().Replace(' ', '-'), log);
                }
                settings.CurrentView = DebugView.None;
                SkyOff = true;
                yield return new WaitForSecondsRealtime(1f);
                yield return Shot(dir, (n++).ToString("00") + "-sky-off", log);
                SkyOff = false;
                ExtraOptions = Tgpt.OptionNoPassThrough;
                yield return new WaitForSecondsRealtime(1f);
                yield return Shot(dir, (n++).ToString("00") + "-pass-through-off", log);
                settings.CurrentView = DebugView.Indirect;
                yield return new WaitForSecondsRealtime(0.5f);
                yield return Shot(dir, (n++).ToString("00") + "-pass-through-off-indirect", log);
                settings.CurrentView = DebugView.None;
                ExtraOptions = 0;
                // The water with HDRP's own reflections, to compare with the traced ones in the first shot.
                if (settings.WaterReflections.Value)
                {
                    WaterOff = true;
                    yield return new WaitForSecondsRealtime(0.5f);
                    yield return Shot(dir, (n++).ToString("00") + "-water-reflections-off", log);
                    WaterOff = false;
                }
                string traced = HdrpPasses.ExposureText;
                settings.CurrentMode = TracingMode.Off;
                yield return new WaitForSecondsRealtime(1f);
                yield return Shot(dir, (n++).ToString("00") + "-game", log);
                log.Append("Exposure while tracing: " + traced + "\nExposure of the game's own rendering (path tracing off): " + HdrpPasses.ExposureText + "\n");
            }
            finally
            {
                SkyOff = false; ExtraOptions = 0; WaterOff = false; Rays = 0; DiHistory = 0; GiHistory = 0; keep = false;
                settings.CurrentView = view;
                settings.CurrentMode = mode;
                try { File.AppendAllText(Path.Combine(dir, "report.txt"), "\n\n== Capture\n" + log); } catch { }
                Session.Log.LogInfo("Capture written to " + dir);
                Notices.Show("tgpt.capture", PathTracingPlugin.Name, "Capture written to BepInEx/TGPathTracing/Captures", 4f);
                Running = false;
            }
        }

        // One of the comparisons of what boils: the image with those options (and rays per pixel) once the denoiser's history
        // has settled on it, again a quarter of a second later, and the difference between the two; their change goes in change.
        private static IEnumerator Pair(string dir, string name, uint options, uint rays, uint diHistory, StringBuilder log, StringBuilder change, string what,
                                        uint giHistory = 0u)
        {
            ExtraOptions = options; Rays = rays; DiHistory = diHistory; GiHistory = giHistory;
            yield return new WaitForSecondsRealtime(2f);
            keep = true;
            yield return Shot(dir, name, log);
            var first = kept;
            yield return new WaitForSecondsRealtime(0.25f);
            yield return Shot(dir, name + "-again", log, false);
            keep = false;
            if (first != null && kept != null && first.Length == kept.Length) SaveDifference(dir, name + "-difference", first, kept, keptWidth, keptHeight);
            change.Append("; " + what + " " + BlockChange(first, kept, keptWidth, keptHeight));
            ExtraOptions = 0; Rays = 0; DiHistory = 0; GiHistory = 0;
        }
        // The mean change between two shots over the 8x8-pixel blocks of the image's middle (the HUD's corners, top and bottom
        // left out), in 8-bit levels of their luma, with its 90th percentile, and in brackets the mean over the 128 pixels
        // around the crosshair: blocks that boil change, the rest hold still (a block's mean averages its pixels' noise out).
        internal static string BlockChange(Color32[] a, Color32[] b, int width, int height)
        {
            if (a == null || b == null || a.Length != b.Length || a.Length != width * height || width < 64 || height < 64) return "n/a";
            // Rows start at the image's bottom: the middle is 12 % to 75 % down the screen.
            int x0 = width / 5, x1 = width * 4 / 5, y0 = height / 4, y1 = height * 88 / 100;
            int cx = width / 2, cy = height / 2;
            var changes = new List<float>();
            double sum = 0, near = 0; int nearCount = 0;
            for (int by = y0; by + 8 <= y1; by += 8)
                for (int bx = x0; bx + 8 <= x1; bx += 8)
                {
                    float sa = 0f, sb = 0f;
                    for (int y = by; y < by + 8; y++)
                        for (int x = bx; x < bx + 8; x++)
                        {
                            var p = a[y * width + x]; var q = b[y * width + x];
                            sa += 0.2126f * p.r + 0.7152f * p.g + 0.0722f * p.b;
                            sb += 0.2126f * q.r + 0.7152f * q.g + 0.0722f * q.b;
                        }
                    float d = Mathf.Abs(sa - sb) / 64f;
                    changes.Add(d); sum += d;
                    if (Mathf.Abs(bx + 4 - cx) <= 64 && Mathf.Abs(by + 4 - cy) <= 64) { near += d; ++nearCount; }
                }
            if (changes.Count == 0) return "n/a";
            changes.Sort();
            return (sum / changes.Count).ToString("0.00") + " (90th percentile " + changes[(int)(0.9f * (changes.Count - 1))].ToString("0.00") +
                   ") [" + (nearCount > 0 ? (near / nearCount).ToString("0.00") : "n/a") + "]";
        }
        // With keep, Shot also leaves the image's pixels in kept (the difference image's inputs).
        private static bool keep;
        private static Color32[] kept;
        private static int keptWidth, keptHeight;
        private static void SaveDifference(string dir, string name, Color32[] a, Color32[] b, int width, int height)
        {
            try
            {
                var diff = new Color32[a.Length];
                for (int i = 0; i < a.Length; i++)
                    diff[i] = new Color32((byte)Mathf.Min(255, 4 * Mathf.Abs(a[i].r - b[i].r)), (byte)Mathf.Min(255, 4 * Mathf.Abs(a[i].g - b[i].g)),
                                          (byte)Mathf.Min(255, 4 * Mathf.Abs(a[i].b - b[i].b)), 255);
                var t = new Texture2D(width, height, TextureFormat.RGBA32, false);
                t.SetPixels32(diff); t.Apply(false);
                File.WriteAllBytes(Path.Combine(dir, name + ".jpg"), t.EncodeToJPG(90));
                UnityEngine.Object.Destroy(t);
            }
            catch (Exception ex) { Session.Log.LogWarning("The capture's difference image failed: " + ex.Message); }
        }
        // The frame as shown (HUD included), at most 1920 pixels wide, as a JPEG: the screen's own bytes, averaged over
        // blocks of pixels on the CPU. Blitted into an sRGB render texture, they went through the sRGB curve a second time
        // (the texture Unity returns reads them as linear), which lifted every shadow and washed a 4K screen's captures out.
        private static IEnumerator Shot(string dir, string name, StringBuilder log, bool save = true)
        {
            yield return new WaitForEndOfFrame();
            Texture2D full = null, small = null;
            try
            {
                full = ScreenCapture.CaptureScreenshotAsTexture();
                var image = full;
                int factor = Mathf.CeilToInt(full.width / (float)ImageWidth);
                if (factor > 1)
                {
                    int w = full.width / factor, h = full.height / factor, n = factor * factor;
                    var src = full.GetPixels32();
                    var dst = new Color32[w * h];
                    for (int y = 0; y < h; y++)
                        for (int x = 0; x < w; x++)
                        {
                            int r = 0, g = 0, b = 0;
                            for (int j = 0; j < factor; j++)
                            {
                                int row = (y * factor + j) * full.width + x * factor;
                                for (int i = 0; i < factor; i++) { var c = src[row + i]; r += c.r; g += c.g; b += c.b; }
                            }
                            dst[y * w + x] = new Color32((byte)(r / n), (byte)(g / n), (byte)(b / n), 255);
                        }
                    small = new Texture2D(w, h, TextureFormat.RGBA32, false);
                    small.SetPixels32(dst); small.Apply(false);
                    image = small;
                }
                if (save) File.WriteAllBytes(Path.Combine(dir, name + ".jpg"), image.EncodeToJPG(90));
                if (keep) { kept = image.GetPixels32(); keptWidth = image.width; keptHeight = image.height; }
                log.Append(name + ": view " + Session.Settings.View.Value + ", " + HdrpPasses.ExposureText + "\n");
            }
            catch (Exception ex) { log.Append(name + ": failed: " + ex.Message + "\n"); }
            finally
            {
                if (full != null) UnityEngine.Object.Destroy(full);
                if (small != null) UnityEngine.Object.Destroy(small);
            }
        }

        private static string Report(string probe)
        {
            var sb = new StringBuilder();
            var settings = Session.Settings;
            var cam = Camera.main;
            sb.Append("TG Path Tracing capture, " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "\n");
            var scenes = new List<string>();
            for (int i = 0; i < SceneManager.sceneCount; i++) scenes.Add(SceneManager.GetSceneAt(i).name);
            sb.Append("Scenes: " + string.Join(", ", scenes.ToArray()) + "\n");
            if (cam != null)
            {
                var p = cam.transform.position; var f = cam.transform.forward;
                sb.Append("Camera: (" + p.x.ToString("0.00") + ", " + p.y.ToString("0.00") + ", " + p.z.ToString("0.00") + ") looking (" +
                    f.x.ToString("0.00") + ", " + f.y.ToString("0.00") + ", " + f.z.ToString("0.00") + "), field of view " + cam.fieldOfView.ToString("0") + "\n");
            }
            sb.Append("Screen " + Screen.width + "x" + Screen.height + ", HDR output " + (HDROutputSettings.main != null && HDROutputSettings.main.active ? "on" : "off") + "\n");
            sb.Append("Settings: mode " + settings.Mode.Value + ", denoiser " + settings.CurrentDenoiser + ", indirect color " + settings.IndirectColor.Value +
                ", reflection boost " + settings.ReflectionBoost.Value + ", sky " + settings.SkyIntensity.Value + ", bounces " + settings.Bounces.Value +
                ", spatial samples " + settings.SpatialSamples.Value + ", rays per pixel " + settings.RaysPerPixel.Value + ", exposure " +
                settings.Exposure.Value.ToString("+0.0;-0.0;0.0") + " EV, dark adaptation " + settings.DarkAdaptation.Value + "%, shadow detail " + settings.ShadowDetail.Value +
                "%, darkest EV " + settings.ExposureLowest.Value + ", boiling filter " + (settings.BoilingFilter.Value ? "on" : "off") +
                ", radiance cache " + (settings.RadianceCache.Value ? "on" : "off") + ", crevice light " + (settings.CreviceLight.Value ? "on" : "off") +
                ", reflections " + settings.Reflections.Value + ", water reflections " + (settings.WaterReflections.Value ? "on" : "off") +
                ", fill light fog " + (settings.FillLightFog.Value ? "on" : "off") +
                ", emissive lights " + (settings.EmissiveLights.Value ? "on" : "off") + "\n");
            sb.Append("Status: " + Session.StatusText + "\n");
            string stages = Session.Tracer != null ? Session.Tracer.StageTimes() : "";
            sb.Append("GPU time by stage (the last second): " + (stages.Length > 0 ? stages : "not measured yet") + "\n");
            sb.Append("Scene: " + SceneCapture.Summary + " | characters " + CharacterCapture.Summary + "\n");
            float log2 = HdrpPasses.LiveExposureLog;
            // HDRP's exposure is 1 / (1.2 * 2^EV100).
            sb.Append("Exposure: " + HdrpPasses.ExposureText + (float.IsNaN(log2) ? "" : " (EV " + (-log2 - Mathf.Log(1.2f, 2f)).ToString("0.00") + ")") + "\n");
            sb.Append("HDRP exposure setup: " + HdrpPasses.ExposureSetupText + "\n");
            sb.Append("Interior (rays see no sky): " + (SceneLights.Interior ? "yes" : "no") + "\n");
            if (Session.HaveStatus)
            {
                var st = Session.Status;
                uint surface = st.PickSurface;
                string there = (surface & 4u) != 0 ? "a GBuffer surface" : (surface & 8u) != 0 ? "drawn forward" : "no surface";
                sb.Append("Traced under the crosshair: " + (st.PickFound != 0 ? SceneCapture.DescribeTraced(st.PickInstance, st.PickMaterial) + ", " +
                    st.PickDistance.ToString("0.00") + " m away" : "nothing (the sky, or out of the traced scene)") +
                    "; HDRP drew " + there + " there, roughness " + (((surface >> 8) & 0xFFu) / 255f).ToString("0.00") + " (the tracer's " +
                    (((surface >> 16) & 0xFFu) / 255f).ToString("0.00") + ")" + ((surface & 2u) != 0 ? ", glass seen through" : "") + "\n");
            }
            sb.Append("Diffusion profiles: " + HdrpPasses.DiffusionProfilesText + "\n");
            sb.Append("Water: HDRP water surfaces " + WaterSurfaces() + " | " + HdrpPasses.WaterText + "\n\n");
            sb.Append("== Probe\n" + probe + "\n\n");
            sb.Append(Lights(cam));
            return sb.ToString();
        }

        // HDRP's water surfaces (its water system: lakes, rivers, the sea) in the loaded scenes: name, kind, geometry and the
        // game's water shader, at most twelve.
        private static string WaterSurfaces()
        {
            var surfaces = UnityEngine.Object.FindObjectsByType<WaterSurface>(FindObjectsSortMode.None);
            if (surfaces.Length == 0) return "none";
            var parts = new List<string>();
            foreach (var s in surfaces)
            {
                if (s == null) continue;
                parts.Add("\"" + s.name + "\" " + s.surfaceType + " " + s.geometryType +
                    (s.customMaterial != null && s.customMaterial.shader != null ? " (" + s.customMaterial.shader.name + ")" : "") + (s.isActiveAndEnabled ? "" : ", off"));
                if (parts.Count >= 12) break;
            }
            return surfaces.Length + ": " + string.Join(", ", parts.ToArray());
        }

        private static string PathOf(Transform t, int levels)
        {
            var parts = new List<string>();
            for (int i = 0; t != null && i < levels; ++i, t = t.parent) parts.Add(t.name);
            parts.Reverse();
            return string.Join("/", parts.ToArray());
        }
        // Every light within 40 m of the camera, nearest first: what HDRP has and what the tracer makes of it.
        private static string Lights(Camera cam)
        {
            var sb = new StringBuilder();
            if (cam == null) return "== Lights: no camera\n";
            var eye = cam.transform.position;
            var near = new List<(Light light, float distance)>();
            foreach (var light in SceneLights.AllLights())
            {
                if (light == null) continue;
                float d = light.type == LightType.Directional ? 0f : Vector3.Distance(light.transform.position, eye);
                if (d <= LightDistance) near.Add((light, d));
            }
            near.Sort((a, b) => a.distance.CompareTo(b.distance));
            int traced = 0, leftOut = 0;
            foreach (var entry in near) if (SceneLights.SlotOf(entry.light) >= 0) traced++; else leftOut++;
            sb.Append("== Lights within " + LightDistance + " m: " + near.Count + " (" + traced + " traced, " + leftOut + " not)\n");
            sb.Append("distance | name (up to 6 levels) | position | type, active | intensity, color | range, traced reach, radius | cookie, fade | shadows | HDRP dimmers | tracer\n");
            foreach (var (light, distance) in near)
            {
                var hd = light.GetComponent<HDAdditionalLightData>();
                var c = light.color;
                int slot = SceneLights.SlotOf(light);
                var at = light.transform.position;
                sb.Append(distance.ToString("0.0").PadLeft(5) + " m | " + PathOf(light.transform, 6) + " | (" + at.x.ToString("0.00") + ", " + at.y.ToString("0.00") + ", " + at.z.ToString("0.00") + ") | " +
                    light.type + (light.isActiveAndEnabled ? "" : ", inactive") +
                    " | " + light.intensity.ToString("0.###") + (light.useColorTemperature ? " at " + light.colorTemperature.ToString("0") + " K" : "") +
                    " color (" + c.r.ToString("0.00") + ", " + c.g.ToString("0.00") + ", " + c.b.ToString("0.00") + ")" +
                    " | range " + light.range.ToString("0.##") + " m, reach " + SceneLights.Reach(light, hd).ToString("0.#") + " m, radius " + (hd != null ? hd.shapeRadius.ToString("0.###") : "?") + " m" +
                    " | " + (light.cookie != null ? "cookie \"" + light.cookie.name + "\"" : "no cookie") + ", fade " + (hd != null ? hd.fadeDistance.ToString("0") : "?") + " m" +
                    " | " + light.shadows +
                    " | " + (hd != null ? "light " + hd.lightDimmer.ToString("0.##") + ", fog " + (hd.affectsVolumetric ? hd.volumetricDimmer.ToString("0.##") : "off") + (hd.affectDiffuse ? "" : ", no diffuse") + (hd.affectSpecular ? "" : ", no specular") : "no HDRP data") +
                    (SceneLights.DarkenedDimmer(light, out float dimmer) ? " (darkened, was " + dimmer.ToString("0.##") + ")" : "") +
                    " | " + (slot >= 0 ? "traced in slot " + slot + ", " + SceneLights.TracedColorText(slot) : "not traced") + "; " + SceneLights.Classification(light) + "\n");
            }
            return sb.ToString();
        }
    }
}
