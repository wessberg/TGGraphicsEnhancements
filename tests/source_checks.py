#!/usr/bin/env python3
"""Textual/package sanity checks: NOT Windows compilation or GPU validation."""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]

class SourceConsistency(unittest.TestCase):
    def text(self, relative):
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_managed_targets(self):
        tree = ET.fromstring(self.text("src/TGFrameGeneration/TGFrameGeneration.csproj"))
        self.assertEqual(tree.find(".//TargetFrameworks").text, "net6.0;net472")
        self.assertEqual(tree.find(".//Version").text, "0.5.0")
        self.assertEqual(tree.find(".//PlatformTarget").text, "x64")
        self.assertIn("BEPINEX_MONO", self.text("src/TGFrameGeneration/TGFrameGeneration.csproj"))
        self.assertIn("BEPINEX_IL2CPP", self.text("src/TGFrameGeneration/TGFrameGeneration.csproj"))

    def test_abi_and_exports(self):
        header = self.text("native/include/tgfg_api.h")
        managed = self.text("src/TGFrameGeneration/NativeAbi.cs")
        self.assertIn("0x00050000", header)
        self.assertIn("0x00050000", managed)
        self.assertIn("sizeof(TGFG_Status)==188", header)
        self.assertIn("sizeof(TGFG_Request)==48", header)
        self.assertIn("sizeof(TGFG_Camera)==416", header)
        self.assertIn("RequestSize = 48, StatusSize = 188, CameraSize = 416, HdrpInputsSize = 48", managed)
        self.assertIn("sizeof(TGFG_HdrpInputs)==48", header)
        native = self.text("native/src/control.cpp")
        symbols = re.findall(r"TGFG_CALL\s+(TGFG_\w+)\(", header)
        self.assertEqual(len(symbols), 9)
        for symbol in symbols:
            self.assertIn(symbol + "(", native)

    def test_reason_enum_matches(self):
        cpp = self.text("native/include/tgfg_policy.hpp")
        cs = self.text("src/TGFrameGeneration/NativeAbi.cs")
        cpp_body = re.search(r"enum class Reason[^\{]*\{([^}]+)", cpp).group(1)
        cs_body = re.search(r"enum Reason[^\{]*\{([^}]+)", cs).group(1)
        values = lambda s: dict(re.findall(r"(\w+)\s*=\s*(\d+)", s))
        self.assertEqual(values(cpp_body), values(cs_body))
        self.assertEqual(len(values(cpp_body)), 33)

    def test_no_third_party_fg_wrapper(self):
        # FSR and XeSS come from AMD's and Intel's own SDKs, never from a wrapper or patched runtime.
        paths = list((ROOT / "native").rglob("*")) + list((ROOT / "src").rglob("*.cs"))
        for path in paths:
            if not path.is_file() or path.suffix not in {".cpp", ".hpp", ".h", ".cs", ".hlsl"}:
                continue
            text = path.read_text(encoding="utf-8").lower()
            for forbidden in ["optiscaler", "requires patched"]:
                self.assertNotIn(forbidden, text, path)
        self.assertFalse((ROOT / "tools" / "patch_optiscaler.py").exists())

    def test_vendor_runtimes_are_signed_and_queried(self):
        fsr = self.text("native/src/fsr_backend.cpp")
        xess = self.text("native/src/xess_backend.cpp")
        self.assertIn('L"ADVANCED MICRO DEVICES"', fsr)
        self.assertIn('L"INTEL"', xess)
        for token in ["FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12", "ffxDispatchDescFrameGenerationPrepareV2",
                      "FFX_API_QUERY_DESC_TYPE_GET_VERSIONS"]:
            self.assertIn(token, fsr)
        # XeSS's multi-frame maximum comes from the runtime, and XeSS-FG always runs with XeLL.
        for token in ["xefgSwapChainGetProperties", "XEFG_SWAPCHAIN_USE_MAX_SUPPORTED_INTERPOLATED_FRAMES",
                      "xefgSwapChainD3D12GetInitializationParameters", "xefgSwapChainSetLatencyReduction",
                      "xefgSwapChainSetNumInterpolatedFrames", "XEFG_SWAPCHAIN_RV_ONLY_NOW"]:
            self.assertIn(token, xess)
        packaging = self.text("tools/Package.Common.ps1")
        for token in ["'amd_fidelityfx_framegeneration_dx12.dll'", "'libxess_fg.dll'", "'libxell.dll'", "LICENSE-XeSS.txt", "LICENSE-FidelityFX.md",
                      "'LICENSE-NVIDIA-RTX-SDKs.txt'", "'LICENSE-Streamline.txt'", "'LICENSE-NVIDIA-Reflex.txt'"]:
            self.assertIn(token, packaging)

    def test_single_latency_selector(self):
        rows = self.text("src/TGFrameGeneration/NativeVideoSettings.cs")
        caps = self.text("src/TGFrameGeneration/VideoCapabilities.cs")
        policy = self.text("native/include/tgfg_policy.hpp")
        self.assertIn('"Low Latency"', rows)
        self.assertNotIn('"NVIDIA Reflex", 1, "Off", "On"', rows)
        for label in ['"NVIDIA Reflex"', '"NVIDIA Reflex + Boost"', '"Intel XeLL"', '"AMD Anti-Lag 2"']:
            self.assertIn(label, caps)
        # Low latency may be Off only while frame generation is Off; a single choice greys the row out.
        self.assertIn("if (selected == FrameGenerationSetting.Off) allowed.Add(LowLatencySetting.Off);", caps)
        self.assertIn("latency.SetInteractabilityFunction((Func<bool>)(() => caps.AllowedLatencies(Selected).Length > 1));", rows)
        self.assertIn("select_latency", policy)
        self.assertIn("if(output==Output::XeSS) return c.xell?Latency::XeLL:Latency::Off;", policy)

    def test_frame_generation_inputs(self):
        # Inputs come from the upscaler (DLSS) when it runs and otherwise from HDRP's own buffers.
        hdrp = self.text("src/TGFrameGeneration/HdrpInputs.cs")
        for token in ["CustomPassInjectionPoint.BeforePostProcess", "ctx.cameraDepthBuffer", "ctx.cameraMotionVectorsBuffer",
                      "hd.taaJitter.x", "hd.actualWidth", "FrameSettingsField.MotionVectors", "FrameBridge.GameplayFrame(hd.camera, out uint frameId)",
                      "IssuePluginEventAndData(bridge.RenderEvent, CaptureEvent"]:
            self.assertIn(token, hdrp)
        self.assertIn("private const int CaptureEvent = 3;", hdrp)
        control = self.text("native/src/control.cpp")
        self.assertIn("eventId==3 && haveRenderFrame && renderFrameId==id && frame->renderBegun && !frame->renderEnded && frame->hdrpPending", control)
        self.assertIn("frame->evalCount?frame->evalCount:frame->hdrpCaptured?1u:0u", control)
        capture = self.text("native/src/ngx_capture.cpp")
        # The same jitter and motion vector scale HDRP's DLSS pass hands NGX, and the upscaler first.
        self.assertIn("constants.jitterOffset={-in.jitterX,-in.jitterY};constants.mvecScale={-1.f,-1.f};", capture)
        self.assertIn("if(s.upscalerSeen && frame.id-s.lastUpscalerFrame<=1u)", capture)
        self.assertIn("s.lastUpscalerFrame=renderFrameId;s.upscalerSeen=true;", capture)
        rows = self.text("src/TGFrameGeneration/NativeVideoSettings.cs")
        self.assertIn("HdrpInputs.Available || upscaling == null || upscaling.IsDLSSEnabled", rows)
        self.assertIn("upscaling.UpscalingTypeOption.onChange += (Action<ToggleOption>)(_ => RefreshInteractability());", rows)
        self.assertIn("InputSource = HdrpInputs.Available ||", self.text("src/TGFrameGeneration/VideoCapabilities.cs"))

    def test_il2cpp_ports(self):
        # Both runtimes build the Video rows and the HDRP inputs; IL2CPP registers the subclasses and runs
        # the IL2CPP base constructor, which Il2CppInterop's derived-object allocation skips.
        rows = self.text("src/TGFrameGeneration/NativeVideoSettings.cs")
        hdrp = self.text("src/TGFrameGeneration/HdrpInputs.cs")
        for source in (rows, hdrp):
            self.assertNotIn("#if BEPINEX_MONO", source)
            self.assertNotIn("only implemented for the Mono runtime", source)
        self.assertIn('Il2CppTypes.Construct(this, typeof(Setting), "NativeMethodInfoPtr__ctor_Protected_Void_0");', rows)
        self.assertIn('Il2CppTypes.Construct(this, typeof(DependentOption), "NativeMethodInfoPtr__ctor_Public_Void_PrefOption_Func_1_Boolean_Il2CppReferenceArray_1_PrefOption_0",', rows)
        self.assertIn("Il2CppTypes.Register<SwitchRowsOption>();", rows)
        # Tooltips must not return managed strings through Il2CppInterop's delegate bridge (game crash in String.Trim).
        self.assertIn("new Func<IntPtr>(() => IL2CPP.ManagedStringToIl2Cpp(text()))", rows)
        self.assertNotIn(".AddTooltip((Func<string>)", rows)
        # Setting inherits Element<SettingsMaster>; Il2CppInterop can't map its T without the tolerant lookup.
        self.assertIn("Il2CppTypes.Register<FrameGenerationVideoSetting>(genericBase: true);", rows)
        self.assertIn('AccessTools.Method(typeof(ClassInjector), "SystemTypeFromIl2CppType")', self.text("src/TGFrameGeneration/Il2CppTypes.cs"))
        self.assertIn('Il2CppTypes.Construct(this, typeof(CustomPass), "NativeMethodInfoPtr__ctor_Protected_Void_0");', hdrp)
        self.assertIn("Il2CppTypes.Register<FrameInputPass>();", hdrp)
        self.assertIn("private new void Execute(CustomPassContext ctx) => HdrpInputs.Execute(ctx);", hdrp)
        project = self.text("src/TGFrameGeneration/TGFrameGeneration.csproj")
        self.assertNotIn("(Mono only)", project)

    def test_plugin_identity(self):
        plugin = self.text("src/TGFrameGeneration/Plugin.cs")
        self.assertIn('Id = "com.wessberg.tgframegeneration"', plugin)
        self.assertIn('Name = "TG Frame Generation"', plugin)
        self.assertIn('BepInDependency("com.avalonmodmanager", BepInDependency.DependencyFlags.SoftDependency)', plugin)
        self.assertLess(plugin.index("ModManagers.KeepOutOfAvalon(Id, Logger);"), plugin.index("Session.Start(Config, Logger);"))
        for path in ["src/TGFrameGeneration/" + p for p in ["Plugin.cs", "Session.cs", "Diagnostics.cs", "NativeVideoSettings.cs"]]:
            self.assertNotIn("xperimental", self.text(path), path)

    def test_mod_manager_rows_match_video_tab(self):
        # The plugin config holds exactly the Video tab's rows, under the Video tab's names; the rest is hidden.
        settings = self.text("src/TGFrameGeneration/Settings.cs")
        rows = self.text("src/TGFrameGeneration/NativeVideoSettings.cs")
        names = re.findall(r'Row\(\w+Key, [^,]+, "([^"]+)", (\d)', settings)
        self.assertEqual([n for n, _ in names], ["Frame Generation", "Frame Generation Mode", "Frame Multiplier",
                                                  "Max Frame Multiplier", "Target Framerate", "Low Latency"])
        self.assertEqual([int(o) for _, o in names], list(range(6)))
        for name, _ in names:
            self.assertIn('"' + name + '"', rows)
        self.assertEqual(len(re.findall(r"file\.Bind\(AdvancedSection", settings)), 6)
        self.assertEqual(len(re.findall(r"Hidden\(\n?\s*\"", settings)), 6)
        self.assertNotIn("VideoSettings.cfg\")", self.text("src/TGFrameGeneration/Diagnostics.cs"))
        self.assertIn("ModManagers.Tick();", self.text("src/TGFrameGeneration/Session.cs"))

    def test_private_interposer_name(self):
        # Streamline-aware injectors hook a module named sl.interposer.dll; TGFG loads its own under a private name.
        self.assertIn('L"TGFG.Streamline.dll"', self.text("native/src/sl_runtime.cpp"))
        self.assertIn("'sl.interposer.dll'='TGFG.Streamline.dll'", self.text("Stage-Runtime.ps1"))
        names = re.search(r"function Get-TgfgRuntimeNames \{(.*?)\n\}", self.text("tools/Package.Common.ps1"), re.S).group(1)
        self.assertIn("'TGFG.Streamline.dll'", names)
        self.assertNotIn("'sl.interposer.dll'", names)
        self.assertIn('"TGFG.Streamline.dll"', self.text("src/TGFG.Autoload/Bootstrap.cs"))

    def test_direct_sdk_calls(self):
        code = "\n".join(self.text("native/src/" + p) for p in ["sl_runtime.cpp", "control.cpp", "dx11_bridge.cpp"])
        for name in ["slInit", "slSetD3DDevice", "slGetNewFrameToken", "slSetConstants", "slSetTagForFrame", "slDLSSGSetOptions", "slDLSSGGetState"]:
            self.assertIn(name, code)
        self.assertIn("sl::DLSSGMode::eDynamic", code)
        self.assertIn("dynamicTargetFrameRate", code)
        self.assertIn("generated_frames(s.request,s.status.maxGeneratedFrames)", code)
        self.assertIn("eBlockPresentingClientQueue", code)

    def test_api_separation(self):
        code = self.text("native/src/dx11_bridge.cpp")
        hooks = self.text("native/src/graphics_hooks.cpp")
        policy = self.text("native/include/tgfg_policy.hpp")
        self.assertIn("s.status.sourceApi=11;s.status.presentationApi=12", code)
        self.assertIn("clientFactory_->QueryInterface", code)
        self.assertIn("return d11_->QueryInterface(id,out)", code)
        self.assertIn("s.sl.createDevice", code)
        self.assertIn("s.sl.createFactory2", code)
        self.assertIn("D3D11CreateDeviceAndSwapChain", hooks)
        self.assertIn("sourceGraphicsApi!=11", policy.replace(" ", ""))

    def test_stable_client_buffer_zero(self):
        policy = self.text("native/include/tgfg_interop_policy.hpp")
        bridge = self.text("native/src/dx11_bridge.cpp")
        self.assertRegex(policy, r"virtual_buffer_count[^\{]+\{[^}]*return 1u;")
        self.assertRegex(policy, r"next_virtual_buffer[^\{]+\{\s*return 0u;")
        self.assertIn("output_->GetCurrentBackBufferIndex()", bridge)
        self.assertIn("return colors_[index].source.d11->QueryInterface", bridge)
        self.assertIn("DXGI_PRESENT_DO_NOT_WAIT", bridge)

    def test_interop_synchronization_order(self):
        code = self.text("native/src/dx11_bridge.cpp")
        present = code.split("HRESULT present(", 1)[1].split("HRESULT resize(", 1)[0]
        ordered = ["c11_->Signal(", "queue_->Wait(shared12_", "cmd->CopyResource(",
                   "queue_->ExecuteCommandLists(", "before_present(", "output_->Present(sync,flags);", "after_present(hr);",
                   "queue_->Wait(s.lastInputsFence", "queue_->Signal(shared12_", "c11_->Wait(shared11_"]
        last = -1
        for token in ordered:
            current = present.find(token, last + 1)
            self.assertGreater(current, last, token)
            last = current
        self.assertIn("D3D11_RESOURCE_MISC_SHARED_NTHANDLE", code)
        self.assertIn("OpenSharedHandle", code)
        self.assertIn("OpenSharedFence", code)
        self.assertIn("SwapDeviceContextState(game.Get(),nullptr)", code)
        self.assertNotIn("D3D11_USAGE_STAGING", code)

    def test_optional_hudless_is_not_fabricated(self):
        managed = self.text("src/TGFrameGeneration/FrameBridge.cs")
        self.assertNotIn("CaptureHudless", managed)
        self.assertNotIn("TGFG_TagHudless", managed)
        bridge = self.text("native/src/dx11_bridge.cpp")
        self.assertIn("slot.hudlessValid?&h:nullptr", bridge)
        self.assertIn("slot.hudlessValid=true", bridge)
        self.assertIn("frameId, void* context, void* colorResource", self.text("native/include/tgfg_api.h"))

    def test_hdr_conversion_is_actual_shader(self):
        shader = self.text("native/shaders/interop.hlsl")
        bridge = self.text("native/src/dx11_bridge.cpp")
        for token in ["bt2020*80.0", "2610.0/16384.0", "2523.0/32.0", "sourceTexture.Load"]:
            self.assertIn(token, shader)
        self.assertIn("encode_hdr(color.source.srv.Get(),color.encoded)", bridge)
        self.assertIn("SetColorSpace1(out)", bridge)
        self.assertIn("DXGI_FORMAT_R10G10B10A2_UNORM", bridge)

    def test_launcher_waits_for_entrypoint_then_bootstraps(self):
        managed = self.text("src/TGFrameGeneration/NativeBridge.cs")
        self.assertNotIn("static extern IntPtr LoadLibrary", managed)
        launcher = self.text("native/launcher/main.cpp")
        for token in ["CREATE_SUSPENDED", "DEBUG_ONLY_THIS_PROCESS", "stop_at_entrypoint", "DebugActiveProcessStop", '"TGFG_Bootstrap"', "-force-d3d11"]:
            self.assertIn(token, launcher)
        # Examine the main launch sequence, not the debugger helper's loader resume.
        main = launcher[launcher.index("int wmain("):]
        self.assertLess(main.index("stop_at_entrypoint("), main.index('"TGFG_Bootstrap"'))
        self.assertLess(main.index('"TGFG_Bootstrap"'), main.rindex("ResumeThread("))

    def test_cmake_inputs_exist_and_shader_test_is_windows_only(self):
        cmake = self.text("CMakeLists.txt")
        self.assertIn("VERSION 0.5.0", cmake)
        for path in re.findall(r"(?:native|tests)/[\w/.-]+\.(?:cpp|hpp|hlsl|in)", cmake):
            self.assertTrue((ROOT / path).is_file(), path)
        self.assertNotIn("resource_tracker.cpp", cmake)
        self.assertIn("tgfg_shader_tests", cmake)
        self.assertIn("interop_shader.hpp.in", cmake)
        self.assertLess(cmake.index("if(TGFG_BUILD_NATIVE)"), cmake.index("tgfg_shader_tests"))

    def test_installed_preflight(self):
        preflight = self.text("Test-Setup.ps1")
        helpers = self.text("tools/Package.Common.ps1")
        self.assertIn("Get-FileHash", helpers)
        self.assertIn("Install-manifest.json", preflight)
        self.assertIn("Test-TgfgPayload", preflight)
        self.assertNotRegex(preflight, r"Join-Path[^\n]*artifacts")
        self.assertNotIn("TGFG.Launcher.exe", preflight)
        # The retired launcher shim is gone; players start the game from Steam.
        self.assertFalse((ROOT / "Launch-Game.ps1").exists())

    def test_testing_limits_disclosed(self):
        readme = " ".join(self.text("README.md").replace("**", "").split())
        for token in ["not yet been confirmed in game", "and neither has the IL2CPP build", "The HUD is interpolated with the scene",
                      "HDRP's own depth and motion vectors"]:
            self.assertIn(token, readme)
        self.assertNotIn("requires D3D12 game rendering", readme)
        for path in ["README.md", "docs/PLAYER-INSTALL.txt"]:
            self.assertNotIn("experimental", self.text(path).lower(), path)

if __name__ == "__main__":
    unittest.main(verbosity=2)
