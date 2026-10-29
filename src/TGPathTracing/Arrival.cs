// SPDX-License-Identifier: MIT
// PT-98: how long the traced world takes to be whole, as a timeline the game writes itself (the owner, 2026-10-09: "I
// frequently see stuff pop in over time ... so I'm never sure when I can trust the visuals"; of a guard's textures and a
// town square filling in: "it took a LONG time before it kicked in").
// From the moment tracing can capture (it was switched on, its passes were made, or a scene was loaded with it on) one
// line a second says what is there and what is still to come, until the world is whole; then one line says how long it
// took and which queue emptied last; then nothing, until the next start or until things have been arriving again for
// StallSeconds (a walk into a new place). Counters only: what the scan, the registering and the texture watch count
// anyway, and the native side's status. Nothing by pixel, nothing asked of Unity that the scan does not ask already.
//
// "Whole" is: no mesh waits to be registered (the plugin's queue) or built (the native side's); every registered mesh
// has been through a scan (a scan lists a mesh's instances only once the mesh has a handle) and no listed instance
// waits for its mesh; no character is being read back; no texture is kept back; no tile waits to be baked; and the
// game is loading no sharper level of a texture a traced material reads (a load that shows no progress for
// LoadingStallSeconds is no longer waited for, and the line says so: the game's streaming must not hold the notice up
// for ever). What the last scan met without a mesh, found new and followed of texture swaps is in the lines and does
// not count by itself: a missing or new mesh is in the queue to register, a new material and a followed swap are done
// when the scan ends. Opacity micromaps still to make and plants still to be grouped for the wind are in the lines
// and do not count either: neither changes what a still picture shows (the alpha test serves until a micromap is
// there; a plant stands still until its group is made).
// Session's notice and the harness read the same state (ToCome, Whole, NoticeText).
using System.Globalization;
using UnityEngine;
namespace TGPathTracing
{
    internal static class Arrival
    {
        // Past this many seconds of lines one a second, a line every LateEvery seconds (a counter that never empties must
        // not fill the log); things arriving for this long after the world was whole start a timeline of their own.
        private const float LinesEverySecondFor = 60f, LateEvery = 10f, StallSeconds = 2f;
        // The game's own loading of sharper texture levels is waited for while its count changes; the same count for this
        // long is taken for a load that will not end (the game over its streaming budget), and the world is whole without it.
        private const float LoadingStallSeconds = 5f;
        private static int loadingSeen;
        private static float loadingSince;
        private static bool loadingStalled;

        // The state others read. ToCome: the things still to come (meshes to register, build and place, characters being
        // read, textures kept back, loading or to bake); 0 with Whole false while a listed instance still waits for its
        // mesh, or before the first scan.
        internal static bool Whole { get; private set; }
        internal static int ToCome { get; private set; }
        internal static bool Running => started >= 0f && !Whole;
        // Seconds the last arrival took (negative until one ended), and since the running one started.
        internal static float LastSeconds { get; private set; } = -1f;
        internal static float Seconds => started >= 0f ? Time.unscaledTime - started : 0f;
        // Meshes that got a handle since the last scan began: the next scan lists their instances (SceneCapture scans
        // sooner while there are any).
        internal static int ToPlace => Mathf.Max(0, registered - registeredSeenByScan);
        // The notice's words while the world is built: one line, a number that falls.
        internal static string NoticeText
        {
            get
            {
                int meshes = queue[ToRegister] + Mathf.Max(queue[ToBuild], queue[ToPlaceQ]), textures = queue[KeptBack] + queue[Loading] + queue[Tiles];
                if (meshes + textures + queue[Characters] <= 0) return "building the traced world: placing the last things";
                string text = "";
                if (meshes > 0) text = meshes + (meshes == 1 ? " mesh" : " meshes");
                if (textures > 0) text += (text.Length > 0 ? " and " : "") + textures + (textures == 1 ? " texture" : " textures");
                if (queue[Characters] > 0) text += (text.Length > 0 ? " and " : "") + queue[Characters] + (queue[Characters] == 1 ? " character" : " characters");
                return "building the traced world: " + text + " to go";
            }
        }

        private static float started = -1f, nextLine, stallSince = -1f;
        private static bool capturing;
        private static string reason = "tracing was switched on", pendingReason;
        private static int arrivals;

        // What the scan counts (SceneCapture): the times the running scan asked for a mesh that has no handle yet (a thing
        // the game draws that is not in the scan's list); the same of the last scan that ended, with what it found new and
        // what it cost.
        private static bool inScan;
        private static int scanNotYet, scans;
        private static int lastNotYet, lastNewMeshes, lastNewMaterials, lastSwaps, lastInstances;
        private static int queuedAtScanStart, materialsAtScanStart, swapsAtScanStart;
        private static double lastScanMs, slowestScanMs, scanMsTotal;
        private static float lastScanEnd;
        // Meshes registered in all, and how many of them the last ended scan could know of (registered before it began).
        private static int registered, registeredAtScanStart, registeredSeenByScan;
        // This second's sums, for its line.
        private static int secondFrames, secondRegistered, secondScans, looksAtSecondStart;
        private static double secondRegisterMs, secondWaitMs, secondScanMs, registerMsTotal, waitMsTotal, lookMsAtSecondStart, lookMsAtStart;
        private static float secondStart, secondLongest;

        // The queues, for "which emptied last": the second each was last seen with something in it. The first Gating of them
        // must be empty for the world to be whole; the rest are told and do not count (the file's head says why).
        private const int ToRegister = 0, ToBuild = 1, ToPlaceQ = 2, InstancesWaiting = 3, Characters = 4, KeptBack = 5, Loading = 6, Tiles = 7, Gating = 8,
            NotYet = 8, NewFound = 9, Swaps = 10, Micromaps = 11, Plants = 12, Count_ = 13;
        private static readonly string[] QueueNames =
        {
            "meshes to register", "meshes to build", "registered meshes a scan has yet to place", "listed instances whose mesh is not built",
            "characters being read", "textures kept back", "textures the game is loading", "tiles to bake",
            "things the last scan met whose mesh was missing", "new meshes or materials found by the last scan", "texture swaps followed by the last scan",
            "opacity micromaps to make (speed only)", "plants to group for the wind (they stand still until then)"
        };
        private static readonly int[] queue = new int[Count_];
        private static readonly float[] lastBusy = new float[Count_];

        // ------------------------------------------------------------------ what the scan and the registering report
        internal static void SceneCleared(string why = "a scene was unloaded and the traced scene started over")
        {
            if (capturing && started >= 0f && !Whole)
                Session.Log.LogInfo("Arrival stopped at " + S(Time.unscaledTime - started) + " s, not whole: the traced scene started over.");
            pendingReason = why; capturing = false; started = -1f; Whole = false; ToCome = 0;
            inScan = false; registered = registeredAtScanStart = registeredSeenByScan = 0;
            lastNotYet = lastNewMeshes = lastNewMaterials = lastSwaps = lastInstances = 0; lastScanMs = 0;
        }
        internal static void ScanBegins(int queuedMeshes, int materials)
        {
            inScan = true; scanNotYet = 0;
            registeredAtScanStart = registered; queuedAtScanStart = queuedMeshes; materialsAtScanStart = materials; swapsAtScanStart = TextureShare.Swaps;
        }
        // A scan asked for a mesh's handle; notYet: it has none yet (queued, the thing is not in this scan's list).
        internal static void MeshAsked(bool notYet) { if (inScan && notYet) ++scanNotYet; }
        // queuedMeshes against the count at the scan's start: the meshes the scan found new (the queue only grows in a scan).
        internal static void ScanEnds(double ms, int instances, int queuedMeshes, int materials)
        {
            if (!inScan) return;
            inScan = false; ++scans; ++secondScans;
            lastNotYet = scanNotYet; lastInstances = instances;
            lastNewMeshes = Mathf.Max(0, queuedMeshes - queuedAtScanStart); lastNewMaterials = Mathf.Max(0, materials - materialsAtScanStart);
            lastSwaps = Mathf.Max(0, TextureShare.Swaps - swapsAtScanStart);
            registeredSeenByScan = registeredAtScanStart;
            lastScanMs = ms; scanMsTotal += ms; secondScanMs += ms; if (ms > slowestScanMs) slowestScanMs = ms;
            lastScanEnd = Time.unscaledTime;
        }
        // A frame's registering: how many meshes, the time it took, and of that the first call's (which waits for Unity's
        // render thread to finish the frame it is drawing: at a low frame rate most of a frame).
        internal static void Registered(int count, double ms, double firstMs)
        {
            registered += count; secondRegistered += count; secondRegisterMs += ms; secondWaitMs += firstMs; registerMsTotal += ms; waitMsTotal += firstMs;
        }

        // ------------------------------------------------------------------ every frame (Session.Tick)
        // Before the frame's registering and scan: the first frame that captures starts the timeline, so its scan is counted.
        internal static void Begin(float now)
        {
            if (capturing) return;
            capturing = true; Count(now); Start(now, pendingReason ?? "tracing was switched on"); pendingReason = null;
        }
        // After them (canCapture false: nothing was captured this frame: tracing is off, its passes are being made, a load).
        internal static void Tick(float now, bool canCapture)
        {
            if (!canCapture)
            {
                if (capturing && started >= 0f && !Whole)
                    Session.Log.LogInfo("Arrival stopped at " + S(now - started) + " s, not whole: tracing no longer captures (switched off, a load, or paused).");
                capturing = false; started = -1f; Whole = false; ToCome = 0; stallSince = -1f;
                return;
            }
            Begin(now);
            Count(now);
            ++secondFrames; secondLongest = Mathf.Max(secondLongest, Time.unscaledDeltaTime);
            if (Whole)
            {
                // Whole, and things arrive again (the player walked on, the game loaded something): said once that has
                // lasted StallSeconds, as a timeline of its own.
                if (ToCome <= 0 && !Owed()) { stallSince = -1f; return; }
                if (stallSince < 0f) { stallSince = now; return; }
                if (now - stallSince < StallSeconds) return;
                Start(stallSince, "things arrived again after the world was whole");
            }
            if (!Owed() && ToCome <= 0 && scans > 0) { Finish(now); return; }
            if (now < nextLine) return;
            float seconds = now - started;
            nextLine = now + (seconds < LinesEverySecondFor ? 1f : LateEvery);
            Session.Log.LogInfo("Arrival " + S(seconds) + " s | " + Line(now));
            NewSecond(now);
        }

        private static void NewSecond(float now)
        {
            secondFrames = 0; secondRegistered = 0; secondScans = 0; secondRegisterMs = 0; secondWaitMs = 0; secondScanMs = 0; secondLongest = 0f; secondStart = now;
            looksAtSecondStart = TextureShare.Looks; lookMsAtSecondStart = TextureShare.LookMs;
        }
        private static void Start(float now, string why)
        {
            started = now; Whole = false; reason = why; ++arrivals; stallSince = -1f;
            loadingSince = now; loadingStalled = false; // a new arrival waits for the game's loading afresh
            nextLine = now + 1f; NewSecond(now);
            scans = 0; slowestScanMs = 0; scanMsTotal = 0; registerMsTotal = 0; waitMsTotal = 0; lookMsAtStart = TextureShare.LookMs;
            for (int i = 0; i < lastBusy.Length; ++i) lastBusy[i] = -1f;
            Session.Log.LogInfo("Arrival starts (number " + arrivals + " of this session: " + why + "): a line a second until the traced world is whole. " + Line(now));
        }

        // The queues now. NotYet, NewFound and Swaps are of the last scan that ended (told, not counted).
        private static void Count(float now)
        {
            var s = Session.Status;
            bool have = Session.HaveStatus;
            queue[ToRegister] = SceneCapture.Waiting;
            queue[ToBuild] = have ? (int)s.MeshesQueued : 0;
            queue[ToPlaceQ] = ToPlace;
            queue[InstancesWaiting] = have ? (int)s.InstancesWaiting : 0;
            queue[NotYet] = lastNotYet;
            queue[NewFound] = lastNewMeshes + lastNewMaterials;
            queue[Characters] = CharacterCapture.Reading;
            queue[KeptBack] = TextureShare.KeptBack;
            queue[Loading] = TextureShare.WatchedLoading;
            queue[Tiles] = have ? (int)s.TilesToBake : 0;
            queue[Swaps] = lastSwaps;
            queue[Micromaps] = have ? (int)s.MicromapsWaiting : 0;
            queue[Plants] = have ? (int)s.PlantsWaiting : 0;
            // The game's loading: waited for while its count moves (LoadingStallSeconds).
            if (queue[Loading] != loadingSeen || queue[Loading] == 0) { loadingSeen = queue[Loading]; loadingSince = now; }
            loadingStalled = queue[Loading] > 0 && now - loadingSince >= LoadingStallSeconds;
            if (loadingStalled) queue[Loading] = 0;
            if (started >= 0f) for (int i = 0; i < queue.Length; ++i) if (queue[i] > 0) lastBusy[i] = now - started;
            // Things, each once: a mesh is to register, then to build and to place at once (the larger of those two).
            ToCome = queue[ToRegister] + Mathf.Max(queue[ToBuild], queue[ToPlaceQ]) + queue[Characters] + queue[KeptBack] + queue[Loading] + queue[Tiles];
        }
        // Nothing is counted as to come, and a listed instance still waits for its mesh (or a scan is running).
        private static bool Owed() => queue[InstancesWaiting] > 0 || inScan;

        private static void Finish(float now)
        {
            float seconds = now - started;
            Whole = true; LastSeconds = seconds; stallSince = -1f;
            int last = -1;
            string order = "", also = "";
            // The queues by when each was last busy, the last first.
            var done = new bool[Gating];
            for (int n = 0; n < Gating; ++n)
            {
                int best = -1;
                for (int i = 0; i < Gating; ++i) if (!done[i] && lastBusy[i] >= 0f && (best < 0 || lastBusy[i] > lastBusy[best])) best = i;
                if (best < 0) break;
                done[best] = true; if (last < 0) last = best;
                order += (order.Length > 0 ? "; " : "") + QueueNames[best] + " until " + S(lastBusy[best]) + " s";
            }
            for (int i = Gating; i < Count_; ++i)
                if (queue[i] > 0 || lastBusy[i] >= 0f) also += "; " + QueueNames[i] + ": " + (queue[i] > 0 ? queue[i] + " still" : "done by " + S(lastBusy[i]) + " s");
            Session.Log.LogInfo("Arrival whole after " + S(seconds) + " s (" + reason + "). The last to empty: " + (last >= 0 ? QueueNames[last] : "nothing was waiting") +
                (order.Length > 0 ? ". Each queue was last busy: " + order : "") + (also.Length > 0 ? ". Not counted" + also : "") + ". " + scans + " scans took " + Ms(scanMsTotal) +
                " ms in all, the slowest " + Ms(slowestScanMs) + " ms; registering meshes took " + Ms(registerMsTotal) + " ms, of which " + Ms(waitMsTotal) +
                " ms waiting for Unity's render thread; the looks at the textures took " + Ms(TextureShare.LookMs - lookMsAtStart) + " ms. Now: " + Line(now));
        }

        // What is there and what is to come, in the timeline's and the summary's words.
        private static string Line(float now)
        {
            var s = Session.Status;
            bool have = Session.HaveStatus;
            bool counted = Session.Tracer != null && Session.Tracer.ArrivalCounted;
            float span = Mathf.Max(0.001f, now - secondStart);
            string frame = secondFrames > 0 ? Ms(1000.0 * span / secondFrames) + " ms a frame (" + secondFrames + " frames in " + S(span) + " s, the longest " + Ms(1000.0 * secondLongest) + " ms)" : "no frame yet";
            return "instances: the scan listed " + lastInstances + ", met " + lastNotYet + " more whose mesh has no handle yet; the tracer traces " +
                    (have ? s.InstancesTraced + " of " + s.Instances + (counted ? ", " + s.InstancesWaiting + " wait for their mesh" : "") : "n/a") +
                " | meshes: " + SceneCapture.MeshesKnown + " seen, " + registered + " registered (+" + secondRegistered + " in " + Ms(secondRegisterMs) + " ms, " + Ms(secondWaitMs) +
                    " ms of it the frame's first call, which waits for the render thread), " + queue[ToRegister] + " to register, " + queue[ToPlaceQ] + " to place, " +
                    (have ? s.MeshesReady + " built, " + s.MeshesQueued + " building, " + s.MeshesFailed + " failed" : "n/a") +
                " | textures: " + TextureShare.Wanted + " wanted, " + TextureShare.Open + " open, " + queue[KeptBack] + " kept back; of " + TextureShare.WatchedCount + " watched " +
                    TextureShare.WatchedFull + " at full size, " + TextureShare.WatchedBelow + " below what the game wants, " + TextureShare.WatchedLoading + " loading" +
                    (loadingStalled ? " (the same count for " + S(LoadingStallSeconds) + " s: not waited for)" : "") + "; " + TextureShare.Swaps + " swaps followed (+" + lastSwaps +
                    " by the last scan); " + (counted ? s.TilesToBake + " tiles to bake; " : "tiles to bake not counted by this native side; ") + (TextureShare.Looks - looksAtSecondStart) + " looks in " +
                    Ms(TextureShare.LookMs - lookMsAtSecondStart) + " ms; the game's streaming: " + Streaming() +
                " | materials " + SceneCapture.MaterialsKnown + " | characters: " + CharacterCapture.Traced + " traced, " + queue[Characters] + " being read" +
                (have && (s.Features & Tgpt.FeatureDxr12) != 0 ? " | micromaps: " + s.MicromapMeshes + " meshes, " + s.MicromapsWaiting + " to make" : "") +
                (counted ? " | plants to group: " + s.PlantsWaiting : "") +
                " | scans: " + scans + " (+" + secondScans + ", " + Ms(secondScanMs) + " ms; the last " + Ms(lastScanMs) + " ms, " + (scans > 0 ? S(now - lastScanEnd) + " s ago" : "none yet") +
                    ", found " + lastNewMeshes + " new meshes, " + lastNewMaterials + " new materials)" +
                " | " + frame + (have ? ", tracer GPU " + Ms(s.GpuMs) + " ms, render thread " + Ms(s.FrameMs) + " ms" : "") +
                " | to come " + ToCome + (queue[InstancesWaiting] > 0 ? " (and " + queue[InstancesWaiting] + " listed instances wait for their mesh)" : "");
        }
        private static string Streaming()
        {
            try { return Texture.streamingTextureLoadingCount + " loading, " + Texture.streamingTexturePendingLoadCount + " waiting"; }
            catch (System.Exception) { return "not read"; }
        }
        private static string S(float seconds) => seconds.ToString("0.0", CultureInfo.InvariantCulture);
        private static string Ms(double ms) => ms.ToString("0.0", CultureInfo.InvariantCulture);
    }
}
