// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using BepInEx.Configuration;
using Awaken.TG.Main.Settings;
using Awaken.TG.Main.Settings.Options;
using Awaken.TG.MVC;
using TGGraphicsBridge;
#if BEPINEX_IL2CPP
using Il2CppInterop.Runtime.Injection;
#endif
namespace TGNeuralRendering
{
    internal sealed class NeuralRenderingSetting : Setting
    {
        private readonly VideoRows rows;
#if BEPINEX_IL2CPP
        private readonly Il2CppSystem.Collections.Generic.IEnumerable<PrefOption> options;
        public NeuralRenderingSetting(IntPtr pointer) : base(pointer) { }
        internal NeuralRenderingSetting(VideoRows rows) : base(ClassInjector.DerivedConstructorPointer<NeuralRenderingSetting>())
        {
            ClassInjector.DerivedConstructorBody(this);
            Il2CppTypes.Construct(this, typeof(Setting), "NativeMethodInfoPtr__ctor_Protected_Void_0");
            this.rows = rows;
            var list = new Il2CppSystem.Collections.Generic.List<PrefOption>();
            foreach (var row in rows.Top) list.Add(row);
            options = list.Cast<Il2CppSystem.Collections.Generic.IEnumerable<PrefOption>>();
        }
        public new string SettingName => "Neural Rendering";
        public new Il2CppSystem.Collections.Generic.IEnumerable<PrefOption> Options => options;
        public new bool AutoApplyOnInit => false;
        public new void OnApply() => rows.Apply();
#else
        internal NeuralRenderingSetting(VideoRows rows) { this.rows = rows; }
        public override string SettingName => "Neural Rendering";
        public override IEnumerable<PrefOption> Options => rows.Top;
        protected override bool AutoApplyOnInit => false;
        protected override void OnApply() => rows.Apply();
#endif
    }
    internal sealed class VideoRows
    {
        private static VideoRows rows;
        private static NeuralRenderingSetting setting;
        private static SettingsMaster owner;
        private readonly EnumArrowsOption mode;
        private readonly EnumArrowsOption model, autoMask, advanced, passes;
        private readonly EnumArrowsOption placement;
        private readonly ToggleOption[] placementChoices;
        private readonly ToggleOption[] choices;
        private readonly SliderOption[] sliders;
        private readonly ConfigEntry<float>[] floats;
        private readonly PrefOption[] interactive;
        internal readonly PrefOption[] Top;
        private string synced;
        private bool wasAvailable;
        private VideoRows()
        {
            mode = VideoTab.Enum("TGNR.Video.", "Mode", "Neural Rendering", 0, out choices, "Off", "DLSS");
            mode.SetInteractabilityFunction((Func<bool>)(() => Session.Available));
            VideoTab.Tooltip(mode, () => RowAlerts.Has(AlertRow.NeuralRendering) ? RowAlerts.Text(AlertRow.NeuralRendering) : Session.Available ? Session.Mode.Description.Description : Session.Reason);
            model = VideoTab.Enum("TGNR.Video.", "Model", "Style", 0, out _, Session.StyleLabels);
            placement = VideoTab.Enum("TGNR.Video.", "Placement", "Placement", 0, out placementChoices, Session.PlacementLabels);
            placement.SetInteractabilityFunction((Func<bool>)(() => Session.Available && SceneCapture.BeforeAvailable));
            VideoTab.Tooltip(placement, () => Session.Placement.Description.Description);
            autoMask = VideoTab.Enum("TGNR.Video.", "AutoMask", "Automatic Mask", 0, out _, "Off", "On");
            advanced = VideoTab.Enum("TGNR.Video.", "Advanced", "Advanced", 0, out _, "Off", "On");
            VideoTab.Tooltip(advanced, () => Session.Advanced.Description.Description);
            advanced.SetInteractabilityFunction((Func<bool>)(() => Session.Available));
            passes = VideoTab.Enum("TGNR.Video.", "Passes", "Passes", 0, out _, Session.PassLabels);
            VideoTab.Tooltip(passes, () => Session.Passes.Description.Description);
            passes.SetInteractabilityFunction((Func<bool>)(() => Session.Available));
            VideoTab.Tooltip(model, () => Session.Model.Description.Description);
            VideoTab.Tooltip(autoMask, () => Session.AutoMask.Description.Description);
            model.SetInteractabilityFunction((Func<bool>)(() => Session.Available));
            autoMask.SetInteractabilityFunction((Func<bool>)(() => Session.Available));
            floats = new[] { Session.Strength, Session.LocalTone, Session.LocalStructure, Session.SkinStructure };
            sliders = new SliderOption[floats.Length];
            for (int i = 0; i < floats.Length; i++)
            {
                var entry = floats[i]; string name = entry.Definition.Key;
#if BEPINEX_IL2CPP
                sliders[i] = new SliderOption("TGNR.Video." + name.Replace(" ", ""), name, 0f, 1f, false, "{0:0.00}", (float)entry.DefaultValue, false, new Il2CppSystem.Nullable<float>());
#else
                sliders[i] = new SliderOption("TGNR.Video." + name.Replace(" ", ""), name, 0f, 1f, false, "{0:0.00}", (float)entry.DefaultValue, false, 0.05f);
#endif
                sliders[i].SetInteractabilityFunction((Func<bool>)(() => Session.Available));
                VideoTab.Tooltip(sliders[i], () => entry.Description.Description);
            }
            var skin = new PrefOption[] { sliders[3] };
            var maskRows = SwitchRowsOption.Create(autoMask, i => i == 1 ? skin : new PrefOption[0], skin);
            var fine = maskRows != null ? new PrefOption[] { sliders[1], model, passes, sliders[2], maskRows }
                : new PrefOption[] { sliders[1], model, passes, sliders[2], autoMask, sliders[3] };
            var advancedRows = SwitchRowsOption.Create(advanced, i => i == 1 ? fine : new PrefOption[0], fine);
            var nested = advancedRows != null ? new PrefOption[] { placement, sliders[0], advancedRows }
                : new PrefOption[] { placement, sliders[0], advanced, sliders[1], model, passes, sliders[2], autoMask, sliders[3] };
            var switched = SwitchRowsOption.Create(mode, i => i == 1 ? nested : new PrefOption[0], nested);
            interactive = new PrefOption[] { mode, placement, sliders[0], advanced, sliders[1], model, passes, sliders[2], autoMask, sliders[3] };
            Top = switched != null ? new PrefOption[] { switched } : interactive;
            RowAlerts.Register(AlertRow.NeuralRendering, mode);
            Sync();
        }
        private void Sync()
        {
            wasAvailable = Session.Available;
            VideoTab.Forbid(mode, wasAvailable ? new ToggleOption[0] : new[] { choices[1] });
            VideoTab.Forbid(placement, SceneCapture.BeforeAvailable ? new ToggleOption[0] : new[] { placementChoices[1] });
            synced = Key();
            mode.OptionInt = wasAvailable && Session.Mode.Value == "On" ? 1 : 0;
            model.OptionInt = Array.IndexOf(Session.Models, Session.Model.Value);
            placement.OptionInt = Session.Placement.Value == Session.Placements[1] ? 1 : 0;
            autoMask.OptionInt = Session.AutoMask.Value == "On" ? 1 : 0;
            advanced.OptionInt = Session.Advanced.Value == "On" ? 1 : 0;
            passes.OptionInt = Session.Passes.Value - 1;
            mode.Apply(); model.Apply(); placement.Apply(); autoMask.Apply(); advanced.Apply(); passes.Apply();
            for (int i = 0; i < sliders.Length; i++) { sliders[i].Value = floats[i].Value; sliders[i].Apply(); }
        }
        private static string Key() => Session.Mode.Value + "/" + Session.Model.Value + "/" + Session.AutoMask.Value + "/" + Session.Placement.Value + "/" +
            Session.Strength.Value + "/" + Session.LocalTone.Value + "/" + Session.LocalStructure.Value + "/" + Session.SkinStructure.Value + "/" + Session.Advanced.Value + "/" + Session.Passes.Value;
        internal void Apply()
        {
            // Preserve a saved On preference while the runtime is absent or HDR temporarily bypasses NR.
            if (Session.Available) Session.Mode.Value = mode.OptionInt == 1 ? "On" : "Off";
            Session.Model.Value = Session.Models[Math.Max(0, Math.Min(Session.Models.Length - 1, model.OptionInt))];
            if (SceneCapture.BeforeAvailable) Session.Placement.Value = Session.Placements[placement.OptionInt == 1 ? 1 : 0];
            Session.AutoMask.Value = autoMask.OptionInt == 1 ? "On" : "Off";
            Session.Advanced.Value = advanced.OptionInt == 1 ? "On" : "Off";
            Session.Passes.Value = Math.Max(1, Math.Min(Session.PassLabels.Length, passes.OptionInt + 1));
            for (int i = 0; i < sliders.Length; i++) floats[i].Value = sliders[i].Value;
        }
        internal static void RefreshInteractability()
        {
            if (rows != null && VideoTab.IsSettingsOpen)
            {
                VideoTab.Forbid(rows.mode, Session.Available ? new ToggleOption[0] : new[] { rows.choices[1] });
                VideoTab.Forbid(rows.placement, SceneCapture.BeforeAvailable ? new ToggleOption[0] : new[] { rows.placementChoices[1] });
                VideoTab.RefreshInteractability(rows.interactive);
            }
        }
        internal static void Tick()
        {
            var master = World.Any<SettingsMaster>();
            if (master == null || VideoTab.IsSettingsOpen) return;
            var list = VideoTab.GraphicSettings(master); if (list == null) return;
            if (setting == null || !VideoTab.Same(owner, master) || VideoTab.IndexOf(list, setting) < 0)
            {
#if BEPINEX_IL2CPP
                Il2CppTypes.Register<NeuralRenderingSetting>(genericBase: true);
#endif
                rows = new VideoRows(); setting = new NeuralRenderingSetting(rows); owner = master;
                VideoTab.AddGraphicSetting(master, setting);
            }
            if (rows.synced != Key() || rows.wasAvailable != Session.Available) rows.Sync();
            VideoTab.Reposition(list, setting, "UpScaling", "FrameGenerationVideoSetting", "PathTracingSetting");
        }
    }
}
