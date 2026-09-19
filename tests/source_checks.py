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
        self.assertEqual(tree.find(".//Version").text, "0.4.0")
        self.assertEqual(tree.find(".//PlatformTarget").text, "x64")
        self.assertIn("BEPINEX_MONO", self.text("src/TGFrameGeneration/TGFrameGeneration.csproj"))
        self.assertIn("BEPINEX_IL2CPP", self.text("src/TGFrameGeneration/TGFrameGeneration.csproj"))

    def test_abi_and_exports(self):
        header = self.text("native/include/tgfg_api.h")
        managed = self.text("src/TGFrameGeneration/NativeAbi.cs")
        self.assertIn("0x00040000", header)
        self.assertIn("0x00040000", managed)
        self.assertIn("sizeof(TGFG_Status)==152", header)
        self.assertIn("typeof(NativeStatus)) != 152", managed)
        native = self.text("native/src/control.cpp")
        symbols = re.findall(r"TGFG_CALL\s+(TGFG_\w+)\(", header)
        self.assertEqual(len(symbols), 8)
        for symbol in symbols:
            self.assertIn(symbol + "(", native)

    def test_reason_enum_matches(self):
        cpp = self.text("native/include/tgfg_policy.hpp")
        cs = self.text("src/TGFrameGeneration/NativeAbi.cs")
        cpp_body = re.search(r"enum class Reason[^\{]*\{([^}]+)", cpp).group(1)
        cs_body = re.search(r"enum Reason[^\{]*\{([^}]+)", cs).group(1)
        values = lambda s: dict(re.findall(r"(\w+)\s*=\s*(\d+)", s))
        self.assertEqual(values(cpp_body), values(cs_body))
        self.assertEqual(len(values(cpp_body)), 31)

    def test_no_alternative_fg_dependency(self):
        paths = list((ROOT / "native").rglob("*")) + list((ROOT / "src").rglob("*.cs"))
        for path in paths:
            if not path.is_file() or path.suffix not in {".cpp", ".hpp", ".h", ".cs", ".hlsl"}:
                continue
            text = path.read_text(encoding="utf-8").lower()
            for forbidden in ["optiscaler", "fidelityfx", "ffx_fsr", "requires patched"]:
                self.assertNotIn(forbidden, text, path)
        self.assertFalse((ROOT / "tools" / "patch_optiscaler.py").exists())

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
        self.assertIn("s.request.fixedMultiplier-1", code)
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
        self.assertIn("VERSION 0.4.0", cmake)
        for path in re.findall(r"(?:native|tests)/[\w/.-]+\.(?:cpp|hpp|hlsl|in)", cmake):
            self.assertTrue((ROOT / path).is_file(), path)
        self.assertNotIn("resource_tracker.cpp", cmake)
        self.assertIn("tgfg_shader_tests", cmake)
        self.assertIn("interop_shader.hpp.in", cmake)
        self.assertLess(cmake.index("if(TGFG_BUILD_NATIVE)"), cmake.index("tgfg_shader_tests"))

    def test_installed_preflight(self):
        preflight = self.text("Test-Setup.ps1")
        helpers = self.text("tools/Package.Common.ps1")
        compatibility = self.text("Launch-Game.ps1")
        self.assertIn("Get-FileHash", helpers)
        self.assertIn("Install-manifest.json", preflight)
        self.assertIn("Test-TgfgPayload", preflight)
        self.assertNotRegex(preflight, r"Join-Path[^\n]*artifacts")
        self.assertNotIn("TGFG.Launcher.exe", preflight)
        self.assertIn("Test-Setup.ps1", compatibility)
        self.assertNotIn("-force-d3d11", compatibility)
        self.assertNotIn("Copy-Item", compatibility)
        self.assertNotIn("Start-Process", compatibility)

    def test_testing_limits_disclosed(self):
        readme = self.text("README.md").replace("**", "")
        for token in ["experimental source", "not been compiled", "No FoA/GPU run", "not a verified", "HUD"]:
            self.assertIn(token, readme)
        self.assertNotIn("requires D3D12 game rendering", readme)

if __name__ == "__main__":
    unittest.main(verbosity=2)
