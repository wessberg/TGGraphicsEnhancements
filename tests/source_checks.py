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
        self.assertIn("#define TGUP_ABI 0x00010400u", header)
        self.assertIn("internal const uint Abi = 0x00010400;", managed)
        self.assertIn('static_assert(sizeof(TGUP_Mode)==8, "TG Better Upscaling mode ABI");', header)
        self.assertIn("FrameSize = 280, StatusSize = 88, ModeSize = 8, DlssOverrideSize = 24;", managed)
        self.assertIn('static_assert(sizeof(TGUP_Frame)==280, "TG Better Upscaling frame ABI");', header)
        # Ray Reconstruction's optional inputs from TG Path Tracing (looked up by name: an older one has none), the
        # traced reflections' hit distance and the color before transparency, into NGX's evaluation.
        self.assertIn("uint64_t specularHitDistance, colorBeforeTransparency;", header)
        self.assertIn('extraGuides = type.GetMethod("TryGetExtraGuides", BindingFlags.Public | BindingFlags.Static); // nor this',
                      self.text("src/TGBetterUpscaling/PathTracingLink.cs"))
        # The optional inputs can never cost a frame: the hit distance is read as a single float channel (through the
        # color view it was refused, every frame, until Ray Reconstruction turned itself off), trouble drops them for
        # the frame, and NGX refusing them drops them for the session and evaluates again without them. The smoke test
        # hands them to Ray Reconstruction with every frame.
        upscaler = self.text("native/upscaling/src/upscaler.cpp")
        self.assertIn("{f.specularHitDistance, &st.guideHitDistance, DXGI_FORMAT_R16_FLOAT, tgb::ReadAs::Depth}, // one float channel", upscaler)
        self.assertIn('log_throttled(std::string("Ray Reconstruction\'s optional inputs from TG Path Tracing are left out: ")+e.what());', upscaler)
        self.assertIn("f.specularHitDistance=uint64_t(uintptr_t(hitDistance.Get())); f.colorBeforeTransparency=uint64_t(uintptr_t(color.Get()));",
                      self.text("tests/upscaling_gpu_smoke.cpp"))
        self.assertIn("WatchRayReconstruction(now);", self.text("src/TGPathTracing/Session.cs"))
        # The F8 capture scales the screen's own bytes down on the CPU: blitted into an sRGB render texture, a 4K screen's
        # images went through the sRGB curve twice (lifted shadows, washed out against the screen).
        capture = self.text("src/TGPathTracing/DebugCapture.cs")
        self.assertIn("var src = full.GetPixels32();", capture)
        self.assertNotIn("Graphics.Blit(", capture)
        # Only while frames are traced, shown and ask for its guides: it said so at the main menu, before a save was loaded;
        # and never with FSR or XeSS upscaling, where NRD serving is the row's own fallback.
        self.assertIn("if (!PathTracingRows.DlssSelected()) { rrMissingSince = now; return; }", self.text("src/TGPathTracing/Session.cs"))
        # NRD after its review: 2D motion (HDRP's previous view-projection takes positions relative to this frame's camera,
        # as Ray Regeneration's guides now use it), its own radiance units, nothing recorded for a frame it can't denoise,
        # the device ready only once its denoisers are probed, and the notices of what the DLL compiles in.
        prep = self.text(PT + "shaders/pt_nrd_prep.hlsl")
        self.assertIn("motion[p] = float4(-mv.Load(int3(p, 0)), 0.0, 0.0);", prep)
        self.assertIn("RELAX_FrontEnd_PackRadianceAndHitDist(b.rgb * asfloat(pass.b.x), b.a, true);", prep)
        self.assertIn("float prevZ = -mul(g.prevViewProj, float4(rel, 1.0)).w;", self.text(PT + "shaders/pt_rrg_guides.hlsl"))
        nrd = self.text(PT + "src/nrd_denoiser.cpp")
        self.assertIn("c.motionVectorScale[0]=c.motionVectorScale[1]=1.f; c.motionVectorScale[2]=0.f;", nrd)
        self.assertIn("std::vector<D3D12_GPU_VIRTUAL_ADDRESS> constantsOf(count, 0);", nrd)
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("const bool restart=!previous || !(ratio>0.25 && ratio<4.0);", tracer)
        self.assertIn("list->CopyResource(st.nrdDiffuseOut.resource.Get(), st.diffuseRaw.resource.Get());", tracer)
        self.assertIn("e.status.features|=denoisers;\n        e.status.deviceReady=1;", tracer.replace("\r\n", "\n"))
        self.assertIn("float fromNrd = pass.a.z == INVALID ? 1.0 : asfloat(pass.a.z);", self.text(PT + "shaders/pt_compose.hlsl"))
        cmake = self.text("CMakeLists.txt")
        self.assertIn("install(FILES \"${TG_RTXDI}/LICENSE.txt\" DESTINATION TGPathTracing/Notices RENAME LICENSE-NVIDIA-RTX-SDKs.txt COMPONENT TGNative)", cmake)
        self.assertIn("Notices=@('LICENSE-NVIDIA-RTX-SDKs.txt','LICENSE-MathLib.txt','LICENSE-ShaderMake.txt')})", self.text("tools/Package.Common.ps1"))
        self.assertIn('"NRD\'s error in the bounced light alone (under the ceiling) is within a quarter of the built-in denoiser\'s"', self.text("tests/pathtracing_gpu_test.cpp"))
        # Camera cuts start the histories over: another camera, HDRP's own reset as it rises, a jump or a sharp turn.
        self.assertIn("Vector3.Dot(forward, lastCameraForward) < 0.5f) frame.Flags |= Tgpt.FrameReset;", self.text("src/TGPathTracing/HdrpPasses.cs"))
        self.assertIn("bool tracing = WantsRayReconstruction && Time.frameCount - RayReconstruction.GuidesFrame <= 2 && compositedAt >= 0f && now - compositedAt < 1f;",
                      self.text("src/TGPathTracing/Session.cs"))
        rr = self.text("native/upscaling/src/rr_upscaler.cpp")
        self.assertIn("if(NVSDK_NGX_FAILED(r) && extras) {", rr)
        self.assertIn("eval.pInSpecularHitDistance=in.specularHitDistance;", rr)
        self.assertIn("eval.pInColorBeforeTransparency=in.colorBeforeTransparency; eval.pInColorAfterTransparency=in.color;", rr)
        self.assertIn("list->ResourceBarrier(UINT(readCount+1), barriers);", rr)
        self.assertNotIn("list->ResourceBarrier(7, barriers);", rr)
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
        # The row names FSR by the short name of the version that runs ("FSR 4", "FSR 3.1": AMD's major and minor number,
        # a .0 minor dropped), in the Video tab and FoA Mod Manager alike, the full version in the FSR rows' tooltips and
        # the log; the config keeps the plain name, so a stored choice stays valid on a GPU that runs another version.
        self.assertIn('return version != null ? "FSR " + version : upscaler.ToString();', settings)
        self.assertIn('return parts.Count > 1 && parts[1] != 0 ? parts[0] + "." + parts[1] : parts[0].ToString();', settings)
        self.assertIn("FsrVersion = Fsr ? Newest(FsrVersions) : null;", settings)
        self.assertIn('rows[UpscalerKey].ChoiceLabels = string.Join(";", upscalers.Select(u => u + "=" + label(u.ToString())).ToArray());', settings)
        self.assertIn("this.upscaler.Value = upscaler.ToString();", settings)
        self.assertIn("\" This GPU runs FSR \" + version + \", AMD's choice for it.\"", row)
        self.assertIn('fsrChoice = VideoTab.Choice("UpScalingType_FSR", Capabilities.Label(UpscalerSetting.FSR));', row)

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
        self.assertIn("const bool denoise=!(f.options&(TGPT_OPTION_NO_DENOISER|TGPT_OPTION_RAY_RECONSTRUCTION)) && !rrg && !useNrd;", tracer)
        self.assertIn("if (Session.WantsRayReconstruction) { frame.Flags |= Tgpt.FrameGuides;", self.text("src/TGPathTracing/HdrpPasses.cs"))

    def test_ggx_peak_uncapped(self):
        # The GGX distribution's peak isn't capped for smooth surfaces (an absolute 1e-7 in its denominator took up to
        # 99.9 % of a glossy floor's, a puddle's or polished metal's highlights of the sun and the lamps).
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("return a2 / max(PI * d * d, 1e-30); }", common)
        self.assertNotIn("PI * d * d + 1e-7", common)

    def test_acceleration_structures_created_in_their_state(self):
        # Default-heap buffers start in COMMON, except acceleration structures, which must be created in their own state
        # and never leave it (created in COMMON, the debug layer reported thousands of errors a harness run).
        gpu = self.text(PT + "src/gpu.hpp")
        self.assertIn("state!=D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE", gpu)
        scene = self.text(PT + "src/scene.cpp")
        self.assertIn("accelerationArena.initial=D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;", scene)

    def test_radiance_cache(self):
        # The radiance cache (pt_common.hlsli, pt_cache.hlsl): paths end on it at their last vertex, reflections and what
        # glass shows included; the paths feed it (the first ray of a quarter of the pixels, by whole 8x8 tiles); its
        # frame resolves it after every pass that reads or feeds it. The harness's furnace measured every bounce's light,
        # and no more, with one bounce ending on it (0.994 of the exact radiance).
        common = self.text(PT + "shaders/pt_common.hlsli")
        tracer = self.text(PT + "src/tracer.cpp")
        header = self.text(PT + "include/tgpt_api.h")
        slots = re.search(r"static const uint CACHE_SLOTS = 1u << (\d+);", common)
        self.assertIsNotNone(slots)
        self.assertIn("constexpr uint32_t kCacheSlots=1u<<%s;" % slots.group(1), tracer)
        self.assertIn("radiance += throughput * m.reflectance * cache_light(hit.position, hit.normal, rng);", common)
        self.assertIn("if (feed) cache_add(hit.position, hit.normal, clamp_radiance(cache_sample(hit, offset, rng), LIMIT_INDIRECT), rng);", common)
        self.assertIn("float3 shade_path(Hit hit, float3 dir, uint bounces, inout Rng rng) { return shade_path(hit, dir, bounces, rng, false); }", common)
        self.assertIn("bool cache_feeds(uint2 p, uint salt) { return cache_on() && ((pcg((p.x >> 3) + (p.y >> 3) * 4096u) + g.size.z + salt) & 3u) == 0u; }", common)
        gi = self.text(PT + "shaders/pt_gi.hlsl")
        self.assertIn("const bool feed = feeds && k == 0;", gi)
        self.assertIn("cache_add(s.position, s.geoNormal, radiance * (cs > 1e-3 ? min(saturate(dot(s.geoNormal, dir)) / cs, 4.0) : 0.0), rng);", gi)
        self.assertNotIn("if (feed && above) cache_add(", gi)
        # A cell's 512 samples of at most CACHE_SAMPLE_MOST in each channel fit its 32-bit sums, rounding up included.
        common_cache = self.text(PT + "shaders/pt_common.hlsli")
        scale = float(re.search(r"static const float CACHE_SCALE = ([0-9.]+);", common_cache).group(1))
        samples = int(re.search(r"static const uint CACHE_SAMPLES_PER_FRAME = ([0-9]+)u;", common_cache).group(1))
        most = float(re.search(r"static const float CACHE_SAMPLE_MOST = ([0-9.]+);", common_cache).group(1))
        self.assertLess((most * scale + 1.0) * samples, 2.0 ** 32)
        self.assertIn("v = min(v, CACHE_SAMPLE_MOST) * CACHE_SCALE;", common_cache)
        # The hair and eyes' feeder gathers as pt_gi's rays do, about the surface's own normal.
        hdrp = self.text(PT + "shaders/pt_hdrp_shadow.hlsl")
        self.assertIn("const float3 dir = cosine_sample(s.geoNormal, rng.next2());", hdrp)
        self.assertIn("g_pathFrom = s.position; g_pathGathers = true;", hdrp)
        self.assertIn("if (found) radiance = shade_path(hit, l, 1u, rng, cache_feeds(p, 2u));", self.text(PT + "shaders/pt_specular.hlsl"))
        # Its cells are the world's: the scene origin is added, and a camera cut keeps them (only another scene or the
        # cache turned back on empties it).
        self.assertIn("const int3 c = int3(floor((position + g.cacheOrigin.xyz) / (CACHE_CELL * exp2(float(level)))));", common)
        self.assertIn("for(int i=0;i<3;++i) k.cacheOrigin[i]=float(st.origin[i]);", tracer)
        clear = "if(cache && (!st.cacheRan || st.cacheScene!=st.scene.clear_generation())) dispatch(list, st.cache.Get(), {1u}, b, kCacheSlots/256, 1);"
        self.assertIn(clear, tracer)
        resolve = "if(cache) dispatch(list, st.cache.Get(), {0u}, b, kCacheSlots/256, 1);"
        self.assertIn(resolve, tracer)
        self.assertLess(tracer.index("dispatch_pass(list, st.specularSer.Get(), 1, st.specular.Get(), b);"), tracer.index(resolve))
        self.assertLess(tracer.index(clear), tracer.index("dispatch_pass(list, st.giSer.Get(), 0, st.gi.Get(), b);"))
        self.assertIn("cache:pt_cache", self.text("CMakeLists.txt"))
        self.assertIn('#include "pt_cache.h"', self.text(PT + "shaders/pt_shaders.hpp.in"))
        # The Radiance Cache row (on by default) turns it off.
        self.assertIn("#define TGPT_OPTION_NO_RADIANCE_CACHE 65536u", header)
        self.assertIn("internal const uint OptionNoRadianceCache = 65536;", self.text("src/TGPathTracing/NativeTracer.cs"))
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertIn('RadianceCache = file.Bind(Section, "Radiance cache", true, Row(order++, "Radiance Cache", null,', settings)
        self.assertIn("(RadianceCache.Value ? 0u : Tgpt.OptionNoRadianceCache)", settings)
        self.assertIn("const bool cache=!(f.options&TGPT_OPTION_NO_RADIANCE_CACHE) && st.cache && st.cacheKeysUav!=kInvalid;", tracer)
        self.assertIn('radianceCache = new ToggleOption(IdPrefix + "RadianceCache", "Radiance Cache", true, false);', self.text("src/TGPathTracing/PathTracingRow.cs"))
        # The harness starts every case over with an empty cache (its first frame without it), as a new scene does in game.
        self.assertIn("if(frameIndex==0) f.options|=TGPT_OPTION_NO_RADIANCE_CACHE;", self.text("tests/pathtracing_gpu_test.cpp"))

    def test_path_tracing_abi(self):
        header = self.text(PT + "include/tgpt_api.h")
        managed = self.text("src/TGPathTracing/NativeTracer.cs")
        self.assertIn("#define TGPT_ABI 0x00010F00u", header)
        self.assertIn("internal const uint Abi = 0x00010F00;", managed)
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
        self.assertIn('static_assert(sizeof(FrameConstants)==1632, "frame constants layout");', self.text(PT + "src/tracer.cpp"))
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertEqual(common.count("float4x4 "), 3 + 0)
        # 3 matrices, 29 float4/uint4 rows, RTXDI's eight parameter blocks (16+16+32+16+48 B for DI, 32+32+16 B for GI), the
        # 16 diffusion profiles and their 32 rows of skin transmission: 3*64 + 29*16 + 128 + 80 + 256 + 512 = 1632.
        block = re.search(r"struct FrameConstants \{(.*?)\};", common, re.S).group(1)
        self.assertEqual(len(re.findall(r"^\s*(?:float4|uint4) \w+;", block, re.M)), 29)
        self.assertIn("float4 sssProfiles[16];", block)
        for member in ["RTXDI_RuntimeParameters rtxdiRuntime;", "RTXDI_ReservoirBufferParameters rtxdiReservoirs;",
                       "RTXDI_DITemporalResamplingParameters diTemporal;", "RTXDI_BoilingFilterParameters diBoiling;",
                       "RTXDI_DISpatialResamplingParameters diSpatial;", "RTXDI_GITemporalResamplingParameters giTemporal;",
                       "RTXDI_GISpatialResamplingParameters giSpatial;", "RTXDI_BoilingFilterParameters giBoiling;"]:
            self.assertIn(member, block)
        self.assertIn("uint4 grid;", common)

    def test_path_tracing_mask_maps(self):
        # Mask maps where rays meet a material (tgpt_gpu_test measures them): the flag and the record's two fields agree
        # between the ABI, the shaders and the plugin; a metal keeps no diffuse color; the atlas reads a mask map's bytes as
        # they are (Unity creates linear textures typeless too), stores metallic and smoothness inverted (a tile not baked
        # yet reads as a rough dielectric), starts white again with a cleared scene, and keeps a quarter for color.
        header = self.text(PT + "include/tgpt_api.h")
        common = self.text(PT + "shaders/pt_common.hlsli")
        managed = self.text("src/TGPathTracing/NativeTracer.cs")
        self.assertIn("#define TGPT_MATERIAL_MASK_TILE 1024u", header)
        self.assertIn("#define MATERIAL_MASK_TILE 1024", common)
        self.assertIn("internal const uint MaterialMaskTile = 1024;", managed)
        for field in ["int32_t maskTile;", "uint32_t maskRemap;"]:
            self.assertIn(field, header)
        self.assertIn("uint flags; int alphaTile; int maskTile; uint maskRemap;", common)
        for field in ["internal int MaskTile;", "internal uint MaskRemap;"]:
            self.assertIn(field, managed)
        self.assertIn("s.albedo = base * (1.0 - finish.x);", common)
        # A path vertex sends a metal's reflected light on as diffuse light of the same energy (its base color), not none.
        self.assertIn("s.reflectance = base;", common)
        self.assertIn("throughput *= m.reflectance;", common)
        self.assertNotIn("throughput *= m.albedo;", common)
        self.assertIn("s.f0 = lerp(0.04, base, finish.x);", common)
        self.assertNotIn("metallic * 0.8", common)
        self.assertIn("return float2(lerp(ends.x, ends.y, 1.0 - t.r), lerp(ends.z, ends.w, 1.0 - t.a));", common)
        prepare = self.text(PT + "shaders/pt_prepare.hlsl")
        self.assertIn("roughness = lerp(traced.roughness, 0.05, forwardBoost);", prepare)
        self.assertIn("f0 = lerp(traced.f0, 0.6, 0.7 * forwardBoost);", prepare)
        self.assertIn("const float forwardBoost = strands ? 0.0 : boost;", prepare)
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("v.Format=b.mask ? linear_view(d.Format) : color_view(d.Format);", tracer)
        self.assertIn("case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;", tracer)
        self.assertIn("if(st.atlas.t.uav) st.c11->ClearUnorderedAccessViewFloat(st.atlas.t.uav.Get(), one);", tracer)
        self.assertIn("if (c0.z & 2) {", self.text(PT + "shaders/d3d11.hlsl"))
        self.assertIn("if(e.textureTiles.size()>=kMaxTiles/4*3) return -3;", self.text(PT + "src/extension.cpp"))
        # The plugin reads mask maps only where it knows how the game's GBuffer pass reads them (Cloth_Wind: smoothness in
        # blue), and glass keeps GlassFinish's finish.
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        for shader in ["HDRP/Lit", "HDRP/LayeredLit", "TG/Character/Kandra_Lit", "TG/Architecture/KF_Architecture", "TG/Clothes/Cloth_Wind"]:
            self.assertIn('["' + shader + '"] = new MaskLayout', capture, shader)
        self.assertIn('["TG/Clothes/Cloth_Wind"] = new MaskLayout { Texture = "_Mask_Map", SmoothnessChannel = 2 },', capture)
        self.assertIn('Texture = "_MaskMap", Keyword = "_MASKMAP"', capture)
        self.assertIn("string mask = ReadFinish(m, shaderName, ref mat, !GlassFinish.IsGlass(m));", capture)
        # Shader Graphs that tile their maps through properties of their own are traced with that tiling.
        self.assertIn('mat.ScaleX = mat.ScaleY = m.GetFloat("_Base_Tiling");', capture)
        self.assertIn("            ShaderGraphTiling(m, shaderName, ref mat);", capture)

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
        # Last frame's GI path vertices are relative to its scene origin: when it moves they are not reused. A scene
        # reset forgets the terrain material.
        self.assertIn("&& all(g.prevOriginDelta.xyz == 0.0)) {", self.text(PT + "shaders/pt_gi.hlsl"))
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
        # Scaled around its brightness, never past a channel going negative (clamped at 0 it turned warm bounce red).
        gi_spatial = self.text(PT + "shaders/pt_gi_spatial.hlsl")
        self.assertIn("if (scale > 1.0 && result[c] < l) scale = max(1.0, min(scale, l / max(l - result[c], 1e-6)));", gi_spatial)
        self.assertIn("result = l + (result - l) * scale;", gi_spatial)
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
        # No Built-in choice (the user's call): None, NRD, Ray Regeneration, Ray Reconstruction; a stored Built-in becomes
        # NRD, read before binding (BepInEx would replace it with the first choice, None).
        self.assertIn('DenoiserNames = { "None", "NRD", "Ray Regeneration", "Ray Reconstruction" };', settings)
        self.assertIn("public enum DenoiserChoice { None, Nrd, RayRegeneration, RayReconstruction }", settings)
        self.assertIn('if (storedDenoising == "Built-in") Denoiser.Value = DenoiserNames[(int)DenoiserChoice.Nrd];', settings)
        self.assertNotIn("DenoiserChoice.BuiltIn", settings + self.text("src/TGPathTracing/PathTracingRow.cs") + self.text("src/TGPathTracing/Session.cs"))
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
        self.assertEqual(order, ["Path Tracing", "Denoising", "Exposure", "Dark Adaptation", "Shadow Detail", "Rays Per Pixel", "Bounces", "Radiance Cache",
                                 "Indirect Color", "Reflections", "Water Reflections", "Hair Shadows", "Fill Light Fog", "Spatial Samples", "Boiling Filter",
                                 "Emissive Lights", "Crevice Light", "Ray Distance", "Scene Radius", "Sky Intensity", "Reflection Boost", "Debug View"])
        # The Boiling Filter row turns ReSTIR GI's boiling filter off (the direct light's stays): the harness's honest energy
        # reference showed it throwing away 21 % of the bounced light next to a light by a wall.
        self.assertIn("(BoilingFilter.Value ? 0u : Tgpt.OptionNoGiBoiling) |", settings)
        self.assertIn("k.giBoiling.enableBoilingFilter=(f.options&(TGPT_OPTION_NO_BOILING|TGPT_OPTION_NO_GI_BOILING)) ? 0u : 1u;", self.text(PT + "src/tracer.cpp"))
        self.assertIn("PathTracingRow.Tick(now);", self.text("src/TGPathTracing/Session.cs"))
        self.assertIn('public static void Report(bool available)', self.text("src/TGPathTracing/RayReconstruction.cs"))
        self.assertIn('report = type.GetMethod("Report", BindingFlags.Public | BindingFlags.Static);', self.text("src/TGBetterUpscaling/PathTracingLink.cs"))
        # Frame generation resumes only once the cursor has stayed locked (mods fighting over it in a menu).
        self.assertIn("if(Settings.SuspendOnCursor.Value && (unlocked || now-cursorUnlockedAt<0.5f)) flags|=Suspend.CursorUnlocked;",
                      self.text("src/TGFrameGeneration/Session.cs"))

    def test_path_tracing_water_reflections(self):
        # Traced reflections on water fill what HDRP's transparent SSR found nothing for (render event 4, right behind that
        # step): HDRP's water and transparent shading take their reflections from its texture, then probes and the sky.
        tracer = self.text(PT + "src/tracer.cpp")
        d3d11 = self.text(PT + "shaders/d3d11.hlsl")
        water = self.text(PT + "shaders/pt_water.hlsl")
        passes = self.text("src/TGPathTracing/HdrpPasses.cs")
        # Built for both sets: every ray query of the 6.9 passes accepts micromaps, and a 6.6 query must never meet one.
        self.assertIn("compose:pt_compose water:pt_water hdrpShadow:pt_hdrp_shadow)", self.text("CMakeLists.txt"))
        shaders = self.text(PT + "shaders/pt_shaders.hpp.in")
        self.assertIn('#include "pt_water.h"', shaders)
        self.assertIn('#include "pt_water_dxr12.h"', shaders)
        self.assertIn("st.water=make_pso(shaders::waterDxr12, sizeof(shaders::waterDxr12));", tracer)
        self.assertIn("st.water=make_pso(shaders::water, sizeof(shaders::water));", tracer)
        self.assertIn("else if(eventId==TGPT_EVENT_WATER) water_event(sequence);", tracer)
        # The traced reflection replaces all of HDRP's, its SSR's hits too, with the full weight (LightEvaluation.hlsl's
        # ApplyScreenSpaceReflectionWeight reads it premultiplied), divided by the reflection lighting multiplier HDRP applies
        # to all of it (LightLoop.hlsl's GetIndirectSpecularMultiplier); HDRP's texture is only where its water reads it.
        self.assertIn("target[p] = float4(fill * cameraDelta.w, 1.0);", d3d11)
        self.assertNotIn("own.rgb", d3d11)
        self.assertIn("wc.cameraDelta[3]=1.f/std::max(request.multiplier, 1e-3f);", tracer)
        self.assertIn("waterMultiplier = hdCamera.volumeStack.GetComponent<IndirectLightingController>().reflectionLightingMultiplier.value;", passes)
        # Only the surfaces HDRP gives these reflections (its TraceReflectionRay stencil bit), never its shared black texture.
        self.assertIn("constexpr uint32_t kTraceReflectionRay=8;", tracer)
        self.assertIn("target[p] = (stencil.Load(int3(p, 0)).g & c0.z) != 0u ? source.Load(int3(p, 0)) : 0.0;", d3d11)
        self.assertIn('TextureBuiltin = AccessTools.Field(typeof(TextureHandle), "builtin");', passes)
        self.assertIn("if (!transparent || failed || waterFailed || waterPass == null || hdCamera == null) return;", passes)
        # D3D11 applies the water frame just before this one only, traced a frame ago: it never waits for this frame's.
        self.assertIn("prev.frameIndex+1==frame.frameIndex", tracer)
        # Pre-exposed with the traced frame's exposure: the previous frame's, which HDRP takes its SSR texture back out of.
        self.assertIn("output[p] = float4(max(color, 0.0) * (exposure() * reference_radiance() * traced_gain_at(position)), reach);", water)
        # Leaves and grass are lit from behind by shade_path's light sampling now, not a second sampling of the water's own.
        self.assertNotIn("TRANSMISSION * direct_diffuse(", water)
        # The Water Reflections row decides whether the pass is recorded; the capture compares with HDRP's own reflections.
        self.assertIn("if (!Session.Settings.WaterReflections.Value || DebugCapture.WaterOff) return;", passes)
        self.assertIn('WaterReflections = file.Bind(Section, "Water reflections", true, Row(order++, "Water Reflections", null,',
                      self.text("src/TGPathTracing/Settings.cs"))
        self.assertIn('-water-reflections-off"', self.text("src/TGPathTracing/DebugCapture.cs"))
        # Water is never a solid plane to rays (a custom water's mesh renderer carries the water material).
        self.assertIn('if (m.FindPass("WaterGBuffer") >= 0)', self.text("src/TGPathTracing/SceneCapture.cs"))

    def test_path_tracing_nrd(self):
        # NVIDIA NRD (RELAX) is the fallback denoiser for GPUs without Ray Reconstruction or Ray Regeneration, as in DOOM: The
        # Dark Ages. Like RTXDI it is fetched beside the repository and compiled in, never committed (the NVIDIA RTX SDKs
        # License allows object code only); a build without it has no NRD and the built-in denoiser serves.
        cmake = self.text("CMakeLists.txt")
        self.assertIn('file(GLOB TG_LOCAL_NRD LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/NRD-SDK-*")', cmake)
        self.assertIn('add_subdirectory("${TG_NRD}" "${CMAKE_CURRENT_BINARY_DIR}/nrd" EXCLUDE_FROM_ALL)', cmake)
        self.assertIn("set(NRD_STATIC_LIBRARY ON CACHE BOOL \"\" FORCE)", cmake)
        self.assertIn("set(NRD_EMBEDS_DXIL_SHADERS ON CACHE BOOL \"\" FORCE)", cmake)
        self.assertIn("target_compile_definitions(TGPathTracing.Native PRIVATE TGPT_NRD=1)", cmake)
        self.assertIn('-I "${TG_NRD}/Shaders" -Fh "${PT_GENERATED}/pt_nrd_prep.h" -Vn nrdPrep', cmake)
        self.assertIn("/NRD-SDK-*/", self.text(".gitignore"))
        self.assertIn("NVIDIA NRD", self.text("docs/SOURCES.md"))
        header = self.text(PT + "include/tgpt_api.h")
        self.assertIn("#define TGPT_OPTION_NRD 1024u", header)
        self.assertIn("#define TGPT_FEATURE_NRD 16u", header)
        self.assertIn("#define TGPT_FEATURE_NRD_ACTIVE 32u", header)
        # NRD takes the bounced light only (RELAX's diffuse denoiser), guided by the geometric normal, with 2D motion; the
        # direct light and the reflections keep the tracer's own filters (sharp shadows, stable on normal maps).
        nrd = self.text(PT + "src/nrd_denoiser.cpp")
        self.assertIn("const nrd::DenoiserDesc denoiser{kRelax, nrd::Denoiser::RELAX_DIFFUSE};", nrd)
        self.assertIn("relax.diffusePrepassBlurRadius=0.f;", nrd)
        self.assertIn("c.motionVectorScale[0]=c.motionVectorScale[1]=1.f; c.motionVectorScale[2]=0.f;", nrd)
        self.assertIn("c.accumulationMode=restart_ ? nrd::AccumulationMode::CLEAR_AND_RESTART : f.reset ? nrd::AccumulationMode::RESTART : nrd::AccumulationMode::CONTINUE;", nrd)
        self.assertIn('#include <NRD.h>', nrd)
        prep = self.text(PT + "shaders/pt_nrd_prep.hlsl")
        self.assertIn("normalRoughness[p] = NRD_FrontEnd_PackNormalAndRoughness(s.geoNormal, s.roughness, 0.0);", prep)
        self.assertIn("motion[p] = float4(-mv.Load(int3(p, 0)), 0.0, 0.0);", prep)
        self.assertIn("diffuse[p] = RELAX_FrontEnd_PackRadianceAndHitDist(b.rgb * asfloat(pass.b.x), b.a, true);", prep)
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("!st.nrdFailed && nrd_available() && f.debugView==TGPT_VIEW_NONE;", tracer)
        self.assertIn("const bool denoiseOwn=denoise || useNrd;", tracer)
        self.assertIn("if(useNrd) k.denoise2[2]=st.nrdDiffuseOut.uav;", tracer)
        self.assertIn("denoisers=(rrg_available() ? TGPT_FEATURE_RAY_REGENERATION : 0u)|(nrd_available() ? TGPT_FEATURE_NRD : 0u);", tracer)
        # Denoising offers it where it started; the best order is Ray Reconstruction, Ray Regeneration, NRD. NRD also
        # serves whenever the chosen one doesn't run (Ray Reconstruction without DLSS upscaling, a failed Ray Regeneration).
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertIn("if (offered.Contains(DenoiserChoice.Nrd)) return DenoiserChoice.Nrd;", settings)
        self.assertIn("(CurrentDenoiser != DenoiserChoice.None ? Tgpt.OptionNrd : 0u);", settings)
        self.assertIn("bool nrd = (Status.Features & Tgpt.FeatureNrd) != 0;", self.text("src/TGPathTracing/Session.cs"))
        harness = self.text("tests/pathtracing_gpu_test.cpp")
        self.assertIn('"NRD\'s error against the undenoised reference is within half again of the built-in denoiser\'s"', harness)
        self.assertIn('"NVIDIA NRD started on the device (this build has it)"', harness)

    def test_path_tracing_review_fixes(self):
        # From the review of Kuan-Mi's Unity RTXDI integration: the boiling filter compares a weight with the rest of its
        # group (RTXDI's counts the weight itself and can never fire in sparse groups); the GI reconnection Jacobian is
        # validated as NVIDIA's sample does; the alpha test is bilinear within its tile, with the micromap bake reading
        # the same footprint; Ray Reconstruction's albedo guides are never both near 0.
        boil = self.text(PT + "shaders/pt_boil.hlsli")
        self.assertIn("if (own <= 0.0 || others < 4.0) return -1.0;", boil)
        self.assertIn("return (sum - own) / others * (10.0 / clamp(strength, 1e-6, 1.0) - 9.0);", boil)
        rtxdi = self.text(PT + "shaders/pt_rtxdi.hlsli")
        self.assertIn("if (jacobian > 10.0 || jacobian < 0.1) return false;", rtxdi)
        self.assertIn("jacobian = clamp(jacobian, 1.0 / 3.0, 3.0);", rtxdi)
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("return lerp(lerp(a00, a10, f.x), lerp(a01, a11, f.x), f.y);", common)
        self.assertIn("if (alphaTile) { lo = floor(min(p0, min(p1, p2)) - 0.5); hi = floor(max(p0, max(p1, p2)) - 0.5) + 1.0; }",
                      self.text(PT + "shaders/pt_omm_bake.hlsl"))
        self.assertIn("if (dot(diffuse + spec, 1.0 / 3.0) < 0.05) diffuse += 0.05;", self.text(PT + "shaders/d3d11.hlsl"))
        self.assertIn('"micromaps leave every pixel as the alpha test alone makes it"', self.text("tests/pathtracing_gpu_test.cpp"))

    def test_path_tracing_amd_portability(self):
        # From the review for GPUs without NVIDIA's extras (a Radeon RX 7900 XTX): what a driver may lack is checked, not
        # assumed; nothing depends on behaviour the specifications leave undefined.
        tracer = self.text(PT + "src/tracer.cpp")
        # Typed UAV loads: the extra formats (a driver has them together) and R32G32_UINT (on its own), checked at start.
        self.assertIn("if(reason.empty() && !o.TypedUAVLoadAdditionalFormats)", tracer)
        self.assertIn("D3D12_FEATURE_DATA_FORMAT_SUPPORT fs{DXGI_FORMAT_R32G32_UINT};", tracer)
        # The D3D11 passes that read HDRP's color back through a UAV only where the format loads there, as float4.
        self.assertEqual(tracer.count("uavFormat!=DXGI_FORMAT_UNKNOWN && float_format(uavFormat) && typed_uav_load(uavFormat)"), 2)
        # The Ray Reconstruction hit-distance texture is released with the others when the size changes.
        self.assertIn("for(auto& out:st.outputs) { release_shared(out.a); release_shared(out.b); release_shared(out.c); }", tracer)
        # SVGF's moments in 32-bit floats and its variance carried as the standard deviation (half precision overflowed).
        for name in ("diffuseMoments", "directMoments", "specMoments"):
            self.assertRegex(tracer, r"st\." + name + r"\[i\]=make_texture\(d, heap, w, h, DXGI_FORMAT_R32G32B32A32_FLOAT,")
        self.assertIn("historyOut[p] = float4(color, sqrt(variance));", self.text(PT + "shaders/pt_temporal.hlsl"))
        atrous = self.text(PT + "shaders/pt_atrous.hlsl")
        self.assertIn("variance3 += deviation * deviation * w;", atrous)
        self.assertIn("output[p] = float4(sum / weights, sqrt(variance) / weights);", atrous)
        # A retry past a pass-through surface never sets TMin past TMax (nor past what stands still's shorter end).
        common_retry = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("for (uint pass = 0; pass < 5 && t0 < tmax; ++pass) {", common_retry)
        self.assertIn("if (t0 >= end) { mask = 0xFFu; t0 = max(t0, SWAY_REACH); continue; }", common_retry)
        self.assertIn("for (uint pass = 0; pass < 3 && t0 < nearEnd; ++pass) {", common_retry)
        # The boiling filter sums its group once, and the micromap bake counts texels in integers.
        self.assertIn("if (index == 0u) {", self.text(PT + "shaders/pt_boil.hlsli"))
        bake = self.text(PT + "shaders/pt_omm_bake.hlsl")
        self.assertIn("for (uint j = 0; j < count.y; ++j) {", bake)
        self.assertNotIn("for (float", bake)
        # The harness expects DirectX Raytracing 1.2 exactly where the driver reports it, and runs on any GPU by vendor.
        harness = self.text("tests/pathtracing_gpu_test.cpp")
        self.assertIn("if(agility) expect(dxr12==driverDxr12,", harness)
        self.assertIn('GetEnvironmentVariableA("TGPT_TEST_ADAPTER"', harness)
        # A failed Agility SDK runtime is tried again on another GPU or driver, read from the registry (no early dxgi.dll).
        agility = self.text(BR + "src/agility.cpp")
        self.assertIn('+", "+gpu_identity();', agility)
        self.assertNotIn("CreateDXGIFactory", agility)

    def test_path_tracing_capture(self):
        # The probe key captures: the images (each debug view, the sky's light off, the pass-through off, the game's own
        # rendering) and a report, restoring the view and mode however the capture ends.
        capture = self.text("src/TGPathTracing/DebugCapture.cs")
        self.assertIn("try { DebugCapture.Start(Host); }", self.text("src/TGPathTracing/Session.cs"))
        self.assertIn("Session.Host = gameObject.AddComponent<PathTracingBehaviour>();", self.text("src/TGPathTracing/Plugin.cs"))
        for needle in ('ExtraOptions = Tgpt.OptionNoPassThrough;', 'SkyOff = true;', 'settings.CurrentMode = TracingMode.Off;',
                       'settings.CurrentView = view;', 'settings.CurrentMode = mode;', 'File.WriteAllText(Path.Combine(dir, "report.txt"), report);'):
            self.assertIn(needle, capture)
        passes = self.text("src/TGPathTracing/HdrpPasses.cs")
        self.assertIn("Options = settings.Options | DebugCapture.ExtraOptions |", passes)
        self.assertIn("SkyIntensity = DebugCapture.SkyOff || SceneLights.Interior ? 1e-6f : settings.SkyIntensity.Value", passes)
        # The camera light and the traced light's gain hold while a capture compares its images.
        self.assertIn("if (!DebugCapture.Running && measured >= 0f && !float.IsNaN(measured) && !float.IsInfinity(measured))", passes)
        self.assertIn("if (settings.CurrentView != DebugView.None || DebugCapture.Running) return;", passes)
        self.assertIn("#define TGPT_OPTION_NO_PASS_THROUGH 128u", self.text(PT + "include/tgpt_api.h"))
        self.assertIn("internal const uint OptionRayRegeneration = 64, OptionNoPassThrough = 128, OptionDiVisibility = 256, OptionSpatialFeedback = 512, OptionNrd = 1024, OptionNoGiBoiling = 32768;", self.text("src/TGPathTracing/NativeTracer.cs"))
        self.assertIn("if (t >= nearBack * 3.0 || (g.settings2.y & OPTION_NO_PASS_THROUGH) != 0) return false;", self.text(PT + "shaders/pt_common.hlsli"))
        csproj = self.text("src/TGPathTracing/TGPathTracing.csproj")
        self.assertIn('Include="UnityEngine.ScreenCaptureModule"', csproj)
        self.assertIn('Include="UnityEngine.ImageConversionModule"', csproj)

    def test_path_tracing_grain(self):
        # Grain and blur, measured in tgpt_gpu_test against an undenoised converged frame: ReSTIR's temporal neighbour
        # is permuted every frame for every denoiser (RTXDI's permutation sampling, for bounced light as for direct);
        # the direct light (sun and lamps) is a signal of its own with a short filter, apart from the bounced light;
        # the filters are steered by the accumulated value's noise (a single frame's, and a 3x3 spread, blurred sharp
        # shadows away); ReSTIR DI reuses only lights a pixel sees.
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("k.giTemporal.maxReservoirAge=giVisibility ? 256u : 30u; k.giTemporal.enablePermutationSampling=1;", tracer)
        self.assertNotIn("OPTION_RAY_RECONSTRUCTION", self.text(PT + "shaders/pt_gi.hlsl"))
        spatial = self.text(PT + "shaders/pt_di_spatial.hlsl")
        self.assertIn("sun[p] = float4(to_storage(sunDiffuse + clamp_radiance(localDiffuse, LIMIT_DIRECT)), 1.0);", spatial)
        self.assertNotIn("g.signals.y", spatial) # the bounced light's signal is not written here
        self.assertIn("float3 diffuse = diffuseFinal[p].rgb * fromNrd + sun[p].rgb; // the direct light, denoised on its own", self.text(PT + "shaders/pt_compose.hlsl"))
        temporal = self.text(PT + "shaders/pt_temporal.hlsl")
        self.assertIn("variance /= len;", temporal)
        self.assertNotIn("spatial.y - spatial.x * spatial.x", temporal)
        di = self.text(PT + "shaders/pt_di.hlsl")
        # No visibility reuse (it conflicts with RTXDI's permutation sampling): the shading pass traces the final visibility.
        self.assertNotIn("occluded_from_surface", di)
        self.assertIn("if (RTXDI_IsValidDIReservoir(r) && W > 0.0 && W < 1e20) through = rtxdi_transmittance(surface, shadowPoint);", spatial)
        self.assertIn("shadowPoint = RAB_SamplePolymorphicLight(RAB_LoadLightInfo(RTXDI_GetDIReservoirLightIndex(r), false), surface, rng.next2());", spatial)
        # Lamp shadows: each light's own size (no 10 cm minimum), candidates without repeats.
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertNotIn("SHADOW_LIGHT_RADIUS", common)
        self.assertIn("float2 disk = disk_sample(u) * light.radius;", common)
        # Candidates drawn by how much each light can matter in the cell (cumulative weights, scene.cpp), each weighted
        # by one over its own probability; every light once when there are no more than the candidates.
        self.assertIn("uint i = cell_candidate(first, cellCount, candidates, c, u, invPdf);", di)
        self.assertIn("LightRecord light = load_light(cell_candidate(first, count, candidates, c, u, invPdf));", common)
        self.assertIn("float w = target * invPdf;", common)
        self.assertIn("if (count <= n) { invPdf = float(count); return entries[first + (min(uint(u * count), count - 1) + c) % count].x; }", common)
        scene = self.text(PT + "src/scene.cpp")
        self.assertIn("gridWeights_[entry]=float(power/std::max(d2, 1.0));", scene)
        self.assertIn("gridIndexBuffer_, gridIndices_.data(), uint32_t(gridIndices_.size()/2), 8, barriers);", scene)
        # Rays from the G-buffer's surface pass out through a traced mesh they start inside (a back face close by);
        # paths from traced hits use the plain functions.
        self.assertIn("bool passes_through(uint instanceId, uint geometry, bool frontFace, float t, float nearBack) {", common)
        # The material decides whether a back face is a real hit, not the object: hair made a head's inside stop the sun.
        self.assertIn("return (materials[materialIndices[inst.materialBase + geometry]].flags & MATERIAL_ALPHA_TEST) == 0;", common)
        self.assertNotIn("INSTANCE_ALPHA_TEST", common)
        self.assertIn('"rays starting inside an object with alpha-tested parts pass out through its opaque faces"', self.text("tests/pathtracing_gpu_test.cpp"))
        for name, call in (("pt_gi.hlsl", "trace_from_surface(origin, dir, 0.0, g.params.y, near_back(s.distance), hit)"),
                           ("pt_specular.hlsl", "trace_from_surface(origin, l, 0.0, g.params.y, near_back(s.distance), hit)"),
                           ("pt_di_spatial.hlsl", "transmittance_from_surface(sunOrigin, l, 0.0, g.params.y, nearBack)"),
                           ("pt_gi_spatial.hlsl", "occluded_from_surface(origin, dir, 0.0, sky ? g.params.y : max(dist - 0.02, 0.0), near_back(s.distance))")):
            self.assertIn(call, self.text(PT + "shaders/" + name), name)
        self.assertIn('"rays starting inside the traced mesh pass out through it"', self.text("tests/pathtracing_gpu_test.cpp"))
        # Shadow-only meshes are stand-ins rays from a drawn surface pass through close by; translucent surfaces
        # (HDRP's transmission) are lit through from behind unless their diffusion profile is thick (skin: HDRP's
        # Regular transmission mode, read per profile and handed over as a bit mask); characters carry their texture
        # coordinates.
        self.assertIn("if (inst.flags & INSTANCE_STAND_IN) return true;", common)
        self.assertIn("#define TGPT_INSTANCE_STAND_IN 4u", self.text(PT + "include/tgpt_api.h"))
        self.assertIn("AddMesh(standIn.sharedMesh, renderer.sharedMaterials, renderer.localToWorldMatrix, Tgpt.InstanceStandIn)", self.text("src/TGPathTracing/SceneCapture.cs"))
        self.assertIn("bool translucent = (feature == 2u || feature == 3u) && !solid_transmission(s, feature);", self.text(PT + "shaders/pt_prepare.hlsl"))
        self.assertIn("uint profile = uint(gb2.b / (16.0 / 255.0) + 1.0 / 255.0) & 15u;", common)
        self.assertIn("return ((g.settings.x >> profile) & 1u) != 0;", common)
        self.assertIn("if (solid_transmission(gb2, feature)) c = float3(1.0, 0.55, 0.1);", self.text(PT + "shaders/pt_compose.hlsl"))
        self.assertIn("k.settings[0]=f.thickProfiles;", self.text(PT + "src/tracer.cpp"))
        self.assertIn("uint32_t thickProfiles;", self.text(PT + "include/tgpt_api.h"))
        self.assertIn("internal uint DiHistory, ThickProfiles;", self.text("src/TGPathTracing/NativeTracer.cs"))
        passes = self.text("src/TGPathTracing/HdrpPasses.cs")
        self.assertIn("ThickProfiles = ThickProfiles(hd),", passes)
        self.assertIn('typeof(DiffusionProfileSettings).GetField("profile"', passes)
        # This game sets DiffusionProfile_Human_Skin to Thin Object, as it does its foliage: skin counts by name too.
        self.assertIn('thick = (mode != null && Convert.ToUInt32(mode) == 0u) || settings.name.IndexOf("skin", StringComparison.OrdinalIgnoreCase) >= 0;', passes)
        self.assertIn("if (profiles[i] != null && IsThick(profiles[i])) mask |= 1u << i;", passes)
        self.assertIn("float nl = translucent_cos(s.normal, ls.dir, s.through);", self.text(PT + "shaders/pt_rtxdi.hlsli"))
        self.assertIn("character.Handle = Session.Tracer.RegisterSkinnedMesh(key, reads.Indices, vertexCount, reads.SubmeshStarts, reads.Uvs);", self.text("src/TGPathTracing/CharacterCapture.cs"))
        for check in ('"a translucent surface (grass, leaves) is lit through from behind"', '"a character\'s cut-outs let light through, texel by texel"',
                      '"a surface whose diffusion profile is thick (skin) is not lit through from behind"',
                      '"rays from a drawn surface pass through a stand-in close by, whichever way it faces"'):
            self.assertIn(check, self.text("tests/pathtracing_gpu_test.cpp"))
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("const uint32_t atrousIterations=denoise ? 5 : 0;", tracer)
        self.assertIn("k.denoise1[0]=denoiseOwn ? st.directOut.uav : st.sunRaw.uav;", tracer)
        self.assertIn("dispatch(list, st.atrous.Get(), {st.directTmp.uav, st.directOut.uav, 2u, 0u, st.directHist[cur].uav}, b);", tracer)
        self.assertIn("dispatch(list, st.compose.Get(), {rrg ? 1u : 0u, out.c.uav, nrdToStorage}, b);", tracer)
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertIn('var legacy = file.Bind(Section, "Temporal history", 0, Hidden("No longer used."));', settings)
        self.assertNotIn("TemporalHistory =", settings)
        # ReSTIR GI's history is RTXDI's default of 8 frames (1 was the least grainy with the tracer's own reuse, before
        # RTXDI's decorrelation); the hidden Bounced light history sets another.
        self.assertIn('BouncedLightHistory = file.Bind(Advanced, "Bounced light history", 0, Hidden(', settings)
        self.assertIn("GiHistory = DebugCapture.GiHistory != 0 ? DebugCapture.GiHistory : settings.BouncedLightHistory.Value > 0 ? (uint)settings.BouncedLightHistory.Value : 8u,", self.text("src/TGPathTracing/HdrpPasses.cs"))
        self.assertIn("k.giTemporal.maxHistoryLength=f.giHistory ? std::clamp(f.giHistory, 1u, 64u) : 8u;", self.text(PT + "src/tracer.cpp"))
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
        self.assertIn("indirectControl.indirectDiffuseLightingMultiplier.Override(0f);", passes)
        self.assertNotIn("noIndirect", passes)
        self.assertIn("profile.Add<ScreenSpaceReflection>().enabled.Override(false);", passes)
        # Each override names its parameter: Add<T>(true) overrides every parameter with HDRP's defaults, and the Fog
        # component's (enabled: false) took the game's fog away while tracing (Sagremor Castle's night mist).
        for path in sorted((ROOT / "src").rglob("*.cs")):
            if "bin" in path.parts or "obj" in path.parts: continue
            self.assertNotRegex(path.read_text(encoding="utf-8"), r"\.Add<\w+>\(\s*(overrides:\s*)?true\s*\)", str(path))
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

    def test_path_tracing_rtxdi(self):
        # ReSTIR DI's reuse is NVIDIA RTXDI's (fetched beside the repository, compiled in, never committed): the tracer's
        # light-grid candidates stream into its reservoirs, its temporal reuse (permutation sampling, history in frames)
        # and boiling filter run in pt_di, its pairwise-MIS spatial reuse in pt_di_spatial, over the bridge in pt_rtxdi.
        cmake = self.text("CMakeLists.txt")
        self.assertIn('file(GLOB TG_LOCAL_RTXDI LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/RTXDI-Library-*")', cmake)
        self.assertEqual(cmake.count('-I "${TG_RTXDI}/Include"'), 5) # the passes' four DXC commands, and NRD's front end
        self.assertIn('"${TG_RTXDI}/Source/RtxdiUtils.cpp"', cmake)
        self.assertIn("/RTXDI-Library-*/", self.text(".gitignore"))
        self.assertIn("This software contains source code provided by NVIDIA Corporation.", self.text("docs/SOURCES.md"))
        bridge = self.text(PT + "shaders/pt_rtxdi.hlsli")
        for name in ["RAB_GetGBufferSurface", "RAB_IsSurfaceValid", "RAB_GetSurfaceNormal", "RAB_GetSurfaceLinearDepth", "RAB_GetMaterial",
                     "RAB_AreMaterialsSimilar", "RAB_ClampSamplePositionIntoView", "RAB_LoadLightInfo", "RAB_TranslateLightIndex",
                     "RAB_SamplePolymorphicLight", "RAB_GetLightSampleTargetPdfForSurface", "RAB_GetConservativeVisibility",
                     "RAB_GetTemporalConservativeVisibility", "RAB_EmptySurface", "RAB_EmptyLightInfo", "RAB_EmptyLightSample"]:
            self.assertIn(name + "(", bridge, name)
        self.assertIn("r.s.position += g.prevOriginDelta.xyz;", bridge)
        di = self.text(PT + "shaders/pt_di.hlsl")
        self.assertIn("RTXDI_StreamSample(r, i, uv, rng.next(), RAB_GetLightSampleTargetPdfForSurface(ls, surface), invPdf);", di)
        self.assertIn("r.M = 1;", di)
        self.assertIn("r = RTXDI_DITemporalResampling(p, surface, r, rtxdiRng, g.rtxdiRuntime, g.rtxdiReservoirs,", di)
        # The boiling filters are the tracer's own (pt_boil.hlsli): RTXDI's assume waves of at least 32 lanes (Intel's GPUs
        # run 8 or 16, out of its shared memory's bounds).
        self.assertIn("if (boiling_outlier(local, g.diBoiling.boilingFilterStrength, r.weightSum * r.targetPdf)) r = RTXDI_EmptyDIReservoir();", di)
        boil = self.text(PT + "shaders/pt_boil.hlsli")
        self.assertIn("groupshared float s_boilingWeights[64];", boil)
        self.assertNotIn("Wave", boil)
        for name in ("pt_di.hlsl", "pt_gi_boil.hlsl"):
            self.assertNotIn("RTXDI_ENABLE_BOILING_FILTER", self.text(PT + "shaders/" + name), name)
        # No visibility reuse by default: with Basic spatial normalization it widened a lamp's shadow edge to 14 pixels,
        # with the ray-traced one still to 4.4 (measured); an option turns it on, with the ray-traced correction.
        self.assertIn("if ((g.settings2.y & OPTION_DI_VISIBILITY) != 0 && RTXDI_IsValidDIReservoir(r)) {", di)
        self.assertIn("k.diTemporal.biasCorrectionMode=diVisibility ? ReSTIRDI_TemporalBiasCorrectionMode::Raytraced : ReSTIRDI_TemporalBiasCorrectionMode::Basic;", self.text(PT + "src/tracer.cpp"))
        self.assertIn('"      with direct light\'s visibility reuse (option): 2 cm light %.2f pixels\\n"', self.text("tests/pathtracing_gpu_test.cpp"))
        spatial = self.text(PT + "shaders/pt_di_spatial.hlsl")
        # Spatial reuse is RTXDI's Basic (its default; its pairwise MIS counted empty neighbours against the pixel's own
        # light), and the light sample is taken from the resulting reservoir (Basic returns one only when a neighbour won).
        self.assertIn("#define RTXDI_ALLOWED_BIAS_CORRECTION RTXDI_BIAS_CORRECTION_RAY_TRACED", spatial) # the visibility option picks it at run time
        self.assertNotIn("PAIRWISE", spatial)
        self.assertIn("        if (RTXDI_IsValidDIReservoir(r))\n            ls = RAB_SamplePolymorphicLight(", spatial.replace("\r\n", "\n"))
        self.assertIn("r = RTXDI_DISpatialResampling(p, surface, r, rtxdiRng, g.rtxdiRuntime, g.rtxdiReservoirs, g.rtxdi.x, g.diSpatial, ls);", spatial)
        # Spatial results feed the next frame only with the option (TGPT_OPTION_SPATIAL_FEEDBACK), into the array the next
        # frame's temporal reuse reads; otherwise the arrays alternate and spatial reuse writes nothing.
        self.assertIn("if (feedback) RTXDI_StoreDIReservoir(r, g.rtxdiReservoirs, p, g.rtxdi.y);", spatial)
        self.assertIn("const uint32_t rcur=feedback ? 1u : cur, rprev=feedback ? 0u : prev;", self.text(PT + "src/tracer.cpp"))
        tracer = self.text(PT + "src/tracer.cpp")
        for token in ["static_assert(sizeof(FrameConstants)==1632", "k.diTemporal.enablePermutationSampling=1;",
                      "k.diSpatial.biasCorrectionMode=diVisibility ? ReSTIRDI_SpatialBiasCorrectionMode::Raytraced : ReSTIRDI_SpatialBiasCorrectionMode::Basic;",
                      "k.diSpatial.discountNaiveSamples=0u;",
                      "k.giTemporal.enableFallbackSampling=0;", "const bool reset=st.resetNext || (f.flags&TGPT_FRAME_RESET) || gap;",
                      "k.extra[3]=st.scene.previousLightsSrv;",
                      "k.diTemporal.uniformRandomNumber=rtxdi::JenkinsHash(f.frameIndex);", "k.diBoiling.enableBoilingFilter=(f.options&TGPT_OPTION_NO_BOILING) ? 0u : 1u; k.diBoiling.boilingFilterStrength=0.2f;",
                      "k.diTemporal.maxHistoryLength=f.diHistory ? std::clamp(f.diHistory, 1u, 64u) : 20u;",
                      "rtxdi::FillNeighborOffsetBuffer(bytes.data(), kNeighborOffsets);", "k.extra[1]=st.material[prev].uav;"]:
            self.assertIn(token, tracer)
        # The direct light's history is 4 (RTXDI's 20 boiled on a cage's bars: the capture's 0.40 at 20, 0.14 at 4).
        self.assertIn("DiHistory = DebugCapture.DiHistory != 0 ? DebugCapture.DiHistory : settings.DirectLightHistory.Value > 0 ? (uint)settings.DirectLightHistory.Value : 4u,", self.text("src/TGPathTracing/HdrpPasses.cs"))
        # ReSTIR GI's reuse is RTXDI's too: its temporal reuse in pt_gi (a ray generation shader with Shader Execution
        # Reordering, so its boiling filter, which synchronizes a thread group, runs in a compute pass of its own), its
        # spatial reuse in pt_gi_spatial, over one reservoir buffer with both frames' arrays.
        gi = self.text(PT + "shaders/pt_gi.hlsl")
        self.assertIn("r = RTXDI_GITemporalResampling(p, surface, rtxdi_motion(p, s), g.rtxdi.w, r, rtxdiRng, g.rtxdiRuntime, g.rtxdiReservoirs, tparams);", gi)
        self.assertIn("RTXDI_StoreGIReservoir(r, g.rtxdiReservoirs, p, g.rtxdi.z);", gi)
        self.assertNotIn("RTXDI_GIBoilingFilter", gi)
        self.assertIn("const float bound = boiling_bound(local, g.giBoiling.boilingFilterStrength, weight);",
                      self.text(PT + "shaders/pt_gi_boil.hlsl"))
        # A GI reservoir that isn't finite is dropped before it's stored; rays a normal map points into the surface count
        # as samples that found no light (mirrored out, their density doubled without their weight saying so).
        self.assertIn("if (!(r.weightSum < 1e30) || !all(r.radiance < 1e30)) r = RTXDI_EmptyGIReservoir();", gi)
        self.assertIn("const bool found = above && trace_from_surface(", gi)
        self.assertNotIn("above_surface(", gi)
        # Temporal reuse weighs a light at the previous surface as it was then: the previous frame's lights, matched by
        # identity (the Light's instance id; 0 an empty slot), not by a slot another light may have taken.
        rab = self.text(PT + "shaders/pt_rtxdi.hlsli")
        self.assertIn("else if (index < g.settings2.x) i.l = load_previous_light(index);", rab)
        self.assertIn("return id != 0u && load_previous_light(index).id == id ? int(index) : -1;", rab)
        self.assertIn("previousLights_=lights_;", self.text(PT + "src/scene.cpp"))
        self.assertIn("var n = new NativeLight { Id = light != null ? (uint)light.GetInstanceID() : 0u };", self.text("src/TGPathTracing/SceneLights.cs"))
        # Every pass rebuilds when an RTXDI header changes.
        self.assertIn('file(GLOB_RECURSE TG_RTXDI_HEADERS "${TG_RTXDI}/Include/*.h" "${TG_RTXDI}/Include/*.hlsli")', cmake)
        self.assertIn("giBoil:pt_gi_boil", cmake)
        self.assertIn('#include "pt_gi_boil.h"', self.text(PT + "shaders/pt_shaders.hpp.in"))
        gis = self.text(PT + "shaders/pt_gi_spatial.hlsl")
        self.assertIn("r = RTXDI_GISpatialResampling(p, surface, g.rtxdi.z, r, rtxdiRng, g.rtxdiRuntime, g.rtxdiReservoirs, g.giSpatial);", gis)
        self.assertIn("if ((g.settings2.y & OPTION_SPATIAL_FEEDBACK) != 0) RTXDI_StoreGIReservoir(r, g.rtxdiReservoirs, p, g.rtxdi.w);", gis)
        self.assertIn("if(k.giBoiling.enableBoilingFilter) dispatch(list, st.giBoil.Get(), {}, b);", tracer)
        self.assertLess(tracer.index("dispatch_pass(list, st.giSer.Get(), 0, st.gi.Get(), b);"), tracer.index("dispatch(list, st.giBoil.Get(), {}, b);"))
        self.assertLess(tracer.index("dispatch(list, st.giBoil.Get(), {}, b);"), tracer.index("dispatch(list, st.giSpatial.Get(), {}, b);"))
        self.assertIn("k.rtxdi[0]=rcur; k.rtxdi[1]=rprev; k.rtxdi[2]=rcur; k.rtxdi[3]=rprev;", tracer)
        self.assertIn("st.giUav=structured_uav(d, heap, st.giRes.resource.Get(), st.rtxdiArrayPitch*2, sizeof(RTXDI_PackedGIReservoir));", tracer)
        self.assertNotIn("GIReservoir {", self.text(PT + "shaders/pt_common.hlsli"))

    def test_path_tracing_crevices(self):
        # The Crevice Light row (TGPT_OPTION_GI_VISIBILITY, off by default), measured under a canvas close over sunlit sand
        # (the harness's crevices): ReSTIR GI's reuse counts a pixel for a sample only if it sees it (RTXDI's ray-traced bias
        # correction; Basic left the pocket's opening 14 % short), keeps a history whatever its sample's age (RTXDI's
        # 30-frame drop restarted the dark histories: 9 % too bright), and its boiling filter brings an outlier down to its
        # group's bound (dropped, a quarter of the light deep inside once histories last). Off, all is as before.
        for name in ("pt_gi.hlsl", "pt_gi_spatial.hlsl"):
            self.assertIn("#define RTXDI_GI_ALLOWED_BIAS_CORRECTION RTXDI_BIAS_CORRECTION_RAY_TRACED", self.text(PT + "shaders/" + name), name)
        rab = self.text(PT + "shaders/pt_rtxdi.hlsli")
        self.assertIn("bool RAB_GetConservativeVisibility(RAB_Surface surface, float3 samplePosition) { return rtxdi_gi_visible(surface, samplePosition); }", rab)
        self.assertIn("bool RAB_GetTemporalConservativeVisibility(RAB_Surface currentSurface, RAB_Surface previousSurface, float3 samplePosition) { return rtxdi_gi_visible(previousSurface, samplePosition); }", rab)
        self.assertIn("if (dot(dir, s.geoNormal) <= 0.0) return false;", rab)
        tracer = self.text(PT + "src/tracer.cpp")
        for token in ["const bool giVisibility=(f.options&TGPT_OPTION_GI_VISIBILITY)!=0;",
                      "const auto giBias=giVisibility ? RTXDI_GIBiasCorrectionMode::Raytraced : RTXDI_GIBiasCorrectionMode::Basic;",
                      "k.giTemporal.biasCorrectionMode=giBias;", "k.giSpatial.biasCorrectionMode=giBias;",
                      "k.giTemporal.maxReservoirAge=giVisibility ? 256u : 30u; k.giTemporal.enablePermutationSampling=1;"]:
            self.assertIn(token, tracer)
        boil = self.text(PT + "shaders/pt_gi_boil.hlsl")
        self.assertIn("if (bound >= 0.0 && weight > bound) {", boil)
        self.assertIn("if ((g.settings2.y & OPTION_GI_VISIBILITY) != 0) r.weightSum *= bound / weight;", boil)
        self.assertIn("else r = RTXDI_EmptyGIReservoir();", boil)
        # The direct light's filter still drops (not measured here: the pocket has no direct light).
        self.assertIn("if (boiling_outlier(local, g.diBoiling.boilingFilterStrength, r.weightSum * r.targetPdf)) r = RTXDI_EmptyDIReservoir();",
                      self.text(PT + "shaders/pt_di.hlsl"))
        # The same bit on every side, and the row after Boiling Filter (test_path_tracing_video_rows checks the order).
        self.assertIn("#define TGPT_OPTION_GI_VISIBILITY 2097152u", self.text(PT + "include/tgpt_api.h"))
        self.assertIn("#define OPTION_GI_VISIBILITY 2097152", self.text(PT + "shaders/pt_common.hlsli"))
        self.assertIn("internal const uint OptionGiVisibility = 2097152;", self.text("src/TGPathTracing/NativeTracer.cs"))
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertIn('CreviceLight = file.Bind(Section, "Crevice light", false, Row(order++, "Crevice Light", null,', settings)
        self.assertIn("(CreviceLight.Value ? Tgpt.OptionGiVisibility : 0u) |", settings)
        rows = self.text("src/TGPathTracing/PathTracingRow.cs")
        self.assertIn('creviceLight = new ToggleOption(IdPrefix + "CreviceLight", "Crevice Light", false, false);', rows)
        self.assertEqual(rows.count("boilingFilter, emissiveLights, creviceLight, distance"), 2)
        self.assertIn("boilingFilter.Enabled, emissiveLights.Enabled, creviceLight.Enabled, distance.Value", rows)
        harness = self.text("tests/pathtracing_gpu_test.cpp")
        self.assertIn('"with Crevice Light, ReSTIR\'s bounced light reaches the opening of a dark pocket (reuse counts a pixel only for samples it sees)"', harness)
        self.assertIn('"with Crevice Light, ReSTIR\'s bounced light deep in a dark pocket is neither lost to the boiling filter nor inflated by restarted histories"', harness)

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
        self.assertIn("diffuse = ls.irradiance * nl * terminator_softening(s, ls.dir) / PI;", self.text(PT + "shaders/pt_rtxdi.hlsli"))

    def test_path_tracing_characters_light(self):
        common = self.text(PT + "shaders/pt_common.hlsli")
        header = self.text(PT + "include/tgpt_api.h")
        managed = self.text("src/TGPathTracing/NativeTracer.cs")
        scene = self.text(PT + "src/scene.cpp")
        tracer = self.text(PT + "src/tracer.cpp")
        harness = self.text("tests/pathtracing_gpu_test.cpp")
        # Hair stops a ray by chance, as much of a texel as its strands cover (not where a fifth is covered: hard, full
        # shadows of every card), camera rays excepted (HDRP drew hair at the pixel); micromaps would settle it by the cutoff
        # instead.
        self.assertIn("#define MATERIAL_STRANDS 4096", common)
        self.assertIn("#define TGPT_MATERIAL_STRANDS 4096u", header)
        self.assertIn("internal const uint MaterialStrands = 4096;", managed)
        strands = common.index("if ((m.flags & MATERIAL_STRANDS) != 0)")
        self.assertLess(common.index("if ((m.flags & (MATERIAL_HDRP_SHADED | MATERIAL_STRANDS)) != 0 && g_cameraRays) return true;"), strands)
        self.assertLess(strands, common.index("return a >= m.alphaCutoff;"))
        self.assertIn("if(materials_[index].flags&TGPT_MATERIAL_STRANDS) { mesh.omm=Mesh::Omm::None; return; }", scene)
        self.assertIn("if ((mat.Flags & Tgpt.MaterialAlphaTile) != 0) mat.Flags |= Tgpt.MaterialStrands;", self.text("src/TGPathTracing/SceneCapture.cs"))
        self.assertIn('"hair\'s shadow lets light through between its strands, as much as they leave uncovered"', harness)
        # Light through thin skin: HDRP's transmittance of its GBuffer thickness and profile (its own constants), kept where
        # a transmission material's specular color is, lit with HDRP's wrapped cosine from behind through the traced skin
        # itself: a far side within 3 cm, then nothing in the way.
        self.assertIn("float4 transmission[32];", common)
        self.assertIn("float transmission[128];", tracer)
        self.assertIn("return max(tint.rgb, 0.0) * third * (third * third + 3.0) * mask;", common)
        self.assertIn("const float w = 0.258819; // cos(90 - 15 degrees)", common)
        self.assertIn("static const float THIN_SKIN = 0.03;", common)
        self.assertIn("if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT || q.CommittedTriangleFrontFace()) return false;", common)
        self.assertIn("if ((bits & 16u) != 0) s.f0 = 0.04;", common)
        # Reflection Boost leaves skin as the game made it (a neck mirrored the chin above it in sharp patches).
        self.assertIn("float boost = solid_transmission(s, feature) ? 0.0 : g.cameraPos.w;", self.text(PT + "shaders/pt_prepare.hlsl"))
        # Ray Reconstruction's guides describe skin as the tracer shades it (the thick profiles reach the D3D11 pass too).
        self.assertIn("const float boost = deferred && !skin ? cameraDelta.w : 0.0;", self.text(PT + "shaders/d3d11.hlsl"))
        self.assertIn("c.extras[1]=frame.thickProfiles;", self.text(PT + "src/tracer.cpp"))
        self.assertIn('"Reflection Boost leaves skin\'s reflections as the game made them"', self.text("tests/pathtracing_gpu_test.cpp"))
        rtxdi = self.text(PT + "shaders/pt_rtxdi.hlsli")
        # Lit from behind, through the skin itself: reuse's visibility and the final shading's alike (the final shading's
        # ray from the surface was stopped by the ear's own front, so no lamp shone through), to a lamp or a glowing point.
        self.assertIn("bool rtxdi_through_skin(Surface s, RAB_LightSample ls) { return transmitted_unoccluded(s, ls.dir, light_ray_end(ls, ls.dist, 0.0)); }", rtxdi)
        self.assertIn("if (through_skin(s, ls.dir)) return rtxdi_through_skin(s, ls);", rtxdi)
        self.assertIn("if (through_skin(s, ls.dir)) return rtxdi_through_skin(s, ls) ? 1.0 : 0.0;", rtxdi)
        # A ray to an emissive triangle's point ends just short of it in both, as triangle_shadow_ray's.
        self.assertIn("return ls.emitter ? max(dist - max(1e-3 * dist, 2e-3), 0.0) : max(dist - 0.05 - offset, 0.0);", rtxdi)
        self.assertEqual(rtxdi.count("light_ray_end(ls, dist, offset)"), 2)
        self.assertNotIn("float tmax = max(dist - 0.05 - offset, 0.0);", rtxdi)
        self.assertIn("} else if (through_skin(s, l) && transmitted_unoccluded(s, l, g.params.y)) sunDiffuse = skinThrough;", self.text(PT + "shaders/pt_di_spatial.hlsl"))
        self.assertIn("std::memcpy(k.transmission, e.transmission.data(), sizeof(k.transmission));", tracer)
        passes = self.text("src/TGPathTracing/HdrpPasses.cs")
        for field in ['"transmissionTintAndFresnel0"', '"shapeParamAndMaxScatterDist"', '"worldScaleAndFilterRadiusAndThicknessRemap"',
                      "hd.frameSettings.IsEnabled(FrameSettingsField.Transmission)", "Session.Tracer.SetSkinTransmission(transmissionTable, 16)"]:
            self.assertIn(field, passes)
        self.assertIn('"thin skin lit from behind gets the light HDRP\'s transmission lets through"', harness)
        self.assertIn('"no light comes through skin with a thick body behind it"', harness)
        # What HDRP shades itself (hair, eyes) is darkened by the traced shadows of the lights HDRP doesn't shadow, against the
        # bounced light from the radiance cache, never brightened: traced and filtered before the cache takes its samples in,
        # and multiplied in by the composite (B's alpha 2).
        self.assertIn("#define TGPT_OPTION_NO_HDRP_SHADOWS 4194304u", header)
        self.assertIn("#define TGPT_LIGHT_HDRP_SHADOWS 0x10000u", header)
        self.assertIn("internal const uint OptionNoHdrpShadows = 4194304, LightHdrpShadows = 0x10000;", managed)
        self.assertIn("hdrpShadow:pt_hdrp_shadow", self.text("CMakeLists.txt"))
        for name in ['#include "pt_hdrp_shadow.h"', '#include "pt_hdrp_shadow_dxr12.h"']:
            self.assertIn(name, self.text(PT + "shaders/pt_shaders.hpp.in"))
        self.assertIn("st.hdrpShadow=make_pso(shaders::hdrpShadowDxr12, sizeof(shaders::hdrpShadowDxr12));", tracer)
        self.assertIn("st.hdrpShadow=make_pso(shaders::hdrpShadow, sizeof(shaders::hdrpShadow));", tracer)
        self.assertLess(tracer.index("dispatch(list, st.diSpatial.Get(), {}, b);"), tracer.index("dispatch(list, st.hdrpShadow.Get()"))
        self.assertLess(tracer.index("dispatch(list, st.hdrpShadow.Get()"), tracer.index("if(cache) dispatch(list, st.cache.Get(), {0u}, b, kCacheSlots/256, 1);"))
        self.assertIn("k.material[2]=hdrpShadows ? st.hdrpHistory[cur].uav : kInvalid;", tracer)
        shadow = self.text(PT + "shaders/pt_hdrp_shadow.hlsl")
        # Only where the camera ray found a surface HDRP shades (hair, eyes, a character's forward pixels): what the traced
        # scene lacks may be unlit (a glow card, an impostor).
        self.assertIn("hdrpShaded = hdrp;", self.text(PT + "shaders/pt_prepare.hlsl"))
        self.assertIn("s.hdrpShaded = (bits & 32u) != 0;", common)
        self.assertIn("bool hdrp_shaded(Surface s) { return s.valid && !s.deferred && s.hdrpShaded && s.distance < HDRP_SHADOW_RANGE; }", shadow)
        self.assertIn("if (s.valid && !s.deferred && s.hdrpShaded && g.material.z != INVALID) {", self.text(PT + "shaders/pt_compose.hlsl"))
        self.assertIn('"a surface drawn forward that the traced scene lacks keeps HDRP\'s light"', harness)
        # The light of what HDRP shadows itself stays in the share (the sun as far as the traced sun reaches the pixel): left
        # out, a candle blocked behind a sunlit head took the sun's light with its own (hair and eyes black in daylight).
        self.assertIn("if ((light.flags & LIGHT_HDRP_SHADOWS) != 0) { kept += w; continue; }", shadow)
        self.assertIn("if (g.sunColor.w < 1.5) { wsum += w; sun = w > 0.0; }", shadow)
        self.assertIn("else if (w > 0.0 && !occluded_from_surface(origin, cone_sample(g.sunDir.xyz, g.sunDir.w, rng.next2()), 0.0, g.params.y, nearBack)) kept += w;", shadow)
        self.assertIn("shares[p] = visible ? 1.0 : saturate((kept + a) / (kept + wsum + a));", shadow)
        # Emissive clusters are no light of HDRP's.
        self.assertIn("if (light.type == LIGHT_CLUSTER) continue;", shadow)
        # Every HDRP-shaded pixel is a texel of its kind, a share of 1 too.
        compose = self.text(PT + "shaders/pt_compose.hlsl")
        self.assertIn("outA[p] = float4(share, share, share, distance); outB[p] = float4(rr_clamp(max(add, 0.0)), 2.0); outC[p] = HIT_DISTANCE_MISS;", compose)
        # Eyes alone take the traced bounced light for HDRP's ambient taken from them; hair's traced color is not HDRP's.
        self.assertIn("if (s.eye && taken > 0.0 && cache_on()) {", compose)
        # Spatial Samples reaches 16 in the tracer as in the row (the row went to 16 while the tracer still stopped at 8).
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("k.diSpatial.numSamples=std::min(f.spatialSamples, 16u);", tracer)
        self.assertIn("k.giSpatial.numSamples=std::min(f.spatialSamples, 16u);", tracer)
        self.assertIn("eye = hdrp && (materials[hit.material].flags & MATERIAL_CAMERA_ONLY) != 0;", self.text(PT + "shaders/pt_prepare.hlsl"))
        self.assertNotIn("if (share < 0.999)", compose)
        # The share of the game's ambient light HDRP leaves hair and eyes still reaches the native side (TGPT_Frame.forward).
        self.assertIn("frame.Forward[0] = ForwardAmbientKept;", passes)
        self.assertIn("ForwardAmbientKept = cameraLight;", passes)
        d3d11 = self.text(PT + "shaders/d3d11.hlsl")
        # HDRP's color times the share, plus the traced bounced light it gains (B's color), into this frame's exposure.
        self.assertIn("if (weights <= 1e-3 && shadeWeights > 1e-3) return float4(max(shadeAdd / shadeWeights * shareRatio, 0.0), saturate(shade / shadeWeights));", d3d11)
        self.assertIn("if (tracedB.Load(int3(best, 0)).a > 1.5) return float4(max(tracedB.Load(int3(best, 0)).rgb * shareRatio, 0.0), saturate(tracedA.Load(int3(best, 0)).r));", d3d11)
        lights = self.text("src/TGPathTracing/SceneLights.cs")
        self.assertIn("if (HdrpShadows(light, hd, (p - camera).magnitude)) n.Flags |= Tgpt.LightHdrpShadows;", lights)
        self.assertIn("frame.SunColor[3] = sun.HdrpShadows && ShadowMaps ? 2f : 1f;", passes)
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertIn('HairShadows = file.Bind(Section, "Hair shadows", true, Row(order++, "Hair Shadows", null,', settings)
        self.assertIn("(HairShadows.Value ? 0u : Tgpt.OptionNoHdrpShadows) |", settings)
        self.assertIn('hairShadows = new ToggleOption(IdPrefix + "HairShadows", "Hair Shadows", true, false);', self.text("src/TGPathTracing/PathTracingRow.cs"))
        # The fill lights the tracer leaves out keep lighting the game's fog (an interior's haze) unless Fill Light Fog is off.
        self.assertIn('FillLightFog = file.Bind(Section, "Fill light fog", true, Row(order++, "Fill Light Fog", null,', settings)
        self.assertIn('fillLightFog = new ToggleOption(IdPrefix + "FillLightFog", "Fill Light Fog", true, false);', self.text("src/TGPathTracing/PathTracingRow.cs"))
        self.assertIn("bool fog = entry.volumetric && (settings == null || settings.FillLightFog.Value);", self.text("src/TGPathTracing/SceneLights.cs"))
        self.assertIn('"what HDRP shades itself is darkened where the traced scene shadows a lamp it doesn\'t"', harness)
        self.assertIn('"a lamp HDRP shadows itself darkens nothing twice"', harness)
        # Characters as far as 100 m, their acceleration structures refitted to each frame's pose and rebuilt every fourth.
        self.assertIn("private const float MaxDistance = 100f;", self.text("src/TGPathTracing/CharacterCapture.cs"))
        self.assertIn("constexpr uint32_t kCharacterRebuild=4;", scene)
        self.assertIn("D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD|D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE", scene)
        self.assertIn("mesh.skinScratchBytes=std::max(info.ScratchDataSizeInBytes, info.UpdateScratchDataSizeInBytes);", scene)
        self.assertIn("if(!rebuild) { desc.Inputs.Flags|=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE; desc.SourceAccelerationStructureData=m->blasVa; }", scene)

    def test_path_tracing_exposure(self):
        # Nothing traced depends on HDRP's exposure: the radiance clamps measure against the scene's reference radiance (a
        # white surface in full sun, or by the brightest light 1 m away), never the exposure (in pre-exposed units a hall's
        # bounced light followed the auto exposure), and traced light is stored relative to that reference (half precision)
        # with the built-in denoiser's history rescaled when it changes. The harness renders the same frames at two
        # exposures. The composite puts the one-frame-old traced light into this frame's exposure.
        restir = self.text(PT + "shaders/pt_restir.hlsli")
        self.assertIn("float l = luminance(radiance) / clamp_reference();", restir)
        # The clamps' reference is the sun's, or the brightest light's 25 cm away: indoors a candle-lit table and a
        # fireplace's walls are the bounced light's main sources.
        self.assertIn("double ref=std::max(sun, lamp), clampRef=std::max(sun, 16.0*lamp);", self.text(PT + "src/tracer.cpp"))
        self.assertNotIn("exposure()", restir)
        self.assertIn("if (!(l < 1e30)) return 0.0;", restir)
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("const double lamp=double(st.scene.brightest_light())/3.14159265358979;", tracer)
        self.assertIn("k.radiance[0]=float(st.reference); k.radiance[1]=float(previous>0.0 ? previous : st.reference); k.radiance[2]=float(st.clampReference);", tracer)
        # While the traced light replaces HDRP's, HDRP keeps the game's exposure (everything HDRP draws itself is made for
        # it: with HDRP's auto exposure on the traced image, flames, glows, fog and hair blew out) and the traced light gets
        # its own gain, metered on the traced light (pt_meter: median towards a mid tone, the brightest fiftieth at most 1.4
        # times white).
        # The lights left out as placed light nothing HDRP shades itself either.
        passes = self.text("src/TGPathTracing/HdrpPasses.cs")
        self.assertNotIn("profile.Add<Exposure>", passes)
        # Never darker than the game's own exposure (metering sunny views down to a mid tone left exteriors underexposed);
        # dark views brightened part of the way to a mid tone as Dark Adaptation sets; Exposure adds the player's offset
        # (both rows in the Video tab and FoA Mod Manager; the old hidden compensation carries over).
        self.assertIn("float brighten = Mathf.Max(0f, Mathf.Min((MidTone - mid) * adaptation, Highlight - high));", passes)
        self.assertIn("return Mathf.Min(brighten, most);", passes)
        self.assertIn("return float.IsNaN(automatic) ? tracedLog2Gain : automatic + Mathf.Clamp(settings.Exposure.Value, -3f, 3f);", passes)
        # Shadow Detail: after every denoiser (HDRP's step before its post-processing), strongest in dark, uneven views, the
        # row's value applied at once; the curve keeps everything above a mid tone and black as they are.
        self.assertIn('AccessTools.Method(typeof(HDRenderPipeline), "BeforeCustomPostProcessPass")', passes)
        self.assertIn("target = SmoothStep(0f, 2f, automatic) * SmoothStep(4.5f, 6.5f, Session.Status.TracedLog2High - Session.Status.TracedLog2Mid);", passes)
        self.assertIn("ShadowStrength = Mathf.Clamp(settings.ShadowDetail.Value, 0, 100) / 100f * shadowActivation;", passes)
        d3d11 = self.text(PT + "shaders/d3d11.hlsl")
        self.assertIn("float d = max(0.0, -3.06 - log2(max(y, 1e-12)));", d3d11)
        self.assertIn("return c * exp2(c1.x * min(3.0, 0.4 * h));", d3d11)
        self.assertIn("else if(eventId==TGPT_EVENT_SHADOW_DETAIL) shadow_detail_event(sequence);", self.text(PT + "src/tracer.cpp"))
        self.assertIn('"Shadow Detail lifts the dark below a mid tone along its curve and leaves the rest and black alone"', self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn("private const float Highlight = 0.5f;", passes)
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertIn('DarkAdaptation = file.Bind(Section, "Dark adaptation", 50, Row(order++, "Dark Adaptation", new AcceptableValueRange<int>(0, 100),', settings)
        self.assertIn('var legacy = file.Bind(Advanced, "Auto exposure compensation", 0f, Hidden("Replaced by Exposure."));', settings)
        # The eyes stop camera rays and stay HDRP's (its eye shader refracts the iris): traced, they took the sclera's
        # texture; left out, the camera ray went on to the socket behind them. Both came out white.
        self.assertIn("AlphaCutoff = 0f, Flags = Tgpt.MaterialAlphaTest | Tgpt.MaterialCameraOnly | Tgpt.MaterialHdrpShaded });", self.text("src/TGPathTracing/SceneCapture.cs"))
        # Only what HDRP shades itself (the eyes, for now) keeps its shading; hair drawn forward is the tracer's.
        self.assertIn("const bool hdrp = (materials[hit.material].flags & MATERIAL_HDRP_SHADED) != 0;", self.text(PT + "shaders/pt_prepare.hlsl"))
        self.assertIn('"a surface HDRP shades itself stops the camera ray and keeps its shading"', self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn("float most = Mathf.Clamp(gameEv - settings.ExposureLowest.Value, 0f, 12f);", passes)
        self.assertIn("TracedGain = TracedGain,", passes)
        # The gain's automatic part fades as HDRP's fog (laid over the composite at the game's exposure) hides a surface, in
        # what the composite shows and what the meter measures alike; the player's Exposure stays.
        self.assertIn("float exStored = ex * reference_radiance() * (s.valid ? traced_gain_at(s.position) : traced_gain());", self.text(PT + "shaders/pt_compose.hlsl"))
        meter = self.text(PT + "shaders/pt_meter.hlsl")
        self.assertIn("float v = luminance(light) / traced_gain_at(s.position);", meter)
        self.assertNotIn("/ traced_gain();", meter)
        common_text = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("return player * (1.0 + (automatic - 1.0) * exp(-height_fog_depth(g.cameraPos.xyz, position)));", common_text)
        self.assertIn("return k * len * (below + (1.0 - below) * mean);", common_text)
        self.assertIn("frame.Fog[3] = Mathf.Pow(2f, Mathf.Clamp(settings.Exposure.Value, -3f, 3f));", passes)
        self.assertIn("FogExtinction = 1f / fog.meanFreePath.value; FogBaseHeight = fog.baseHeight.value; FogScaleHeight = layer * 0.144765f;", passes)
        self.assertIn("for(int i=0;i<4;++i) k.fog[i]=std::isfinite(f.fog[i]) ? f.fog[i] : 0.f;", self.text(PT + "src/tracer.cpp"))
        self.assertIn("meter:pt_meter", self.text("CMakeLists.txt"))
        self.assertIn("e.status.tracedLog2Mid=percentile(0.5); e.status.tracedLog2High=percentile(0.98);", self.text(PT + "src/tracer.cpp"))
        self.assertIn("k.diSpatial.discountNaiveSamples=0u;", self.text(PT + "src/tracer.cpp"))
        self.assertIn('"the traced light\'s gain scales the traced light"', self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn("SceneLights.DarkenPlaced(on);", passes)
        lights = self.text("src/TGPathTracing/SceneLights.cs")
        self.assertIn("if (data.lightDimmer != 0f) data.lightDimmer = 0f;", lights)
        self.assertIn("try { if (entry.data != null) { entry.data.lightDimmer = entry.dimmer; entry.data.affectsVolumetric = entry.volumetric; } } catch { }", lights)
        # Their fog as the game has it with Fill Light Fog on, none with it off, by HDRP's switch for it: the game's
        # LightWithOverride remembers the fog dimmer when an override starts and writes it back when it stops, so one started
        # while it was 0 left the fog dark afterwards.
        self.assertIn("if (data.affectsVolumetric != fog) data.affectsVolumetric = fog;", lights)
        self.assertNotIn("volumetricDimmer = 0f", lights)
        # An effect's lights (a portal: 131,072 cd, cut off 0.6 m from it in the game) keep HDRP's range; flames and lamps
        # keep their physical reach.
        self.assertIn("if (IsEffectLight(light)) return Mathf.Max(light.range, 0.01f);", lights)
        self.assertIn("var result = (placed, !placed && !kept && !flame, parent);", lights)
        self.assertIn("(light.intensity <= 0f && !Names(light.name, KeptNames))", lights)
        # HDRP's fog is the game's own while tracing, its ambient light too: it is the art of a place, and with HDRP at
        # the game's exposure the tavern's haze is the game's (the teal haze came with the auto exposure, gone since).
        self.assertNotIn("Add<Fog>", passes)
        # What HDRP still lights with its sky (the ambient light and reflections of glass, eyes, hair) is scaled by the
        # light the tracer measures at the camera against the open sky's (pt_probe.hlsl): glass glowed in a dark room.
        # The game's own values times that: a volume weighted 1 - cameraLight toward 0 above the game's.
        self.assertIn("scaled.Add<IndirectLightingController>().reflectionLightingMultiplier.Override(0f);", passes)
        self.assertIn("if (scaledVolume != null) scaledVolume.weight = 1f - cameraLight;", passes)
        self.assertIn("float value = GameIndirectMultiplier() * cameraLight;", passes)
        # Dark Adaptation measures what is seen too, HDRP's fog over the traced light (a copy of the image before post,
        # read back): a view the mist fills is not dark, and brightened as one it showed what the game's fog hides.
        self.assertIn("mid = Mathf.Max(mid, seenLog2Mid - seenLog2Gain);", passes)
        self.assertIn("CustomPassUtils.Copy(ctx, ctx.cameraColorBuffer, seenCopy);", passes)
        self.assertIn("ctx.cmd.RequestAsyncReadback(seenCopy.rt, request => OnSeen(request, gain));", passes)
        # HDRP's distortion (heat shimmer) is off while tracing: it resampled the undenoised traced light Ray Reconstruction
        # takes, and the air above candles boiled. The camera's own frame settings come back as they were.
        self.assertIn("data.renderingPathCustomFrameSettings.SetEnabled(FrameSettingsField.Distortion, false);", passes)
        self.assertIn("distortionCamera.renderingPathCustomFrameSettingsOverrideMask = savedMask;", passes)
        self.assertIn("dispatch(list, st.probe.Get(), {st.probeUav}, b, 1, 1);", self.text(PT + "src/tracer.cpp"))
        self.assertIn("\"the sky the camera sees is measured against the open sky's\"", self.text("tests/pathtracing_gpu_test.cpp"))
        # The sky only: counting the lamps' light the paths found made it rise in a lit room as if the sky were there.
        self.assertIn("if (!trace_closest(g.cameraPos.xyz, dir, 0.3, g.params.y, hit)) seen += open;", self.text(PT + "shaders/pt_probe.hlsl"))
        # Eyes and HDRP's opaque Unlit decals are in the traced scene for camera rays only: other rays pass through them.
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("if ((m.flags & MATERIAL_CAMERA_ONLY) != 0 && !g_cameraRays) return false;", common)
        self.assertIn("if(materials_[index].flags&(TGPT_MATERIAL_CAMERA_ONLY|TGPT_MATERIAL_GLASS)) { mesh.omm=Mesh::Omm::None; return; }", self.text(PT + "src/scene.cpp"))
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        self.assertIn('bool cameraOnly = unlit && !transparent && shaderName == "HDRP/Unlit";', capture)
        self.assertIn('bool eye = shaderName.StartsWith("TG/Character/Eye", StringComparison.OrdinalIgnoreCase);', capture)
        self.assertIn('"a surface seen by camera rays only casts no shadow"', self.text("tests/pathtracing_gpu_test.cpp"))
        # Surfaces HDRP draws forward (hair, beards, some props) are found along the camera ray in the traced scene and
        # lit by the tracer; the composite replaces HDRP's shading there with light already multiplied by their color.
        self.assertIn("const bool found = trace_closest(g.cameraPos.xyz, dir, max(dist - window, 0.0), dist + window, hit);", self.text(PT + "shaders/pt_prepare.hlsl"))
        self.assertIn("outB[p] = float4(rr_clamp(max(specular * exStored, 0.0)), 1.0);", self.text(PT + "shaders/pt_compose.hlsl"))
        d3d11 = self.text(PT + "shaders/d3d11.hlsl")
        self.assertIn("const bool forward = a.a < 0.5;", d3d11)
        self.assertIn("float3 base = forward ? 1.0 : decode(a.rgb);", d3d11)
        self.assertIn('"a surface HDRP draws forward is lit by the tracer like a deferred one"', self.text("tests/pathtracing_gpu_test.cpp"))
        # Within 30 m the finest LOD is traced (the game's switch as the camera came closer changed traced shadows at once),
        # registered from 40 m, and a LOD with a registered mesh stands in until the wanted one's is.
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        self.assertIn("internal const float NearDetail = 30f, PrefetchDetail = 40f;", capture)
        self.assertEqual(capture.count("float level = TracedLevel(levels, drawnLevels,"), 2)
        # params.x is the traced light's own gain (the only exposure-like factor, applied in compose), never HDRP's exposure.
        self.assertIn("k.params[0]=std::isfinite(f.tracedGain) && f.tracedGain>0.f ? std::clamp(f.tracedGain, 1.f/64.f, 4096.f) : 1.f;", tracer)
        self.assertIn("float exStored = ex * reference_radiance() * (s.valid ? traced_gain_at(s.position) : traced_gain());", self.text(PT + "shaders/pt_compose.hlsl"))
        self.assertIn("history *= rescale;", self.text(PT + "shaders/pt_temporal.hlsl"))
        for name, write in (("pt_gi_spatial.hlsl", "diffuse[p] = float4(to_storage(max(result, 0.0)), hitDistance);"),
                            ("pt_di_spatial.hlsl", "sun[p] = float4(to_storage(sunDiffuse + clamp_radiance(localDiffuse, LIMIT_DIRECT)), 1.0);"),
                            ("pt_specular.hlsl", "clamp_radiance(weight * radiance, LIMIT_REFLECTION)")):
            self.assertIn(write, self.text(PT + "shaders/" + name), name)
        passes = self.text("src/TGPathTracing/HdrpPasses.cs")
        self.assertNotIn("ClampExposure", passes)
        self.assertIn("if (pipeline != null) TrackExposure(pipeline, hd);", passes)
        self.assertIn('"the traced light doesn\'t depend on HDRP\'s exposure"', self.text("tests/pathtracing_gpu_test.cpp"))
        d3d11 = self.text(PT + "shaders/d3d11.hlsl")
        self.assertIn("float ratio = (en > 0.0 && et > 0.0 && en < 1e20 && et < 1e20) ? en / et : 1.0;", d3d11)
        self.assertIn("in.exposure.t.srv.Get(), st.inputs[prev.parity].exposure.t.srv.Get(), out.c.t.srv.Get()", self.text(PT + "src/tracer.cpp"))
        # A pixel no previous traced texel matches takes the light of a traced texel nearby, picked at random per pixel and
        # frame (one texel for a whole band drew streaks along the edge a quick turn revealed), never HDRP's own lighting.
        self.assertIn("if (stored > 0.0 && error < closest) { closest = error; best = n; }", d3d11)
        self.assertIn("c.cameraDelta[3]=float(frame.frameIndex&0xFFFFu);", self.text(PT + "src/tracer.cpp"))
        # MSVC takes at most 16380 characters in one string literal: the composite's HLSL is embedded in pieces.
        self.assertIn('string(SUBSTRING "${TGPT_D3D11_SOURCE}" ${TGPT_D3D11_OFFSET} 8000 TGPT_D3D11_CHUNK)', self.text("CMakeLists.txt"))

    def test_path_tracing_no_placed_lights(self):
        # No fake lights: only lights that stand for a flame, a lamp or an effect (by their own or a parent's name) are
        # traced, whatever they are called themselves (this game's candlesticks, candelabras and torches name their room
        # light PointLight_Ambience); fog and player lights are always left out. Kept lights reach as far as a physical
        # light still gives 50 lux (at most 40 m), not HDRP's range, and HDRP's fade with camera distance isn't applied.
        # Every light in the loaded scenes counts, including DontSave ones (Conversation Lighting Overhaul's).
        lights = self.text("src/TGPathTracing/SceneLights.cs")
        self.assertNotIn("string[] PlacedNames", lights)
        self.assertIn("return Mathf.Max(light.range, Mathf.Min(Mathf.Sqrt(Mathf.Max(intensity, 0f) / ReachLux), MaxReach));", lights)
        self.assertIn("n.Range = Reach(light, hd);", lights)
        self.assertNotIn("Fade(Vector3.Distance(p, camera)", lights)
        self.assertIn("foreach (var l in Resources.FindObjectsOfTypeAll<Light>())", lights)
        self.assertNotIn("FindObjectsByType<Light>", lights)
        for other in ("src/TGPathTracing/Probe.cs", "src/TGPathTracing/DebugCapture.cs"):
            self.assertIn("SceneLights.AllLights()", self.text(other))
        # Kept lights' HDRP dimmer carries the traced light's gain (what HDRP shades itself matches the traced light),
        # the traced light itself taken with the dimmer they had; interiors give rays no sky.
        self.assertIn("if (darken) ApplyGain(HdrpPasses.TracedGain);", lights)
        self.assertIn("return new Vector3(c.r, c.g, c.b) * OriginalDimmer(light, hd);", lights)
        # One record of a light's own dimmers, whether it was darkened as placed light, carried the gain, or both in turn:
        # path tracing off gives back exactly what the game had (two lists could leave a light at 0).
        self.assertIn("if (!originals.TryGetValue(id, out var entry)) { entry = (data, data.lightDimmer, data.affectsVolumetric); originals[id] = entry; }", lights)
        # Hair is shaded by the tracer, colored as its shader colors it (RealHair: _DiffuseColor times _DiffuseStrength; read as
        # the texture alone it came out grey), never left to HDRP's hair shader.
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        self.assertIn('float strength = m.HasProperty("_DiffuseStrength") ? Mathf.Clamp(m.GetFloat("_DiffuseStrength"), 0f, 4f) : 1f;', capture)
        self.assertNotIn("mat.Flags |= Tgpt.MaterialHdrpShaded;", capture) # set for the eyes alone (their own material)
        # Read again every frame: the game colors a character's hair on its clone after it first shows (far hair stayed grey).
        self.assertIn("if (hairColors.TryGetValue(index, out var read)) RefreshHairColor(m, index, read);", capture)
        self.assertIn("if (index < materialsUploaded && tracer != null && tracer.Connected) tracer.SetMaterials(index, new[] { mat }, 1);", capture)
        # Camera rays stop at hair (and what HDRP shades) whatever the alpha: HDRP decided the pixel's coverage, and by the
        # tracer's alpha a ray slipped between strands to the scalp, so far hair flickered with the traced skin behind it.
        self.assertIn("if ((m.flags & (MATERIAL_HDRP_SHADED | MATERIAL_STRANDS)) != 0 && g_cameraRays) return true;", self.text(PT + "shaders/pt_common.hlsli"))
        # A character's forward pixels no longer stay HDRP's whatever the camera ray meets (the rule: nothing keeps HDRP's
        # shading while tracing); characters' instances stay marked.
        self.assertNotIn("(instances[hit.instance].flags & INSTANCE_CHARACTER) != 0", self.text(PT + "shaders/pt_prepare.hlsl"))
        self.assertIn("(alpha ? TGPT_INSTANCE_ALPHA_TEST : 0u)|TGPT_INSTANCE_CHARACTER", self.text(PT + "src/scene.cpp"))
        # Glass (by name): shadow rays pass through by chance as bright as its color, micromaps skip it (they would make
        # its texels opaque to every ray), and while tracing it has a glass's finish (the game's values come back after).
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("if ((m.flags & MATERIAL_GLASS) != 0 && !g_cameraRays && !g_glassSolid) {", common)
        # every traversal: shadow, sky and bounced light, light through skin, and the tinted shadow rays
        self.assertEqual(common.count("ray_seed(origin, dir);"), 7)
        self.assertIn("if(materials_[index].flags&(TGPT_MATERIAL_CAMERA_ONLY|TGPT_MATERIAL_GLASS)) { mesh.omm=Mesh::Omm::None; return; }", self.text(PT + "src/scene.cpp"))
        # The game's materials are left as they are: the tracer gives glossy glass texels a mirror finish itself.
        self.assertNotIn("GlassFinish.Set(", self.text("src/TGPathTracing/HdrpPasses.cs"))
        self.assertIn("if (glass) { roughness = 0.03; f0 = 0.04; }", self.text(PT + "shaders/pt_prepare.hlsl"))
        # Light scattered under skin: HDRP's diffusion profiles (Burley, its own shape and filter radius, in the world's
        # metres) spread the traced diffuse light of solid-profile surfaces before the composite multiplies the albedo back.
        sss = self.text(PT + "shaders/pt_sss.hlsl")
        self.assertIn("return shape * (exp(-shape * r) + exp(-shape * r / 3.0)) / (8.0 * PI * r);", sss)
        self.assertIn("scratch[p] = float4(sum / max(weights, 1e-12), center.a);", sss)
        self.assertIn("if (!solid_transmission(b, feature)) return INVALID;", sss)
        self.assertIn("dispatch(list, st.sss.Get(), {out.a.uav, st.tmpC.uav, 0u}, b);", self.text(PT + "src/tracer.cpp"))
        self.assertIn("v = new Vector4(shape.x * mmPerUnit, shape.y * mmPerUnit, shape.z * mmPerUnit, radius / mmPerUnit);", self.text("src/TGPathTracing/HdrpPasses.cs"))
        self.assertIn('"light scattered under skin keeps the light\'s total"', self.text("tests/pathtracing_gpu_test.cpp"))
        # See-through glass: pt_prepare marks the smooth texels of glass (the camera ray finds its material), pt_specular
        # refracts through it (solid to that path) to what lies behind, and it has no diffuse light.
        self.assertIn("glass = glassCandidate && (flags & MATERIAL_GLASS) != 0;", self.text(PT + "shaders/pt_prepare.hlsl"))
        spec = self.text(PT + "shaders/pt_specular.hlsl")
        self.assertIn("float3 t = refract(-v, n, 1.0 / GLASS_IOR);", spec)
        self.assertIn("const float3 leaving = refract(t, far.normal, GLASS_IOR);", spec)
        self.assertIn("added += clamp_radiance((1.0 - fresnel_schlick(s.f0, nv)) * s.albedo * seen, LIMIT_REFLECTION);", spec)
        self.assertIn("if (s.glass) diffuse = 0.0;", self.text(PT + "shaders/pt_compose.hlsl"))
        self.assertIn('"see-through glass shows what lies behind it"', self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn('"see-through glass shows the room behind it about as bright as it is"', self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn("seen = shade_path(behind, dir2, 1u + min(g.settings.z, 3u), rng);", spec)
        self.assertIn('"grey glass lets light through as bright as its color (its shadow about 0.64 of a solid one\'s)"', self.text("tests/pathtracing_gpu_test.cpp"))
        # The Denoising tooltip: one short line per denoiser.
        self.assertLess(len(re.search(r'DenoiserDescription =\s*((?:"[^"]*"\s*\+?\s*)+);', settings_text := self.text("src/TGPathTracing/Settings.cs")).group(1)), 260)
        self.assertNotIn("DOOM", settings_text)
        self.assertIn("if (!on) RestoreAll();", lights)
        self.assertIn("Interior = !Sun.Valid || InteriorScene();", lights)
        for token in ['private static readonly string[] AlwaysPlacedNames = { "HeroLight", "Volumetric", "Fake" };',
                      "for (int depth = 0; t != null && depth <= 5; depth++, t = t.parent)", "if (Names(t.name, SourceNames)) placed = false;",
                      "if (IsPlacedLight(light))"]:
            self.assertIn(token, lights)
        for source in ['"Fire"', '"Candle"', '"Candela"', '"Brazier"', '"Lamp"', '"Lantern"', '"Torch"', '"VFX"']:
            self.assertIn(source, lights)
        self.assertIn("SceneLights.IsPlacedLight(light)", self.text("src/TGPathTracing/Probe.cs"))
        # The player's own Conversation Lighting Overhaul portrait light stays.
        self.assertIn('private static readonly string[] KeptNames = { "TGConversationLightingOverhaul" };', lights)
        # The dialogue light keeps the brightness its mod set at the game's exposure: the traced light's automatic gain is
        # taken back out of it (the player's Exposure stays).
        self.assertIn("if (Names(light.name, KeptNames)) color *= HdrpPasses.AutomaticGainInverse;", lights)
        self.assertIn("return Mathf.Min(1f, Mathf.Pow(2f, player - tracedLog2Gain));", self.text("src/TGPathTracing/HdrpPasses.cs"))
        self.assertIn("kept = Names(light.name, KeptNames);", lights)
        self.assertIn("if (kept) placed = false;", lights)

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
        self.assertIn("public enum TracingMode { Off, On }", settings)
        self.assertIn('internal static readonly string[] ModeNames = { "Off", "On" };', settings)
        self.assertIn('bool wasOn = stored == "Global Illumination" || stored == "Full";', settings)
        self.assertIn("if (wasOn) Mode.Value = ModeNames[(int)TracingMode.On];", settings)
        self.assertIn('var legacy = file.Bind(Section, "Indirect intensity", 1f, Hidden("No longer used."));', settings)
        rows = self.text("src/TGPathTracing/PathTracingRow.cs")
        self.assertIn('mode = VideoTab.Enum(IdPrefix, "Mode", "Path Tracing", (int)TracingMode.On, out modeChoices, Settings.ModeNames);', rows)
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
        self.assertIn("dispatch(list, st.compose.Get(), {rrg ? 1u : 0u, out.c.uav, nrdToStorage}, b);", tracer)
        # The reflections are divided by their albedo for the denoiser and multiplied back by compose alike (on see-through
        # glass an albedo that covers the light it lets through too: specular_guide_albedo).
        self.assertIn("float3 spec = specular_guide_albedo(s, ", self.text(PT + "shaders/pt_rrg_guides.hlsl"))
        self.assertIn("if (pass.a.x == 1u) specular *= specular_guide_albedo(s, ", self.text(PT + "shaders/pt_compose.hlsl"))
        self.assertIn("float3 a = specular_albedo(s.f0, s.roughness, nv);", self.text(PT + "shaders/pt_common.hlsli"))
        session = self.text("src/TGPathTracing/Session.cs")
        self.assertIn("(Status.Features & Tgpt.FeatureRayRegeneration) != 0 &&\n            Settings.CurrentDenoiser == DenoiserChoice.RayRegeneration;", session)
        helpers = self.text("tools/Package.Common.ps1")
        self.assertIn("Files=@('amd_fidelityfx_loader_dx12.dll','amd_fidelityfx_denoiser_dx12.dll')", helpers)
        self.assertIn("Kits/FidelityFX/denoisers/include/ffx_denoiser.h", self.text("CMakeLists.txt"))

    def test_path_tracing_glass_tint_and_guides(self):
        # The final shading's shadow rays (RTXDI's chosen light and the sun at G-buffer surfaces, both at path vertices)
        # take glass's color channel by channel; RTXDI's reuse keeps the binary test with glass passed by chance.
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("if (g_glassTint) { ++g_glassMet; return false; }", common)
        direct = common.split("float3 direct_diffuse(", 1)[1].split("float3 shade_path(", 1)[0]
        self.assertIn("if (nl > 0.0) result += g.sunColor.rgb * nl * transmittance(position + n * (dot(n, l) >= 0.0 ? offset : -offset), l, 0.0, g.params.y);", direct)
        # A translucent vertex (grass, leaves, thin cloth) is lit from behind too, as the camera's translucent pixels are, by
        # the share its material lets through.
        self.assertIn("float nl = translucent_cos(n, l, through);", direct)
        self.assertIn("float target = luminance(e) * translucent_cos(n, l, through);", direct)
        self.assertIn("float3 origin = position + n * (dot(n, chosenL) >= 0.0 ? offset : -offset);", direct)
        # (An emissive triangle's shadow ray ends at its sampled point; a light's short of it by the offset.)
        self.assertIn("else { dir = shadow_ray(chosen, origin, rng.next2(), tmax); tmax = max(tmax - offset, 0.0); }", direct)
        self.assertIn("* (wsum / (float(candidates) * chosenTarget)) * transmittance(origin, dir, 0.0, tmax);", direct)
        self.assertNotIn("occluded(", direct)
        di = self.text(PT + "shaders/pt_di_spatial.hlsl")
        self.assertIn("through = rtxdi_transmittance(surface, shadowPoint);", di)
        self.assertIn("through = transmittance_from_surface(sunOrigin, l, 0.0, g.params.y, nearBack);", di)
        self.assertNotIn("rtxdi_visible(", di)
        rtxdi = self.text(PT + "shaders/pt_rtxdi.hlsli")
        self.assertIn("bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample ls) { return rtxdi_visible(surface, ls); }", rtxdi)
        self.assertIn("if (!rtxdi_visible(surface, chosen)) RTXDI_StoreVisibilityInDIReservoir(r, 0.0, true);", self.text(PT + "shaders/pt_di.hlsl"))
        # The crossings in order, by a query that commits glass alone (the visibility test may report one twice: the
        # geometry has no NO_DUPLICATE_ANYHIT_INVOCATION), each strictly past the last, as often as the test met glass at most.
        walk = common.split("float3 glass_walk(", 1)[1].split("float3 transmittance(", 1)[0]
        for token in ["PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_OPAQUE) q;",
                      "for (uint crossing = 0; crossing < min(met, 8u) && t0 < tmax; ++crossing) {",
                      "& MATERIAL_GLASS) != 0) q.CommitNonOpaqueTriangleHit();",
                      "through *= sqrt(saturate(glass_color(inst, m, q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics())));",
                      "t0 = t + max(1e-4, t * 1e-5);"]:
            self.assertIn(token, walk)
        self.assertNotIn("NO_DUPLICATE_ANYHIT_INVOCATION", self.text(PT + "src/scene.cpp"))
        for name in ["transmittance(", "transmittance_from_surface("]:
            body = common.split("float3 " + name, 1)[1].split("\n}", 1)[0]
            self.assertIn("g_glassTint = true; g_glassMet = 0;", body)
            self.assertIn("g_glassTint = false;", body)
            self.assertIn("return g_glassMet > 0 ? glass_walk(origin, dir, tmin, tmax, g_glassMet) : 1.0;", body)
        # The denoisers take see-through glass as specular only: NRD leaves it alone as the sky, AMD's Ray Regeneration gets
        # no diffuse albedo, a specular albedo covering what it lets through, and a material type of its own, and the
        # composite corrects Ray Reconstruction's guides there (B's alpha -1 marks it; forward stays > 0.5).
        self.assertIn("if (!s.valid || !s.deferred || s.glass) {", self.text(PT + "shaders/pt_nrd_prep.hlsl"))
        rrg = self.text(PT + "shaders/pt_rrg_guides.hlsl")
        self.assertIn("normals[p] = float4(octahedral(s.normal), s.roughness, s.glass ? 1.0 / 3.0 : 0.0);", rrg)
        self.assertIn("diffuseAlbedo[p] = float4(s.glass ? 0.0 : sqrt(saturate(s.albedo)), 1.0);", rrg)
        self.assertIn("if (s.glass) a += (1.0 - fresnel_schlick(s.f0, nv)) * s.albedo;", common)
        self.assertIn("outB[p] = float4(rr_clamp(max(specular * exStored, 0.0)), s.glass ? -1.0 : 0.0);", self.text(PT + "shaders/pt_compose.hlsl"))
        d3d11 = self.text(PT + "shaders/d3d11.hlsl")
        # The bilinear taps take one kind of texel (tracedB's alpha: -1 glass, 0 deferred, 1 forward, 2 a share of HDRP's
        # shading), the one with the most weight among those the surface takes; the fallbacks forward or not.
        self.assertIn("int texel_kind(float k) { return k < -0.5 ? 0 : k < 0.5 ? 1 : k < 1.5 ? 2 : 3; }", d3d11)
        self.assertIn("const int want = forward ? (kinds[3] > kinds[2] ? 3 : 2) : (kinds[0] > kinds[1] ? 0 : 1);", d3d11)
        self.assertIn("|| texel_kind(tracedB.Load(int3(c, 0)).a) != want) continue;", d3d11)
        self.assertIn("if (wgt > heaviest) { heaviest = wgt; g_hitDistance = tracedC.Load(int3(c, 0)); g_glass = want == 0; }", d3d11)
        self.assertEqual(d3d11.count("g_glass = tracedB.Load(int3("), 1)
        self.assertEqual(d3d11.count("(tracedB.Load(int3(c, 0)).a > 0.5)"), 2)
        # The random pick for a deferred pixel never takes a forward texel's light, already times its own color.
        self.assertIn("if (tracedB.Load(int3(n, 0)).a > 0.5) continue;", d3d11)
        self.assertIn("if (g_glass) glass_guides(p);", d3d11)
        self.assertIn("normalGuide[p] = float4(n, 0.03);", d3d11)
        self.assertIn("float3 spec = saturate(env_brdf_approx2(0.04, 0.03 * 0.03, nv) + (1.0 - fresnel) * decode(albedo.Load(int3(p, 0)).rgb));", d3d11)
        self.assertEqual(d3d11.count("float3 env_brdf_approx2("), 1)
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("out.c.t.srv.Get(), in.nrm.t.srv.Get()};", tracer)
        self.assertIn("extras ? st.guides[0].uav.Get() : nullptr, extras ? st.guides[1].uav.Get() : nullptr, extras ? st.guides[2].uav.Get() : nullptr};", tracer)
        self.assertIn("st.c11->CSSetUnorderedAccessViews(0, 6, u, nullptr);", tracer)
        # Its hit distance is the distance to what is seen behind it, where it lets through more than it reflects.
        spec = self.text(PT + "shaders/pt_specular.hlsl")
        self.assertIn("if (luminance((1.0 - fresnel) * s.albedo) > luminance(fresnel)) hitDistance = seenDistance;", spec)
        # It reflects its lamps by its reflection ray meeting their spheres (a near-mirror's lobe is too narrow for points
        # sampled on a lamp's disc to fall in), and the direct light's samples leave its lamps' highlights out.
        self.assertIn("if (s.glass) radiance += reflected_lamps(s.position, origin, l, found ? length(hit.position - origin) : g.params.y);", spec)
        self.assertIn("sum += e * max(dist * dist, l.radius * l.radius + 1e-4) / (PI * l.radius * l.radius);", spec)
        # Both sides by the same rule, so a light another kind of estimate serves keeps its highlight on glass.
        self.assertIn("bool reflected_as_sphere(LightRecord l) { return l.type <= 1u && l.radius > 0.0 && (l.flags & LIGHT_NO_SPECULAR) == 0u; }", common)
        self.assertIn("if (!reflected_as_sphere(l)) continue;", spec)
        self.assertIn("if (s.glass && reflected_as_sphere(RAB_LoadLightInfo(RTXDI_GetDIReservoirLightIndex(r), false).l)) spec = 0.0;",
                      self.text(PT + "shaders/pt_di_spatial.hlsl"))
        # And the sun's disc where its reflection ray escapes towards it, which the sun's sample then leaves out on glass.
        self.assertIn("if (s.glass && !found && g.sunColor.w > 0.0 && dot(l, g.sunDir.xyz) >= g.sunDir.w)", spec)
        self.assertIn("radiance += g.sunColor.rgb / (2.0 * PI * max(1.0 - g.sunDir.w, 1e-7));", spec)
        self.assertIn("if (dot(s.normal, l) > 0.0 && !s.glass) spec += g.sunColor.rgb * through * ggx_specular(", self.text(PT + "shaders/pt_di_spatial.hlsl"))
        self.assertIn('"see-through glass reflects the sun"', self.text("tests/pathtracing_gpu_test.cpp"))
        harness = self.text("tests/pathtracing_gpu_test.cpp")
        for name in ["green glass tints the sun's light through it green, channel by channel",
                     "green glass tints a lamp's light through it green, channel by channel",
                     "light through green glass reaches path vertices green (bounced light)",
                     "no diffuse albedo guide on see-through glass",
                     "the specular albedo guide on see-through glass covers the light it lets through",
                     "the guide on see-through glass has the tracer's near-mirror roughness",
                     "the hit distance on see-through glass is the distance to what is seen behind it",
                     "see-through glass reflects the lamps around it"]:
            self.assertIn('"' + name + '"', harness)

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
        # MinHook's own install rules stay out of the staged mods: one install rule per mod DLL, and the notices of what
        # TGPathTracing.Native.dll compiles in beside it.
        self.assertEqual(len(re.findall(r"install\(TARGETS [^)]*COMPONENT TGNative\)", cmake)), 4)
        self.assertEqual(len(re.findall(r"install\(FILES [^)]*DESTINATION TGPathTracing/Notices [^)]*COMPONENT TGNative\)", cmake)), 3)
        self.assertEqual(cmake.count("COMPONENT TGNative"), 7)
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

    def test_path_tracing_emissive_lights(self):
        # Emissive surfaces as ReSTIR DI's lights (the Emissive Lights row, on by default): the option, flags and view as the
        # coordinator assigned them, mirrored in C#.
        header = self.text(PT + "include/tgpt_api.h")
        for token in ["#define TGPT_OPTION_EMISSIVE_LIGHTS 131072u", "#define TGPT_MATERIAL_EMISSIVE_ALBEDO 128u", "#define TGPT_INSTANCE_MOVING 16u",
                      "#define TGPT_VIEW_EMISSIVE 10u", "#define TGPT_VIEW_COUNT 11u"]:
            self.assertIn(token, header)
        managed = self.text("src/TGPathTracing/NativeTracer.cs")
        for token in ["internal const uint OptionEmissiveLights = 131072;", "internal const uint MaterialEmissiveAlbedo = 128;", "InstanceMoving = 16;", "Emissive = 10 }"]:
            self.assertIn(token, managed)
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertIn('EmissiveLights = file.Bind(Section, "Emissive lights", true, Row(order++, "Emissive Lights", null,', settings)
        self.assertIn("(EmissiveLights.Value ? Tgpt.OptionEmissiveLights : 0u)", settings)
        rows = self.text("src/TGPathTracing/PathTracingRow.cs")
        self.assertIn('emissiveLights = new ToggleOption(IdPrefix + "EmissiveLights", "Emissive Lights", true, false);', rows)
        self.assertIn("emissiveLights.Enabled = s.EmissiveLights.Value;", rows)
        self.assertIn("st.scene.emissiveLights=(f.options&TGPT_OPTION_EMISSIVE_LIGHTS)!=0;", self.text(PT + "src/tracer.cpp"))
        # Each point of an emitter's light is counted once: the light sampling takes it within its reach's window
        # (triangle_incident), and every path vertex that gathers diffuse light counts the rest through one helper,
        # emission_weight (which the radiance cache's samples must go through too); pt_gi's rays gather for the camera's
        # surface, a reflection's (and glass's) never do. ReSTIR DI gives an emissive triangle no specular light: the
        # reflection rays count it whole (counted in both, a glossy floor's reflection came out twice as bright).
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertEqual(common.count("float emission_weight(Hit hit, float3 from, bool gathers) {"), 1)
        self.assertIn("return 1.0 - smooth_window(dot(d, d), 1.0 / max(t.range * t.range, 1e-8), 1.0);", common)
        self.assertIn("return material_emission(m, st) * (emission_coverage(m, st) * c * w * 0.5 * twiceArea / max(d2, 1e-4));", common)
        self.assertIn("radiance += throughput * (path_emission(hit, m.emissive, from, gathers) + direct_diffuse(hit.position, hit.normal, m.reflectance, offset, rng, m.through));", common)
        self.assertIn("from = hit.position; gathers = true;", common)
        # its definition, shade_path's use, and the radiance cache's samples, which gather for the vertex they leave
        self.assertEqual(len(re.findall(r"\bpath_emission\(", common)), 3)
        self.assertIn("return path_emission(next, m.emissive, hit.position, true) + direct_diffuse(", common)
        self.assertIn("g_pathFrom = RAB_GetSurfaceWorldPos(surface); g_pathGathers = true;", self.text(PT + "shaders/pt_gi.hlsl"))
        self.assertIn("g_pathFrom = s.position; g_pathGathers = false;", self.text(PT + "shaders/pt_specular.hlsl"))
        rtxdi = self.text(PT + "shaders/pt_rtxdi.hlsli")
        self.assertIn("if (dot(s.normal, ls.dir) > 0.0 && !ls.emitter && !ls.diffuseOnly) specular = ", rtxdi)
        # A light HDRP keeps from affecting specular makes no highlight here either (TGPT_LIGHT_NO_SPECULAR, SceneLights).
        self.assertIn("r.diffuseOnly = (l.flags & LIGHT_NO_SPECULAR) != 0;", rtxdi)
        self.assertIn("if (hd != null && !hd.affectSpecular) n.Flags |= Tgpt.LightNoSpecular;", self.text("src/TGPathTracing/SceneLights.cs"))
        self.assertIn("return now.type == LIGHT_TRIANGLE && now.id != 0u && load_emitter(slot, true).id == now.id ? int(index) : -1;", rtxdi)
        # The previous frame's records are relative to its origin: its temporal reuse moves them to this frame's, as it does
        # the previous surfaces (the harness never moves the origin, every 512 m of travel in game).
        self.assertIn("if (previousFrame) t.position += g.prevOriginDelta.xyz;", common)
        self.assertIn("r.s.position += g.prevOriginDelta.xyz;", rtxdi)
        # A single-sided emissive surface glows from its front alone, the side its triangles face in the mesh's own space.
        self.assertIn("h.front = dot(n, rayDir) * determinant(linearPart) < 0.0;", common)
        self.assertIn("s.emissive = h.front || (m.flags & MATERIAL_DOUBLE_SIDED) != 0 ? material_emission(m, st) : 0.0;", common)
        emissive = self.text(PT + "shaders/pt_emissive.hlsl")
        self.assertIn("if (determinant((float3x3)toWorld) < 0.0) {", emissive)
        # A kept light's own surface (a lantern's glass round its candle) lights nothing: the light has its light.
        self.assertIn("if (dot(q, q) <= own * own) { flags |= EMITTER_PAIRED; break; }", emissive)
        self.assertIn("if ((t.flags & EMITTER_PAIRED) != 0) return 0.0;", common)
        # Emitters: static instances only (not what the plugin saw move, not stand-ins, not characters), brightest first, within
        # a triangle budget; the records' pass is built and dispatched.
        scene = self.text(PT + "src/scene.cpp")
        self.assertIn("if(inst.flags&(TGPT_INSTANCE_MOVING|TGPT_INSTANCE_STAND_IN)) continue;", scene)
        self.assertIn("if(!m || m->skinned || m->data.geometryClusters.size()!=m->data.geometries.size()+1) continue;", scene)
        self.assertIn("if(triangles+c.triangles>kEmitterBudget) { ++overBudget; continue; }", scene)
        self.assertIn("constexpr uint32_t kEmitterBudget=16384;", self.text(PT + "src/scene.hpp"))
        self.assertIn("emissive:pt_emissive", self.text("CMakeLists.txt"))
        self.assertIn('#include "pt_emissive.h"', self.text(PT + "shaders/pt_shaders.hpp.in"))
        self.assertIn("st.scene.record_emitters(list, st.emissive.Get());", self.text(PT + "src/tracer.cpp"))
        # HDRP's emission as the camera sees it: its exposure weight and Albedo Affects Emissive, and the renderers that moved.
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        for token in ['exposureWeight = m.HasProperty("_EmissiveExposureWeight") ? Mathf.Clamp01(m.GetFloat("_EmissiveExposureWeight")) : 1f;',
                      "private static float ExposureScale(float weight) => weight >= 0.999f ? 1f : Mathf.Lerp(1f / Mathf.Max(CurrentExposure(), 1e-12f), 1f, weight);",
                      'if (m.HasProperty("_AlbedoAffectEmissive") && m.GetFloat("_AlbedoAffectEmissive") > 0.5f) mat.Flags |= Tgpt.MaterialEmissiveAlbedo;',
                      "Moved(renderer.GetInstanceID(), world) ? Tgpt.InstanceMoving : 0u"]:
            self.assertIn(token, capture)
        # The harness's checks for it.
        gpu = self.text("tests/pathtracing_gpu_test.cpp")
        for check in ["an emissive surface's light converges to the same image with Emissive Lights on or off",
                      "an emissive surface sampled as a light lights the floor with far less noise",
                      "a reflection of an emissive surface counts its light once with Emissive Lights on",
                      "an emissive triangle's light is sampled in the right units and density",
                      "a single-sided emissive surface lights nothing behind it",
                      "an emissive surface right around a kept light is that light's surface: it adds no light of its own",
                      "emissive surfaces are sampled as lights except a light's own, moving ones and those past the triangle budget"]:
            self.assertIn('"' + check + '"', gpu)

if __name__ == "__main__":
    unittest.main(verbosity=2)
