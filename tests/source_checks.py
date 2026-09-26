#!/usr/bin/env python3
"""Textual/package sanity checks: NOT Windows compilation or GPU validation."""
from pathlib import Path
import json
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
FG = "native/framegen/"
UP = "native/upscaling/"
BR = "native/bridge/"
PT = "native/pathtracing/"

def compiled_for(text, il2cpp):
    """The lines of a C# file that compile for one runtime: #if [!]BEPINEX_IL2CPP/BEPINEX_MONO regions resolved."""
    kept, stack = [], []  # stack: whether each open region is compiled; None for other conditions (both branches kept)
    for line in text.splitlines():
        directive = line.strip()
        match = re.match(r"#if\s+(!?)(BEPINEX_IL2CPP|BEPINEX_MONO)\s*$", directive)
        if match:
            wanted = (match.group(2) == "BEPINEX_IL2CPP") == il2cpp
            stack.append(wanted != (match.group(1) == "!"))
        elif directive.startswith("#if"):
            stack.append(None)
        elif directive.startswith("#else") and stack:
            stack[-1] = None if stack[-1] is None else not stack[-1]
        elif directive.startswith("#endif") and stack:
            stack.pop()
        elif False not in stack:
            kept.append(line)
    return "\n".join(kept)

class SourceConsistency(unittest.TestCase):
    def text(self, relative):
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_managed_targets(self):
        props = ET.fromstring(self.text("src/Game.References.props"))
        self.assertEqual(props.find(".//TargetFrameworks").text, "net6.0;net472")
        self.assertEqual(props.find(".//PlatformTarget").text, "x64")
        for token in ["BEPINEX_MONO", "BEPINEX_IL2CPP"]:
            self.assertIn(token, self.text("src/Game.References.props"))
        for project in ["TGGraphicsBridge", "TGFrameGeneration", "TGBetterUpscaling", "TGPathTracing"]:
            tree = ET.fromstring(self.text(f"src/{project}/{project}.csproj"))
            self.assertEqual(tree.find(".//Version").text, "1.2.0", project)
        self.assertEqual(ET.fromstring(self.text("src/TGGraphicsBridge.Autoload/Autoload.props")).find(".//Version").text, "1.2.0")
        # The mods built on the bridge reference it without copying it (Build.ps1 bundles it), and build
        # their own copy of its preloader.
        for project in ["TGFrameGeneration", "TGBetterUpscaling", "TGPathTracing"]:
            text = self.text(f"src/{project}/{project}.csproj")
            self.assertIn('<ProjectReference Include="..\\TGGraphicsBridge\\TGGraphicsBridge.csproj" Private="false" />', text)
            self.assertIn(f'<ProjectReference Include="..\\{project}.Autoload\\{project}.Autoload.csproj" ReferenceOutputAssembly="false" Private="false" />', text)

    def test_bridge_abi(self):
        header = self.text(BR + "include/tgb/bridge_c_api.h")
        self.assertIn("#define TGB_ABI 0x00010100u", header)
        self.assertIn("sizeof(TGB_Status)==68", header)
        managed = self.text("src/TGGraphicsBridge/GraphicsBridge.cs")
        self.assertIn("public const uint Abi = 0x00010100;", managed)
        self.assertIn("internal const int StatusSize = 68, PerformanceSize = 56;", managed)
        loader = self.text("src/TGGraphicsBridge.Autoload/Bootstrap.cs")
        self.assertIn("private const uint ExpectedAbi = 0x00010100;", loader)
        for symbol in re.findall(r"TGB_CALL\s+(TGGraphicsBridge_\w+)\(", header) + ["TGGraphicsBridge_Bootstrap"]:
            self.assertIn(symbol + "(", self.text(BR + "src/exports.cpp") + self.text(BR + "src/core.cpp"), symbol)
        for symbol in ["TGGraphicsBridge_GetAbiVersion", "TGGraphicsBridge_AddExtension", "TGGraphicsBridge_Bootstrap"]:
            self.assertIn('"' + symbol + '"', loader)
        for symbol in ["TGGraphicsBridge_ReadStatus", "TGGraphicsBridge_GetExtensionName", "TGGraphicsBridge_GetExtensionState"]:
            self.assertIn('"' + symbol + '"', managed)

    def test_extension_discovery(self):
        # The preloader finds every mod built on the bridge by its manifest; each points inside its own folder.
        loader = self.text("src/TGGraphicsBridge.Autoload/Bootstrap.cs")
        self.assertIn('ManifestName = "graphics-bridge-extension.json"', loader)
        self.assertIn("points outside its own folder", loader)
        self.assertIn("disable-autoload.txt", loader)
        self.assertIn('"TGFG.Autoload.dll"', loader)  # warns about 0.5.0's preloader
        self.assertIn("function Get-TgExtensionPath([string]$Mod) { \"BepInEx/$Mod/graphics-bridge-extension.json\" }", self.text("tools/Package.Common.ps1"))
        for mod, native in [("TGFrameGeneration", "Native/TGFG.Native.dll"), ("TGBetterUpscaling", "Native/TGBetterUpscaling.Native.dll"),
                            ("TGPathTracing", "Native/TGPathTracing.Native.dll")]:
            manifest = json.loads(self.text(f"src/{mod}/graphics-bridge-extension.json"))
            self.assertEqual(manifest["native"], native)
        api = self.text(BR + "include/tgb/bridge_api.hpp")
        self.assertIn("TGGraphicsBridge_CreateExtension", self.text(FG + "src/extension.cpp"))
        self.assertIn("TGGraphicsBridge_CreateExtension", self.text(UP + "src/extension.cpp"))
        self.assertIn("TGGraphicsBridge_CreateExtension", self.text(PT + "src/extension.cpp"))
        self.assertIn("uint32_t needs() noexcept override { return tgb::NeedD3D12; }", self.text(PT + "src/extension.cpp"))
        self.assertIn("NeedPresentation", api)
        self.assertIn("uint32_t needs() noexcept override { return tgb::NeedD3D12; }", self.text(UP + "src/extension.cpp"))

    def test_abi_and_exports(self):
        header = self.text(FG + "include/tgfg_api.h")
        managed = self.text("src/TGFrameGeneration/NativeAbi.cs")
        self.assertIn("0x00010100", header)
        self.assertIn("0x00010100", managed)
        self.assertIn("sizeof(TGFG_Status)==188", header)
        self.assertIn("sizeof(TGFG_Request)==48", header)
        self.assertIn("sizeof(TGFG_Camera)==416", header)
        self.assertIn("RequestSize = 48, StatusSize = 188, CameraSize = 416, HdrpInputsSize = 48", managed)
        self.assertIn("sizeof(TGFG_HdrpInputs)==48", header)
        native = self.text(FG + "src/control.cpp")
        symbols = re.findall(r"TGFG_CALL\s+(TGFG_\w+)\(", header)
        # 12, plus the troubleshooting report's diagnostics, timings and notes, and presenting without tearing.
        self.assertEqual(len(symbols), 16)
        self.assertIn("sizeof(TGFG_Timing)==72", header)
        self.assertIn("internal const int SizeBytes = 72;", managed)
        for symbol in symbols:
            self.assertIn(symbol + "(", native)
        self.assertNotIn("TGFG_Bootstrap", header)

    def test_upscaling_abi(self):
        header = self.text(UP + "include/tgup_api.h")
        managed = self.text("src/TGBetterUpscaling/NativeUpscaler.cs")
        self.assertIn("#define TGUP_ABI 0x00010300u", header)
        self.assertIn("internal const uint Abi = 0x00010300;", managed)
        self.assertIn('static_assert(sizeof(TGUP_Mode)==8, "TG Better Upscaling mode ABI");', header)
        self.assertIn("FrameSize = 264, StatusSize = 88, ModeSize = 8, DlssOverrideSize = 24;", managed)
        self.assertIn('static_assert(sizeof(TGUP_Frame)==264, "TG Better Upscaling frame ABI");', header)
        self.assertIn('static_assert(sizeof(TGUP_Status)==88, "TG Better Upscaling status ABI");', header)
        events = dict(re.findall(r"#define TGUP_EVENT_(\w+) (\d+)", header))
        self.assertEqual(events, {"UPSCALE": "1", "OUTPUT": "2", "PREPARE_RAY_RECONSTRUCTION": "3"})
        self.assertIn("internal const int EventPrepareRayReconstruction = 3;", managed)
        flags = dict(re.findall(r"#define TGUP_FLAG_(\w+) (\d+)u", header))
        self.assertEqual(flags, {"RESET": "1", "DEPTH_INVERTED": "2", "FSR_AUTO_EXPOSURE": "4", "FSR_DEBUG_CHECK": "8", "FSR_DEBUG_VIEW": "16"})
        self.assertIn("internal const uint Reset = 1, DepthInverted = 2, FsrAutoExposure = 4, FsrDebugCheck = 8, FsrDebugView = 16;", managed)
        self.assertIn("#define TGUP_BACKEND_FSR 1u", header)
        self.assertIn("#define TGUP_BACKEND_XESS 2u", header)
        self.assertIn("#define TGUP_BACKEND_RAY_RECONSTRUCTION 3u", header)
        self.assertIn("internal const uint BackendFsr = 1, BackendXess = 2, BackendRayReconstruction = 3;", managed)
        # The request's camera matrices mirror the header's (16 floats each, after the guides).
        self.assertIn("uint64_t diffuseAlbedo, specularAlbedo, normalRoughness;", header)
        self.assertIn("float worldToView[16], viewToClip[16];", header)
        self.assertIn("internal ulong DiffuseAlbedo, SpecularAlbedo, NormalRoughness;", managed)
        self.assertIn("internal fixed float WorldToView[16];", managed)
        self.assertIn("internal fixed float ViewToClip[16];", managed)
        exports = self.text(UP + "src/extension.cpp")
        for symbol in re.findall(r"TGUP_CALL\s+(TGUP_\w+)\(", header):
            self.assertIn(symbol + "(", exports)
            self.assertIn('"' + symbol + '"', managed)

    def test_upscaling_takes_stps_place(self):
        # FSR and XeSS run where HDRP runs STP, on HDRP's own inputs, in STP's output texture.
        host = self.text("src/TGBetterUpscaling/StpHost.cs")
        for token in ['AccessTools.Method(typeof(HDRenderPipeline), "DoStpPasses")',
                      'AccessTools.Method(typeof(UpScalingController), "OnSettingChanged", new[] { typeof(Setting) })',
                      '"GetPostprocessUpsampledOutputHandle"', "colorBufferRG = output, nonMSAAColorBufferRG = inputColor",
                      "depthBufferRG = inputDepth, motionVectorBufferRG = inputMotion",
                      "MotionScaleX = -(float)renderWidth, MotionScaleY = -(float)renderHeight",
                      "JitterX = jitter.x, JitterY = jitter.y", "camera.allowDeepLearningSuperSampling = false;",
                      "camera.allowDynamicResolution = true;", "DynamicResolutionHandler.SetActiveDynamicScalerSlot(DynamicResScalerSlot.User);",
                      "ctx.cmd.IssuePluginEventAndData(Session.Upscaler.RenderEvent, UpscaleEvent,",
                      "HDUtils.BlitCameraTexture(ctx.cmd, source, ctx.cameraColorBuffer, 0f, true)",
                      "SystemInfo.usesReversedZBuffer ? NativeFlags.DepthInverted : 0u",
                      'Il2CppTypes.Construct(this, typeof(CustomPass), "NativeMethodInfoPtr__ctor_Protected_Void_0");',
                      "private new void Execute(CustomPassContext ctx) => StpHost.Execute(ctx);"]:
            self.assertIn(token, host)
        self.assertIn("private const int UpscaleEvent = 1;", host)
        # Only the gameplay camera; anything unusual leaves STP in charge.
        self.assertIn("camera.GetInstanceID() == main.GetInstanceID() && camera.cameraType == CameraType.Game && camera.targetTexture == null", host)
        self.assertIn("if (!IsGameplayCamera(hdCamera)) { ++keptCamera; return true; }", host)
        # The jitter is HDRP's own (Halton(2,3) over its 1024-frame index); STP derives the jitter it removes
        # from that index, so changing it would break STP whenever it upscales in FSR's or XeSS's place.
        for token in ["GetJitteredProjectionMatrix", "taaJitter =", "Halton(", "Matrix4x4.Frustum", "Jit16("]:
            self.assertNotIn(token, host.split("namespace TGBetterUpscaling", 1)[1], token)
        self.assertEqual(host.count("candidate.Patch("), 2)
        # History: a gap since the last hosted frame resets FSR's or XeSS's; STP taking over right after one
        # resets STP's (HDRP reads resetPostProcessingHistory in PopulateStpConfig, after this prefix).
        self.assertIn("Time.frameCount - lastHostedFrame > 1) flags |= NativeFlags.Reset;", host)
        self.assertIn("private static int lastHostedFrame = -100;", host)
        stp_runs = host.split("private static bool StpRuns(HDCamera hdCamera)", 1)[1].split("internal static void Execute(", 1)[0]
        self.assertIn("Time.frameCount - lastHostedFrame <= 1 && lastHostedFrame != Time.frameCount && IsGameplayCamera(hdCamera)", stp_runs)
        self.assertIn("ResetStpHistory(hdCamera);", stp_runs)
        self.assertIn("private static void ResetStpHistory(HDCamera hd) => ResetHistory?.SetValue(hd, true);", host)
        self.assertIn("private static void ResetStpHistory(HDCamera hd) => hd.resetPostProcessingHistory = true;", host)
        before = host.split("private static bool BeforeStp(", 1)[1].split("private static bool StpRuns(", 1)[0]
        self.assertIn("if (!Active) { ++keptInactive; return StpRuns(hdCamera); }", before)
        self.assertEqual(before.count("return StpRuns(hdCamera);"), 4)
        # FSR's own sharpening, and none for XeSS; half strength unless the player chose otherwise.
        self.assertIn("Sharpness = fsr ? settings.Sharpness : 0f", host)
        settings = self.text("src/TGBetterUpscaling/Settings.cs")
        self.assertIn("internal const float DefaultSharpness = 0.5f;", settings)
        self.assertIn("file.Bind(VideoSection, SharpnessKey, DefaultSharpness,", settings)
        self.assertIn('"{0:P0}", Settings.DefaultSharpness, false, NoStep)', self.text("src/TGBetterUpscaling/UpscalerRow.cs"))
        # No capture or comparison code in a release build.
        for path in ["src/TGBetterUpscaling/StpHost.cs", "src/TGBetterUpscaling/Session.cs", "src/TGBetterUpscaling/Settings.cs", "src/TGBetterUpscaling/NativeUpscaler.cs"]:
            text = self.text(path)
            self.assertIsNone(re.search(r"(?<!String)Comparison", text), path)
            for token in ["CaptureTexture", "DoUpscalingAndAntiAliasing", "KeyCode"]:
                self.assertNotIn(token, text, path + ": " + token)
        self.assertFalse((ROOT / "src/TGBetterUpscaling/Comparison.cs").exists())
        self.assertIn("!Session.Status.Usable(backend)) { ++keptRuntime; return StpRuns(hdCamera); }", host)
        native = self.text(UP + "src/upscaler.cpp")
        self.assertIn("const bool single=frame.output!=0;", native)
        self.assertIn("kFailuresBeforeDisable=5", native)
        policy = self.text(UP + "include/tgup_policy.hpp")
        self.assertIn("inline void sdk_jitter(const TGUP_Frame& f, float& x, float& y) { x=-f.jitterX; y=-f.jitterY; }", policy)

    def test_game_upscaling_type_is_not_initialized_early(self):
        # UpScaling's static constructor translates labels: run before the game loads its localization, it
        # throws and the game's settings model (and loading) fails. Its statics are read only once the game
        # created its own instance, or from the game's own controller.
        row = self.text("src/TGBetterUpscaling/UpscalerRow.cs")
        self.assertLess(row.index("var up = Game();"), row.index("UpScaling.IsSTPAvailable"))
        self.assertLess(row.index("if (up == null)"), row.index("UpScaling.IsSTPAvailable"))
        # Under IL2CPP any use of the type runs that constructor, a type argument included: the game's
        # setting is looked up by class name, so the type is touched only once the game has created it.
        self.assertIn('internal static UpScaling Game() => VideoTab.GraphicSetting<UpScaling>("UpScaling");', compiled_for(row, True))
        self.assertIn("internal static UpScaling Game() => World.Any<UpScaling>();", compiled_for(row, False))
        self.assertIn('var upscaling = VideoTab.GraphicSetting<UpScaling>("UpScaling");', compiled_for(self.text("src/TGFrameGeneration/NativeVideoSettings.cs"), True))
        self.assertIn("if (TypeName(list[i]) == className) return list[i].TryCast<T>();", compiled_for(self.text("src/TGGraphicsBridge/VideoTab.cs"), True))
        for path in sorted((ROOT / "src").rglob("*.cs")):
            if "obj" in path.parts or "bin" in path.parts: continue
            code = re.sub(r"//.*", "", compiled_for(path.read_text(encoding="utf-8"), True))
            self.assertNotIn("World.Any<UpScaling>", code, str(path))

    def test_upscaling_pass_is_recorded_with_its_targets_data(self):
        # The interop's CustomPass.ExecuteInternal hands IL2CPP the boxed RenderTargets for its `in` parameter:
        # HDRP read garbage targets, dropped the pass every frame and leaked memory until the system ran out.
        host = self.text("src/TGBetterUpscaling/StpHost.cs")
        il2cpp = re.sub(r"//.*", "", compiled_for(host, True))
        self.assertNotIn(".ExecuteInternal(", il2cpp)
        self.assertIn("args[4] = (void*)IL2CPP.il2cpp_object_unbox(IL2CPP.Il2CppObjectBaseToPtrNotNull(targets));", il2cpp)
        self.assertIn('NativeMethod(typeof(CustomPass), "NativeMethodInfoPtr_ExecuteInternal_")', il2cpp)
        # A pass HDRP keeps dropping ends FSR/XeSS for the session, on both runtimes.
        self.assertIn("if (armed && ++unexecuted >= 3)", host)
        self.assertIn("++executed; unexecuted = 0;", host)

    def test_sliders_pass_a_step_the_interop_accepts(self):
        # SliderOption's step is a Nullable<float>: null on Mono, but a boxed object in the IL2CPP interop
        # that may not be null itself (NullReferenceException in the constructor); an empty one there.
        sliders = 0
        for path in sorted((ROOT / "src").rglob("*.cs")):
            if "obj" in path.parts or "bin" in path.parts: continue
            text = path.read_text(encoding="utf-8")
            for call in re.findall(r"new SliderOption\((.*)\);", compiled_for(text, True)):
                sliders += 1
                self.assertNotRegex(call, r",\s*null$", str(path))
        self.assertEqual(sliders, 3) # the bridge's, TG Better Upscaling's, and TG Path Tracing's (Mono only)
        self.assertIn("var step = new Il2CppSystem.Nullable<float>();", compiled_for(self.text("src/TGGraphicsBridge/PerformanceOverlayRow.cs"), True))
        self.assertIn("private static Il2CppSystem.Nullable<float> NoStep => new Il2CppSystem.Nullable<float>();", compiled_for(self.text("src/TGBetterUpscaling/UpscalerRow.cs"), True))
        for path in ["src/TGBetterUpscaling/Session.cs", "src/TGBetterUpscaling/Plugin.cs", "src/TGBetterUpscaling/Settings.cs", "src/TGBetterUpscaling/NativeUpscaler.cs",
                     "src/TGFrameGeneration/Session.cs", "src/TGFrameGeneration/Plugin.cs", "src/TGGraphicsBridge/Plugin.cs"]:
            self.assertIsNone(re.search(r"\bUpScaling\.(Is|Up|DLSS|STP|Quality)", self.text(path)), path)
        # StpHost reads UpScaling's statics only from the game's own controller (BeforeSettingChanged).
        host = self.text("src/TGBetterUpscaling/StpHost.cs")
        start = host.index("private static bool BeforeSettingChanged(")
        end = host.index("private static bool ConfigureDlss(")
        uses = [m.start() for m in re.finditer(r"UpScaling\.IsSTPAvailable", host)]
        self.assertTrue(uses)
        for at in uses:
            self.assertTrue(start < at < end, at)

    def test_upscaler_row_is_the_games_own(self):
        # One selector: FSR and XeSS are entries 3 and 4 of the game's Upscaling row, forbidden when unavailable.
        row = self.text("src/TGBetterUpscaling/UpscalerRow.cs")
        settings = self.text("src/TGBetterUpscaling/Settings.cs")
        self.assertIn("public enum UpscalerSetting { None = 0, STP = 1, DLSS = 2, FSR = 3, XeSS = 4 }", settings)
        for token in ['VideoTab.Choice("UpScalingType_FSR"', 'VideoTab.Choice("UpScalingType_XeSS"', "if (entries.Length == 3)",
                      "VideoTab.Forbid(type,", 'private const string TypeKey = "UpScalingType";', "PrefMemory.GetInt(TypeKey, -1)",
                      "SwitchRowsOption.Create(type,", "i == FsrIndex ? fsrRows : i == XessIndex ? xessRows : sliderRows", "ApplyRefreshing(up);",
                      "private static void ApplyRefreshing(UpScaling up) => up.Apply(true, out _);"]:
            self.assertIn(token, row)
        self.assertNotIn('"FSR Upscaling"', row)
        # The mod-manager rows are the Video tab's.
        names = re.findall(r'Row\(\w+Key, [^,]+, "([^"]+)", (\d)', settings)
        self.assertEqual([n for n, _ in names], ["Upscaling"])
        self.assertIn('DisplayName = "FSR Sharpness"', settings)
        self.assertIn('"FSR Sharpness"', row)
        self.assertIn("rows[SharpnessKey].Visible = Upscaler == UpscalerSetting.FSR;", settings)
        # No FSR version choice: FSR runs the newest version AMD's runtime offers on this GPU.
        for text in (row, settings):
            self.assertNotIn("FSR Version", text)
            self.assertNotIn("FsrVersionIndex", text)
        self.assertIn("fsr.newest=newest_version(fsr.versionNames);", self.text(UP + "src/fsr_upscaler.cpp"))
        self.assertIn("ModManagers.RequestFoARefresh();", settings)

    def test_reason_enum_matches(self):
        cpp = self.text(FG + "include/tgfg_policy.hpp")
        cs = self.text("src/TGFrameGeneration/NativeAbi.cs")
        cpp_body = re.search(r"enum class Reason[^\{]*\{([^}]+)", cpp).group(1)
        cs_body = re.search(r"enum Reason[^\{]*\{([^}]+)", cs).group(1)
        values = lambda s: dict(re.findall(r"(\w+)\s*=\s*(\d+)", s))
        self.assertEqual(values(cpp_body), values(cs_body))
        self.assertEqual(len(values(cpp_body)), 34)
        # The native log names each reason by its index.
        names = re.search(r"static const char\* names\[\]=\{(\"Ready\".*?)\};", self.text(FG + "src/control.cpp"), re.S).group(1)
        self.assertEqual(names.count('"'), 2 * 34)

    def test_no_third_party_wrapper(self):
        # FSR and XeSS come from AMD's and Intel's own SDKs, never from a wrapper or patched runtime.
        paths = list((ROOT / "native").rglob("*")) + list((ROOT / "src").rglob("*.cs"))
        # Only the check that finds OptiScaler in the game, and the explanations that tell the player it
        # conflicts, name it; the check reads module names and files and never loads anything.
        # Notices maps the OptiScaler notice to the rows it concerns.
        detectors = {"Compatibility.cs", "Explain.cs", "FrameRateCheck.cs", "Notices.cs"}
        for path in paths:
            if not path.is_file() or path.suffix not in {".cpp", ".hpp", ".h", ".cs", ".hlsl"}:
                continue
            text = path.read_text(encoding="utf-8").lower()
            for forbidden in ["optiscaler", "requires patched"]:
                if forbidden == "optiscaler" and path.name in detectors:
                    continue
                self.assertNotIn(forbidden, text, path)
            # The one exception is the plugin ID of 0.4.0 and earlier, which settings migration has to name.
            self.assertNotIn("experimental", text.replace("tgfoa.experimental.dlss-frame-generation", ""), path)
        self.assertFalse((ROOT / "tools" / "patch_optiscaler.py").exists())
        compatibility = self.text("src/TGGraphicsBridge/Compatibility.cs")
        for loader in ["LoadLibrary", "DllImport", "Assembly.Load"]:
            self.assertNotIn(loader, compatibility)

    def test_vendor_runtimes_are_signed_and_queried(self):
        fsr = self.text(FG + "src/fsr_backend.cpp")
        xess = self.text(FG + "src/xess_backend.cpp")
        self.assertIn('L"ADVANCED MICRO DEVICES"', fsr)
        self.assertIn('L"INTEL"', xess)
        for token in ["FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12", "ffxDispatchDescFrameGenerationPrepareV2",
                      "FFX_API_QUERY_DESC_TYPE_GET_VERSIONS"]:
            self.assertIn(token, fsr)
        for token in ["xefgSwapChainGetProperties", "XEFG_SWAPCHAIN_USE_MAX_SUPPORTED_INTERPOLATED_FRAMES",
                      "xefgSwapChainD3D12GetInitializationParameters", "xefgSwapChainSetLatencyReduction",
                      "xefgSwapChainSetNumInterpolatedFrames", "XEFG_SWAPCHAIN_RV_ONLY_NOW"]:
            self.assertIn(token, xess)
        upscaler = self.text(UP + "src/fsr_upscaler.cpp")
        for token in ["FFX_API_QUERY_DESC_TYPE_GET_VERSIONS", "FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION", "FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION"]:
            self.assertIn(token, upscaler)
        # No version override: without a version choice for the player, AMD asks for its default.
        self.assertNotIn("FFX_API_DESC_TYPE_OVERRIDE_VERSION", upscaler)
        self.assertNotIn("ffxOverrideVersion", upscaler)
        packaging = self.text("tools/Package.Common.ps1")
        for token in ["'amd_fidelityfx_framegeneration_dx12.dll'", "'amd_fidelityfx_upscaler_dx12.dll'", "'libxess.dll'", "'libxess_fg.dll'", "'libxell.dll'",
                      "'LICENSE-XeSS.txt'", "'LICENSE-FidelityFX.md'", "'LICENSE-NVIDIA-RTX-SDKs.txt'", "'LICENSE-Streamline.txt'", "'LICENSE-NVIDIA-Reflex.txt'"]:
            self.assertIn(token, packaging)

    def test_performance_overlay(self):
        # One overlay from the shared bridge; each mod adds its section. Switched on in the Video settings'
        # last row, FoA Mod Manager or the config, and nothing else (no hotkey).
        overlay = self.text("src/TGGraphicsBridge/PerformanceOverlay.cs")
        plugin = self.text("src/TGGraphicsBridge/Plugin.cs")
        self.assertIn('shown = config.Bind(Section, "Performance overlay", false, new ConfigDescription(Description, null, Row("Performance Overlay", 10)));', overlay)
        self.assertIn('private const string Section = "Video", RetiredSection = "Debug";', overlay)
        self.assertIn("public static void AddSection(string title, int order, Func<IEnumerable<string>> lines)", overlay)
        self.assertNotIn("Input.", overlay)
        self.assertEqual(plugin.count('Step("its performance overlay setting", () => PerformanceOverlay.Bind(Config));'), 2)
        self.assertIn("try { PerformanceOverlay.Draw(); Notices.Draw(); }", plugin)
        self.assertIn("PerformanceOverlayRow.Tick(now);", plugin)
        self.assertIn('PerformanceOverlay.AddSection("Upscaling", 10, UpscalingOverlay.Lines);', self.text("src/TGBetterUpscaling/Session.cs"))
        self.assertIn('PerformanceOverlay.AddSection("Frame generation", 20, FrameGenerationOverlay.Lines);', self.text("src/TGFrameGeneration/Session.cs"))
        # The rates the overlay, the framerate check and the troubleshooting test share (FrameRates).
        fg = self.text("src/TGFrameGeneration/FrameRates.cs")
        self.assertIn("else if (backend == FrameGenerationSetting.FSR) Output = Rendered * (status.FsrMaxGenerated + 1);", fg)
        # Rates over the last second of status reads, timed by the reads (0.1 s), not by the 0.25 s redraws.
        self.assertIn("while (window.Count > 2 && at - window[1].Time >= 1f) window.RemoveAt(0);", fg)
        session = self.text("src/TGFrameGeneration/Session.cs")
        self.assertIn("Status=result; StatusTime=now;", session)
        self.assertIn("FrameRates.Update(Status,now,", session)
        self.assertNotIn("Time.unscaledTime", fg)
        self.assertNotIn("estimate", fg.lower())
        self.assertNotIn("estimate", self.text("src/TGFrameGeneration/FrameGenerationOverlay.cs").lower())
        native = self.text(BR + "src/performance.cpp")
        for token in ["GPU Engine(*)", "Utilization Percentage", "QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)",
                      "GetProcessTimes(GetCurrentProcess()", "try { sampler(); }"]:
            self.assertIn(token, native)
        self.assertIn('static_assert(sizeof(TGB_Performance)==56, "TG Graphics Bridge performance ABI");', self.text(BR + "include/tgb/bridge_c_api.h"))
        self.assertIn("PerformanceSize = 56", self.text("src/TGGraphicsBridge/GraphicsBridge.cs"))
        self.assertIn("TGUP_GetDlssDll", self.text("src/TGBetterUpscaling/NativeUpscaler.cs"))

    def test_performance_overlay_video_row(self):
        # The last row of the Video settings, added once even with both mods installed, with the same rows
        # FoA Mod Manager lists: Performance Overlay, and nested under it Overlay Position and Overlay Size while
        # it is on and Create Troubleshooting Report always.
        row = self.text("src/TGGraphicsBridge/PerformanceOverlayRow.cs")
        overlay = self.text("src/TGGraphicsBridge/PerformanceOverlay.cs")
        for token in ['internal const string Name = "Performance Overlay";', 'VideoTab.Enum(IdPrefix, "PerformanceOverlay", PerformanceOverlaySetting.Name, 0, out _, "Off", "On")',
                      '"Overlay Position"', '"Overlay Size"', "SwitchRowsOption.Create(shown, i => i == 1 ? whenOn : whenOff, whenOn)", "var whenOn = new PrefOption[] { corner, size, report };",
                      "var whenOff = new PrefOption[] { report };", "Top = rows != null ? new PrefOption[] { rows } :",
                      "VideoTab.MoveToEnd(list, setting);", "if (VideoTab.TypeName(list[i]) != nameof(PerformanceOverlaySetting)) continue;",
                      "Il2CppTypes.Register<PerformanceOverlaySetting>(genericBase: true);", "if (open) return; // never change the list while the screen is open"]:
            self.assertIn(token, row)
        self.assertEqual(row.count("VideoTab.AddGraphicSetting(master, setting);"), 1)
        for token in ['Row("Overlay Position", 11,', 'Row("Overlay Size", 12)', "if (cornerRow != null) cornerRow.Visible = on;",
                      "if (sizeRow != null) sizeRow.Visible = on;", "if (!storing) Changed?.Invoke();"]:
            self.assertIn(token, overlay)
        self.assertIn("public static bool MoveToEnd(SettingList list, Setting item)", self.text("src/TGGraphicsBridge/VideoTab.cs"))

    def test_driver_dlss_overrides(self):
        # The NVIDIA App's DLSS override lives in the driver's profile for the game; the overlay reads it
        # there (NGX itself has no query for the model in use).
        profile = self.text(UP + "src/nvidia_profile.cpp")
        for token in ["kSrOverride=0x10E41E01, kSrPreset=0x10E41DF3, kSrMode=0x10AFB768, kDlaaOverride=0x10E41DF4, kSrScaling=0x10E41DF5",
                      'static_assert(sizeof(NvSetting)==12320, "NVDRS_SETTING_V1 as the driver expects it");',
                      "o.known=readable ? 1u : 0u;", "Sleep(30000);"]:
            self.assertIn(token, profile)
        overlay = self.text("src/TGBetterUpscaling/UpscalingOverlay.cs")
        self.assertIn('return ((char)(\'A\' + preset - 1)) + " (NVIDIA driver override)";', overlay)
        self.assertIn("if (overridden && DriverMode(driver) is string forced) lines.Add(forced);", overlay)
        self.assertIn('static_assert(sizeof(TGUP_DlssOverride)==24, "TG Better Upscaling DLSS override ABI");', self.text(UP + "include/tgup_api.h"))
        self.assertIn("DlssOverrideSize = 24", self.text("src/TGBetterUpscaling/NativeUpscaler.cs"))

    def test_quality_modes_instead_of_the_slider(self):
        # DLSS, FSR and XeSS offer the modes their runtimes report; STP keeps the game's slider, and an
        # upscaler whose runtime reports none keeps it too.
        modes = self.text("src/TGBetterUpscaling/QualityModes.cs")
        self.assertIn("if (mode.Scale <= percent + 0.5f) return mode;", modes)  # round down, as the user chose
        self.assertIn("internal static readonly int[] DlssIds = { DlssDlaa, DlssQuality, DlssBalanced, DlssPerformance, DlssUltraPerformance };", modes)
        dlss = self.text("src/TGBetterUpscaling/DlssModes.cs")
        self.assertIn("device.GetOptimalSettings(width, height, (DLSSQuality)id, out data) && data.outRenderWidth > 0", dlss)
        settings = self.text("src/TGBetterUpscaling/Settings.cs")
        self.assertEqual(settings.count('modes[upscaler] = Row(key, QualityModes.DefaultName, "Quality Mode", 1, names, name => name, description);'), 1)
        self.assertEqual(settings.count("ModeRow(UpscalerSetting."), 3)
        self.assertIn("rows[key].Visible = offered.Length > 0 && Upscaler == u;", settings)
        self.assertIn("var mode = QualityModes.Default(offered, gamePercent);", settings)
        row = self.text("src/TGBetterUpscaling/UpscalerRow.cs")
        for token in ['ModeRow(UpscalerSetting.DLSS, "DlssMode")', 'ModeRow(UpscalerSetting.FSR, "FsrMode")', 'ModeRow(UpscalerSetting.XeSS, "XessMode")',
                      "i == (int)UpscalerSetting.DLSS ? dlssRows", "var dlssRows = dlssMode != null ? new PrefOption[] { dlssMode } : sliderRows;",
                      "internal static void OnPreset(UpScaling up)", "var mode = QualityModes.Default(offered, percent);"]:
            self.assertIn(token, row)
        host = self.text("src/TGBetterUpscaling/StpHost.cs")
        # Never at plugin load: patching compiles the method, and Mono runs UpScaling's static constructor,
        # which waits for localization the game hasn't loaded yet (the game hung at startup).
        initialize = host.split("internal static void Initialize()", 1)[1].split("private static string InjectionPoint()", 1)[0]
        self.assertNotIn("SetValueForPreset", initialize)
        self.assertNotIn("typeof(UpScaling)", initialize)
        self.assertIn("StpHost.HookPresets();", row)
        self.assertLess(row.index("owner = up;"), row.index("StpHost.HookPresets();"))
        for token in ['AccessTools.Method(typeof(UpScaling), "SetValueForPreset")', "postfix: new HarmonyMethod(typeof(StpHost), nameof(AfterPreset))",
                      "camera.deepLearningSuperSamplingQuality = (uint)mode.Id;", "camera.deepLearningSuperSamplingUseCustomQualitySettings = true;",
                      "hostScale = QualityModes.RenderScale(mode);", "Mode = rr ? rrMode : technology == UpscalerSetting.XeSS ? hostMode : 0u"]:
            self.assertIn(token, host)
        self.assertIn("FFX_API_QUERY_DESC_TYPE_UPSCALE_GETUPSCALERATIOFROMQUALITYMODE", self.text(UP + "src/fsr_upscaler.cpp"))
        xess = self.text(UP + "src/xess_upscaler.cpp")
        self.assertIn("if(uint32_t(kXessRanges[i].preset)==f.mode && accepts(i)) index=i;", xess)
        self.assertIn("f.renderWidth!=renderWidth_ || f.renderHeight!=renderHeight_ || f.mode!=mode_;", xess)

    def test_fsr_takes_hdrps_exposure(self):
        # FSR gets HDRP's exposure as HDRP's own DLSS pass takes it (readback, 0.2..2), as its pre-exposure
        # and as its exposure texture; XeSS keeps working on the pre-exposed color.
        host = self.text("src/TGBetterUpscaling/StpHost.cs")
        self.assertIn("PreExposure = PreExposure(__instance, hdCamera),", host)
        self.assertIn("Mathf.Clamp(value, 0.2f, 2f)", host)
        self.assertIn("hd.RequestGpuExposureValue(texture);", host)
        self.assertIn('AccessTools.Method(typeof(HDCamera), "RequestGpuExposureValue", new[] { typeof(RTHandle) })', host)
        fsr = self.text(UP + "src/fsr_upscaler.cpp")
        self.assertIn("write_exposure(list, f.preExposure);", fsr)
        self.assertIn("dispatch.exposure=ffxApiGetResourceDX12(exposure_.Get(), FFX_API_RESOURCE_STATE_COMMON);", fsr)
        self.assertIn("dispatch.preExposure=f.preExposure;", fsr)
        self.assertIn("xs.xessSetExposureMultiplier(context_, 1.f);", self.text(UP + "src/xess_upscaler.cpp"))

    def test_fsr_mip_bias(self):
        # AMD's texture mip bias for FSR is one mip below HDRP's log2(render/output), which XeSS keeps.
        host = self.text("src/TGBetterUpscaling/StpHost.cs")
        self.assertIn("internal const float FsrMipBias = -1f;", host)
        self.assertIn("camera.materialMipBias = biasBefore + FsrMipBias;", host)
        self.assertIn("biasedCamera.materialMipBias = biasBefore;", host)
        self.assertIn("SetMipBias(CameraOf(__instance), UpscalerSetting.None);", host)
        self.assertIn("SetMipBias(camera, failed ? UpscalerSetting.None : upscaler);", host)  # STP after a failure: no FSR bias

    def test_bundled_dlss_for_the_games_dlss(self):
        # The game's DLSS gets the bundled nvngx_dlss.dll only over an older file, from the load NGX
        # makes; nothing in the game folder is written and no other DLL is touched.
        dlss = self.text(UP + "src/dlss_dll.cpp")
        policy = self.text(UP + "include/tgup_policy.hpp")
        self.assertIn('GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "LoadLibraryExW")', dlss)
        self.assertIn('host().trusted_signer(path.c_str(), L"NVIDIA")', dlss)
        self.assertIn("if(!reading && is_dlss_sr_path(path))", dlss)
        self.assertIn("use=use_bundled_dlss(theirs, bundledVersion);", dlss)
        # A DLSS mod's set (DLSS 5: neural rendering beside it) keeps its own nvngx_dlss.dll.
        self.assertIn("if(part_of_dlss_set(theirs, neuralPresent, {reconstruction, generation})) {", dlss)
        self.assertIn('sibling(L"nvngx_dlssnr.dll")', dlss)
        self.assertIn("inline bool part_of_dlss_set(uint64_t requested, bool neuralRendering, std::initializer_list<uint64_t> siblings) {", policy)
        self.assertIn("return requested && bundled && requested<bundled;", policy)
        for forbidden in ["CopyFile", "MoveFile", "DeleteFile", "std::filesystem::copy", "std::filesystem::rename", "std::filesystem::remove"]:
            self.assertNotIn(forbidden, dlss, forbidden)
        extension = self.text(UP + "src/extension.cpp")
        self.assertLess(extension.index("install_dlss_dll(root);"), extension.index("load_fsr_runtime(root)"))
        packaging = self.text("tools/Package.Common.ps1")
        self.assertIn("@{Folder='DLSS'; Signer='NVIDIA'; Optional=@()\n                  Files=@('nvngx_dlss.dll','nvngx_dlssd.dll'); Notices=@('LICENSE-NVIDIA-RTX-SDKs.txt')}", packaging)
        stage = self.text("Stage-Runtime.ps1")
        self.assertIn("$vendors+=@{Folder='TGBetterUpscaling\\DLSS'; Signer='NVIDIA'", stage)
        self.assertIn("'lib\\Windows_x86_64\\rel\\nvngx_dlss.dll'", stage)
        self.assertIn("'lib\\Windows_x86_64\\rel\\nvngx_dlssd.dll'", stage)

    def test_single_latency_selector(self):
        rows = self.text("src/TGFrameGeneration/NativeVideoSettings.cs")
        caps = self.text("src/TGFrameGeneration/VideoCapabilities.cs")
        policy = self.text(FG + "include/tgfg_policy.hpp")
        self.assertIn('"Low Latency"', rows)
        self.assertNotIn('"NVIDIA Reflex", 1, "Off", "On"', rows)
        for label in ['"NVIDIA Reflex"', '"NVIDIA Reflex + Boost"', '"Intel XeLL"', '"AMD Anti-Lag 2"']:
            self.assertIn(label, caps)
        self.assertIn("if (selected == FrameGenerationSetting.DLSS) allowed.AddRange(new[] { LowLatencySetting.Reflex, LowLatencySetting.ReflexBoost });", caps)
        self.assertIn("else if (selected == FrameGenerationSetting.XeSS) allowed.Add(LowLatencySetting.XeLL);", caps)
        self.assertIn("allowed.Add(LowLatencySetting.Off);", caps)
        self.assertIn("latency.SetInteractabilityFunction((Func<bool>)(() => caps.AllowedLatencies(Selected).Length > 1));", rows)
        self.assertIn("select_latency", policy)
        self.assertIn("if(output==Output::XeSS) return c.xell?Latency::XeLL:Latency::Off;", policy)

    def test_frame_generation_inputs(self):
        # Inputs come from the upscaler (DLSS) when it runs and otherwise from HDRP's own buffers.
        hdrp = self.text("src/TGFrameGeneration/HdrpInputs.cs")
        for token in ["CustomPassInjectionPoint.BeforePostProcess", "ctx.cameraDepthBuffer", "ctx.cameraMotionVectorsBuffer",
                      "hd.taaJitter.x", "hd.actualWidth", "FrameSettingsField.MotionVectors", "FrameBridge.GameplayFrame(hd.camera, out uint frameId)",
                      "IssuePluginEventAndData(bridge.RenderEvent, CaptureEvent", "NativeTextures.Pointer(ctx.cameraDepthBuffer)"]:
            self.assertIn(token, hdrp)
        self.assertIn("private const int CaptureEvent = 3;", hdrp)
        control = self.text(FG + "src/control.cpp")
        self.assertIn("eventId==3 && haveRenderFrame && renderFrameId==id && frame->renderBegun && !frame->renderEnded && frame->hdrpPending", control)
        self.assertIn("frame->evalCount?frame->evalCount:frame->hdrpCaptured?1u:0u", control)
        capture = self.text(FG + "src/ngx_capture.cpp")
        self.assertIn("constants.jitterOffset={-in.jitterX,-in.jitterY};constants.mvecScale={-1.f,-1.f};", capture)
        self.assertIn("if(s.upscalerSeen && frame.id-s.lastUpscalerFrame<=1u)", capture)
        self.assertIn("s.lastUpscalerFrame=renderFrameId;s.upscalerSeen=true;", capture)
        rows = self.text("src/TGFrameGeneration/NativeVideoSettings.cs")
        self.assertIn("HdrpInputs.Available || upscaling == null || upscaling.IsDLSSEnabled", rows)
        self.assertIn("upscaling.UpscalingTypeOption.onChange += (Action<ToggleOption>)(_ => RefreshInteractability());", rows)
        self.assertIn("InputSource = HdrpInputs.Available ||", self.text("src/TGFrameGeneration/VideoCapabilities.cs"))

    def test_motion_vector_fixes_are_shared(self):
        # Both mods read the game's motion vectors; the bridge corrects them while either asks.
        self.assertIn("MotionVectors.Request(Plugin.Name, selected.Backend != FrameGenerationSetting.Off);", self.text("src/TGFrameGeneration/Session.cs"))
        self.assertIn("MotionVectors.Request(UpscalingPlugin.Name, StpHost.Active", self.text("src/TGBetterUpscaling/Session.cs"))
        for path in ["src/TGGraphicsBridge/FirstPersonMotion.cs", "src/TGGraphicsBridge/KandraMotionHistory.cs"]:
            self.assertIn("MotionVectors.Requested", self.text(path))
            self.assertNotIn("FrameGenerationSetting", self.text(path))
        self.assertFalse((ROOT / "src/TGFrameGeneration/KandraMotionHistory.cs").exists())

    def test_il2cpp_ports(self):
        rows = self.text("src/TGFrameGeneration/NativeVideoSettings.cs")
        hdrp = self.text("src/TGFrameGeneration/HdrpInputs.cs")
        switch = self.text("src/TGGraphicsBridge/SwitchRowsOption.cs")
        tab = self.text("src/TGGraphicsBridge/VideoTab.cs")
        for source in (rows, hdrp):
            self.assertNotIn("#if BEPINEX_MONO", source)
            self.assertNotIn("only implemented for the Mono runtime", source)
        self.assertIn('Il2CppTypes.Construct(this, typeof(Setting), "NativeMethodInfoPtr__ctor_Protected_Void_0");', rows)
        self.assertIn('Il2CppTypes.Construct(this, typeof(DependentOption), "NativeMethodInfoPtr__ctor_Public_Void_PrefOption_Il2CppReferenceArray_1_PrefOption_0",', switch)
        self.assertIn("Il2CppOptionConditions.Register(this, rows.Show);", switch)
        self.assertIn("Il2CppTypes.Register<SwitchRowsOption>();", rows)
        self.assertIn("Il2CppTypes.Register<SwitchRowsOption>();", self.text("src/TGBetterUpscaling/UpscalerRow.cs"))
        # Tooltips must not return managed strings through Il2CppInterop's delegate bridge (game crash in String.Trim).
        self.assertIn("new Func<IntPtr>(() => IL2CPP.ManagedStringToIl2Cpp(text()))", tab)
        for source in (rows, tab, self.text("src/TGBetterUpscaling/UpscalerRow.cs")):
            self.assertNotIn(".AddTooltip((Func<string>)", source)
        self.assertIn("Il2CppTypes.Register<FrameGenerationVideoSetting>(genericBase: true);", rows)
        self.assertIn('AccessTools.Method(typeof(ClassInjector), "SystemTypeFromIl2CppType")', self.text("src/TGGraphicsBridge/Il2CppTypes.cs"))
        self.assertIn('Il2CppTypes.Construct(this, typeof(CustomPass), "NativeMethodInfoPtr__ctor_Protected_Void_0");', hdrp)
        self.assertIn("Il2CppTypes.Register<FrameInputPass>();", hdrp)
        self.assertIn("private new void Execute(CustomPassContext ctx) => HdrpInputs.Execute(ctx);", hdrp)
        # One shared condition table: a mod rebuilding its rows removes only its own entries.
        self.assertNotIn("Il2CppOptionConditions.Clear()", rows)
        self.assertIn("Il2CppOptionConditions.Remove(old)", rows)

    def test_plugin_identity(self):
        plugin = self.text("src/TGFrameGeneration/Plugin.cs")
        self.assertIn('Id = "com.wessberg.tgframegeneration"', plugin)
        self.assertIn('Name = "TG Frame Generation"', plugin)
        up = self.text("src/TGBetterUpscaling/Plugin.cs")
        self.assertIn('Id = "com.wessberg.tgbetterupscaling"', up)
        self.assertIn('Name = "TG Better Upscaling"', up)
        bridge = self.text("src/TGGraphicsBridge/Plugin.cs")
        self.assertIn('Id = "com.wessberg.tggraphicsbridge"', bridge)
        self.assertIn('Name = "TG Graphics Bridge"', bridge)
        for text in (plugin, up):
            self.assertIn("[BepInDependency(BridgePlugin.Id, BepInDependency.DependencyFlags.HardDependency)]", text)
            self.assertIn('Version = "1.2.0"', text)
            self.assertIn('BepInDependency("com.avalonmodmanager", BepInDependency.DependencyFlags.SoftDependency)', text)
            self.assertLess(text.index("ModManagers.KeepOutOfAvalon(Id, Logger);"), text.index("Session.Start(Config, Logger);"))
        self.assertIn("ModManagers.Tick();", bridge)

    def test_mod_manager_rows_match_video_tab(self):
        settings = self.text("src/TGFrameGeneration/Settings.cs")
        rows = self.text("src/TGFrameGeneration/NativeVideoSettings.cs")
        names = re.findall(r'Row\(\w+Key, [^,]+, "([^"]+)", (\d)', settings)
        self.assertEqual([n for n, _ in names], ["Frame Generation", "Frame Generation Mode", "Frame Multiplier",
                                                  "Max Frame Multiplier", "Target Framerate", "Low Latency"])
        self.assertEqual([int(o) for _, o in names], list(range(6)))
        for name, _ in names:
            self.assertIn('"' + name + '"', rows)
        self.assertEqual(len(re.findall(r"file\.Bind\(AdvancedSection", settings)), 11)
        self.assertEqual(len(re.findall(r"Hidden\(\n?\s*\"", settings)), 11)
        self.assertNotIn("VideoSettings.cfg\")", self.text("src/TGFrameGeneration/Diagnostics.cs"))
        attributes = self.text("src/TGGraphicsBridge/ConfigurationManagerAttributes.cs")
        self.assertIn("public string DisplayName, DispName, DisplaySection, Category, ChoiceLabels;", attributes)
        self.assertNotIn("class ConfigurationManagerAttributes", settings)

    def test_error_containment(self):
        # Findings of the final review: no C++ exception leaves a native entry point, a removed device isn't
        # mistaken for completion, unconfirmed GPU work keeps its resources, and a mod's start or settings
        # file can't take it down silently.
        control = self.text(FG + "src/control.cpp")
        self.assertIn("try { render_event_body(eventId,encodedId); } catch(...)", control)
        for name in ["Submit", "BeginSimulation", "EndSimulation", "SetCamera", "SetHdrpInputs", "SetHudless", "SetDebugFlags"]:
            self.assertIn("try { return " + name + "Body(", control)
        self.assertIn("try { render_event_body(eventId, data); }", self.text("native/upscaling/src/upscaler.cpp"))
        self.assertIn("try { sampler(); }", self.text(BR + "src/performance.cpp"))
        bridge = self.text(FG + "src/dx11_bridge.cpp")
        interop = self.text(BR + "include/tgb/d3d_interop.hpp")
        for code in (bridge, interop):
            self.assertIn("Interop fence woke before its value", code)
        self.assertIn("quarantine();", bridge.split("~Dx11Swapchain()", 1)[1].split("void quarantine()", 1)[0])
        self.assertIn("tgb::quarantine(", self.text("native/upscaling/src/upscaler.cpp"))
        for path in [BR + "src/core.cpp", "native/upscaling/src/extension.cpp", FG + "src/sl_runtime.cpp"]:
            log = self.text(path).split("void log(const std::string& message) {", 1)[1].split("\n}\n", 1)[0]
            self.assertIn("} catch(...) {}", log, path)
        for mod in ["TGFrameGeneration", "TGBetterUpscaling"]:
            self.assertIn("try { Session.Start(Config, Logger); }", self.text("src/" + mod + "/Plugin.cs"))
            session = self.text("src/" + mod + "/Session.cs")
            self.assertLess(session.index("SaveOnConfigSet"), session.index("new Settings(config)"))
            settings = self.text("src/" + mod + "/Settings.cs")
            self.assertEqual(settings.count("File.Save();"), 1)  # only the guarded fallback inside Save()
            self.assertIn("catch { try { File.Save(); } catch { } }", settings)
        trouble = self.text("src/TGGraphicsBridge/Troubleshooting.cs")
        start = trouble.split("private static void StartRun()", 1)[1].split("private static void ApplyStep()", 1)[0]
        self.assertLess(start.index("File.WriteAllText(Marker"), start.index("phase = Phase.Running"))
        self.assertIn("if (back) { try { if (File.Exists(Marker)) File.Delete(Marker); }", trouble)
        self.assertNotIn("if (InterruptedRun) File.Delete(Marker);", trouble)
        for path in ["src/TGFrameGeneration/DisplayMode.cs", "src/TGFrameGeneration/HybridVsync.cs"]:
            self.assertIn("bool videoApply = fromVideoApply;\n            fromVideoApply = false;", self.text(path))
        self.assertNotIn("if (failed || (index != UpscalerRow.FsrIndex", self.text("src/TGBetterUpscaling/StpHost.cs"))

    def test_ray_reconstruction_link(self):
        # TG Better Upscaling finds TG Path Tracing's RayReconstruction class by name; neither references the other.
        link = self.text("src/TGBetterUpscaling/PathTracingLink.cs")
        rr = self.text("src/TGPathTracing/RayReconstruction.cs")
        self.assertIn('Type.GetType("TGPathTracing.RayReconstruction, TGPathTracing", false)', link)
        self.assertIn("namespace TGPathTracing", rr)
        self.assertIn("public static class RayReconstruction", rr)
        self.assertIn("public static bool Wanted =>", rr)
        self.assertIn("public static bool TryGetGuides(int width, int height, out ulong diffuseAlbedo, out ulong specularAlbedo, out ulong normalRoughness)", rr)
        self.assertIn("public static void Ran(int frame)", rr)
        for member in ['GetProperty("Wanted"', 'GetMethod("TryGetGuides"', 'GetMethod("Ran"', "new object[] { (int)width, (int)height, 0ul, 0ul, 0ul }"]:
            self.assertIn(member, link)
        for project in ["src/TGBetterUpscaling/TGBetterUpscaling.csproj", "src/TGPathTracing/TGPathTracing.csproj"]:
            self.assertNotRegex(self.text(project), r"TGPathTracing\.csproj|TGBetterUpscaling\.csproj", project)
        # Ray Reconstruction runs on the bridge's D3D12 device (NGX's D3D11 path faults in 310.9.1), with NGX linked
        # statically and the NVIDIA-signed nvngx_dlssd.dll bundled.
        backend = self.text(UP + "src/rr_upscaler.cpp")
        self.assertIn("NGX_D3D12_CREATE_DLSSD_EXT(list, 1, 1, &feature_, params_, &create)", backend)
        self.assertIn("NGX_D3D12_EVALUATE_DLSSD_EXT(list, feature_, params_, &eval)", backend)
        self.assertNotIn("NGX_D3D11_", backend)
        self.assertIn("create.InRoughnessMode=NVSDK_NGX_DLSS_Roughness_Mode_Packed;", backend)
        self.assertIn("eval.pInWorldToViewMatrix=worldToView_; eval.pInViewToClipMatrix=viewToClip_;", backend)
        cmake = self.text("CMakeLists.txt")
        self.assertIn("native/upscaling/src/rr_upscaler.cpp", cmake)
        self.assertIn('"${DLSS_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib"', cmake)
        self.assertIn('"-DDLSS_ROOT=$DlssRoot"', self.text("Build-Native.ps1"))
        # TG Path Tracing writes the guides and hands its light out undenoised while Ray Reconstruction runs.
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("if((frame.flags&TGPT_FRAME_GUIDES) && work.inputs", tracer)
        self.assertIn("const bool denoise=!(f.options&(TGPT_OPTION_NO_DENOISER|TGPT_OPTION_RAY_RECONSTRUCTION)) && !rrg;", tracer)
        self.assertIn("if (Session.WantsRayReconstruction) { frame.Flags |= Tgpt.FrameGuides;", self.text("src/TGPathTracing/HdrpPasses.cs"))

    def test_path_tracing_abi(self):
        header = self.text(PT + "include/tgpt_api.h")
        managed = self.text("src/TGPathTracing/NativeTracer.cs")
        self.assertIn("#define TGPT_ABI 0x00010600u", header)
        self.assertIn("internal const uint Abi = 0x00010600;", managed)
        self.assertIn(f"Marshal.SizeOf(typeof(NativeGuides)) != {dict(re.findall(r'sizeof\((TGPT_\w+)\)==(\d+)', header))['TGPT_Guides']}", managed)
        sizes = dict(re.findall(r"sizeof\((TGPT_\w+)\)==(\d+)", header))
        mirrored = {"TGPT_Submesh": "NativeSubmesh", "TGPT_MeshDesc": "NativeMeshDesc", "TGPT_CpuMeshDesc": "NativeCpuMeshDesc",
                    "TGPT_Material": "NativeMaterial", "TGPT_Instance": "NativeInstance", "TGPT_Light": "NativeLight"}
        for native, csharp in mirrored.items():
            self.assertIn(f"Marshal.SizeOf(typeof({csharp})) != {sizes[native]}", managed, native)
        self.assertIn(f"sizeof(NativeFrame) != {sizes['TGPT_Frame']}", managed)
        self.assertIn(f"Marshal.SizeOf(typeof(NativeStatus)) != {sizes['TGPT_Status']}", managed)
        self.assertIn(f"Size = {sizes['TGPT_Frame']}, Abi = Tgpt.Abi", self.text("src/TGPathTracing/HdrpPasses.cs"))
        self.assertIn(f"Size = {sizes['TGPT_Status']}, Abi = Tgpt.Abi", managed)
        for symbol in re.findall(r"TGPT_CALL\s+(TGPT_\w+)\(", header):
            self.assertIn(symbol + "(", self.text(PT + "src/extension.cpp"), symbol)
            if symbol != "TGPT_GetAbiVersion":
                self.assertIn('"' + symbol + '"', managed, symbol)
        # The shaders' constant layout is kept in step by hand: the C++ struct's size is asserted.
        self.assertIn('static_assert(sizeof(FrameConstants)==544, "frame constants layout");', self.text(PT + "src/tracer.cpp"))
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertEqual(common.count("float4x4 "), 3 + 0)
        # 3 matrices, then 21 float4/uint4 rows: 3*64 + 21*16 = 528.
        block = re.search(r"struct FrameConstants \{(.*?)\};", common, re.S).group(1)
        self.assertEqual(len(re.findall(r"^\s*(?:float4|uint4) \w+;", block, re.M)), 22)
        self.assertIn("uint4 grid;", common)

    def test_path_tracing_is_mono_only_and_packaged(self):
        helpers = self.text("tools/Package.Common.ps1")
        self.assertIn("function Get-TgModNames { @('TGFrameGeneration','TGBetterUpscaling','TGPathTracing') }", helpers)
        self.assertIn("Runtimes=@('Mono')", helpers)
        self.assertIn("TGPathTracing='TGPathTracing/TGPathTracing.Native.dll'", helpers)
        self.assertIn("<TargetFrameworks>net472</TargetFrameworks>", self.text("src/TGPathTracing/TGPathTracing.csproj"))
        for script in ["Build.ps1", "Package.ps1"]:
            self.assertIn("Test-TgModRuntime", self.text(script), script)
        # No d3d12.lib: the tracer only uses the bridge's device, so its DLL never loads d3d12.dll itself.
        cmake = self.text("CMakeLists.txt")
        line = re.search(r"target_link_libraries\(TGPathTracing\.Native PRIVATE ([^)]*)\)", cmake).group(1)
        self.assertNotIn("d3d12", line.split())

    def test_path_tracing_dxr12(self):
        # DirectX Raytracing 1.2 only where the device has Shader Model 6.9 and tier 1.2: then every pass is the
        # 6.9 build (a 6.6 ray query must never meet an opacity micromap), a refused 6.9 set falls back to 6.6
        # without micromaps, and Shader Execution Reordering only where the GPU says it reorders.
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("o5.RaytracingTier>=D3D12_RAYTRACING_TIER_1_2", tracer)
        self.assertIn("sm69.HighestShaderModel>=D3D_SHADER_MODEL_6_9", tracer)
        self.assertIn("st.scene.ommEnabled=st.useDxr12;", tracer)
        self.assertIn("if(st.useDxr12 && st.serReorders) {", tracer)
        for name in ["prepare", "gi", "giSpatial", "di", "diSpatial", "specular", "temporal", "atrous", "compose"]:
            self.assertIn(f"st.{name}=make_pso(shaders::{name}Dxr12, sizeof(shaders::{name}Dxr12));", tracer, name)
            self.assertIn(f"st.{name}=make_pso(shaders::{name}, sizeof(shaders::{name}));", tracer, name)
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("#define PT_RAYQUERY(flags) RayQuery<flags, RAYQUERY_FLAG_ALLOW_OPACITY_MICROMAPS>", common)
        for shader in ["pt_common.hlsli", "pt_compose.hlsl"]:
            self.assertNotRegex(self.text(PT + "shaders/" + shader), r"RayQuery<RAY_FLAG", shader)
        cmake = self.text("CMakeLists.txt")
        self.assertIn("-T cs_6_9 -E main -D PT_DXR12", cmake)
        self.assertIn("-T lib_6_9 -D PT_DXR12 -D PT_SER", cmake)
        # Micromaps: made for the first alpha-tested instance's materials once their atlas tiles are baked; an
        # instance whose materials differ, or a frame that turns them off, traces with them disabled.
        scene = self.text(PT + "src/scene.cpp")
        self.assertIn("const bool omm=linked && alpha && ommAllowed && omm_matches(*m, inst.materialBase);", scene)
        self.assertIn("D3D12_RAYTRACING_INSTANCE_FLAG_DISABLE_OMMS", scene)
        self.assertIn("D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_DISABLE_OMMS", scene)
        # Micromaps wait for the tile the alpha test reads: the 256x256 alpha tile when the material has one.
        self.assertIn("const int32_t tile=alphaTile ? mat.alphaTile : mat.albedoTile;", scene)
        self.assertIn("(size_t(tile)>=ready.size() || !ready[size_t(tile)])", scene)
        self.assertIn("st.scene.tile_settled(b.tile);", tracer)
        self.assertIn("st.scene.ommAllowed=!(f.options&TGPT_OPTION_NO_MICROMAPS);", tracer)
        bake = self.text(PT + "shaders/pt_omm_bake.hlsl")
        self.assertIn("OMM_SPECIAL_FULLY_TRANSPARENT = -1, OMM_SPECIAL_FULLY_OPAQUE = -2", bake)
        self.assertIn("if (m.albedoTile < 0) {", bake)
        # TG Path Tracing ships Microsoft's Agility SDK runtime beside its native DLL, where the bridge looks.
        helpers = self.text("tools/Package.Common.ps1")
        self.assertIn("@{Folder='D3D12'; Signer='Microsoft Corporation'", helpers)
        self.assertIn("Files=@('D3D12Core.dll'); Notices=@('LICENSE-DirectX-Agility-SDK.txt')", helpers)
        self.assertIn("$vendors+=@{Folder='TGPathTracing\\D3D12'; Signer='Microsoft Corporation'", self.text("Stage-Runtime.ps1"))

    def test_path_tracing_gpu_lifetimes(self):
        # Descriptor slots, the atlas and sky, a start that fails part-way, and a list that is never submitted:
        # the GPU may still be reading, or the bookkeeping may describe work that never ran.
        gpu = self.text(PT + "src/gpu.hpp")
        self.assertIn("void release(uint32_t index) { if(index!=kInvalid) retiring_.emplace_back(retireAt_, index); }", gpu)
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertLess(tracer.index("st.heap.set_retire(st.consumer.value+1);"), tracer.index("st.scene.intake(&st.heap"))
        self.assertIn("if(!bakes.empty()) wait_for_scene_readers();", tracer)
        sky = tracer[tracer.index("void update_sky()"):]
        self.assertLess(sky.index("wait_for_scene_readers();"), sky.index("run_cs("))
        self.assertIn("if(st.d3d12Ready) return true;", tracer)
        self.assertIn("if(st.d11 && generation==st.generation && st.d3d11Ready) return true;", tracer)
        self.assertEqual(tracer.count("} catch(...) { release_all(); throw; }"), 2)
        self.assertLess(tracer.index("st.recording=true;"), tracer.index("st.scene.record(st.d12.Get()"))
        self.assertIn("st.recording=false; st.broken=true;", tracer)
        # Last frame's GI path vertices move with the scene origin, and a scene reset forgets the terrain material.
        self.assertIn("if (gi_valid(rp) && !gi_sky(rp)) rp.pos += g.prevOriginDelta.xyz;", self.text(PT + "shaders/pt_gi.hlsl"))
        reset = self.text("src/TGPathTracing/SceneCapture.cs")
        reset = reset[reset.index("internal static void Reset()"):]
        self.assertLess(reset.index("terrainMaterial = uint.MaxValue;"), reset.index("}"))

    def test_mods_share_ngx_and_direct3d12_compatibly(self):
        # Without TG Path Tracing nothing about NGX changes: TG Better Upscaling starts it for Ray Reconstruction
        # only when TG Path Tracing's DLL runs (the bridge loads every extension before the device exists), and
        # its status line only then mentions Ray Reconstruction.
        upscaler = self.text("native/upscaling/src/upscaler.cpp")
        self.assertIn('const bool pathTracing=GetModuleHandleW(L"TGPathTracing.Native.dll")!=nullptr;', upscaler)
        # And only once TG Path Tracing asks (a render event), never at start-up.
        self.assertIn("st.rrPending=vendor==0x10DE && pathTracing;", upscaler)
        self.assertEqual(upscaler.count("rr_probe("), 1)
        self.assertLess(upscaler.index("void prepare_ray_reconstruction()"), upscaler.index("rr_probe("))
        self.assertIn("if(eventId==TGUP_EVENT_PREPARE_RAY_RECONSTRUCTION) { prepare_ray_reconstruction(); return; }", upscaler)
        session = self.text("src/TGBetterUpscaling/Session.cs")
        self.assertIn("bool pathTracing = PathTracingLink.Wanted;", session)
        self.assertIn("if (pathTracing && !rrPrepared", session)
        self.assertIn("if (rrReapply && UpscalerRow.Reapply()) rrReapply = false;", session)
        # RenoDX's DLSS 5 needs the game's own DLSS at every hook stage: Denoising never starts on Ray Reconstruction
        # while it is loaded; picked by the player, Ray Reconstruction runs.
        self.assertIn('GetModuleHandleW("renodx-dlss.addon64")', self.text("src/TGPathTracing/RenoDxDlss.cs"))
        pt = self.text("src/TGPathTracing/Session.cs")
        self.assertIn("Settings.CurrentDenoiser == DenoiserChoice.RayReconstruction;", pt)
        # Ray Reconstruction is offered and is the default with RenoDX's DLSS 5 loaded too: the two work together.
        self.assertNotIn("RenoDxDlss.Loaded", pt)
        self.assertIn("internal static DenoiserChoice BestDenoiser => Settings.Best(OfferedDenoisers);", pt)
        # Indirect Color scales the bounced light's color around its brightness, where it is shaded.
        self.assertIn("result = l + (result - l) * g.prevOriginDelta.w;", self.text(PT + "shaders/pt_gi_spatial.hlsl"))
        self.assertIn("k.prevOriginDelta[3]=std::clamp(f.indirectColor, 0.f, 4.f);", self.text(PT + "src/tracer.cpp"))
        self.assertIn("IndirectColor = settings.IndirectColor.Value", self.text("src/TGPathTracing/HdrpPasses.cs"))
        # A refused frame hands TG Path Tracing its own denoiser back; a feature created in a list that never ran is
        # created again; refusals count against successes.
        self.assertIn("if (rr && Session.RayReconstructionSucceeding()) PathTracingLink.Ran(Time.frameCount);", self.text("src/TGBetterUpscaling/StpHost.cs"))
        self.assertIn("if(!recorded) backend->discarded();", upscaler)
        self.assertIn("if(createdInList_ && feature_) { NVSDK_NGX_D3D12_ReleaseFeature(feature_); feature_=nullptr; }", self.text("native/upscaling/src/rr_upscaler.cpp"))
        self.assertIn("if (g.size.w & (FLAG_RESET | FLAG_DENOISER_RESET)) == 0) {".replace("if (g", "if ((g"), self.text("native/pathtracing/shaders/pt_temporal.hlsl"))
        self.assertIn("(PathTracingLink.Found ? \", \" + Tech(NativeFlags.BackendRayReconstruction", self.text("src/TGBetterUpscaling/Session.cs"))
        # NGX keeps one session per device, Streamline's on the shared device: TG Frame Generation lists TG Better
        # Upscaling's DLSS folder after its own only when TG Path Tracing and that folder's nvngx_dlssd.dll exist.
        sl = self.text("native/framegen/src/sl_runtime.cpp")
        self.assertIn('bepinex/L"TGBetterUpscaling"/L"Native"/L"DLSS"', sl)
        self.assertIn('!std::filesystem::exists(dlss/L"nvngx_dlssd.dll",error) ||', sl)
        self.assertIn('!std::filesystem::exists(bepinex/L"TGPathTracing"/L"Native"/L"TGPathTracing.Native.dll",error)) return {};', sl)
        self.assertLess(sl.index("s.pluginPaths[0]=s.runtimeDir.c_str();"), sl.index("s.pluginPaths[1]=s.rayReconstructionDir.c_str();"))
        self.assertIn("pref.numPathsToPlugins=pluginPathCount;", sl)
        # An extension's Agility SDK runtime that Direct3D 12 failed on is not used again on the next start.
        agility = self.text("native/bridge/src/agility.cpp")
        self.assertIn('line=outcome("extension-failed-before"', agility)
        self.assertLess(agility.index('outcome("extension-failed-before"'), agility.index("*requested=newest;"))
        device = self.text("native/bridge/src/device.cpp")
        self.assertIn("note_agility_device(false);", device)
        self.assertIn("note_agility_device(true);", device)
        # The combinations run on this machine's GPU (skipped where the runtimes are missing).
        cmake = self.text("CMakeLists.txt")
        for name in ["tgup_gpu_smoke_with_pt", "tgup_gpu_smoke_all_mods", "tgup_gpu_smoke_fg_installed"]:
            self.assertIn("add_test(NAME " + name, cmake)

    def test_path_tracing_video_rows(self):
        # TG Path Tracing's options are Video settings rows, and FoA Mod Manager shows exactly them: the Denoiser
        # offers only the denoisers this GPU runs (NGX's report from TG Better Upscaling for Ray Reconstruction).
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertIn('DenoiserNames = { "None", "Built-in", "Ray Regeneration", "Ray Reconstruction" };', settings)
        self.assertIn("public enum DenoiserChoice { None, BuiltIn, RayRegeneration, RayReconstruction }", settings)
        # Until the player picks another, Denoising follows the best this PC runs; the old Denoiser key carries over.
        self.assertIn("bool keep = DenoisingChosen.Value && choices.Contains(Denoiser.Value);", settings)
        self.assertIn('var legacy = file.Bind(Section, "Denoiser", "", Hidden("Replaced by Denoising."));', settings)
        self.assertIn('DefaultValue?.SetValue(denoiser, denoiserChoices[(int)best]);', self.text("src/TGPathTracing/PathTracingRow.cs"))
        self.assertIn("File.Remove(Denoiser.Definition);", settings)
        self.assertIn("ModManagers.RequestFoARefresh();", settings)
        self.assertIn('ModeKey = file.Bind(Section, "Mode key", new KeyboardShortcut(KeyCode.Insert), Hidden(', settings)
        rows = self.text("src/TGPathTracing/PathTracingRow.cs")
        self.assertIn("internal sealed class PathTracingSetting : Setting", rows)
        self.assertIn("VideoTab.AddGraphicSetting(master, setting);", rows)
        self.assertIn("VideoTab.Forbid(denoiser,", rows)
        self.assertIn("if (open) return; // never change the list while the screen is open", rows)
        # Rows in FoA's order: the Video tab's nested rows follow the config's SortOrder.
        order = re.findall(r'Row\(order\+\+, "([^"]+)"', settings)
        self.assertEqual(order, ["Path Tracing", "Denoising", "Rays Per Pixel", "Bounces", "Indirect Color", "Reflections", "Spatial Samples",
                                 "Ray Distance", "Scene Radius", "Sky Intensity", "Reflection Boost", "Debug View"])
        self.assertIn("PathTracingRow.Tick(now);", self.text("src/TGPathTracing/Session.cs"))
        self.assertIn('public static void Report(bool available)', self.text("src/TGPathTracing/RayReconstruction.cs"))
        self.assertIn('report = type.GetMethod("Report", BindingFlags.Public | BindingFlags.Static);', self.text("src/TGBetterUpscaling/PathTracingLink.cs"))
        # Frame generation resumes only once the cursor has stayed locked (mods fighting over it in a menu).
        self.assertIn("if(Settings.SuspendOnCursor.Value && (unlocked || now-cursorUnlockedAt<0.5f)) flags|=Suspend.CursorUnlocked;",
                      self.text("src/TGFrameGeneration/Session.cs"))

    def test_path_tracing_grain(self):
        # Grain and blur, measured in tgpt_gpu_test against an undenoised converged frame: ReSTIR's temporal neighbour
        # is permuted for every denoiser and it reuses a sample for about a frame (no option: longer reuse made noise
        # stand still); the direct light (sun and lamps) is a signal of its own with a short filter, apart from the
        # bounced light; the filters are steered by the accumulated value's noise (a single frame's, and a 3x3
        # spread, blurred sharp shadows away); ReSTIR DI reuses only lights a pixel sees.
        restir = self.text(PT + "shaders/pt_restir.hlsli")
        self.assertIn("    { uint h = pcg(g.size.z * 747796405u + 12345u); q ^= int2(h & 1u, (h >> 1) & 1u); }", restir)
        self.assertNotIn("(OPTION_RAY_RECONSTRUCTION | OPTION_RAY_REGENERATION)) { uint h", restir)
        spatial = self.text(PT + "shaders/pt_di_spatial.hlsl")
        self.assertIn("sun[p] = float4(sunDiffuse + clamp_radiance(localDiffuse, LIMIT_DIRECT), 1.0);", spatial)
        self.assertNotIn("g.signals.y", spatial) # the bounced light's signal is not written here
        self.assertIn("float3 diffuse = diffuseFinal[p].rgb + sun[p].rgb; // the direct light, denoised on its own", self.text(PT + "shaders/pt_compose.hlsl"))
        temporal = self.text(PT + "shaders/pt_temporal.hlsl")
        self.assertIn("variance /= len;", temporal)
        self.assertNotIn("spatial.y - spatial.x * spatial.x", temporal)
        di = self.text(PT + "shaders/pt_di.hlsl")
        self.assertIn("if (occluded_from_surface(origin, dir, 0.0, max(tmax - offset, 0.0), near_back(s.distance))) r.W = 0.0;", di)
        # Lamp shadows: each light's own size (no 10 cm minimum), candidates without repeats.
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertNotIn("SHADOW_LIGHT_RADIUS", common)
        self.assertIn("float2 disk = disk_sample(u) * light.radius;", common)
        self.assertIn("uint i = cell_candidate(first, cellCount, candidates, c, start);", di)
        self.assertIn("LightRecord light = load_light(cell_candidate(first, count, candidates, c, start));", common)
        # Rays from the G-buffer's surface pass out through a traced mesh they start inside (a back face close by);
        # paths from traced hits use the plain functions.
        self.assertIn("bool passes_through(uint instanceId, uint geometry, bool frontFace, float t, float nearBack) {", common)
        # The material decides whether a back face is a real hit, not the object: hair made a head's inside stop the sun.
        self.assertIn("return (materials[materialIndices[inst.materialBase + geometry]].flags & MATERIAL_ALPHA_TEST) == 0;", common)
        self.assertNotIn("INSTANCE_ALPHA_TEST", common)
        self.assertIn('"rays starting inside an object with alpha-tested parts pass out through its opaque faces"', self.text("tests/pathtracing_gpu_test.cpp"))
        for name, call in (("pt_gi.hlsl", "trace_from_surface(origin, dir, 0.0, g.params.y, near_back(s.distance), hit)"),
                           ("pt_specular.hlsl", "trace_from_surface(origin, l, 0.0, g.params.y, near_back(s.distance), hit)"),
                           ("pt_di_spatial.hlsl", "occluded_from_surface(sunOrigin, l, 0.0, g.params.y, nearBack)"),
                           ("pt_gi_spatial.hlsl", "occluded_from_surface(origin, dir, 0.0, g.params.y, near_back(s.distance))")):
            self.assertIn(call, self.text(PT + "shaders/" + name), name)
        self.assertIn('"rays starting inside the traced mesh pass out through it"', self.text("tests/pathtracing_gpu_test.cpp"))
        # Shadow-only meshes are stand-ins rays from a drawn surface pass through close by; translucent surfaces
        # (HDRP's transmission) are lit through from behind; characters carry their texture coordinates.
        self.assertIn("if (inst.flags & INSTANCE_STAND_IN) return true;", common)
        self.assertIn("#define TGPT_INSTANCE_STAND_IN 4u", self.text(PT + "include/tgpt_api.h"))
        self.assertIn("AddMesh(standIn.sharedMesh, renderer.sharedMaterials, renderer.localToWorldMatrix, Tgpt.InstanceStandIn)", self.text("src/TGPathTracing/SceneCapture.cs"))
        self.assertIn("bool translucent = feature == 2u || feature == 3u;", self.text(PT + "shaders/pt_prepare.hlsl"))
        self.assertIn("float nl = translucent_cos(s.normal, l, s.translucent);", self.text(PT + "shaders/pt_restir.hlsli"))
        self.assertIn("character.Handle = Session.Tracer.RegisterSkinnedMesh(key, reads.Indices, vertexCount, reads.SubmeshStarts, reads.Uvs);", self.text("src/TGPathTracing/CharacterCapture.cs"))
        for check in ('"a translucent surface (grass, leaves) is lit through from behind"', '"a character\'s cut-outs let light through, texel by texel"',
                      '"rays from a drawn surface pass through a stand-in close by, whichever way it faces"'):
            self.assertIn(check, self.text("tests/pathtracing_gpu_test.cpp"))
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("const uint32_t atrousIterations=denoise ? 5 : 0;", tracer)
        self.assertIn("k.denoise1[0]=denoise ? st.directOut.uav : st.sunRaw.uav;", tracer)
        self.assertIn("dispatch(list, st.atrous.Get(), {st.directTmp.uav, st.directOut.uav, 2u, 0u, st.directHist[cur].uav}, b);", tracer)
        self.assertIn("dispatch(list, st.compose.Get(), {rrg ? 1u : 0u}, b);", tracer)
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertIn('var legacy = file.Bind(Section, "Temporal history", 0, Hidden("No longer used."));', settings)
        self.assertNotIn("TemporalHistory =", settings)
        self.assertIn("TemporalCap = 1, // ReSTIR reuses a sample for about a frame", self.text("src/TGPathTracing/HdrpPasses.cs"))
        test = self.text("tests/pathtracing_gpu_test.cpp")
        self.assertIn('{"history 1, still", 1, false, false, false, false, 0.06}', test)
        self.assertIn("run(TGPT_VIEW_NONE, prev, prev, index++, TGPT_OPTION_NO_DENOISER);", test)
        # Ray Reconstruction counts as succeeding once no failure is recent: a start of int.MinValue overflowed.
        session = self.text("src/TGBetterUpscaling/Session.cs")
        self.assertIn("private static int rrFailedFrame = -1000;", session)
        self.assertNotIn("rrFailedFrame = int.MinValue", session)
        self.assertIn("params_->Set(mode, unsigned(NVSDK_NGX_RayReconstruction_Hint_Render_Preset_F));", self.text("native/upscaling/src/rr_upscaler.cpp"))
        # HDRP's indirect diffuse light is off while tracing: the composite keeps GBuffer3 (where HDRP bakes it) as
        # emission. AO, contact shadows and SSR go too, and their Video rows grey out with Shadows and SSS.
        passes = self.text("src/TGPathTracing/HdrpPasses.cs")
        self.assertIn("profile.Add<IndirectLightingController>(true).indirectDiffuseLightingMultiplier.Override(0f);", passes)
        self.assertNotIn("noIndirect", passes)
        self.assertIn("profile.Add<ScreenSpaceReflection>(true).enabled.Override(false);", passes)
        rows = self.text("src/TGPathTracing/ReplacedRows.cs")
        for row in ['("Shadows", new[] { "_shadowsToggle", "_shadowsDistance" }', '("Shadows", new[] { "_contactShadowsToggle" }',
                    '("SSAO", new[] { "<Option>k__BackingField" }', '("Reflections", new[] { "<Option>k__BackingField" }', '("SSS", new[] { "_enableToggle", "_quality" }']:
            self.assertIn(row, rows)
        self.assertIn("entry.Wrapper = () => !replaced && (original == null || original());", rows)
        self.assertIn("ReplacedRows.Tick(now);", self.text("src/TGPathTracing/Session.cs"))

    def test_ray_reconstruction_pre_exposure(self):
        # Ray Reconstruction gets HDRP's pre-exposure as HDRP's own DLSS pass hands it (clamped GPU exposure): without it
        # NGX assumed 1 and every auto-exposure change looked like new lighting its history had to catch up with.
        self.assertIn("eval.InPreExposure=f.preExposure>0.f ? f.preExposure : 1.f;", self.text("native/upscaling/src/rr_upscaler.cpp"))
        self.assertIn("return float.IsNaN(value) || float.IsInfinity(value) || value <= 0f ? 1f : Mathf.Clamp(value, 0.2f, 2f);",
                      self.text("src/TGBetterUpscaling/StpHost.cs"))

    def test_path_tracing_shadow_terminator(self):
        # Opaque surfaces start shadow rays outside (only a translucent leaf switches to the light's side), the light fades
        # towards the geometric terminator (Chiang et al. 2019), and near the surface the nearest hit decides whether a
        # ray is passed through (with the first hit accepted a nearer real blocker could be skipped).
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("return position + geoNormal * (!translucent || dot(geoNormal, dir) >= 0.0 ? offset : -offset);", common)
        self.assertIn("if (s.translucent) return 1.0;", common)
        self.assertIn("return g * (1.0 + g * (1.0 - g));", common)
        self.assertIn("const float nearEnd = min(tmax, nearBack * 3.0);", common)
        self.assertIn("PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH) f;", common)
        self.assertNotIn("skin_shadow_origin", common)
        spatial = self.text(PT + "shaders/pt_di_spatial.hlsl")
        self.assertIn("float3 sunOrigin = shadow_origin(s.position, s.geoNormal, l, offset, s.translucent);", spatial)
        self.assertIn("sunDiffuse = g.sunColor.rgb * nl * terminator_softening(s, l) / PI;", spatial)
        self.assertIn("diffuse = e * nl * terminator_softening(s, l) / PI;", self.text(PT + "shaders/pt_restir.hlsli"))

    def test_path_tracing_exposure(self):
        # The radiance clamps are measured against HDRP's exposure averaged over ~10 s, not its current one (which follows
        # the view), and the composite puts the one-frame-old traced light into this frame's exposure.
        self.assertIn("float l = luminance(radiance) * (g.params.x > 0.0 ? g.params.x : exposure());", self.text(PT + "shaders/pt_restir.hlsli"))
        self.assertIn("k.params[0]=std::isfinite(f.clampExposure) && f.clampExposure>0.f ? f.clampExposure : 0.f;", self.text(PT + "src/tracer.cpp"))
        passes = self.text("src/TGPathTracing/HdrpPasses.cs")
        self.assertIn("private const float ClampExposureSeconds = 10f;", passes)
        self.assertIn("frame.ClampExposure = ClampExposure(pipeline, hd, (frame.Flags & Tgpt.FrameReset) != 0);", passes)
        d3d11 = self.text(PT + "shaders/d3d11.hlsl")
        self.assertIn("float ratio = (en > 0.0 && et > 0.0 && en < 1e20 && et < 1e20) ? en / et : 1.0;", d3d11)
        self.assertIn("in.exposure.t.srv.Get(), st.inputs[prev.parity].exposure.t.srv.Get()};", self.text(PT + "src/tracer.cpp"))
        # A pixel no previous traced texel matches takes the nearest traced light, never HDRP's own lighting (the fill lights).
        self.assertIn("if (best.x < 0 && tracedA.Load(int3(n, 0)).a > 0.0) best = n;", d3d11)

    def test_path_tracing_no_placed_lights(self):
        # No fake lights: only lights that stand for a flame, a lamp or an effect (by their own or a parent's name) are
        # traced; fill, fog and player lights are left out even inside a lamp's prefab.
        lights = self.text("src/TGPathTracing/SceneLights.cs")
        for token in ['private static readonly string[] PlacedNames = { "Ambien", "Fill", "Bounce", "Fake", "Indirect", "HeroLight", "Volumetric" };',
                      "for (int depth = 0; t != null && depth <= 5; depth++, t = t.parent)", "if (Names(t.name, SourceNames)) { placed = false; break; }",
                      "if (IsPlacedLight(light))"]:
            self.assertIn(token, lights)
        for source in ['"Fire"', '"Candle"', '"Candela"', '"Brazier"', '"Lamp"', '"Lantern"', '"Torch"', '"VFX"']:
            self.assertIn(source, lights)
        self.assertIn("SceneLights.IsPlacedLight(light)", self.text("src/TGPathTracing/Probe.cs"))
        # The player's own Conversation Lighting Overhaul portrait light stays.
        self.assertIn('private static readonly string[] KeptNames = { "TGConversationLightingOverhaul" };', lights)
        self.assertIn("if (Names(light.name, KeptNames)) placed = false;", lights)

    def test_path_tracing_entities_graphics(self):
        # The castle's walls are Entities Graphics entities without Drake, their meshes registered with its
        # BatchRendererGroup at runtime: resolved through EntitiesGraphicsSystem and traced. Shadow-only meshes (coarse
        # copies the game casts shadows with) are then left out; they stand in only while the entities can't be read.
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        for token in ["var graphics = world.GetExistingSystemManaged<EntitiesGraphicsSystem>();",
                      "if (!meshCache.TryGetValue(id, out mesh)) meshCache[id] = mesh = graphics.GetMesh(mmi.MeshID);",
                      "None = new[] { ComponentType.ReadOnly<DrakeMeshMaterialComponent>(), ComponentType.ReadOnly<DisableRendering>() }",
                      "standIns = !covered;", "if (!standIns) { ++objectsStandIn; continue; }",
                      "covered = (entityUnresolved + entityPending) * 10 <= renderers.Count + entityUnresolved;",
                      "for (int j = span.start; j < span.end; j++) added |= Add(pairs[j], v);",
                      "if (settings.ShadowCastingMode == ShadowCastingMode.ShadowsOnly) { drakeShadowOnly += chunk.Count; if (!standIns) return false; standIn = true; }",
                      "if (settings.ShadowCastingMode == ShadowCastingMode.ShadowsOnly) { entityShadowOnly += chunk.Count; continue; }"]:
            self.assertIn(token, capture)

    def test_path_tracing_full_only(self):
        # Full path tracing or none: Global Illumination mode (HDRP's direct light with the traced bounced light added)
        # and its Indirect Intensity are gone from the rows, the ABI, the passes and the composite. A config set to it
        # becomes Full; the Indirect intensity entry leaves the config.
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertIn("public enum TracingMode { Off, Full }", settings)
        self.assertIn('internal static readonly string[] ModeNames = { "Off", "Full" };', settings)
        self.assertIn('bool wasGlobalIllumination = StoredValue(file, "Mode") == "Global Illumination";', settings)
        self.assertIn("if (wasGlobalIllumination) Mode.Value = ModeNames[(int)TracingMode.Full];", settings)
        self.assertIn('var legacy = file.Bind(Section, "Indirect intensity", 1f, Hidden("No longer used."));', settings)
        rows = self.text("src/TGPathTracing/PathTracingRow.cs")
        self.assertIn('mode = VideoTab.Enum(IdPrefix, "Mode", "Path Tracing", (int)TracingMode.Full, out modeChoices, Settings.ModeNames);', rows)
        for path in ["src/TGPathTracing/Settings.cs", "src/TGPathTracing/PathTracingRow.cs", "src/TGPathTracing/HdrpPasses.cs",
                     "src/TGPathTracing/NativeTracer.cs", "src/TGPathTracing/ReplacedRows.cs", "src/TGPathTracing/SceneLights.cs"]:
            code = self.text(path)
            for gone in ["TracingMode.GlobalIllumination", "ModeGi", "Intensity = settings.Intensity", "s.Intensity"]:
                self.assertNotIn(gone, code, path)
        for path in ["include/tgpt_api.h", "src/tracer.cpp", "src/extension.cpp", "src/ray_regeneration.cpp", "shaders/pt_common.hlsli",
                     "shaders/pt_compose.hlsl", "shaders/d3d11.hlsl", "shaders/pt_rrg_guides.hlsl"]:
            code = self.text(PT + path)
            for gone in ["TGPT_MODE_", "MODE_GI", "MODE_FULL", "f.mode", "frame.mode", "frame.intensity", "full_"]:
                self.assertNotIn(gone, code, path)
        self.assertNotIn("TGPT_MODE_", self.text("tests/pathtracing_gpu_test.cpp"))

    def test_path_tracing_ray_regeneration(self):
        # AMD FSR Ray Regeneration: AMD's signed DLLs from TG Path Tracing's own folder, probed on AMD GPUs only;
        # it replaces the built-in denoiser only when it runs, and a failure leaves the built-in one for the session.
        rrg = self.text(PT + "src/ray_regeneration.cpp")
        self.assertIn('host().load_signed(denoiser.c_str(), L"ADVANCED MICRO DEVICES")', rrg)
        self.assertIn("if(vendor!=0x1002)", rrg)
        self.assertIn("FFX_API_RESOURCE_STATE_UNORDERED_ACCESS", rrg)
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("!st.rrgFailed && rrg_available()", tracer)
        self.assertIn("k.settings2[1]=rrg ? f.options : (f.options&~TGPT_OPTION_RAY_REGENERATION);", tracer)
        # AMD's backend binds its own heap and root signature; the tracer's are bound again before compose.
        after = tracer[tracer.index("st.rrgActive=st.rrg.dispatch(list, r, error);"):]
        self.assertLess(after.index("list->SetDescriptorHeaps(1, heaps);"), after.index("dispatch(list, st.compose.Get()"))
        self.assertIn("list->SetComputeRootSignature(st.root.Get());", after)
        self.assertIn("dispatch(list, st.compose.Get(), {rrg ? 1u : 0u}, b);", tracer)
        # The reflections are divided by their albedo for the denoiser and multiplied back by compose alike.
        self.assertIn("specular_albedo(s.f0, s.roughness", self.text(PT + "shaders/pt_rrg_guides.hlsl"))
        self.assertIn("if (pass.a.x == 1u) specular *= specular_albedo(s.f0, s.roughness", self.text(PT + "shaders/pt_compose.hlsl"))
        session = self.text("src/TGPathTracing/Session.cs")
        self.assertIn("(Status.Features & Tgpt.FeatureRayRegeneration) != 0 &&\n            Settings.CurrentDenoiser == DenoiserChoice.RayRegeneration;", session)
        helpers = self.text("tools/Package.Common.ps1")
        self.assertIn("Files=@('amd_fidelityfx_loader_dx12.dll','amd_fidelityfx_denoiser_dx12.dll')", helpers)
        self.assertIn("Kits/FidelityFX/denoisers/include/ffx_denoiser.h", self.text("CMakeLists.txt"))

    def test_config_keys_are_valid_for_bepinex(self):
        # BepInEx's ConfigDefinition throws on these characters in a section or key, which stops the plugin
        # while it binds its settings, before it logs anything (an apostrophe once took TG Frame Generation out).
        bad = set('=\n\t\\"\'[]')
        pattern = re.compile(r'\.Bind\(\s*([A-Za-z_.]+|"[^"]*")\s*,\s*"((?:[^"\\]|\\.)*)"')
        found = 0
        for path in (ROOT / "src").rglob("*.cs"):
            for m in pattern.finditer(path.read_text(encoding="utf-8")):
                found += 1
                section, key = m.group(1), m.group(2)
                self.assertFalse(set(key) & bad, f"{path.name}: config key {key!r}")
                if section.startswith('"'):
                    self.assertFalse(set(section.strip('"')) & bad, f"{path.name}: config section {section}")
        self.assertGreater(found, 15)  # the pattern still finds the binds
        for path in (ROOT / "src").rglob("Settings.cs"):
            for name, value in re.findall(r'(\w+Section) = "([^"]*)"', path.read_text(encoding="utf-8")):
                self.assertFalse(set(value) & bad, f"{path.name}: {name}")

    def test_vsync_off_for_dlss_on_another_gpus_screen(self):
        # NVIDIA: VSync with DLSS-G without independent flip has high latency. Only the game's own toggle is
        # changed, with Target FPS set to Unlimited at that moment, only in that case, and it is greyed out, not fought.
        code = self.text("src/TGFrameGeneration/HybridVsync.cs")
        self.assertIn('Session.Settings.FrameGeneration != FrameGenerationSetting.DLSS', code)
        self.assertIn('GraphicsBridge.Fact("display.crossAdapter") != "1"', code)
        self.assertIn("vsync.Enabled = false;", code)
        self.assertIn("option.SetInteractabilityFunction((Func<bool>)(() => !holding));", code)
        self.assertNotIn("targetFrameRate", code)
        # Target FPS goes to Unlimited only where VSync is turned off, not held there.
        self.assertIn('private const string UnlimitedId = "RefreshRate_Unlimited";', code)
        self.assertLess(code.index("vsync.Enabled = false;"), code.index("resolution.RefreshRateOption.Option = unlimited;"))
        self.assertEqual(code.count("RefreshRateOption.Option ="), 1)
        session = self.text("src/TGFrameGeneration/Session.cs")
        for token in ["Settings.Changed+=HybridVsync.Arm;", "HybridVsync.Tick(now);", "HybridVsync.Release();"]:
            self.assertIn(token, session)
        self.assertIn("HybridVsync.ArmFromVideoApply();", self.text("src/TGFrameGeneration/NativeVideoSettings.cs"))
        # While VSync is held off the bridge presents without the tearing flag: a hybrid laptop that flips the
        # frames straight to its screen would otherwise tear.
        self.assertIn("try { Session.Bridge?.SetNoTearing(holding); } catch { }", code)
        self.assertIn("flags&~UINT(DXGI_PRESENT_ALLOW_TEARING)", self.text(FG + "src/dx11_bridge.cpp"))
        self.assertIn('setNoTearing=Bind<FrameCall>(module,"TGFG_SetNoTearing")', self.text("src/TGFrameGeneration/NativeBridge.cs"))
        # The display check runs again during play, and Streamline's cross-GPU copy time is logged.
        self.assertIn("std::thread(watch_display).detach();", self.text(BR + "src/diagnostics.cpp"))
        self.assertIn("crossAdapterCopyTimeUs", self.text(FG + "src/control.cpp"))

    def test_private_interposer_name(self):
        # Streamline-aware injectors hook a module named sl.interposer.dll; TGFG loads its own under a private name.
        sl = self.text(FG + "src/sl_runtime.cpp")
        self.assertIn('L"TGFG.Streamline.dll"', sl)
        self.assertIn('if(const HMODULE other=GetModuleHandleW(L"sl.interposer.dll")) {', sl)
        self.assertIn("'sl.interposer.dll'='TGFG.Streamline.dll'", self.text("Stage-Runtime.ps1"))
        packaging = self.text("tools/Package.Common.ps1")
        files = re.search(r"Files=@\('TGFG\.Streamline\.dll'[^)]*\)", packaging).group(0)
        self.assertNotIn("'sl.interposer.dll'", files)
        self.assertIn("'BepInEx/TGFrameGeneration/Native/Streamline/sl.interposer.dll'", packaging)  # obsolete, removed on deploy

    def test_direct_sdk_calls(self):
        code = "\n".join(self.text(FG + "src/" + p) for p in ["sl_runtime.cpp", "control.cpp", "dx11_bridge.cpp", "extension.cpp"])
        for name in ["slInit", "slSetD3DDevice", "slGetNewFrameToken", "slSetConstants", "slSetTagForFrame", "slDLSSGSetOptions", "slDLSSGGetState"]:
            self.assertIn(name, code)
        self.assertIn("sl::DLSSGMode::eDynamic", code)
        self.assertIn("dynamicTargetFrameRate", code)
        self.assertIn("generated_frames(s.request,s.status.maxGeneratedFrames)", code)
        self.assertIn("eBlockPresentingClientQueue", code)

    def test_api_separation(self):
        code = self.text(FG + "src/dx11_bridge.cpp")
        hooks = self.text(BR + "src/hooks.cpp")
        policy = self.text(FG + "include/tgfg_policy.hpp")
        self.assertIn("s.status.sourceApi=11;s.status.presentationApi=12", code)
        self.assertIn("clientFactory_->QueryInterface", code)
        self.assertIn("return d11_->QueryInterface(id,out)", code)
        self.assertIn("s.sl.createDevice", self.text(FG + "src/extension.cpp"))
        self.assertIn("s.sl.createFactory2", code)
        self.assertIn("D3D11CreateDeviceAndSwapChain", hooks)
        self.assertIn("sourceGraphicsApi!=11", policy.replace(" ", ""))

    def test_stable_client_buffer_zero(self):
        policy = self.text(FG + "include/tgfg_interop_policy.hpp")
        bridge = self.text(FG + "src/dx11_bridge.cpp")
        self.assertRegex(policy, r"virtual_buffer_count[^\{]+\{[^}]*return 1u;")
        self.assertRegex(policy, r"next_virtual_buffer[^\{]+\{\s*return 0u;")
        self.assertIn("output_->GetCurrentBackBufferIndex()", bridge)
        self.assertIn("return colors_[index].source.d11->QueryInterface", bridge)
        self.assertIn("DXGI_PRESENT_DO_NOT_WAIT", bridge)

    def test_interop_synchronization_order(self):
        code = self.text(FG + "src/dx11_bridge.cpp")
        present = code.split("HRESULT present(", 1)[1].split("HRESULT resize(", 1)[0]
        ordered = ["c11_->Signal(producer11_", "queue_->Wait(producer12_", "cmd->CopyResource(",
                   "queue_->ExecuteCommandLists(", "before_present(", "output_->Present(sync,presentFlags);", "after_present(hr);",
                   "queue_->Wait(s.lastInputsFence", "queue_->Signal(shared12_", "d11_wait(colorCopied_"]
        ordered[4:4] = ["queue_->Signal(shared12_.Get(),copied)", "colorCopied_.at(virtualIndex_)=copied;"]
        last = -1
        for token in ordered:
            current = present.find(token, last + 1)
            self.assertGreater(current, last, token)
            last = current
        # D3D11 never waits for frame generation itself: only for the copy of the buffer it reuses, and for
        # an input slot's retire value just before it rewrites that slot.
        self.assertNotIn("c11_->Wait(shared11_.Get(),lastRetired_)", present)
        self.assertIn("if(usesSlot) slotRetired_[slotIndex]=lastRetired_;", present)
        capture = code.split("bool capture(uint32_t id", 1)[1].split("void finish(", 1)[0]
        self.assertLess(capture.index("d11_wait(slotRetired_[id%inputs_.size()]"), capture.index("copy_input("))
        self.assertIn("check(c11_->Wait(shared11_.Get(),value),what);", code)
        # One signalling queue per fence: D3D11 only signals the producer fence, D3D12 only the consumer
        # fence. A fence Signal sets the value, so two signallers could move it backwards.
        self.assertNotIn("c11_->Signal(shared11_", code)
        self.assertNotIn("queue_->Signal(producer12_", code)
        self.assertNotIn("queue_->Wait(shared12_", code)
        self.assertIn("D3D11_RESOURCE_MISC_SHARED_NTHANDLE", code + self.text(BR + "include/tgb/d3d_interop.hpp"))
        self.assertIn("SwapDeviceContextState(game.Get(),nullptr)", code)
        self.assertEqual(code.count("D3D11_USAGE_STAGING"), 1)
        self.assertIn("staging.ByteWidth=16;staging.Usage=D3D11_USAGE_STAGING", code)
        self.assertIn("D3D11_MAP_FLAG_DO_NOT_WAIT", code)
        # The upscaler: D3D11 signals, D3D12 waits, works, always signals; the game's context waits on the GPU.
        up = self.text(UP + "src/upscaler.cpp")
        body = up.split("Outcome process_inputs(", 1)[1].split("void write_output(", 1)[0]
        last = -1
        for token in ["st.c11->Signal(st.fence.d11.Get(), produced)", "st.queue->Wait(st.fence.d12.Get(), produced)",
                      "backend->record(", "st.queue->Signal(st.fence.d12.Get(), retired)", "st.c11->Wait(st.fence.d11.Get(), retired)"]:
            current = body.find(token, last + 1)
            self.assertGreater(current, last, token)
            last = current
        # Nothing is read back to the CPU: that would stall every frame.
        self.assertNotIn("D3D11_USAGE_STAGING", up)
        self.assertNotIn("->Map(", up)

    def test_optional_hudless_is_not_fabricated(self):
        managed = self.text("src/TGFrameGeneration/FrameBridge.cs")
        self.assertNotIn("CaptureHudless", managed)
        self.assertNotIn("TGFG_TagHudless", managed)
        bridge = self.text(FG + "src/dx11_bridge.cpp")
        self.assertIn("hudless?&h:nullptr", bridge)
        self.assertIn("slot.hudlessValid && s.hudlessOk", bridge)
        self.assertIn("slot.hudlessValid=true", bridge)
        self.assertNotIn("colors_.at(virtualIndex_).source.d11.Get()", bridge)
        self.assertIn("frameId, void* context, void* colorResource", self.text(FG + "include/tgfg_api.h"))
        self.assertIn("TGFG_SetHudlessCapture", self.text(FG + "src/control.cpp"))
        self.assertIn("TGFG_SetHudlessCapture", self.text("src/TGFrameGeneration/NativeBridge.cs"))
        hudless = self.text("src/TGFrameGeneration/HudlessUi.cs")
        self.assertIn("CustomPassInjectionPoint.AfterPostProcess", hudless)
        self.assertIn("ctx.cameraColorBuffer", hudless)
        self.assertNotIn("rendersUIOverlay = false", hudless)
        self.assertIn("HUDLessColor=ffxApiGetResourceDX12", self.text(FG + "src/fsr_backend.cpp"))
        self.assertIn("options.enableUserInterfaceRecomposition=sl::eFalse;", self.text(FG + "src/control.cpp"))  # no UI buffer is tagged
        self.assertIn("XEFG_SWAPCHAIN_RES_HUDLESS_COLOR", self.text(FG + "src/xess_backend.cpp"))
        self.assertIn("TGFG_HUDCHECK", self.text(FG + "shaders/interop.hlsl"))
        self.assertIn("if(!moving) { ++s.diag.hudlessStill; return; }", self.text(FG + "src/control.cpp"))
        self.assertIn("create.maxRenderSize={maxRenderWidth_,maxRenderHeight_}", self.text(FG + "src/fsr_backend.cpp"))

    def test_hdr_conversion_is_actual_shader(self):
        shader = self.text(FG + "shaders/interop.hlsl")
        bridge = self.text(FG + "src/dx11_bridge.cpp")
        for token in ["bt2020*80.0", "2610.0/16384.0", "2523.0/32.0", "sourceTexture.Load"]:
            self.assertIn(token, shader)
        self.assertIn("encode_hdr(color.source.srv.Get(),color.encoded)", bridge)
        self.assertIn("SetColorSpace1(out)", bridge)
        self.assertIn("DXGI_FORMAT_R10G10B10A2_UNORM", bridge)

    def test_bridge_preloader_is_the_only_bootstrap(self):
        # No launcher: the preloader bootstraps synchronously, and the gameplay plugins only bind.
        self.assertFalse((ROOT / "native/launcher").exists())
        loader = self.text("src/TGGraphicsBridge.Autoload/Bootstrap.cs")
        self.assertIn("// Do NOT move this to ThreadPool/Task.Run or a plugin Awake() callback.", loader)
        self.assertLess(loader.index('Bind<AddExtension>("TGGraphicsBridge_AddExtension")'), loader.index('Bind<NativeBootstrap>("TGGraphicsBridge_Bootstrap")'))
        for path in ["src/TGFrameGeneration/NativeBridge.cs", "src/TGBetterUpscaling/NativeUpscaler.cs", "src/TGGraphicsBridge/GraphicsBridge.cs",
                     "src/TGPathTracing/NativeTracer.cs"]:
            text = self.text(path)
            self.assertNotIn("LoadLibrary", text, path)
            self.assertIn("GetModuleHandleW", text, path)

    def test_cmake_inputs_exist_and_shader_test_is_windows_only(self):
        cmake = self.text("CMakeLists.txt")
        self.assertIn("VERSION 1.2.0", cmake)
        for path in re.findall(r"(?:native|tests)/[\w/.-]+\.(?:cpp|hpp|hlsl|in)", cmake):
            self.assertTrue((ROOT / path).is_file(), path)
        self.assertNotIn("resource_tracker.cpp", cmake)
        self.assertIn("tgfg_shader_tests", cmake)
        self.assertIn("interop_shader.hpp.in", cmake)
        self.assertLess(cmake.index("if(TGFG_BUILD_NATIVE)"), cmake.index("tgfg_shader_tests"))
        # MinHook's own install rules stay out of the staged mods: one install rule per mod DLL.
        self.assertEqual(cmake.count("COMPONENT TGNative"), 4)
        self.assertIn("--component TGNative", self.text("Build-Native.ps1"))

    def test_installed_preflight(self):
        preflight = self.text("Test-Setup.ps1")
        helpers = self.text("tools/Package.Common.ps1")
        self.assertIn("Get-FileHash", helpers)
        self.assertIn('function Get-TgInstalledManifestPath([string]$Mod) { "BepInEx/$Mod/Install-manifest.json" }', helpers)
        self.assertIn("Get-TgInstalledManifestPath", preflight)
        self.assertIn("Test-TgPayload", preflight)
        self.assertNotRegex(preflight, r"Join-Path[^\n]*artifacts")
        self.assertNotIn("TGFG.Launcher.exe", preflight)
        self.assertFalse((ROOT / "Launch-Game.ps1").exists())
        # Vortex deploys hard links into its staging folder: deployment replaces files, never writes through.
        self.assertIn("function Copy-TgReplacing", helpers)
        self.assertNotIn("TGFG-PLAYER-INSTALL", helpers + self.text("Package.ps1"))

    def test_testing_limits_disclosed(self):
        readme = " ".join(self.text("README.md").replace("**", "").split())
        for token in ["not yet been confirmed in game", "IL2CPP build", "HDRP's own depth and motion vectors", "TG Graphics Bridge",
                      "TG Better Upscaling", "TG Frame Generation"]:
            self.assertIn(token, readme)
        self.assertNotIn("requires D3D12 game rendering", readme)
        self.assertNotIn("experimental", self.text("README.md").lower())
        self.assertFalse((ROOT / "docs/PLAYER-INSTALL.txt").exists())  # install instructions live on the Nexus pages

    def test_frame_generation_offers_only_what_was_reported(self):
        # Without a native report nothing is offered: no DLSS layout on a GPU that has no DLSS.
        caps = self.text("src/TGFrameGeneration/VideoCapabilities.cs")
        self.assertIn("if (!known) return;", caps)
        self.assertNotIn("maxMultiplier[(int)FrameGenerationSetting.DLSS] = 6", caps)
        rows = self.text("src/TGFrameGeneration/NativeVideoSettings.cs")
        self.assertIn("private bool Usable() => caps.Known &&", rows)
        tooltip = rows.split("private string FrameGenerationTooltip(", 1)[1]
        self.assertLess(tooltip.index("if (!caps.Known)"), tooltip.index("if (!caps.InputSource)"))
        self.assertIn('return Session.UnavailableTip();', tooltip)
        # The row's only choice then reads Off, greyed out; the bridge's alert icon and the tooltip say why.
        self.assertIn('caps.Known ? backends.Select(b => VideoCapabilities.Label(b)).ToArray() : new[] { "Off" }', rows)
        self.assertIn("RegisterAlerts(fg);", rows)
        # Without the native part no report ever comes: the row goes in at once, not after 20 s, so a player
        # who opens the settings right away still finds it with its alert.
        self.assertIn("if (caps == null && NativeRunning() && now - firstTick < 20f)", rows)
        # A problem is the tooltip, first and alone.
        self.assertLess(tooltip.index("string alert = Alert();"), tooltip.index("if (!caps.Known)"))
        # No rows of their own: problems go on the rows they concern.
        self.assertFalse((ROOT / "src/TGGraphicsBridge/ProblemRows.cs").exists())
        alerts = self.text("src/TGGraphicsBridge/RowAlerts.cs")
        self.assertIn('Row = VideoTab.Enum("TGGB.StandIn.", "FrameGeneration", "Frame Generation", 0, out _, "Off");', alerts)
        # The stand-in follows whether the mod started (it claims its row), not only whether it loaded.
        self.assertIn("bool wanted = !RowAlerts.Claimed(AlertRow.FrameGeneration)", alerts)
        self.assertIn("\"Didn't start\"", alerts)
        for mod, row in [("src/TGFrameGeneration/Session.cs", "FrameGeneration"), ("src/TGBetterUpscaling/Session.cs", "Upscaling")]:
            self.assertIn("RowAlerts.Claim(AlertRow." + row + ")", self.text(mod))
        # The report says what it tests from what is installed, not a fixed list.
        trouble = self.text("src/TGGraphicsBridge/Troubleshooting.cs")
        self.assertNotIn("about two minutes", trouble)
        self.assertIn('"each upscaler"', self.text("src/TGBetterUpscaling/UpscalingSweep.cs"))
        self.assertIn('"each frame generation and low latency setting"', self.text("src/TGFrameGeneration/FrameGenerationSweep.cs"))
        self.assertIn("group.ignoreParentGroups = true;", self.text("src/TGGraphicsBridge/AlertIcon.cs"))
        # A report that arrives after the rows were registered replaces them in place, with the screen closed.
        registered = rows.split("VideoTab.IndexOf(list, setting) >= 0)", 1)[1].split("if (open) return;", 1)[0]
        self.assertIn("if (!rows.Known && Session.Settings.Capabilities != null && !open)", registered)
        self.assertIn("slot.Set(rows);", registered)
        self.assertIn("public override IEnumerable<PrefOption> Options => slot.Rows.Top;", rows)
        self.assertIn("options = slot.Options.Cast<", rows)
        # Mod managers show no rows without a report, as the Video tab shows them greyed out.
        settings = self.text("src/TGFrameGeneration/Settings.cs")
        self.assertIn("bool usable = known && caps.InputSource;", settings)
        self.assertIn("rows[LowLatencyKey].Visible = known && caps.Latencies.Length > 1;", settings)
        session = self.text("src/TGFrameGeneration/Session.cs")
        self.assertIn("try { return Explain.Unavailable(); }", session)
        reason = self.text("src/TGFrameGeneration/Explain.cs").split("internal static Why UnavailableWhy()", 1)[1].split("private static Why D3D12(", 1)[0]
        for token in ["InstallCheck.ProblemFor(\"TGFrameGeneration\")", "GraphicsBridge.Loaded", "bridge.Bootstrapped != 1",
                      "GraphicsBridge.IsExtensionActive(Plugin.Name)", 'streamline.StartsWith("foreign", StringComparison.Ordinal)',
                      "bridge.D3D12DeviceReady == 0", "bridge.SwapchainsProvided == 0"]:
            self.assertIn(token, reason)
        # Upscaling says where HDRP runs STP, and so FSR and XeSS.
        self.assertIn("dynamicResolutionSettings.STPInjectionPoint", self.text("src/TGBetterUpscaling/StpHost.cs"))

    def test_frame_generation_on_the_bridge(self):
        extension = self.text(FG + "src/extension.cpp")
        # The vendor probes' own DXGI calls pass the bridge's hooks, as they did inside frame generation.
        self.assertIn("try { SLGuard inside; probe_vendor_backends(s.device.Get(),vendor); }", extension)
        # Capabilities count as known once the probes ran and the game's swapchain was replaced: the plugin
        # reads them once, and a refused swapchain can never engage them.
        self.assertNotIn("flags|=4u|8u", self.text(FG + "src/sl_runtime.cpp"))
        ready = extension.split("void d3d12_device_ready(", 1)[1].split("HRESULT create_swapchain(", 1)[0]
        self.assertLess(ready.index("probe_vendor_backends"), ready.index("s.probed=true;"))
        self.assertIn("if(s.status.bridgeFlags&1u) { offer_reflex(true); s.status.flags|=4u; }", ready)
        bridge = self.text(FG + "src/dx11_bridge.cpp")
        create = bridge.split("HRESULT try_create_dx11_bridge(", 1)[1]
        self.assertLess(create.index("s.status.reason=uint32_t(Reason::BridgeReady);"), create.index("if(s.probed) { offer_reflex(true); s.status.flags|=4u; }"))
        self.assertEqual(len(re.findall(r"status\.flags\|=[^;]*4u", bridge + extension + self.text(FG + "src/sl_runtime.cpp"))), 2)
        # A Streamline device failure is reported in hex, with the HRESULT in frame generation's status.
        self.assertIn("s.status.lastInteropHresult=FAILED(hr) ? hr : E_POINTER;", extension)
        self.assertNotIn("std::to_string(hr)", extension)
        device = self.text(BR + "src/device.cpp")
        # Device creation is retried at later swapchain creations, but not after a device on another adapter.
        self.assertNotIn("deviceAttempted", device + self.text(BR + "src/core.hpp"))
        self.assertIn("if(c.deviceRefused || c.deviceAttempts>=kDeviceAttempts) return false;", device)
        self.assertIn("constexpr int kDeviceAttempts=3;", device)
        luid = device.split("GetAdapterLuid();", 1)[1].split("}", 1)[0]
        self.assertIn("c.deviceRefused=true;", luid)
        # A bootstrap that failed partway replaces nothing.
        swapchain = device.split("HRESULT on_game_swapchain(", 1)[1]
        self.assertLess(swapchain.index("if(c.status.bootstrapped!=1 || !c.needs) return S_FALSE;"), swapchain.index("create_d3d12_device_locked();"))
        self.assertIn("HRESULT_FROM_WIN32(ERROR_HOOK_NOT_INSTALLED)", self.text(BR + "src/hooks.cpp"))
        # Unity's factory exists before the hooks: the swapchain methods are hooked on the bridge's own factory.
        # Hooked before anything else hooks the swapchain methods (the bridge's factory created past any hook
        # already on dxgi.dll's exports), so ReShade's hooks, installed later, run the bridge's inside their guard.
        prime = self.text(BR + "src/hooks.cpp").split("bool prime_swapchain_hooks()", 1)[1].split("bool install_graphics_hooks()", 1)[0]
        for token in ['ExportsAsOnDisk unhooked(dxgi, path, {"CreateDXGIFactory", "CreateDXGIFactory1", "CreateDXGIFactory2"});',
                      'GetProcAddress(dxgi, "CreateDXGIFactory1")', "hook_factory(own.Get());"]:
            self.assertIn(token, prime)
        self.assertNotIn("realFactory1", prime)
        boot = self.text(BR + "src/extensions.cpp")
        self.assertLess(boot.index("prime_swapchain_hooks()"), boot.index("load_extension(e);"))
        self.assertIn("--factory-first", self.text("CMakeLists.txt"))
        # The game names Agility SDK 610 and ships D3D12Core.dll 611: where Windows' own runtime is older
        # (Windows 10), every D3D12 device fails with 0x887E0003 unless the request names what the game ships.
        # d3d12.dll reads the request when it loads, so this comes before any extension loads its runtimes,
        # and none of the native DLLs imports d3d12.dll.
        self.assertLess(boot.index("log(align_agility_sdk(paths));"), boot.index("prime_swapchain_hooks()"))
        agility = self.text(BR + "src/agility.cpp")
        for token in ['GetProcAddress(exe, "D3D12SDKVersion")', 'GetProcAddress(exe, "D3D12SDKPath")',
                      "if(windows>=wanted)", "if(available==wanted)", 'GetModuleHandleW(L"d3d12.dll")', "*requested=available;"]:
            self.assertIn(token, agility)
        self.assertIn("native/bridge/src/agility.cpp", self.text("CMakeLists.txt"))
        # An extension's newer Agility SDK runtime (TG Path Tracing's, for Shader Model 6.9) becomes the request
        # only when it is retail, inside the game folder, Microsoft-signed, and d3d12.dll has not loaded yet.
        for token in ["if(version<600 || version>=700 || version<=newest) continue;",
                      "if(!newest || newest<=windows || newest<=wanted || newest<=otherwise) return false;",
                      "const uint32_t otherwise=windows>=wanted ? windows : (readable ? available : wanted);",
                      'if(part==L"..") inside=false;', 'trusted_signer((best/L"D3D12Core.dll").wstring(), L"MICROSOFT")',
                      "*requested=newest; *folder=path.c_str();"]:
            self.assertIn(token, agility)
        self.assertLess(agility.index("use_extension_runtime(extensions"), agility.index("if(windows>=wanted)"))
        # A failing step of the bridge plugin's start never stops the others, nor BepInEx loading the mods.
        plugin = self.text("src/TGGraphicsBridge/Plugin.cs")
        start = plugin.split("private static void Start()", 1)[1].split("private static void Step(", 1)[0]
        for step in ["ModManagers.KeepOutOfAvalon", "GraphicsBridge.Discover", "KandraMotionHistory.Initialize", "GraphicsBridge.Describe()"]:
            self.assertIn(step, start)
        self.assertEqual(start.count("Step("), 4)
        self.assertEqual(len(re.findall(r"^\s*(?:gameObject\.)?AddComponent<BridgeBehaviour>\(\);", plugin, re.M)), 0)
        self.assertEqual(plugin.count('Step("adding its per-frame component"'), 2)
        self.assertIn("try { step(); }", plugin)

if __name__ == "__main__":
    unittest.main(verbosity=2)
