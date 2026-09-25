// SPDX-License-Identifier: MIT
using System.Collections.Generic;
namespace TGGraphicsBridge
{
    // Frame generation and upscaling both read the game's own motion vectors, and both need the two
    // corrections this plugin makes to them (FirstPersonMotion, KandraMotionHistory). Each mod asks for
    // them while it uses the vectors; with no request left the game renders exactly as shipped.
    public static class MotionVectors
    {
        private static readonly HashSet<string> requests = new HashSet<string>();
        public static void Request(string owner, bool wanted)
        {
            if (wanted) requests.Add(owner); else requests.Remove(owner);
        }
        public static bool Requested => requests.Count > 0;
        internal static string Owners => requests.Count > 0 ? string.Join(", ", new List<string>(requests).ToArray()) : "none";
    }
}
