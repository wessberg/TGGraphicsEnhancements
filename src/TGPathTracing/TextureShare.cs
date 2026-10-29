// SPDX-License-Identifier: MIT
// PT-45, phase 1: the game's own textures opened by the tracer's D3D12 device with no copy (TGPT_RegisterSharedTexture).
// Each traced material's colour, normal and mask textures (and characters' maps) are registered beside their atlas tiles,
// which rays still read: phase 2 moves the shaders over to these. A line in the log says, for the traced scene, how many
// were opened, their video memory, and why the others were not.
using System;
using System.Collections.Generic;
using UnityEngine;
namespace TGPathTracing
{
    internal static class TextureShare
    {
        private static readonly Dictionary<int, int> numbers = new Dictionary<int, int>(); // by texture: its number, or why it has none
        private static readonly Dictionary<int, int> handles = new Dictionary<int, int>(); // by texture: what a material names it by (PT-46), -1 none
        private static readonly Dictionary<int, int> reasons = new Dictionary<int, int>();  // how many had each refusal
        private static readonly Dictionary<uint, int> refusedFormats = new Dictionary<uint, int>(); // the driver's refusals, by DXGI format
        private static int opened, logged, swapped, small;
        private static readonly Dictionary<int, ulong> missing = new Dictionary<int, ulong>();
        internal static bool Incomplete => missing.Count != 0;
        internal static string FailureText {
            get { ulong needed = 0; bool unknown = false; foreach (ulong n in missing.Values) { needed += n; unknown |= n == 0; }
                return "Path tracing paused: " + missing.Count + " textures unavailable (" + (needed / 1048576.0).ToString("0.0") + " MB" + (unknown ? ", plus textures of unknown size" : "") + "). The game is drawing the scene. See the texture names and reasons in the log."; }
        }
        private static void Availability(Texture texture, int code, NativeSharedTexture info)
        {
            int id = texture.GetInstanceID();
            if (code >= 0) { missing.Remove(id); return; }
            bool first = !missing.ContainsKey(id); missing[id] = info.Bytes;
            if (first) Session.Log.LogError("Path tracing texture unavailable: '" + texture.name + "', id " + id + ", " +
                texture.width + "x" + texture.height + ", format " + info.Format + ", needs " + info.Bytes + " bytes: " + Reason(code) +
                ". No atlas stand-in is shown; path tracing is paused while the game draws the scene.");
        }
        private static ulong bytes, smallBytes;
        private static NativeSharedTexture totals;
        private static float nextReport;
        // (_Normal_Map: the game's Cloth_Wind, PT-64. The game's other graphs with names of their own: MaterialPrograms.)
        private static readonly string[] NormalNames = { "_NormalMap", "_BumpMap", "_NormalMap0", "_Normal", "_NormalTex", "_Normal_Map" };

        // A texture whose native pointer the caller has read already (GetNativeTexturePtr waits for Unity's render thread).
        internal static void Register(Texture texture, IntPtr native)
        {
            if (texture == null) return;
            int id = texture.GetInstanceID();
            var tracer = Session.Tracer;
            if (numbers.ContainsKey(id) || tracer == null || !tracer.Connected) return;
            var info = new NativeSharedTexture();
            int n = native == IntPtr.Zero ? Tgpt.ShareOpenFailed : tracer.RegisterSharedTexture(native, out info);
            if (n == Tgpt.ShareBeforeHook) n = Twin(texture, tracer, n, ref info);
            numbers[id] = n;
            handles[id] = n >= 0 ? info.Handle : -1;
            if (!watch.TryGetValue(id, out var w)) watch[id] = w = new Watched { Texture = texture, Id = id, Native = native, Mip = LoadedMip(texture), Updates = texture.updateCount };
            totals = info; Availability(texture, n, info);
            // PT-64: every size is shared; what the small ones cost (each takes a 64 KB page of its own) is counted for the log.
            if (n >= 0) { ++opened; bytes += info.Bytes; if (info.Bytes <= 65536) { ++small; smallBytes += info.Bytes; } return; }
            reasons[n] = (reasons.TryGetValue(n, out int count) ? count : 0) + 1;
            if (n == Tgpt.ShareRefused) refusedFormats[info.Format] = (refusedFormats.TryGetValue(info.Format, out int f) ? f : 0) + 1;
            if (n < 0 && !kept.Contains(w)) kept.Add(w);
        }
        // PT-78: a texture the engine made before the hook could exist (HDRP's own resources, loaded with the pipeline: the
        // water's foam mask, 2026-10-08) can never be shared, and path tracing paused for it in every scene with water. Unity
        // makes such a texture once more, now: the same size, format and mips, its texels copied on the GPU. The twin is made
        // after the hook, so the tracer opens it as any other. It IS a copy (exact, whole, held until the traced scene starts
        // over), the one exception to "no copies", named in the log with its bytes; the fix at the cause is a hook that is in
        // before the engine makes its first texture. A streamed texture gets none (mip streaming makes it anew by itself).
        private static readonly Dictionary<int, Texture2D> twins = new Dictionary<int, Texture2D>();
        private static ulong twinBytes;
        private static int Twin(Texture texture, NativeTracer tracer, int code, ref NativeSharedTexture info)
        {
            int id = texture.GetInstanceID();
            if (!twins.TryGetValue(id, out var twin) || twin == null)
            {
                var source = texture as Texture2D;
                if (source == null || source.streamingMipmaps || source.width <= 0 || source.height <= 0) return code;
                try
                {
                    twin = new Texture2D(source.width, source.height, source.format, source.mipmapCount, !source.isDataSRGB)
                    { name = source.name + " (TG Path Tracing: made again after the hook)", wrapModeU = source.wrapModeU, wrapModeV = source.wrapModeV,
                      filterMode = source.filterMode, anisoLevel = source.anisoLevel, mipMapBias = source.mipMapBias, hideFlags = HideFlags.HideAndDontSave };
                    twin.Apply(false, true); // its GPU texture exists from here on; the texels follow
                    Graphics.CopyTexture(source, twin);
                }
                catch (Exception e)
                {
                    Session.Log.LogWarning("Shared textures: '" + texture.name + "' was made before the hook and could not be made again (" + e.Message + ").");
                    if (twin != null) UnityEngine.Object.Destroy(twin);
                    return code;
                }
                twins[id] = twin;
            }
            IntPtr native = twin.GetNativeTexturePtr();
            if (native == IntPtr.Zero) return code;
            var again = new NativeSharedTexture();
            int n = tracer.RegisterSharedTexture(native, out again);
            if (n < 0)
            {
                Session.Log.LogWarning("Shared textures: '" + texture.name + "' was made again after the hook and still is not opened: " + Reason(n) + ".");
                info = again; return n;
            }
            twinBytes += again.Bytes;
            Session.Log.LogInfo("Shared textures: '" + texture.name + "' (" + texture.width + "x" + texture.height + ", format " + again.Format + ") was made by the engine before the hook " +
                "could exist and can never be shared: made once more, an exact copy the tracer reads (" + again.Bytes + " bytes). " + twins.Count + " such copies now, " +
                (twinBytes / 1048576.0).ToString("0.0") + " MB. The game's own: " + Texels(texture) + "; the copy: " + Texels(twin) + ".");
            info = again; return n;
        }
        // What a texture holds, drawn small and read back: for the log's proof that a copy took the game's texels (PT-81: the
        // sea came out as one sheet of foam, and a copy left at a new texture's grey would do that).
        private static string Texels(Texture texture)
        {
            try
            {
                const int size = 64;
                var target = RenderTexture.GetTemporary(size, size, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear);
                var before = RenderTexture.active;
                Graphics.Blit(texture, target);
                RenderTexture.active = target;
                var read = new Texture2D(size, size, TextureFormat.RGBA32, false, true);
                read.ReadPixels(new Rect(0, 0, size, size), 0, 0);
                RenderTexture.active = before;
                RenderTexture.ReleaseTemporary(target);
                var pixels = read.GetPixels32();
                UnityEngine.Object.Destroy(read);
                double r = 0, g = 0, b = 0, a = 0; int low = 255, high = 0;
                foreach (var c in pixels) { r += c.r; g += c.g; b += c.b; a += c.a; if (c.r < low) low = c.r; if (c.r > high) high = c.r; }
                double n = pixels.Length;
                return "mean " + (r / n).ToString("0") + ", " + (g / n).ToString("0") + ", " + (b / n).ToString("0") + ", " + (a / n).ToString("0") + " of 255, red from " + low + " to " + high;
            }
            catch (Exception e) { return "not read (" + e.Message + ")"; }
        }
        // PT-45 phase 3: a hair or eye map (SceneCapture.NativeTexture), whose native pointer the hair and eye bakes take: watched
        // for mip streaming as material textures are, and when made anew, the bakes' records follow it (TGPT_SwapSharedTexture).
        internal static void Character(Texture texture, IntPtr native)
        {
            Register(texture, native);
            if (!watch.TryGetValue(texture.GetInstanceID(), out var w) || w.Character) return;
            w.Character = true;
            if (w.Uses.Count == 0) watched.Add(w); // a material's texture is in the list already
        }
        // A decal map has no NativeMaterial index but still follows mip streaming through the shared registry.
        internal static void WatchStandalone(Texture texture)
        {
            if (texture == null || !watch.TryGetValue(texture.GetInstanceID(), out var w) || w.Character) return;
            w.Character = true;
            if (w.Uses.Count == 0) watched.Add(w);
        }
        internal static void Register(Texture texture)
        {
            if (texture != null && !numbers.ContainsKey(texture.GetInstanceID())) Register(texture, texture.GetNativeTexturePtr());
        }
        // What a material names a texture by (NativeMaterial.ColorTexture, ...): its handle once registered, -1 without a number
        // (the material's tile serves then).
        internal static int Handle(Texture texture, int field = -1)
        {
            if (texture == null) return -1;
            Register(texture);
            int id = texture.GetInstanceID();
            if (field >= 0 && watch.TryGetValue(id, out var w)) pending.Add(new KeyValuePair<Watched, int>(w, field));
            return handles.TryGetValue(id, out int h) ? h : -1;
        }

        // Unity's mip streaming makes a new D3D11 texture when it loads (or drops) a streamed texture's top mips (2026-10-06's
        // runs: 12 of 12, "64x64, 7 mips" then "2048x2048, 12 mips, NEW OBJECT"): a material's handle then names a texture
        // that dies, and before its top mips the stand-in, too small to share, has none. So each scan looks at a slice of the
        // textures materials read (their loaded mip and update count: cheap); where either changed, it reads the native pointer
        // again, registers a new one and writes its handle into the materials that read it, which are sent again. The old
        // texture's death lets its number go (texture_share.cpp).
        // FieldProgram and up: a word of a material program's record (MaterialPrograms), by its place in the records (PT-64).
        internal const int FieldColor = 0, FieldNormal = 1, FieldMask = 2, FieldEmissive = 3, FieldProgram = 16;
        private sealed class Watched
        {
            internal Texture Texture; internal int Id; internal IntPtr Native; internal int Mip; internal uint Updates;
            internal readonly List<KeyValuePair<int, int>> Uses = new List<KeyValuePair<int, int>>(); // material index, field
            internal bool Character; // a hair or eye map (Character): the native bakes' records follow it too
        }
        private static readonly Dictionary<int, Watched> watch = new Dictionary<int, Watched>();
        private static readonly List<Watched> watched = new List<Watched>();
        private static readonly List<KeyValuePair<Watched, int>> pending = new List<KeyValuePair<Watched, int>>();
        private static int watchNext;
        // PT-45 phase 3: textures the video memory cap kept back (Tgpt.ShareBudget), asked for again every BudgetRetrySeconds:
        // the game frees textures as the player travels, and the room comes back.
        private static readonly List<Watched> kept = new List<Watched>();
        private const float BudgetRetrySeconds = 10f;
        private static float nextBudgetRetry;
        private static int retried;
        private static int LoadedMip(Texture t) => t is Texture2D t2 && t2.streamingMipmaps ? t2.loadedMipmapLevel : 0;
        // For the arrival's timeline (Arrival.cs, PT-98). Wanted: the textures traced materials name; Open: those the tracer
        // has opened; KeptBack: those the video memory cap holds back. Of the watched ones, as the last scan's look found
        // them (Refresh): at full size, below the level the game itself wants for them, and being loaded sharper by the game
        // right now (a swap that is still to come). Swaps: the new textures followed so far.
        internal static int Wanted => numbers.Count;
        internal static int Open { get { int n = 0; foreach (var pair in numbers) if (pair.Value >= 0) ++n; return n; } }
        internal static int KeptBack => kept.Count;
        internal static int WatchedCount => watched.Count;
        internal static int Swaps => swapped;
        internal static int WatchedFull { get; private set; }
        internal static int WatchedBelow { get; private set; }
        internal static int WatchedLoading { get; private set; }
        // The looks at the watched textures so far, and the time they took (the timeline says what a look costs).
        internal static int Looks { get; private set; }
        internal static double LookMs { get; private set; }
        private static readonly System.Diagnostics.Stopwatch lookClock = new System.Diagnostics.Stopwatch();
        // A material is being built: the textures Handle names with a field from now on are its own.
        internal static void BeginMaterial() => pending.Clear();
        // ... and it went into the material list at index.
        internal static void Bind(int index)
        {
            foreach (var use in pending)
            {
                if (use.Key.Uses.Count == 0 && !use.Key.Character) watched.Add(use.Key);
                use.Key.Uses.Add(new KeyValuePair<int, int>(index, use.Value));
            }
            pending.Clear();
        }
        // Each scan, before new materials are sent: every watched texture looked at (up to 4096; its loaded mip and update
        // count: two cheap reads); a swapped one's materials written and the ones already sent sent again. (It was 256 a
        // scan: with a town square's 1,641 textures a swap could wait 13 s to be seen, and a guard stood in 64 x 64
        // stand-ins that long after the game had loaded his sharp textures: PT-95, PT-98.)
        // And between scans, by the clock (SceneCapture.TextureLookInterval, scan false: PT-98): a scan comes every 2 s, and
        // a texture the game has just loaded sharp was read from its 64 x 64 tile until the next one. The trial's look
        // (PT-95, which asks Unity for every texture's pointer) stays with the scan.
        internal static void Refresh(List<NativeMaterial> materials, int uploaded, NativeTracer tracer, bool scan = true)
        {
            // Unity destruction can occur between scans, including textures only registered for a material program.
            foreach (var pair in watch) if (pair.Value.Texture == null) missing.Remove(pair.Key);
            if (tracer == null || !tracer.Connected) return;
            lookClock.Restart();
            try { Look(materials, uploaded, tracer, scan); }
            finally { lookClock.Stop(); ++Looks; LookMs += lookClock.Elapsed.TotalMilliseconds; }
        }
        private static void Look(List<NativeMaterial> materials, int uploaded, NativeTracer tracer, bool scan)
        {
            HashSet<int> resend = null;
            int full = 0, below = 0, loading = 0;
            for (int k = Math.Min(watched.Count, 4096); k > 0; --k)
            {
                var w = watched[watchNext++ % watched.Count];
                if (w.Texture == null) { missing.Remove(w.Id); continue; } // destroyed
                int mip = LoadedMip(w.Texture); uint updates = w.Texture.updateCount;
                // PT-98, for the timeline: a level above 0 is a streamed Texture2D's (LoadedMip).
                if (mip == 0) ++full;
                else { var streamed = (Texture2D)w.Texture; if (streamed.desiredMipmapLevel < mip) ++below; if (streamed.loadingMipmapLevel < mip) ++loading; }
                if (mip == w.Mip && updates == w.Updates) continue;
                w.Mip = mip; w.Updates = updates;
                IntPtr native = w.Texture.GetNativeTexturePtr();
                if (native == IntPtr.Zero || native == w.Native) continue;
                Follow(w, native, materials, uploaded, tracer, ref resend);
                ++swapped;
            }
            WatchedFull = full; WatchedBelow = below; WatchedLoading = loading;
            // Textures the video memory cap kept back, asked for again (every waiting texture): the materials and the hair and eye
            // records that name them take their numbers when there is room again.
            if (kept.Count > 0 && Time.unscaledTime >= nextBudgetRetry)
            {
                nextBudgetRetry = Time.unscaledTime + BudgetRetrySeconds;
                var again = kept.GetRange(0, kept.Count);
                kept.RemoveRange(0, again.Count);
                foreach (var w in again)
                {
                    if (w.Texture == null) { missing.Remove(w.Id); continue; } // unloaded
                    IntPtr native = w.Texture.GetNativeTexturePtr();
                    if (native == IntPtr.Zero) continue;
                    int was = numbers.TryGetValue(w.Texture.GetInstanceID(), out int code) ? code : 0;
                    int now = Follow(w, native, materials, uploaded, tracer, ref resend);
                    if (now == was) continue; // still kept back (Follow listed it again)
                    if (was < 0 && reasons.TryGetValue(was, out int c) && c > 0) reasons[was] = c - 1;
                    if (now >= 0) ++retried; else reasons[now] = (reasons.TryGetValue(now, out int d) ? d : 0) + 1;
                }
            }
            // PT-95 on trial (the hidden "Texture follow"): every texture looked at, whatever the game says of its level.
            int follow = Session.Settings != null ? Session.Settings.TextureFollow.Value : 0;
            if (follow > 0 && scan)
            {
                // (A trial must not stop the scan it runs in: its trouble is a line in the log, once.)
                try { Audit(materials, uploaded, tracer, follow, ref resend); }
                catch (Exception e)
                {
                    if (!auditFailed) Session.Log.LogWarning("Shared textures on trial (PT-95): the look failed (" + e.GetType().Name + ": " + e.Message + "); it goes on at the next scan.");
                    auditFailed = true;
                }
            }
            if (resend == null) return;
            foreach (int i in resend) tracer.SetMaterials((uint)i, new[] { materials[i] }, 1);
        }
        // PT-95 ON TRIAL (the hidden "Texture follow"; docs/agents/kandra-lit.md). A guard came out in blocks of colour, his
        // armour without relief (run 20261009-084003): every material of his read its 64x64 tiles, with no normal map, which is
        // what a material is given whose handles name textures the game has freed (texture_share.cpp resolve). Refresh looks
        // at a texture again only when Unity's loaded mip level or update count changed, and writes the new level down before
        // it knows the texture's object changed: a swap it meets while the new object is not there yet, a level that went
        // down and came back between two looks, or a new object at the old one's address is never followed. That run's own
        // counts leave 484 of 1656 textures freed by the game beyond the swaps followed.
        // 1: each scan every watched texture is asked for the object the game holds now, and for the handle the tracer has
        // for THAT object: where it is not the handle the materials name, the log names the texture (the picture as before:
        // nothing is written). 2: those are followed, as a swap Refresh saw is. 0: none of this runs.
        // The look asks Unity for each texture's native pointer (a wait for its render thread: the first of a scan is the
        // wait, the rest are cheap) and the registry for its handle; the line says what the look took. If the trial is kept,
        // the native side should say which handles are dead instead (no pointer asked for), and Refresh's own test goes.
        private const int AuditNames = 300, AuditLowNames = 12;
        private const float AuditLineSeconds = 30f;
        private static readonly HashSet<int> stale = new HashSet<int>();
        private static int auditFollowed, auditMode;
        private static float nextAuditLine;
        private static bool auditFailed;
        private static void Audit(List<NativeMaterial> materials, int uploaded, NativeTracer tracer, int mode, ref HashSet<int> resend)
        {
            var clock = System.Diagnostics.Stopwatch.StartNew();
            int looked = 0, staleNow = 0, moved = 0, sameAddress = 0, unopened = 0, low = 0, followed = 0, fresh = 0, making = 0, refused = 0;
            var named = new List<string>(); var lowNamed = new List<string>();
            for (int i = 0; i < watched.Count; ++i)
            {
                var w = watched[i];
                if (w.Texture == null) { stale.Remove(w.Id); continue; } // destroyed
                if (!handles.TryGetValue(w.Id, out int held) || held < 0) { ++unopened; continue; } // kept back or refused: Refresh asks again
                IntPtr native = w.Texture.GetNativeTexturePtr();
                if (native == IntPtr.Zero) { ++making; continue; } // being made anew right now: the next look
                ++looked;
                int level = LoadedMip(w.Texture);
                if (level > 0) { ++low; if (lowNamed.Count < AuditLowNames && ReadByCharacter(w)) lowNamed.Add(Describe(w, default(NativeSharedTexture), level, false)); }
                // The handle the tracer has for the object the game holds NOW: the one the materials name while it reads that object.
                int n = tracer.RegisterSharedTexture(native, out var info);
                if (n < 0) { ++refused; continue; } // what the game holds now cannot be opened (the line counts them)
                if (info.Handle == held) { stale.Remove(w.Id); continue; }
                ++staleNow;
                if (native != w.Native) ++moved; else ++sameAddress;
                if (stale.Add(w.Id)) { ++fresh; if (named.Count < AuditNames) named.Add(Describe(w, info, level, true)); }
                if (mode < 2) continue;
                Follow(w, native, materials, uploaded, tracer, ref resend);
                w.Mip = level; w.Updates = w.Texture.updateCount;
                stale.Remove(w.Id); ++followed; ++auditFollowed; ++swapped;
            }
            clock.Stop();
            float now = Time.unscaledTime;
            if (fresh == 0 && followed == 0 && mode == auditMode && now < nextAuditLine) return;
            auditMode = mode; nextAuditLine = now + AuditLineSeconds;
            string streaming;
            try
            {
                streaming = Texture.streamingTextureCount + " streamed textures, " + Texture.streamingTextureLoadingCount + " loading, " +
                    Texture.streamingTexturePendingLoadCount + " waiting; their memory " + (Texture.currentTextureMemory / 1048576.0).ToString("0") + " MB now, " +
                    (Texture.desiredTextureMemory / 1048576.0).ToString("0") + " MB wanted, " + (Texture.targetTextureMemory / 1048576.0).ToString("0") +
                    " MB aimed at, under a budget of " + QualitySettings.streamingMipmapsMemoryBudget.ToString("0") + " MB";
            }
            catch (Exception e) { streaming = "not read (" + e.Message + ")"; }
            Session.Log.LogInfo("Shared textures on trial (PT-95, Texture follow " + mode + "): of " + looked + " textures traced materials read, " + staleNow +
                " are named by a handle of another texture than the game holds now (the tracer shows their 64x64 tiles and no normal map where that one is freed): " +
                moved + " replaced by a new texture, " + sameAddress + " by a new one at the old address; " + fresh + " found in this look" +
                (mode >= 2 ? "; " + followed + " followed now, " + auditFollowed + " since the traced scene started" : "; none followed (1 only looks)") +
                "; " + low + " are below their full size in the game right now (the tracer reads what the game holds)" +
                (unopened > 0 ? "; " + unopened + " have no handle (kept back or refused)" : "") + (making > 0 ? "; " + making + " are being made anew" : "") +
                (refused > 0 ? "; " + refused + " the game holds now could not be opened (not counted above)" : "") +
                "; the look took " + clock.Elapsed.TotalMilliseconds.ToString("0.0") + " ms. The game's streaming: " + streaming + "." +
                (named.Count > 0 ? " Found in this look: " + string.Join("; ", named.ToArray()) + (fresh > named.Count ? "; and " + (fresh - named.Count) + " more" : "") + "." : "") +
                (lowNamed.Count > 0 ? " Characters' textures below their full size: " + string.Join("; ", lowNamed.ToArray()) + "." : ""));
        }
        private static bool ReadByCharacter(Watched w)
        {
            foreach (var use in w.Uses) if (SceneCapture.MaterialLabel(use.Key).IndexOf("TG/Character/", StringComparison.Ordinal) >= 0) return true;
            return false;
        }
        // A texture for the trial's line: its name and full size, what the game holds of it now, and the materials that read it.
        private static string Describe(Watched w, NativeSharedTexture info, int level, bool held)
        {
            var users = new List<string>();
            foreach (var use in w.Uses)
            {
                string label = SceneCapture.MaterialLabel(use.Key);
                if (!users.Contains(label)) users.Add(label);
                if (users.Count == 2) break;
            }
            return "'" + w.Texture.name + "' " + w.Texture.width + "x" + w.Texture.height + " (" +
                (held ? "the game holds " + info.Width + "x" + info.Height + " with " + info.Mips + " levels now, " : "") + "its level " + level + " loaded) read by " +
                (users.Count > 0 ? string.Join(" and ", users.ToArray()) + (w.Uses.Count > users.Count ? " and others" : "") : w.Character ? "a hair, eye or decal map" : "no material");
        }
        // A watched texture's native pointer now (mip streaming's new object, or the same one asked for again): registered, its
        // handle written into the materials that read it (resend: those to send again), and a hair or eye map's records pointed
        // at it. Its number, or why it has none.
        private static int Follow(Watched w, IntPtr native, List<NativeMaterial> materials, int uploaded, NativeTracer tracer, ref HashSet<int> resend)
        {
            IntPtr old = w.Native;
            w.Native = native;
            int id = w.Texture.GetInstanceID();
            int n = tracer.RegisterSharedTexture(native, out var info);
            int handle = n >= 0 ? info.Handle : -1;
            numbers[id] = n; handles[id] = handle; totals = info; Availability(w.Texture, n, info);
            if (n >= 0) { ++opened; bytes += info.Bytes; }
            else if (n < 0 && !kept.Contains(w)) kept.Add(w);
            if (w.Character) tracer.SwapSharedTexture(old, native); // PT-45 phase 3: the hair and eye bakes' records follow
            foreach (var use in w.Uses)
            {
                if (use.Value >= FieldProgram) { MaterialPrograms.SetWord(use.Value - FieldProgram, handle); continue; }
                if (use.Key >= materials.Count) continue;
                var m = materials[use.Key];
                if (use.Value == FieldColor) m.ColorTexture = handle; else if (use.Value == FieldNormal) m.NormalTexture = handle;
                else if (use.Value == FieldEmissive) m.EmissiveTexture = handle; else m.MaskTexture = handle;
                materials[use.Key] = m;
                if (use.Key < uploaded) (resend ?? (resend = new HashSet<int>())).Add(use.Key);
            }
            return n;
        }
        // A material's normal map (the atlas holds none): its handle and strength, as the game's shader reads it. HDRP Lit reads
        // its normal map only with its keyword on and in tangent space (_NormalMapSpace 0); its layered version's base layer's.
        internal static int Normal(Material m, string shaderName, out float scale)
        {
            scale = 1f;
            if (shaderName == "HDRP/Lit" && (!m.IsKeywordEnabled("_NORMALMAP") || (m.HasProperty("_NormalMapSpace") && m.GetFloat("_NormalMapSpace") > 0.5f))) return -1;
            if (shaderName == "HDRP/LayeredLit" && (!m.IsKeywordEnabled("_NORMALMAP0") || (m.HasProperty("_NormalMapSpace0") && m.GetFloat("_NormalMapSpace0") > 0.5f))) return -1;
            foreach (var name in NormalNames)
            {
                if (!m.HasProperty(name)) continue;
                var t = m.GetTexture(name);
                if (t == null) continue;
                foreach (var s in NormalScaleNames) if (m.HasProperty(s)) { scale = m.GetFloat(s); break; }
                return Handle(t, FieldNormal);
            }
            return -1;
        }
        private static readonly string[] NormalScaleNames = { "_NormalScale", "_NormalScale0", "_BumpScale", "_NormalStrength" };

        // After each scan: a line when textures came since the last one, at most every 10 s.
        internal static void Report(float now)
        {
            if (now < nextReport) return;
            nextReport = now + 10f;
            Log("");
        }
        // The traced scene starts over (TGPT_ClearScene let go of every shared texture): its last line, then counts anew.
        internal static void Reset()
        {
            if (numbers.Count != logged) Log(" before the traced scene started over");
            foreach (var twin in twins.Values) if (twin != null) UnityEngine.Object.Destroy(twin); // the tracer let go of them (TGPT_ClearScene)
            twins.Clear(); twinBytes = 0;
            missing.Clear(); numbers.Clear(); handles.Clear(); reasons.Clear(); refusedFormats.Clear();
            watch.Clear(); watched.Clear(); pending.Clear(); watchNext = 0; kept.Clear(); nextBudgetRetry = 0;
            opened = 0; bytes = 0; logged = 0; nextReport = 0; swapped = 0; retried = 0; small = 0; smallBytes = 0;
            stale.Clear(); auditFollowed = 0; auditMode = 0; nextAuditLine = 0; auditFailed = false; // PT-95 on trial
            WatchedFull = 0; WatchedBelow = 0; WatchedLoading = 0; Looks = 0; LookMs = 0;
        }

        private static string Reason(int code)
        {
            switch (code)
            {
                case Tgpt.ShareFlags: return "not made only for shaders to read";
                case Tgpt.ShareShape: return "arrays or cube maps";
                case Tgpt.ShareRefused: return "refused by the driver";
                case Tgpt.ShareBeforeHook: return "made before the hook";
                case Tgpt.ShareOpenFailed: return "D3D12 could not open them";
                case Tgpt.ShareFull: return "no number left";
                case Tgpt.ShareSelfTest: return "in a format the start-up self-test failed on this card (path tracing waits for them)";
                case Tgpt.ShareBudget: return "kept back near this card's video memory budget (path tracing waits; asked for again every 10 s)";
                case Tgpt.ShareUnsupported: return "path tracing unsupported";
                case Tgpt.ShareNotTexture: return "not 2D textures";
                default: return "refused (" + code + ")";
            }
        }
        private static void Log(string when)
        {
            logged = numbers.Count; opened = 0; reasons.Clear(); bytes = 0;
            foreach (var pair in numbers) { if (pair.Value >= 0) ++opened; else reasons[pair.Value] = (reasons.TryGetValue(pair.Value, out int n) ? n : 0) + 1; }
            bytes = totals.OpenBytes;
            var why = new List<string>();
            foreach (var pair in reasons)
            {
                string text = pair.Value + " " + Reason(pair.Key);
                if (pair.Key == Tgpt.ShareRefused)
                {
                    var formats = new List<string>();
                    foreach (var f in refusedFormats) formats.Add("format " + f.Key + " x" + f.Value);
                    text += " (" + string.Join(", ", formats.ToArray()) + ")";
                }
                why.Add(text);
            }
            string hook = totals.Hook == 1 ? "in" : totals.Hook == 2 ? "could not be installed" : totals.Hook == 3 ? "waiting for its start-up self-test" :
                totals.Hook == 4 ? "out (its start-up self-test passed no format on this card: path tracing waits for readable textures)" : "not in";
            // PT-45 phase 3: the start-up self-test's result (TGPathTracing.Native.log names each format).
            string selfTest = totals.SelfTest == 0 ? "" : "; its start-up self-test passed " + (totals.SelfTest & 0xFF) + " of " + ((totals.SelfTest >> 8) & 0xFF) +
                " formats in " + (totals.SelfTest >> 16) + " ms";
            Session.Log.LogInfo("Shared textures (PT-45)" + when + ": " + opened + " of the traced scene's " + numbers.Count + " textures opened by the tracer with no copy, " +
                (bytes / 1048576.0).ToString("0") + " MB" + (small > 0 ? " (" + small + " of them of 64 KB or less, which took atlas tiles until PT-64: " +
                    (smallBytes / 1048576.0).ToString("0.0") + " MB)" : "") + (why.Count > 0 ? "; the others: " + string.Join(", ", why.ToArray()) : "") +
                "; " + (reasons.TryGetValue(Tgpt.ShareBudget, out int keptCount) ? keptCount : 0) + " kept back" +
                (swapped > 0 ? "; " + swapped + " swapped for a new texture by mip streaming" : "") +
                (retried > 0 ? "; " + retried + " opened later, once there was video memory for them" : "") + ". The tracer holds " + totals.Open + " (" + (totals.OpenBytes / 1048576.0).ToString("0") + " MB); " + totals.Opened + " opened and " + totals.Freed +
                " freed by the game since the start; the hook is " + hook + selfTest + ".");
        }
    }
}
