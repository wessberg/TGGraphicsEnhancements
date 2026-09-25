// SPDX-License-Identifier: MIT
// What frame generation runs: the player's settings, or while the troubleshooting test runs, one of its
// steps. A step never touches the settings, so the config file, the Video rows and FoA Mod Manager keep
// showing the player's choices, and a game that closes during the test keeps them too.
namespace TGFrameGeneration
{
    internal struct Selection
    {
        internal FrameGenerationSetting Backend;
        internal GenerationMode Mode; // the native request's: Off, Fixed or Dynamic
        internal int Multiplier, MaxMultiplier;
        internal float TargetFps;
        internal LowLatencySetting Latency;
        internal static Selection From(Settings s) => new Selection
        {
            Backend = s.FrameGeneration, Mode = s.RequestedMode, Multiplier = s.Multiplier, MaxMultiplier = s.MaxMultiplier,
            TargetFps = s.TargetFps, Latency = s.LowLatency
        };
        internal bool Generating => Backend != FrameGenerationSetting.Off && Mode != GenerationMode.Off;
        internal bool SameAs(Selection other) => Backend == other.Backend && Mode == other.Mode && Multiplier == other.Multiplier &&
            MaxMultiplier == other.MaxMultiplier && TargetFps == other.TargetFps && Latency == other.Latency;
        internal string Describe() =>
            (Generating ? VideoCapabilities.Label(Backend) + " " + (Mode == GenerationMode.Dynamic ? "Dynamic up to " + MaxMultiplier + "x" : Multiplier + "x") : "Frame generation off") +
            ", low latency " + VideoCapabilities.Label(Latency);
    }
}
