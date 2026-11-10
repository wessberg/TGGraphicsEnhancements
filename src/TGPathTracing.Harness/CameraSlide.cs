// SPDX-License-Identifier: MIT PT-53's yardstick: "When the player character moves, the hair on NPC peoples
// heads move too... especially noticeable, when the player moves to the sides via A and D on their
// keyboard". In a frozen world the camera swings left and right as a player strafing does, while the step's
// frames are taken: anything on a head that moves in them moved because the camera did, not by the wind or
// an animation. The game's own picture is the reference.
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;
namespace TGPathTracing.Harness
{
    internal static class CameraSlide
    {
        private static Camera held;
        private static Vector3 home, right, forwardDirection, originalHome;
        private static float originalFov, requestedFov;
        private static Quaternion rotation;
        private static float amplitude, period, startedAt;
        private static float turn, walkDistance, walkSpeed, walkStartedAt;
        // A walk at a speed keeps the camera at its height OVER THE GROUND (2026-10-12). Until then it flew level along
        // its view's direction, through whatever stood there: the forest's walk ended 25 m inside a hill, where the
        // camera saw the sea's sheet under the land, and "a camp at 3 frames a second" (PT-131) was that place.
        private static float eyeHeight = float.NaN, walkY, walkedSoFar;
        private static readonly RaycastHit[] groundHits = new RaycastHit[64];
        internal static float SampleTime { get; private set; }
        private static int cycleFrames, cycleStart, holdFrames;
        private static bool hooked;

        internal static bool Sliding => hooked;

        // The camera swings `metres` to either side along its own right, once every `seconds`, from now until Stop.
        internal static string Start(float metres, float seconds, float degrees = 0f, float forward = 0f, float fov = 0f, float walk = 0f, float speed = 0f)
        {
            Stop();
            var camera = Camera.main;
            if (camera == null) return "no camera to slide";
            held = camera; originalHome = camera.transform.position;
            home = originalHome + camera.transform.forward * forward;
            originalFov = camera.fieldOfView; requestedFov = fov > 0f ? Mathf.Clamp(fov, 10f, 120f) : originalFov;
            rotation = camera.transform.rotation; turn = degrees; cycleFrames = holdFrames = 0;
            right = Vector3.ProjectOnPlane(camera.transform.right, Vector3.up).normalized;
            forwardDirection = Vector3.ProjectOnPlane(camera.transform.forward, Vector3.up).normalized;
            walkDistance = walk; walkSpeed = Mathf.Max(0f, speed); walkStartedAt = -1f;
            eyeHeight = float.NaN; walkY = home.y; walkedSoFar = 0f;
            string ground = "";
            if (walk != 0f && walkSpeed > 0f)
            {
                float under;
                if (GroundUnder(home, home.y + 0.3f, home.y, out under) && home.y - under > 0.2f && home.y - under < 6f)
                { eyeHeight = home.y - under; ground = ", the camera kept " + eyeHeight.ToString("0.00") + " m over the ground"; }
                else ground = ", LEVEL (no ground found under the camera: it may fly into a hill)";
            }
            amplitude = metres; period = Mathf.Max(seconds, 0.2f); startedAt = Time.realtimeSinceStartup;
            RenderPipelineManager.beginContextRendering += BeforeRendering;
            hooked = true;
            return "the camera advances " + forward.ToString("0.00") + " m at FOV " + requestedFov.ToString("0.0") +
                ", swings " + metres.ToString("0.00") + " m and turns " + degrees.ToString("0.0") + " degrees to either side" +
                (walk != 0f ? ", then walks forward " + walk.ToString("0.00") + " m" + (walkSpeed > 0f ? " at " + walkSpeed.ToString("0.00") + " m/s" : " during capture") + " and holds the endpoint" + ground : "");
        }

        // The ground under a point: the highest solid thing (no trigger, no character's capsule) at or below `from`,
        // looked for from `top` down. For the walk's start: what the camera stands over.
        private static bool GroundUnder(Vector3 at, float top, float from, out float height)
        {
            height = 0f;
            int n = Physics.RaycastNonAlloc(new Vector3(at.x, top, at.z), Vector3.down, groundHits, 120f, ~0, QueryTriggerInteraction.Ignore);
            bool found = false;
            for (int i = 0; i < n; i++)
            {
                var c = groundHits[i].collider;
                if (c == null || c is CharacterController || c is CapsuleCollider || c is SphereCollider) continue;
                float y = groundHits[i].point.y;
                if (y > from + 0.01f) continue;
                if (!found || y > height) { height = y; found = true; }
            }
            return found;
        }

        // The ground a walk goes on at a point: of every solid top there (looked for from 60 m over the feet down), the one
        // NEAREST the feet's height. So a rise or a rock wall in the way is climbed (its top is the only one near), a roof,
        // an arch or a canopy overhead is not taken for the ground (the floor under it is nearer), and past a cliff's edge
        // the camera goes down to what lies below. (The first try took the highest top at or below a step's reach: at a
        // rock wall higher than the reach that was the sea's bed under the hill, 31 m down: 2026-10-12 03:55.)
        private static bool GroundNear(Vector3 at, float feet, out float height)
        {
            height = 0f;
            int n = Physics.RaycastNonAlloc(new Vector3(at.x, feet + 60f, at.z), Vector3.down, groundHits, 240f, ~0, QueryTriggerInteraction.Ignore);
            bool found = false; float best = 0f;
            for (int i = 0; i < n; i++)
            {
                var c = groundHits[i].collider;
                if (c == null || c is CharacterController || c is CapsuleCollider || c is SphereCollider) continue;
                float y = groundHits[i].point.y, away = Mathf.Abs(y - feet);
                if (!found || away < best) { height = y; best = away; found = true; }
            }
            return found;
        }

        // NR comparisons need the same poses despite different frame rates. Start from the next rendered frame.
        internal static void BeginCycle(int frames, int hold = 0) { cycleFrames = frames; holdFrames = Mathf.Max(0, hold); cycleStart = Time.frameCount + 1; walkStartedAt = -1f; walkY = home.y; walkedSoFar = 0f; }

        // Each frame before HDRP culls (the game puts its camera where the hero looks every frame, as Mirror.Hold knows).
        private static void BeforeRendering(ScriptableRenderContext context, List<Camera> cameras)
        {
            if (held == null) return;
            SampleTime = Time.realtimeSinceStartup;
            int offset = cycleFrames > 0 ? MathOffset(Time.frameCount - cycleStart, cycleFrames + holdFrames) : 0;
            float phase = cycleFrames > 0 ? offset / (float)cycleFrames : (Time.realtimeSinceStartup - startedAt) / period;
            float wave = cycleFrames > 0 && offset >= cycleFrames ? 0f : Mathf.Sin(2f * Mathf.PI * phase);
            // A walk starts after the stationary settling frames, advances once, then holds its endpoint.
            // Unlike a looping swing it must not jump back to the start while NR history is being measured.
            float progress = cycleFrames > 0 ? Mathf.Clamp01((Time.frameCount - cycleStart) / (float)Mathf.Max(1, cycleFrames - 1)) : 0f;
            if (walkSpeed > 0f && walkDistance != 0f && cycleFrames > 0 && Time.frameCount >= cycleStart)
            {
                if (walkStartedAt < 0f) walkStartedAt = SampleTime;
                progress = Mathf.Clamp01((SampleTime - walkStartedAt) * walkSpeed / Mathf.Abs(walkDistance));
            }
            Vector3 position = home + right * (amplitude * wave) + forwardDirection * (walkDistance * progress);
            if (!float.IsNaN(eyeHeight))
            {
                // Where no ground is found (over a gap) it keeps its height.
                float under;
                if (GroundNear(position, walkY - eyeHeight, out under)) walkY = under + eyeHeight;
                walkedSoFar = Mathf.Abs(walkDistance) * progress;
                position.y = walkY;
            }
            held.transform.position = position;
            held.transform.rotation = rotation * Quaternion.Euler(0f, turn * wave, 0f);
            held.fieldOfView = requestedFov;
        }

        private static int MathOffset(int frame, int length) => (frame % length + length) % length;

        // Where the camera is now and where it started, for the progress log.
        internal static string Where()
        {
            if (held == null) return "no camera held";
            Vector3 p = held.transform.position;
            return "the walk's end: the camera at x " + p.x.ToString("0.0") + " y " + p.y.ToString("0.0") + " z " + p.z.ToString("0.0") +
                " (it started at x " + originalHome.x.ToString("0.0") + " y " + originalHome.y.ToString("0.0") + " z " + originalHome.z.ToString("0.0") +
                (float.IsNaN(eyeHeight) ? "; level" : "; kept " + eyeHeight.ToString("0.00") + " m over the ground") + "), walk-end.png";
        }

        internal static void Stop()
        {
            if (hooked) { RenderPipelineManager.beginContextRendering -= BeforeRendering; hooked = false; }
            if (held != null) { held.transform.position = originalHome; held.transform.rotation = rotation; held.fieldOfView = originalFov; }
            held = null;
        }
    }
}
