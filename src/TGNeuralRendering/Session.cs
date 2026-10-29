// SPDX-License-Identifier: MIT
using System;
using BepInEx.Configuration;
using BepInEx.Logging;
using TGGraphicsBridge;
using UnityEngine;
namespace TGNeuralRendering
{
    internal static class Session
    {
        internal static ManualLogSource Log;
        internal static NativeNeural Native;
        internal static ConfigEntry<string> Mode;
        internal static ConfigEntry<float> Strength, LocalTone, LocalStructure, SkinStructure;
        internal static ConfigEntry<string> Model, AutoMask, Placement, Advanced;
        internal static ConfigEntry<int> Passes;
        internal static readonly string[] PassLabels = { "1", "2", "3", "4" };
        private static ConfigEntry<string> wantedPlacement;
        internal static bool BeforeUpscaling => Placement?.Value == "Before Upscaling" && SceneCapture.BeforeAvailable;
        internal static readonly string[] Placements = { "After Upscaling", "Before Upscaling" };
        internal static readonly string[] PlacementLabels = { "Quality", "Performance" };
        private const string PlacementDescription = "Quality runs after DLSS upscaling to preserve more detail. Performance runs before DLSS upscaling, processing fewer pixels for lower GPU cost, but can soften detail and cause larger colour and brightness shifts after movement stops.";
        private static ConfigurationManagerAttributes placementTag;
        private static bool? placementAvailable;
        // Persisted Model A/B/C meant native style 0/1/2. Keep those stored values so
        // correcting the UI labels does not silently change an existing player's picture.
        internal static readonly string[] Models = { "A", "B", "C", "D" };
        internal static readonly string[] StyleLabels = { "Default", "A", "B", "C" };
        private static readonly System.Collections.Generic.List<ConfigurationManagerAttributes> childTags = new System.Collections.Generic.List<ConfigurationManagerAttributes>();
        private static readonly System.Collections.Generic.List<ConfigurationManagerAttributes> advancedTags = new System.Collections.Generic.List<ConfigurationManagerAttributes>();
        private static ConfigEntry<string> wantedMode;
        internal static string Reason => Fault ?? (!DlssSelected ? "Select DLSS in the Upscaling setting to use DLSS Neural Rendering." : HdrActive ? "DLSS Neural Rendering currently requires SDR output." : SceneCapture.BlockReason ?? RuntimeReason);
        private static string RuntimeReason
        {
            get
            {
                var install = InstallCheck.ProblemFor("TGNeuralRendering");
                if (!string.IsNullOrEmpty(install)) return install;
                if (!GraphicsBridge.Loaded) return GraphicsBridge.Detail;
                return Native?.Detail ?? "Waiting for the graphics bridge.";
            }
        }
        internal static bool DlssSelected => VideoTab.UpscalingRow()?.OptionInt == 2;
        internal static bool Available => Fault == null && DlssSelected && !HdrActive && SceneCapture.BlockReason == null && Native != null && Native.Status.State == 4;
        internal static bool Enabled => Available && Mode?.Value == "On";
        internal static bool HdrActive => HDROutputSettings.main != null && HDROutputSettings.main.active;
        internal static string Fault;
        internal static bool Changed;
        private static ConfigurationManagerAttributes strengthTag;
        private static ConfigurationManagerAttributes skinTag;
        private static ConfigurationManagerAttributes modeTag;
        private static ConfigFile config;
        private static bool rebinding;
        private static string choicesReason;
        private static float nextPoll;
        private static bool started;
        internal static void Start(ConfigFile config, ManualLogSource log)
        {
            Session.config = config;
            Log = log; Native = new NativeNeural();
            modeTag = Tag("Neural Rendering", 0);
            // Preserve existing saved On values; FoA shows the same technology label as Video.
            modeTag.ChoiceLabels = "Off=Off;On=DLSS";
            strengthTag = ChildTag("Strength", 2);
            Mode = config.Bind("Video", "Neural Rendering", "Off", new ConfigDescription("Enhance scene lighting and materials with DLSS Neural Rendering.", new AcceptableValueList<string>("Off", "On"), modeTag));
            wantedMode = config.Bind("Internal", "Preferred neural rendering", Mode.Value, new ConfigDescription("Remembers the choice while Neural Rendering is temporarily unavailable.", new AcceptableValueList<string>("Off", "On"), new ConfigurationManagerAttributes { Visible = false }));
            // Settled native style 0, formerly labelled A. Its corrected label is Default.
            Strength = config.Bind("Video", "Strength", 1f, new ConfigDescription("Control the lighting, colour and detail changes in each pass. 0 preserves the original scene. 1 gives the full effect in every pass.", new AcceptableValueRange<float>(0f, 1f), strengthTag));
            var styleTag = AdvancedTag("Style", 5);
            styleTag.ChoiceLabels = "A=Default;B=A;C=B;D=C";
            Model = config.Bind("Video", "Model", "A", new ConfigDescription("Choose a Neural Rendering style. Default selects the NVIDIA Default style. Available looks depend on the installed DLSS version.", new AcceptableValueList<string>(Models), styleTag));
            placementTag = ChildTag("Placement", 1);
            placementTag.ChoiceLabels = "After Upscaling=Quality;Before Upscaling=Performance";
            Placement = config.Bind("Video", "Placement", Placements[0], new ConfigDescription(PlacementDescription, new AcceptableValueList<string>(Placements), placementTag));
            wantedPlacement = config.Bind("Internal", "Preferred placement", Placement.Value, new ConfigDescription("Remembers the placement while Ray Reconstruction requires After Upscaling.", new AcceptableValueList<string>(Placements), new ConfigurationManagerAttributes { Visible = false }));
            LocalTone = Float("Local Tone", "Lower this to keep lighting and colour closer to the original while retaining detail enhancement. 1 gives the full tone changes.", 4, true);
            Advanced = config.Bind("Video", "Advanced", "Off", new ConfigDescription("Show Local Tone, styles, repeated passes and fine detail controls. Hiding them keeps your chosen values.", new AcceptableValueList<string>("Off", "On"), ChildTag("Advanced", 3)));
            Passes = config.Bind("Video", "Passes", 1, new ConfigDescription("Run Neural Rendering again on each previous result, using the same Strength and detail settings each time. Extra passes cost more GPU time and memory and can exaggerate colour changes or artifacts.", new AcceptableValueList<int>(1, 2, 3, 4), AdvancedTag("Passes", 6)));
            LocalStructure = Float("Detail", "How strongly Neural Rendering changes fine surface detail, independently of Local Tone.", 7, true);
            skinTag = AdvancedTag("Skin Detail", 9);
            SkinStructure = config.Bind("Video", "Skin Detail", 1f, new ConfigDescription("How strongly Neural Rendering changes skin detail.", new AcceptableValueRange<float>(0f, 1f), skinTag));
            AutoMask = config.Bind("Video", "Automatic Mask", "Off", new ConfigDescription("Let the model identify skin and other surfaces for its detail controls.", new AcceptableValueList<string>("Off", "On"), AdvancedTag("Automatic Mask", 8)));
            Mode.SettingChanged += ModeChange; Strength.SettingChanged += IntensityChange;
            Model.SettingChanged += Change; LocalTone.SettingChanged += Change;
            LocalStructure.SettingChanged += Change; SkinStructure.SettingChanged += Change; AutoMask.SettingChanged += Change;
            Placement.SettingChanged += PlacementChange;
            Advanced.SettingChanged += IntensityChange; // Menu visibility only; preserve NR history and all tuning.
            Passes.SettingChanged += Change;
            try { SceneCapture.Start(); }
            catch (Exception ex) { Fault = "Neural Rendering could not install its scene pass: " + ex.Message; Log.LogError(ex); }
            started = true; Changed = true;
            RowAlerts.Claim(AlertRow.NeuralRendering);
            Log.LogInfo("Scene pass: immediately before/after upscaling, before depth of field, motion blur, chromatic aberration, vignette and tone mapping. HDR colour adapter. Starts Off. Defaults: NVIDIA style 0 (Default), full tone and detail, automatic mask Off.");
        }
        private static ConfigurationManagerAttributes Tag(string name, int order) => new ConfigurationManagerAttributes {
            DisplayName = name, DispName = name, DisplaySection = "Video", Category = "Video", SortOrder = order, Visible = true };
        private static ConfigurationManagerAttributes ChildTag(string name, int order)
        {
            var tag = Tag(name, order); tag.Visible = false; childTags.Add(tag); return tag;
        }
        private static ConfigurationManagerAttributes AdvancedTag(string name, int order)
        {
            var tag = ChildTag(name, order); advancedTags.Add(tag); return tag;
        }
        private static ConfigEntry<float> Float(string name, string description, int order, bool advanced = false) => config.Bind("Video", name, 1f,
            new ConfigDescription(description, new AcceptableValueRange<float>(0f, 1f), advanced ? AdvancedTag(name, order) : ChildTag(name, order)));
        // NGX intensity applies within each pass and can change without destroying its history.
        private static void IntensityChange(object sender, EventArgs e) { if (!rebinding) Changed = true; }
        private static void Change(object sender, EventArgs e)
        {
            if (rebinding) return;
            Changed = true; SceneCapture.Reset();
        }
        private static void ModeChange(object sender, EventArgs e)
        {
            if (!rebinding) wantedMode.Value = Mode.Value;
            Change(sender, e);
        }
        private static void PlacementChange(object sender, EventArgs e)
        {
            if (!rebinding) wantedPlacement.Value = Placement.Value;
            Change(sender, e);
        }
        private static void RefreshPlacement()
        {
            bool available = SceneCapture.BeforeAvailable;
            if (placementAvailable == available) return;
            placementAvailable = available; rebinding = true;
            bool save = config.SaveOnConfigSet; config.SaveOnConfigSet = false;
            try
            {
                Placement.SettingChanged -= PlacementChange;
                var definition = Placement.Definition; config.Remove(definition);
                Placement = config.Bind(definition, Placements[0], new ConfigDescription(available
                    ? PlacementDescription
                    : "Ray Reconstruction denoises and upscales together, so Neural Rendering runs after it.",
                    new AcceptableValueList<string>(available ? Placements : new[] { Placements[0] }), placementTag));
                Placement.Value = available ? wantedPlacement.Value : Placements[0];
                Placement.SettingChanged += PlacementChange;
                Changed = true; SceneCapture.Reset();
            }
            finally { config.SaveOnConfigSet = save; rebinding = false; }
        }
        private static void RefreshChoices()
        {
            string key = Available ? "" : Reason;
            if (key == choicesReason) return;
            choicesReason = key; rebinding = true;
            try
            {
                bool save = config.SaveOnConfigSet; config.SaveOnConfigSet = false;
                try
                {
                    Mode.SettingChanged -= ModeChange;
                    var definition = Mode.Definition; config.Remove(definition);
                    Mode = config.Bind(definition, "Off", new ConfigDescription(Available ? "Enhance scene lighting and materials with DLSS Neural Rendering." : Reason,
                        new AcceptableValueList<string>(Available ? new[] { "Off", "On" } : new[] { "Off" }), modeTag));
                    Mode.Value = Available ? wantedMode.Value : "Off";
                    Mode.SettingChanged += ModeChange;
                }
                finally { config.SaveOnConfigSet = save; }
                Changed = true;
            }
            finally { rebinding = false; }
        }
        internal static void Tick()
        {
            if (!started) return;
            try
            {
                if (Time.unscaledTime >= nextPoll)
                {
                    nextPoll = Time.unscaledTime + 0.5f; Native.Poll();
                    RefreshPlacement();
                    RefreshChoices();
                    if (Available) Notices.Clear("nr.availability");
                    else Notices.Set("nr.availability", NoticeLevel.Warning, Plugin.Name, "Unavailable", Reason);
                    VideoRows.Tick();
                }
                if (Changed)
                {
                    Changed = false;
                    foreach (var tag in childTags) tag.Visible = Mode.Value == "On";
                    foreach (var tag in advancedTags) tag.Visible = Mode.Value == "On" && Advanced.Value == "On";
                    skinTag.Visible = Mode.Value == "On" && Advanced.Value == "On" && AutoMask.Value == "On";
                    ModManagers.RequestFoARefresh();
                }
                MotionVectors.Request(Plugin.Name, Enabled);
                SceneCapture.SetActive(Enabled);
                VideoRows.RefreshInteractability();
            }
            catch (Exception ex) { Fault = "Neural Rendering stopped: " + ex.Message; Log.LogError(ex); MotionVectors.Request(Plugin.Name, false); SceneCapture.SetActive(false); }
        }
        internal static void Stop()
        {
            if (!started) return;
            started = false; MotionVectors.Request(Plugin.Name, false); SceneCapture.Stop();
            Mode.SettingChanged -= ModeChange; Strength.SettingChanged -= IntensityChange;
            Model.SettingChanged -= Change; LocalTone.SettingChanged -= Change;
            LocalStructure.SettingChanged -= Change; SkinStructure.SettingChanged -= Change; AutoMask.SettingChanged -= Change;
            Placement.SettingChanged -= PlacementChange;
            Advanced.SettingChanged -= IntensityChange;
            Passes.SettingChanged -= Change;
            childTags.Clear();
            advancedTags.Clear();
            Notices.Clear("nr.availability");
        }
    }
}
