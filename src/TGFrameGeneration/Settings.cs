// SPDX-License-Identifier: MIT
using System.IO;
using BepInEx;
using BepInEx.Configuration;
using UnityEngine;
namespace TGFrameGeneration
{
    // Native request mode. Kept for status display and the ABI mapping.
    public enum GenerationMode { Off = 0, Fixed = 1, Dynamic = 2 }
    public enum FrameGenerationSetting { Off = 0, DLSS = 1 }
    public enum FrameGenerationMode { Fixed = 0, Dynamic = 1 }
    public enum ReflexSetting { Off = 0, On = 1, OnPlusBoost = 2 }
    internal sealed class Settings
    {
        internal readonly ConfigFile File;
        // Video-tab settings live in their own file under BepInEx/TGFrameGeneration. The game's
        // Video tab is their editor (native rows with the game's Apply/Undo). Keeping them out of
        // the plugin's main ConfigFile also keeps generic settings managers, which stage and
        // re-apply every plugin config entry, from overwriting what the Video tab saved.
        internal readonly ConfigFile VideoFile;
        internal readonly ConfigEntry<FrameGenerationSetting> FrameGeneration;
        internal readonly ConfigEntry<FrameGenerationMode> Mode;
        internal readonly ConfigEntry<int> Multiplier, MaxMultiplier;
        internal readonly ConfigEntry<float> TargetFps, BaseLimit;
        internal readonly ConfigEntry<ReflexSetting> Reflex;
        internal readonly ConfigEntry<bool> NativeMenu, SuspendOnCursor, RequireCamera;
        internal readonly ConfigEntry<KeyCode> EmergencyKey;
        internal readonly ConfigEntry<bool> DumpNow;
        internal static string VideoFilePath => Path.Combine(Paths.BepInExRootPath, "TGFrameGeneration", "VideoSettings.cfg");
        internal Settings(ConfigFile file)
        {
            File = file;
            VideoFile = new ConfigFile(VideoFilePath, true) { SaveOnConfigSet = false };
            FrameGeneration = VideoFile.Bind("Video", "Frame generation", FrameGenerationSetting.Off,
                "Off or DLSS. Edited in the game's Video settings; applied by the game's Apply Changes. Requires the TGFG.Native.dll Streamline backend and a Frame Generation capable GPU.");
            Mode = VideoFile.Bind("Video", "Frame generation mode", FrameGenerationMode.Fixed,
                "Fixed multiplier or NVIDIA Dynamic Multi Frame Generation. Dynamic requires driver/hardware support reported by the runtime.");
            Multiplier = VideoFile.Bind("Video", "Frame multiplier", 2,
                new ConfigDescription("Total output multiplier in Fixed mode; 2x is one generated frame per rendered frame. Capped by the runtime. Ignored in Dynamic mode.", new AcceptableValueRange<int>(2, 6)));
            MaxMultiplier = VideoFile.Bind("Video", "Dynamic max multiplier", 6,
                new ConfigDescription("Upper bound for Dynamic mode ('Up to Nx'). Sent to the runtime as numFramesToGenerate; Streamline 2.14.1 documents it as ignored in Dynamic mode, so it only takes effect if the installed runtime honours it.", new AcceptableValueRange<int>(2, 6)));
            TargetFps = VideoFile.Bind("Video", "Target framerate", 0f,
                new ConfigDescription("Dynamic mode output target. 0 = follow the display refresh rate (and VSync when enabled).", new AcceptableValueRange<float>(0, 1000)));
            Reflex = VideoFile.Bind("Video", "NVIDIA Reflex", ReflexSetting.On,
                "Off, On (low latency) or On + Boost. Forced to at least On while frame generation is active, as DLSS Frame Generation requires it.");
            BaseLimit = VideoFile.Bind("Advanced", "Reflex base FPS limit", 0f,
                new ConfigDescription("Optional rendered-frame cap through the Reflex limiter, not Unity targetFrameRate. 0 = no cap. Not exposed in the Video tab.", new AcceptableValueRange<float>(0, 1000)));
            VideoFile.Save();
            NativeMenu = file.Bind("02 Menu", "Add rows to Video settings", true,
                "Adds Frame Generation and NVIDIA Reflex rows to the game's own Video settings tab, styled and applied like the built-in options.");
            SuspendOnCursor = file.Bind("03 Safety", "Suspend when cursor unlocked", true,
                "Conservative menu/loading heuristic. May require adjustment for a game build that leaves the cursor unlocked during gameplay.");
            RequireCamera = file.Bind("03 Safety", "Require active main camera", true,
                "Suspend when Camera.main is absent, disabled, or inactive. This safety toggle does not change the render adapter: the current adapter still selects Camera.main.");
            EmergencyKey = file.Bind("03 Safety", "Emergency off key", KeyCode.F8,
                "Sets Frame generation=Off; does not unload any graphics injector. Use None to disable. Does not change ReShade hotkeys.");
            DumpNow = file.Bind("04 Diagnostics", "Write diagnostic report now", false,
                "Set true to write BepInEx/TGFrameGeneration/diagnostics/*.txt, then automatically reset false. Review before sharing.");
        }
        internal GenerationMode RequestedMode =>
            FrameGeneration.Value == FrameGenerationSetting.Off ? GenerationMode.Off :
            Mode.Value == FrameGenerationMode.Dynamic ? GenerationMode.Dynamic : GenerationMode.Fixed;
        internal void SetFrameGenerationOff()
        {
            if (FrameGeneration.Value == FrameGenerationSetting.Off) return;
            FrameGeneration.Value = FrameGenerationSetting.Off;
            VideoFile.Save();
        }
    }
}
