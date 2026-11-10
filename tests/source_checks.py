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

    def test_path_tracing_mirrors_are_replaced_for_nrd(self):
        # PT-127, primary surface replacement (NRD README's section on it; docs/agents/psr.md, psr-2.md): where the camera
        # sees a still pure mirror, NRD is told of the surface the mirror shows. The rule wherever NRD denoises with the
        # tracer's own camera rays since the game's answer of 2026-10-11 (run 20261011-213531: the detail a tavern's mirror
        # keeps under NRD 2.41 against 0.47): no setting, no frame flag (bit 22, the trial's, is free again).
        managed = self.text("src/TGPathTracing/NativeTracer.cs") + self.text("src/TGPathTracing/Settings.cs") + self.text("src/TGPathTracing/HdrpPasses.cs")
        native = self.text(PT + "include/tgpt_api.h")
        for gone in ("PrimarySurfaceReplacement", "Primary surface replacement"):
            self.assertNotIn(gone, managed, gone)
        self.assertIsNone(re.search(r"\bFrame\w+\s*=\s*1u\s*<<\s*22\b", managed))
        self.assertNotIn("PRIMARY_SURFACE_REPLACEMENT", native)
        self.assertIsNone(re.search(r"#define TGPT_FRAME_\w+\s+\(1u\s*<<\s*22\)", native))
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("const bool psrRecords=useNrd && !(f.options&TGPT_OPTION_GBUFFER_SURFACES);", tracer)
        self.assertIn("if(++st.psrSerial==0) st.psrSerial=1;", tracer)
        self.assertIn("k.psr[0]=st.psr.uav; k.psr[3]=st.psrSerial;", tracer)
        self.assertIn("n.primarySurfaceReplacement=psrRecords && st.psr.uav!=kInvalid;", tracer)
        # The record: 64 bytes a pixel (the trial's had 112), written only at mirror pixels and never cleared: it counts in
        # the frame whose serial it carries.
        record_file = self.text(PT + "shaders/pt_psr_record.hlsli")
        record = record_file.split("struct PsrRecord {", 1)[1].split("};", 1)[0]
        self.assertEqual(re.findall(r"^\s*(float4|uint4)\s+(\w+);", record, re.M),
                         [("float4", "motionDepth"), ("uint4", "light"), ("uint4", "surface"), ("uint4", "material")])
        self.assertIn("sizeof(RecordLayout)==64", self.text(PT + "src/primary_surface_replacement.hpp"))
        self.assertIn("uint psr_state(uint4 surface) { return surface.w == g.psr.w ? surface.z >> 24u : 0u; }", record_file)
        # The camera pass marks a still pure mirror's pixel, after its surface is written, and writes nothing elsewhere.
        primary = self.text(PT + "shaders/pt_primary.hlsli")
        self.assertIn("if ((nrm[p].y & 0xFFu) != 0u || (mat[p].x & 0x00FFFFFFu) != 0u) return;", primary)
        self.assertIn("records[psr_index(p)].surface = uint4(0u, 0u, PSR_CANDIDATE << 24u, g.psr.w);", primary)
        self.assertIn("own = write_traced(p, hit, dir, w.stop.t);\n            psr_primary(p, w);", primary.replace("\r\n", "\n"))
        # The reflections' pass alone compiles the capture, and its thread carries one word of it across its rays (no
        # record held in a global: "Code that is switched off still costs frame time", and Shader Execution Reordering
        # moves what is alive across a ray with the thread).
        shaders = ROOT / PT / "shaders"
        defining = sorted(path.name for path in shaders.glob("*.hlsl*") if "#define PT_PSR_CAPTURE" in path.read_text(encoding="utf-8"))
        self.assertEqual(defining, ["pt_specular.hlsl"])
        specular = self.text(PT + "shaders/pt_specular.hlsl")
        self.assertIn('#include "pt_psr_capture.hlsli"', specular)
        self.assertIn("g_psrSlot = traced && found && psr_mirror(s, p) && psr_begin(p, s, seen, met) ? psr_index(p) : INVALID;", specular)
        self.assertIn("if (g_psrSlot != INVALID) { psr_finish(p, s, hit, l, weight * s.specularOcclusion * g_psrFogTransmission); g_psrSlot = INVALID; }", specular)
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("static uint g_psrSlot = INVALID;", common)
        self.assertNotIn("static PsrRecord", common)
        self.assertIn("if (psrEnd) psr_capture(m.normal, roughness, m.albedo, f0, psrDiffuse, psrSpecular);", common)
        self.assertIn("if (secondWay && g_psrSlot != INVALID) psr_capture_reflection(continuationWeight * beyondIt, secondDist);", common)
        # A pure mirror: no roughness and no diffuse colour (a delta event), standing still, flat.
        capture = self.text(PT + "shaders/pt_psr_capture.hlsli")
        self.assertIn("s.roughness != 0.0 || any(s.albedo != 0.0) ||", capture)
        # NRD README's inputs for a replaced surface: the virtual point on the pixel's own view ray, its depth and motion,
        # the normal mirrored, the view direction in the mirror's world, hit distances that start at the replaced surface.
        self.assertIn("const float3 virtualPoint = mirror.position + viewRay * length(endpoint.position - mirror.position);", capture)
        self.assertIn("const float3 virtualPrevious = virtualPoint + reflect(previous - endpoint.position, mirror.normal);", capture)
        self.assertIn("records[slot].surface = uint4(pack_normal(reflect(normal, n)),", capture)
        self.assertIn("pack_normal16(reflect(-incoming, n)) | (f32tof16(throughput.b) << 16u),", capture)
        self.assertIn("return trace_closest(origin, direction, 0.0, g.params.y, next) ? min(length(next.position - endpoint.position), HIT_DISTANCE_MISS) : HIT_DISTANCE_MISS;", capture)
        prep = self.text(PT + "shaders/pt_nrd_prep.hlsl")
        self.assertIn("const float z = replacement.motionDepth.w;", prep)
        self.assertIn("motion[p] = float4(-replacement.motionDepth.xy, replacement.motionDepth.z, 0.0);", prep)
        self.assertIn("normalRoughness[p] = NRD_FrontEnd_PackNormalAndRoughness(psr_normal(replacement), endRoughness, psr_class(replacement));", prep)
        self.assertIn("NRD_MaterialFactors(psr_normal(r), psr_view(r), saturate(psr_albedo(r)), psr_f0(r), psr_roughness(r), d, s);", prep)
        self.assertIn("diffuse[p] = RELAX_FrontEnd_PackRadianceAndHitDist(d, endDiffuse.w, true);", prep)
        self.assertIn("specIn[p] = RELAX_FrontEnd_PackRadianceAndHitDist(sp, endSpecular.w, true);", prep)
        # No light cut and none added: what the record's two parts do not hold comes back as traced.
        self.assertIn("const float3 residual = original.rgb - endDiffuse * hd - endSpecular * hs;", prep)
        self.assertIn("float3 back = residual + (denoisedD * df + denoisedS * sf) * inverseScale;", prep)
        self.assertIn("own[p] = float4(max(back, 0.0), original.a);", prep)
        # With the records NRD compares material classes (README: not to mix a metal's diffuse light with a non-metal's).
        denoiser = self.text(PT + "src/nrd_denoiser.cpp")
        self.assertIn("if(f.primarySurfaceReplacement) relax.minMaterialForDiffuse=relax.minMaterialForSpecular=0.f;", denoiser)
        self.assertIn("if(f.primarySurfaceReplacement) reblur.minMaterialForDiffuse=reblur.minMaterialForSpecular=0.f;", denoiser)
        # Ray Reconstruction's guides are as they were: the first worker's two changes to them (a hair pixel's guides from
        # the surface its ray stopped at; glass's reflection distance from a ray of its own) wait for the game's answer.
        compose = self.text(PT + "shaders/pt_compose.hlsl")
        for text in (compose, primary, specular, self.text(PT + "shaders/d3d11.hlsl")):
            self.assertNotIn("psr_trial", text)
            self.assertNotIn("rr_coherent_guides", text)
        self.assertIn("if (w.met) {", primary)
        self.assertIn("specular[p] = float4(to_storage(finite_radiance(glassLight)), glassFound ? glassDistance : HIT_DISTANCE_MISS);", specular)
        # The offline check (TGPT_TEST_PSR_ONLY=1, alone) reads the records through a view only it can ask for.
        self.assertIn('GetEnvironmentVariableA("TGPT_TEST_PSR_VIEW", view, sizeof(view))', tracer)
        self.assertIn("if (g.psr.z != 0u && g.psr.z <= 5u) {", compose)
        gpu = self.text("tests/pathtracing_gpu_test.cpp")
        self.assertIn('if(GetEnvironmentVariableA("TGPT_TEST_PSR_ONLY", nullptr, 0)>0)\n#include "pathtracing_psr_checks.inl"', gpu.replace("\r\n", "\n"))
        checks = self.text("tests/pathtracing_psr_checks.inl")
        for told in ("the view depth NRD is told is the panel's in the mirror's virtual world", "the normal NRD is told is the panel's mirrored in the mirror's plane",
                     "the motion NRD is told is the virtual point's between the two frames' cameras", "the hit distances start at the replaced surface",
                     "with NRD a mirror's picture of the rough panel holds the undenoised picture's light", "a floor that is no pure mirror keeps its own surface for NRD"):
            self.assertIn(told, checks)
        # The harness's two recipes are plain looks now: the world running, no denoiser, then NRD; nothing to switch.
        recipes = json.loads(self.text("tests/ingame/scenarios.json"))["recipes"]
        for name in ("psr", "psr-mirror"):
            steps = recipes[name]
            self.assertTrue(all(step.get("live") for step in steps))
            self.assertEqual([step["name"] for step in steps], ["0-game", "1-none", "2-nrd", "3-off"])
            self.assertFalse([setting for step in steps for setting in step["set"] if "surface replacement" in setting["key"]])

    def test_managed_targets(self):
        props = ET.fromstring(self.text("src/Game.References.props"))
        self.assertEqual(props.find(".//TargetFrameworks").text, "net6.0;net472")
        self.assertEqual(props.find(".//PlatformTarget").text, "x64")
        for token in ["BEPINEX_MONO", "BEPINEX_IL2CPP"]:
            self.assertIn(token, self.text("src/Game.References.props"))
        for project in ["TGGraphicsBridge", "TGFrameGeneration", "TGBetterUpscaling", "TGPathTracing", "TGNeuralRendering"]:
            tree = ET.fromstring(self.text(f"src/{project}/{project}.csproj"))
            self.assertEqual(tree.find(".//Version").text, "1.2.4", project)
        self.assertEqual(ET.fromstring(self.text("src/TGGraphicsBridge.Autoload/Autoload.props")).find(".//Version").text, "1.2.4")
        # The mods built on the bridge reference it without copying it (Build.ps1 bundles it), and build
        # their own copy of its preloader.
        for project in ["TGFrameGeneration", "TGBetterUpscaling", "TGPathTracing", "TGNeuralRendering"]:
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
        # 12, plus the troubleshooting report's diagnostics, timings and notes, presenting without tearing,
        # and DLSS Frame Generation's preset.
        self.assertEqual(len(symbols), 17)
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
        # Ray Reconstruction's reflection and transparency guides from TG Path Tracing (looked up by name), the
        # traced reflections' hit distance and the color before transparency, into NGX's evaluation.
        self.assertIn("uint64_t specularHitDistance, colorBeforeTransparency;", header)
        self.assertIn('extraGuides = type.GetMethod("TryGetExtraGuides", BindingFlags.Public | BindingFlags.Static); // nor this',
                      self.text("src/TGBetterUpscaling/PathTracingLink.cs"))
        # Reflection distance is required independently; it is read as a single float channel (through the
        # color view it was refused every frame). Only transparency can be omitted/retried; reflection distance stays.
        # The smoke test hands both to Ray Reconstruction with every frame.
        upscaler = self.text("native/upscaling/src/upscaler.cpp")
        self.assertIn("copyGuide(f.specularHitDistance, st.guideHitDistance, DXGI_FORMAT_R16_FLOAT, tgb::ReadAs::Depth);", upscaler)
        self.assertIn('log_throttled(std::string("Ray Reconstruction\'s optional transparency guide is left out: ")+e.what());', upscaler)
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
        self.assertIn("motion[p] = float4(-motion_at(p), motion_depth_change(p, s.position), 0.0);", prep)  # (2.5D: PT-127)
        self.assertIn("diffuse[p] = RELAX_FrontEnd_PackRadianceAndHitDist(light, hitDiffuse, true);", prep)
        self.assertIn("motion[p] = float4(-motion_at(p), -motion_depth_change(p, s.position), 0.0);", self.text(PT + "shaders/pt_rrg_guides.hlsl"))  # (the point's own previous depth: PT-127)
        nrd = self.text(PT + "src/nrd_denoiser.cpp")
        self.assertIn("c.motionVectorScale[0]=c.motionVectorScale[1]=c.motionVectorScale[2]=1.f;", nrd)
        self.assertIn("std::vector<D3D12_GPU_VIRTUAL_ADDRESS> constantsOf(count, 0);", nrd)
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("const bool restart=!previous || !(st.nrdScale>0.0) || !(median>=1.0/16.0 && median<=2.0);", tracer)
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
        self.assertIn("if(NVSDK_NGX_FAILED(r) && transparency) {", rr)
        self.assertIn("eval.pInSpecularHitDistance=in.specularHitDistance;", rr)
        self.assertIn("eval.pInColorBeforeTransparency=in.colorBeforeTransparency; eval.pInColorAfterTransparency=in.color;", rr)
        self.assertIn("list->ResourceBarrier(UINT(readCount+1), barriers);", rr)
        self.assertNotIn("list->ResourceBarrier(7, barriers);", rr)
        # PT-127 / RR 4: dropping transparency cannot drop the required reflection information.
        self.assertNotIn("eval.pInSpecularHitDistance=nullptr", rr)
        self.assertIn("!in.normalRoughness || !in.specularHitDistance", rr)
        self.assertIn("NGX_DLSSD_GET_OPTIMAL_SETTINGS(caps", rr)
        self.assertIn("renderWidth != rrRenderWidth || renderHeight != rrRenderHeight", self.text("src/TGBetterUpscaling/StpHost.cs"))
        compose = self.text(PT + "shaders/pt_compose.hlsl")
        self.assertIn("float3 specular = specular_guide_albedo", compose)
        self.assertIn("outC[p] = float4(rrSpecular, spec[p].a);", compose)
        self.assertIn("tracedC.Load(int3(g_guideTexel, 0)).rgb", self.text(PT + "shaders/d3d11.hlsl"))
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
        # RR 19: scope the exact-size correction to the selected camera and HDRP's size calculation.
        self.assertEqual(host.count("candidate.Patch("), 4)
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
        self.assertEqual(before.count("return StpRuns(hdCamera);"), 5) # RR dimension mismatch also falls back
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
        recorder = self.text("src/Shared/CustomPassRecorder.cs")
        il2cpp = re.sub(r"//.*", "", compiled_for(recorder, True))
        self.assertNotIn(".ExecuteInternal(", il2cpp)
        self.assertIn("args[count++] = (void*)IL2CPP.il2cpp_object_unbox(IL2CPP.Il2CppObjectBaseToPtrNotNull(targets));", il2cpp)
        self.assertIn('field.Name.StartsWith("NativeMethodInfoPtr_ExecuteInternal_", StringComparison.Ordinal)', il2cpp)
        self.assertIn("CustomPassRecorder.Record(pass, volume, __instance, renderGraph, hdCamera, targets)", host)
        for project in ("TGBetterUpscaling", "TGNeuralRendering"):
            self.assertIn('..\\Shared\\CustomPassRecorder.cs', self.text(f"src/{project}/{project}.csproj"))
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
        self.assertEqual(sliders, 4) # bridge, upscaling, path tracing, neural rendering
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
        for token in ['private const string FsrId = "UpScalingType_FSR", XessId = "UpScalingType_XeSS";', "VideoTab.Choice(FsrId,", "VideoTab.Choice(XessId,",
                      "if (entries.Length == 3 || gameFsr != null)",
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
        self.assertIn("rows[SharpnessKey].Visible = Upscaler == UpscalerSetting.FSR && (!known || caps.Fsr);", settings)
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
        self.assertIn("fsrChoice = gameFsr != null && !caps.Fsr ? gameFsr : VideoTab.Choice(FsrId, Capabilities.Label(UpscalerSetting.FSR));", row)

    def test_games_own_fsr_entry(self):
        # The game's update of 2026-10-06 gave its Upscaling row an FSR entry of its own (HDRP's FSR 2) at index 3,
        # under the preference ID this mod's FSR entry always had. TG Better Upscaling 1.2.0 took a row of 4 entries
        # for one it did not know and left it alone: no XeSS, no quality modes, and its controller hook still
        # set DLSS and FSR up from the config, so the game's slider did nothing. One FSR entry, as before: this mod's
        # FSR 3.1 or 4 where AMD's runtime reports it, the game's FSR 2 (at the game's slider) where it doesn't.
        row = self.text("src/TGBetterUpscaling/UpscalerRow.cs")
        settings = self.text("src/TGBetterUpscaling/Settings.cs")
        host = self.text("src/TGBetterUpscaling/StpHost.cs")
        self.assertIn("entries.Length == FsrIndex + 1 && entries[FsrIndex].ID == FsrId ? entries[FsrIndex] : null;", row)
        self.assertIn("VideoTab.SetEntries(type, entries.Take(FsrIndex).Concat(new[] { fsrChoice, xessChoice }).ToArray());", row)
        # The row's current entry stays one of its entries when this mod's takes the game's place (its index is -1 otherwise).
        self.assertIn("if (onGameFsr && !VideoTab.Same(fsrChoice, gameFsr)) type.Option = fsrChoice;", row)
        # What the game offers comes from the game: it forbids its FSR entry where HDRP found no FSR 2.
        self.assertIn("string gameFsrName = gameFsr == null || !GameOffers(gameRow, gameFsr) ? null", row)
        self.assertLess(row.index("GameOffers(gameRow, gameFsr)"), row.index("VideoTab.Forbid(type,"))
        self.assertIn("caps.Offers(UpscalerSetting.FSR) ? null : fsrChoice,", row)
        self.assertIn("case UpscalerSetting.FSR: return Fsr || GameFsr != null;", settings)
        self.assertIn("if (upscaler == UpscalerSetting.FSR && !Fsr && GameFsr != null) return GameFsr;", settings)
        # The game's FSR 2 keeps the game's slider: no quality modes and no FSR Sharpness of this mod's under it.
        self.assertIn("var fsrRows = caps.Fsr ? new PrefOption[] { (PrefOption)fsrMode ?? quality, sharpness }.Where(o => o != null).ToArray() : sliderRows;", row)
        # The controller hook leaves index 3 to the game unless AMD's runtime reported FSR to this mod, and leaves
        # everything but Ray Reconstruction to the game when the row could not be extended.
        self.assertIn("bool hosted = !UpscalerRow.Failed && (index == UpscalerRow.XessIndex || (index == UpscalerRow.FsrIndex && Session.Settings.Capabilities?.Fsr == true));", host)
        self.assertIn("if (!hosted || !UpScaling.IsSTPAvailable)", host)
        self.assertIn("UpScaling.IsDLSSAvailable && !UpscalerRow.Failed && Session.Settings.Mode(UpscalerSetting.DLSS) is QualityMode dlss", host)
        # HDRP runs one upscaler for a camera, the first in its list that the camera allows: FSR 2 is switched off
        # for the camera wherever this mod sets it up (FSR or XeSS in STP's place, Ray Reconstruction, DLSS with a mode).
        self.assertEqual(host.count("camera.allowFidelityFX2SuperResolution = false;"), 3)
        # Nothing that only the updated game has: the same build runs on game builds from before the update.
        for text in (row, settings, host):
            for token in ["IsFSRAvailable", "IsFSREnabled", "UpScalingType.FSR", "EnableFSR"]:
                self.assertNotIn(token, text, token)

    def test_settings_rows_are_listed_without_an_il2cpp_enumerator(self):
        # The normal version's first run of 1.2.4 (2026-10-06), and both of its troubleshooting reports of 2026-09-25:
        # walking the settings screen's own set of views (Model.Views, a HashSet behind an IEnumerable) through the
        # interop's enumerator threw "Collection was modified" the first time the settings opened, RowAlerts gave up
        # for the session, and no row ever got its alert icon. Unity lists the row views instead; no mod steps an
        # IL2CPP enumerator. The Mono version reads the set as before (it never failed there).
        tab = self.text("src/TGGraphicsBridge/VideoTab.cs")
        il2cpp = re.sub(r"//.*", "", compiled_for(tab, True))
        self.assertIn("UnityEngine.Object.FindObjectsByType(Il2CppType.Of<VFocusableSetting>(), FindObjectsSortMode.None);", il2cpp)
        self.assertNotIn("ui.Views", il2cpp)
        self.assertIn("public static List<VFocusableSetting> FocusableViews(AllSettingsUI ui) => ui.Views.OfType<VFocusableSetting>().ToList();", compiled_for(tab, False))
        for path in sorted((ROOT / "src").rglob("*.cs")):
            if "obj" in path.parts or "bin" in path.parts: continue
            code = re.sub(r"//.*", "", compiled_for(path.read_text(encoding="utf-8"), True))
            for token in ["Collections.IEnumerator>()", ".GetEnumerator()"]:
                self.assertNotIn(token, code, str(path) + ": " + token)

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
        for mod, text in (("TGFrameGeneration", plugin), ("TGBetterUpscaling", up)):
            if mod == "TGFrameGeneration":
                self.assertIn('[BepInDependency(BridgePlugin.Id, "1.2.4")]', text)
            else:
                self.assertIn("[BepInDependency(BridgePlugin.Id, BepInDependency.DependencyFlags.HardDependency)]", text)
            self.assertIn('Version = "1.2.4"', text)
            self.assertIn('BepInDependency("com.avalonmodmanager", BepInDependency.DependencyFlags.SoftDependency)', text)
            self.assertLess(text.index("ModManagers.KeepOutOfAvalon(Id, Logger);"), text.index("Session.Start(Config, Logger);"))
        self.assertIn("ModManagers.Tick();", bridge)

    def test_mod_manager_rows_match_video_tab(self):
        settings = self.text("src/TGFrameGeneration/Settings.cs")
        rows = self.text("src/TGFrameGeneration/NativeVideoSettings.cs")
        names = re.findall(r'Row\(\w+Key, [^,]+, "([^"]+)", (\d)', settings)
        self.assertEqual([n for n, _ in names], ["Frame Generation", "Frame Generation Quality", "Frame Generation Mode", "Frame Multiplier",
                                                  "Max Frame Multiplier", "Target Framerate", "Low Latency"])
        self.assertEqual([int(o) for _, o in names], list(range(7)))
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
        self.assertIn("radiance += throughput * m.reflectance * m.occlusion * cache_light(hit.position, hit.normal, rng);", common)
        self.assertIn("if (feed) cache_add(hit.position, hit.normal, finite_radiance(cache_sample(hit, offset, rng)), rng);", common)
        self.assertIn("float3 shade_path(Hit hit, float3 dir, uint bounces, inout Rng rng) { return shade_path(hit, dir, bounces, rng, false); }", common)
        self.assertIn("bool cache_feeds(uint2 p, uint salt) { return cache_on() && ((pcg((p.x >> 3) + (p.y >> 3) * 4096u) + g.size.z + salt) & 3u) == 0u; }", common)
        gi = self.text(PT + "shaders/pt_gi.hlsl")
        # The first ray feeds the cache, unless it leaves from a leaf's far side (gathers_behind): a cell is one side's.
        self.assertIn("const bool feed = feeds && k == 0 && !back;", gi)
        # (arrives: the tint and share of thin cloth the ray passed on its way, 1 without: PT-39)
        self.assertIn("cache_add(s.position, s.geoNormal, radiance * arrives * (cs > 1e-3 ? min(saturate(dot(s.geoNormal, dir)) / cs, 4.0) : 0.0), rng);", gi)
        self.assertNotIn("if (feed && above) cache_add(", gi)
        # A cell's 512 samples of at most CACHE_SAMPLE_MOST in each channel fit its 32-bit sums, rounding up included.
        common_cache = self.text(PT + "shaders/pt_common.hlsli")
        scale = float(re.search(r"static const float CACHE_SCALE = ([0-9.]+);", common_cache).group(1))
        samples = int(re.search(r"static const uint CACHE_SAMPLES_PER_FRAME = ([0-9]+)u;", common_cache).group(1))
        most = float(re.search(r"static const float CACHE_SAMPLE_MOST = ([0-9.]+);", common_cache).group(1))
        self.assertLess((most * scale + 1.0) * samples, 2.0 ** 32)
        self.assertIn("v = min(v, CACHE_SAMPLE_MOST) * CACHE_SCALE;", common_cache)
        # The hair and eyes' feeder gathers as pt_gi's rays do, about the surface's own normal.
        self.assertIn("if (shade) shown = shade_seen(seen, seenDir, rng, cache_feeds(p, 2u));", self.text(PT + "shaders/pt_specular.hlsl"))
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
        # 1.15 (PT-03, grass): TGPT_SetProfileReflectance, a leaf's reflectance from its diffusion profile.
        # 1.16 (PT-35, PT-13): TGPT_ReleaseHairColor, and TGPT_RegisterHairColor may return another tile for a key it knows
        # (hair that shared its detail tile and was colored anew): a plugin of 1.15 would keep reading the old tile.
        # 1.17 (PT-45): TGPT_RegisterSharedTexture and TGPT_SharedTexture, the game's own textures opened with no copy.
        # 1.18 (PT-46): TGPT_Material carries the game's own color, normal and mask textures' handles; TGPT_SharedTexture.handle.
        # 1.19 (PT-49): TGPT_SetClothWind and TGPT_ClothWind, the game's wind on cloth, traced in the pose HDRP draws.
        # 1.1A (PT-45 phase 3): TGPT_SwapSharedTexture, the start-up self-test and the video memory cap.
        # 1.1B (PT-37): TGPT_Frame 600 B (fogColor, fogMip): the game's fog along reflected and refracted rays.
        # 1.1C (PT-48): TGPT_SetWaterShade, TGPT_WaterShade 568 B, TGPT_EVENT_WATER_SHADE 6: the tracer draws HDRP's water.
        # 1.1D: the limits job (PT-50): light flag TGPT_LIGHT_NO_RANGE 0x80000, TGPT_MAX_SKINNED 256
        # 1.1E: real traced caustics (PT-14): TGPT_WaterCaustics 192 B (the waves), FrameConstants 7040 B
        # 1.1F: the tracer's own camera rays (PT-59): TGPT_GetYardstick, TGPT_OPTION_GBUFFER_SURFACES, TGPT_VIEW_YARDSTICK
        # 1.20: grass, leaves and trunks in the game's wind (PT-63): TGPT_SetPlantWind, TGPT_SetWindNoise, TGPT_PlantWind 48 B,
        # TGPT_PlantWindFrame 32 B (built as 1.1F in its copy beside the camera rays' 1.1F)
        # 1.21: the things the game moves (PT-71 item 4): TGPT_SetMoving, TGPT_Moving 112 B, TGPT_INSTANCE_MOVING gone
        # 1.21 integrated: material programs/UV4, optical fields (Material 204 B), own-hit scattering and wet ground; obsolete exports removed.
        # 1.22: decals plus UV4/tangents and receiver layers on static/skinned/moving records, preserving previous motion.
        self.assertIn("#define TGPT_ABI 0x00012700u", header)
        self.assertIn("internal const uint Abi = 0x00012700;", managed)
        self.assertIn(f"Marshal.SizeOf(typeof(NativeGuides)) != {dict(re.findall(r'sizeof\((TGPT_\w+)\)==(\d+)', header))['TGPT_Guides']}", managed)
        sizes = dict(re.findall(r"sizeof\((TGPT_\w+)\)==(\d+)", header))
        mirrored = {"TGPT_Submesh": "NativeSubmesh", "TGPT_MeshDesc": "NativeMeshDesc", "TGPT_CpuMeshDesc": "NativeCpuMeshDesc",
                    "TGPT_Material": "NativeMaterial", "TGPT_Instance": "NativeInstance", "TGPT_Light": "NativeLight",
                    "TGPT_WaterCaustics": "NativeWaterCaustics", "TGPT_SharedTexture": "NativeSharedTexture",
                    "TGPT_ClothWind": "NativeClothWind", "TGPT_Decal": "NativeDecal",
                    "TGPT_Moving": "NativeMoving", "TGPT_SkinnedInstance": "NativeSkinnedInstance"}
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
        self.assertIn('static_assert(sizeof(FrameConstants)==9024, "frame constants layout");', self.text(PT + "src/tracer.cpp"))
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertEqual(common.count("float4x4 "), 4 + 0)
        # 3 matrices, 36 float4/uint4 rows (the fog's color and mip fog, PT-37, the last), RTXDI's eight parameter blocks (16+16+32+16+48 B for DI, 32+32+16 B for GI), the
        # 16 diffusion profiles, their 32 rows of skin transmission and the water's 16 rows of caustics:
        # 3*64 + 33*16 + 128 + 80 + 256 + 512 + 256 = 1952, and the hair layer's strand tiles (two rows of 16 for each of the
        # 128 detail tiles, 256: there were 64 for 32 tiles): 6048, and the diffusion profiles' mirror reflectance (4 rows of
        # 16, a leaf's: TGPT_SetProfileReflectance): 6112, and the game's own textures' row (PT-46): 6128, and the fog's two
        # rows for reflected and refracted rays (PT-37: fogColor, fogMip): 6160. The caustics (PT-14): the water's rows grew from
        # 16 to 60 (15 a slot: its waves' bands, rotation, extent, absorption), and four more rows (each slot's gradient array,
        # the landed light's buffers, the grid) and 8 of the grid's levels: 6160 + 704 + 48 + 128 = 7040. The tracer's own camera
        # rays (PT-59): the view-projection without jitter and their outputs' row: 7040 + 64 + 16 = 7120.
        block = re.search(r"struct FrameConstants \{(.*?)\};", common, re.S).group(1)
        # The wet ground (PT-52): its store's row, its grid's row and its 8 levels: 7120 + 32 + 128 = 7280. And without the two
        # tables by HDRP's G-buffer index (PT-58: the 16 diffusion profiles' scattering and their 32 rows of thin-skin
        # transmission, read for pixels whose surface was HDRP's): 7280 - 256 - 512 = 6512. The water as a surface of the traced
        # scene (PT-71 item 5): its scattering, the waves of the frame before for its motion (13 rows a slot and their buffers), its
        # foam (6 rows a slot and its maps) and its meshes' records: 6512 + 64 + 832 + 16 + 384 + 64 + 16 = 7888.
        # (42 single rows before the two jobs; the decals' row and the water meshes' row: 44. And `look`, the hold's W of the
        # frame, PT-73 e: 45 rows, 7904 + 16 = 7920. And the water's kept wave corners, PT-81, water 8: two rows a slot,
        # `sunGrid[8]`: 7920 + 128 = 8048. And the rain's wetness, PT-108, on trial: one row, `rain`: 46 rows,
        # 8048 + 16 = 8064.)
        # (PT-112, the sky, on trial, docs/agents/sky-2.md: + sky[11], skyBodies[32], skyClouds[11] and the rows skyMaps, skyMaps2:
        # 8112 + 896 = 9008.)
        # (PT-127, the mirrors' records for NRD: one row, `psr`: 9008 + 16 = 9024.)
        self.assertEqual(len(re.findall(r"^\s*(?:float4|uint4) \w+;", block, re.M)), 52)  # (round 20: + waterCeiling, fogMedium, fogBoxes; the sky: + skyMaps, skyMaps2; PT-127: + psr)
        self.assertIn("float highlight_kept() { return g.look.x > 0.0 && g.look.x < 1e30 ? g.look.x : HIGHLIGHT_KEPT; }", common)
        self.assertIn("float highlightKept, frameReserved;", self.text(PT + "include/tgpt_api.h"))
        self.assertIn("float4 wetLevels[8];", block)
        self.assertIn("float4 sunGrid[8];", block)
        # Decal descriptor row appends 16 bytes: 7888 + 16 = 7904.
        self.assertIn("uint4 decals;", block)
        self.assertIn("uint previous; uint renderingLayerMask;", common)
        scene = self.text(PT + "src/scene.cpp")
        self.assertIn("mix(&inst.renderingLayerMask, 4);", scene)
        self.assertIn("GeometryRecordGpu{indexBase, source.indexCount, inst.renderingLayerMask, kGeometryOwnLayers|(source.flags&(kGeometryClosed|kGeometrySolidBits))}", scene)
        material = common.split("SurfaceMaterial hit_material(Hit h) {", 1)[1].split("return s;", 1)[0]
        self.assertEqual(material.count("apply_decals("), 1)
        self.assertLess(material.index("material_program("), material.index("apply_decals("))
        self.assertLess(material.index("wet_material("), material.index("apply_decals("))
        # Round 20: the decals' tree is split by place (scene.cpp: the median of the boxes' centres on the axis they spread
        # most), each node holding the least and the most draw-order index under it, and apply_decals takes a point's decals
        # in draw order one at a time (the least index after the last one taken): the same decals in the same order as the
        # tree split by draw order alone, whose boxes spanned the whole scene.
        self.assertIn("struct DecalNode { float3 lo; uint next; float3 hi; uint decal; uint mesh; uint2 span; uint pad; };", common)
        self.assertIn("if ((taken && b.span.y<=after) || b.span.x>=best ||", common)
        self.assertIn("current=best; after=best; taken=true; firstPrimitive=true;", common)
        self.assertIn("std::nth_element(order.begin()+first, order.begin()+mid, order.begin()+end,", scene)
        self.assertIn("nodes[here].pad[0]=std::min(nodes[left].pad[0], nodes[right].pad[0]); nodes[here].pad[1]=std::max(nodes[left].pad[1], nodes[right].pad[1]);", scene)
        self.assertIn("float4 water[60];", block)
        self.assertIn("float4 causticLevels[8];", block)
        self.assertIn("uint4 textures;", block)
        self.assertIn("float4 fogColor;", block)
        self.assertIn("float4 fogMip;", block)
        for gone in ("sssProfiles", "float4 transmission[32];"):
            self.assertNotIn(gone, block)
        self.assertIn("float4 profileF0[4];", block)
        self.assertIn("float profileF0[16];", self.text(PT + "src/tracer.cpp"))
        for member in ["RTXDI_RuntimeParameters rtxdiRuntime;", "RTXDI_ReservoirBufferParameters rtxdiReservoirs;",
                       "RTXDI_DITemporalResamplingParameters diTemporal;", "RTXDI_BoilingFilterParameters diBoiling;",
                       "RTXDI_DISpatialResamplingParameters diSpatial;", "RTXDI_GITemporalResamplingParameters giTemporal;",
                       "RTXDI_GISpatialResamplingParameters giSpatial;", "RTXDI_BoilingFilterParameters giBoiling;"]:
            self.assertIn(member, block)
        self.assertIn("uint4 grid;", common)

    def test_hair_and_eyes_never_take_their_color_map_alone(self):
        # PT-45 phase 2 read a hair material's shared color map before RealHair's own color: Ron's red hair came out pale
        # blond in game (run 20261007-024109). Hair, brows, lashes and eyes take the shader's color or their baked tile.
        common = self.text(PT + "shaders/pt_common.hlsli")
        body = common[common.index("float4 material_texture(MaterialRecord m, float2 st, float2 gx, float2 gy) {"):]
        body = body[:body.index("\n}\n")]
        self.assertIn("const bool ownShader = (m.flags & (MATERIAL_STRANDS | MATERIAL_HAIR_LAYER | MATERIAL_EYE)) != 0;", body)
        self.assertIn("if (!ownShader && has_shared(m, m.colorTexture)) return shared_sample(m.colorTexture, st, gx, gy);", body)
        self.assertLess(body.index("ownShader"), body.index("has_shared"))

    def test_path_tracing_mirror_shows_the_camera_view(self):
        # PT-46 (docs/agents/mirror.md): what a reflection meets is lit as the camera's own pixel of it is, not as a path vertex.
        common = self.text(PT + "shaders/pt_common.hlsli")
        seen = common.split("float3 shade_seen(", 1)[1]
        # Its diffuse color and its specular lobe (with Reflection Boost, as pt_prepare) take the sun's and the lights' samples.
        self.assertIn("reflection_boost(roughness, f0, g.cameraPos.w);", seen)
        # (A thin sheet's own tint with it; a subsurface material's diffuse light from where a walk inside it comes out: PT-40.)
        self.assertIn("} else radiance += direct_light(hit.position, hit.normal, m.normal, m.albedo, f0, alpha, v, offset, rng, m.through, m.tint);", seen)
        self.assertIn("radiance += direct_diffuse(leaves.position, leaves.normal, inside.normal, inside.albedo, exitOffset, rng, 0.0);", seen)
        # Its bounced light with Indirect Color, as a camera pixel's; its lobe's share of its surroundings.
        self.assertIn("else radiance += m.albedo * m.occlusion * indirect_color(around);", seen)
        self.assertIn("if (!mirrored) radiance += lobe * around;", seen)
        self.assertIn("if (shade) shown = shade_seen(seen, seenDir, rng, cache_feeds(p, 2u));", self.text(PT + "shaders/pt_specular.hlsl"))
        self.assertNotIn("m.reflectance", seen)
        # The hero's first-person body: one bit, the same in the header, the shaders and the plugin. Every ray that shades what
        # it meets passes its back faces, at any distance from the camera (docs/agents/mirror-2.md: the head its mesh keeps is
        # an open shell HDRP never draws from in front; a rule by the distance from the camera left most of it in the mirror).
        header = self.text(PT + "include/tgpt_api.h")
        self.assertIn("#define TGPT_MATERIAL_FIRST_PERSON 1048576u", header)
        self.assertEqual(len(re.findall(r"#define TGPT_MATERIAL_\w+ 1048576u", header)), 1)
        self.assertIn("#define MATERIAL_FIRST_PERSON 1048576", common)
        self.assertIn("internal const uint MaterialFirstPerson = 1048576;", self.text("src/TGPathTracing/NativeTracer.cs"))
        inside = common.split("bool first_person_inside(uint instanceId, uint geometry, bool frontFace) {", 1)[1].split("\n}", 1)[0]
        self.assertIn("if (frontFace) return false;", inside)
        self.assertIn(".flags & (MATERIAL_FIRST_PERSON | MATERIAL_DOUBLE_SIDED)) == MATERIAL_FIRST_PERSON;", inside)
        self.assertNotIn("cameraPos", inside)
        for name in ("FIRST_PERSON_HEAD", "g_reflectionRay", "first_person_head"):
            self.assertNotIn(name, common + header + self.text(PT + "shaders/pt_primary.hlsli"))
        # The camera's own ray (PT-59) passes that inside as the back of any one-sided surface, at any distance, and meets the
        # body's front (a hand, the collar): no rule by the distance from the camera there either.
        walk = self.text(PT + "shaders/pt_primary.hlsli").split("PrimaryWalk primary_walk(", 1)[1].split("\n}", 1)[0]
        self.assertIn("(!q.CandidateTriangleFrontFace() && (m.flags & (MATERIAL_DOUBLE_SIDED | MATERIAL_GLASS)) == 0))) continue;", walk)
        self.assertNotIn("MATERIAL_FIRST_PERSON", walk)
        self.assertIn('"the camera\'s own ray passes the inside of the hero\'s first-person body (the head it sits in)"', self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn('"the inside of the hero\'s first-person body does not show in a mirror 0.5, 1 or 3 m ahead (the back of its head, never drawn from in front)"',
                      self.text("tests/pathtracing_gpu_test.cpp"))
        passes = "first_person_inside(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedTriangleFrontFace())) { t0 = t + 1e-3; continue; }"
        self.assertIn("if (pass < 4 && " + passes, common.split("bool trace_from_surface(", 1)[1].split("\n}", 1)[0])
        # Not in trace_closest (yet): there, a second query from behind the body hung the GPU in the reflections pass on its
        # first frame (docs/agents/mirror-2.md); its rays still meet the body's inside, a gap named in its comment.
        closest = common.split("bool trace_closest(float3 origin", 1)[1].split("\n}", 1)[0]
        self.assertNotIn("first_person", closest)
        self.assertEqual(closest.count("TraceRayInline("), 1)
        self.assertIn("still meet the inside of", common.split("bool trace_closest(float3 origin", 1)[0][-1600:])
        # Shadow rays meet both of its sides: the hero's shadow keeps its head.
        for shadow in ("bool occluded_any(", "bool occluded_from_surface_rays("):
            self.assertNotIn("first_person", common.split(shadow, 1)[1].split("\n}", 1)[0])
        specular = self.text(PT + "shaders/pt_specular.hlsl")
        self.assertIn("g_mirrorRay = s.roughness < 0.15 && !s.skin;", specular)
        self.assertIn("g_mirrorRay = false;", specular)
        self.assertNotIn("g_reflectionRay", specular)
        capture = self.text("src/TGPathTracing/CharacterCapture.cs")
        self.assertIn("var root = controller == null ? null : controller.fppParent;", capture)
        self.assertIn("if (heroBody && IsSkin(materials[s])) material = SceneCapture.FirstPersonMaterial(material);", capture)
        self.assertIn("mat.Flags |= Tgpt.MaterialFirstPerson;", self.text("src/TGPathTracing/SceneCapture.cs"))

    def test_path_tracing_mirrors_are_sharp(self):
        # PT-46, mirrors not sharp (docs/agents/sharp.md): a reflection ray is drawn from the surface's own lobe, a mirror's too,
        # not from roughness_alpha's floor (0.045), which strayed a mirror's rays and widened its cone three times as fast as a
        # pixel's; its footprint is the camera pixel's carried over the whole way.
        common = self.text(PT + "shaders/pt_common.hlsli")
        specular = self.text(PT + "shaders/pt_specular.hlsl")
        self.assertIn("float reflection_alpha(float perceptual) { float r = max(perceptual, 0.01); return r * r; }", common)
        self.assertIn("float roughness_alpha(float perceptual) { float r = max(perceptual, 0.045); return r * r; }", common)
        self.assertIn("float alpha = reflection_alpha(s.roughness);", specular)
        self.assertNotIn("roughness_alpha(", specular)
        self.assertIn("else ray_cone(pixel_angle() * s.distance, pixel_angle() + alpha);", specular)
        seen = common.split("float3 shade_seen(", 1)[1].split("\n}", 1)[0]
        self.assertIn("const float rayAlpha = reflection_alpha(roughness);", seen)
        self.assertIn("ray_cone(hit.footprint, coneSpread + rayAlpha);", seen)
        # Reflection Boost never roughens an existing mirror. Check the same metal/stone rule in traced shading
        # and RR guides.
        boost = common.split("void reflection_boost(", 1)[1].split("\n}", 1)[0]
        self.assertIn("roughness = min(roughness, unboosted);", boost)
        self.assertIn("roughness = min(roughness, unboosted);", self.text(PT + "shaders/d3d11.hlsl"))
        # Ray Reconstruction's hit distance from the surface itself (its guide, 3.4.9), not from the ray's start off it.
        self.assertIn("const float3 toHit = hit.position - s.position;", specular)
        self.assertIn("const float along = abs(across) > 0.2 ? dot(hit.normal, toHit) / across : 0.0;", specular)
        self.assertNotIn("hitDistance = found ? length(hit.position - origin)", specular)
        # The harness's switch: one option bit, the same in the header, the shader and the plugin, behind a hidden key.
        header = self.text(PT + "include/tgpt_api.h")
        self.assertIn("#define TGPT_OPTION_REFLECTION_TOP_MIP 134217728u", header)
        self.assertEqual(len(re.findall(r"#define TGPT_OPTION_\w+ 134217728u", header)), 1)
        self.assertIn("#define OPTION_REFLECTION_TOP_MIP 134217728", common)
        self.assertIn("internal const uint OptionReflectionTopMip = 134217728;", self.text("src/TGPathTracing/NativeTracer.cs"))
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertIn('ReflectionsTopMip = file.Bind(Advanced, "Reflections read the top mip", false, Hidden(', settings)
        self.assertIn("(ReflectionsTopMip.Value ? Tgpt.OptionReflectionTopMip : 0u) |", settings)
        self.assertIn('"a perfect mirror shows a fine texture as sharp as the camera sees it directly"', self.text("tests/pathtracing_gpu_test.cpp"))

    def test_path_tracing_shares_the_games_textures(self):
        # PT-45, phase 1: the game's textures opened by the tracer's D3D12 device with no copy (docs/agents/share-1.md).
        share = self.text(PT + "src/texture_share.cpp")
        hook = share[share.index("HRESULT STDMETHODCALLTYPE create_2d("):share.index("bool install(")]
        # The game never sees a failure the flag causes: a refused creation is made again exactly as the game asked, and
        # nothing else in the hook (two entry points: CreateTexture2D and CreateTexture2D1, which bypasses it).
        for entry in ["create_2d(", "create_2d1("]:
            body = hook[hook.index("HRESULT STDMETHODCALLTYPE " + entry):]
            body = body[:body.index("\n}\n")]
            self.assertIn("shared.MiscFlags|=D3D11_RESOURCE_MISC_SHARED;", body, entry)
            self.assertIn("if(SUCCEEDED(hr)) { made.fetch_add(1, std::memory_order_relaxed); return hr; }", body, entry)
            self.assertIn("const HRESULT plain=real(device, desc, init, out);", body, entry)
            self.assertIn("return plain;", body, entry)
            # Not shareable: straight through, as the game asked.
            self.assertIn("return real(device, desc, init, out);", body.split("return plain;")[1], entry)
            # A driver thread's tiny stack: no locks, no logging, no D3D12 in the hook.
            for banned in ["lock_guard", "log(", "ID3D12", "std::string", "std::vector"]:
                self.assertNotIn(banned, body, entry + " " + banned)
        # The legacy flag alone: the NT-handle one refuses BC4 to BC7 and cube maps (docs/agents/texture-share.md).
        self.assertNotIn("SHARED_NTHANDLE", share)
        self.assertIn("dxgi->GetSharedHandle(&handle)", share)
        self.assertIn("host().add_hook(table[5], reinterpret_cast<void*>(&create_2d), &realCreate2D)", share)
        self.assertIn("host().add_hook(table3[54], reinterpret_cast<void*>(&create_2d1), &realCreate2D1)", share)
        # Only what shaders alone read: render targets, depth, UAVs, CPU-written and shared textures
        # stay. Every size (PT-64): until then one under 64 KB with its mips kept its 64x64 atlas
        # tile.
        self.assertIn("d.BindFlags==D3D11_BIND_SHADER_RESOURCE && d.CPUAccessFlags==0", share)
        self.assertNotIn("kMinBytes", self.text(PT + "src/texture_share.hpp"))
        self.assertNotIn("kMinBytes", share)
        self.assertNotIn("TGPT_SHARE_SMALL", self.text(PT + "include/tgpt_api.h"))
        self.assertIn("template<class Desc> bool wants(const Desc& d) { return shareable(d) && plain(d) && format_passed(d.Format) && !host().internal(); }", share)
        # D3D12's reference keeps the memory after the game frees a texture: a death notice in its private data, no
        # reference held to the D3D11 texture, and the notice's own lock a leaf.
        self.assertIn("texture->SetPrivateDataInterface(kNotice, notifier)", share)
        self.assertIn("if(FAILED(tagged)) return refuse(TGPT_SHARE_OPEN_FAILED);", share)
        self.assertIn("uintptr_t texture=0;   // the D3D11 texture, as a number: no reference is held", share)
        # In with the game's device whatever the Mode row says (2026-10-06's runs: path tracing turned on after loading left
        # every texture made before the hook unshared), wherever path tracing can run.
        extension = self.text(PT + "src/extension.cpp")
        self.assertNotIn("read_launch_mode", extension + share)
        self.assertIn("if(supported) share::game_device_ready(host().game_device(), device);", extension)
        self.assertIn("void game_device_ready(ID3D11Device* game, ID3D12Device* device) {\n    start(game, device, \"the game's device on\");\n}", share)
        # PT-46: a material names a texture by a handle (number and serial), resolved before every upload: a dead texture's
        # or one not yet in the heap reads as -1 (its tile), and a death is freed only after the first frame no longer naming it.
        self.assertIn("return s.state==kOpen && s.published && r.haveHeap && (s.serial&kSerialMask)==serial ? int32_t(n) : -1;", share)
        self.assertIn("s.state=kDead; s.retireAt=r.retire;", share)
        self.assertIn("for(const uint32_t n:r.dying) if(r.slots[n].retireAt==kPending) r.slots[n].retireAt=retire;", share)
        scene = self.text(PT + "src/scene.cpp")
        self.assertIn("if(materialsDirty_ || !materialBuffer_.buffer || shareGeneration!=shareGeneration_) {", scene)
        self.assertIn("share::resolve(gpuMaterials_.data(), gpuMaterials_.size());", scene)
        # Released with the scene, with textures the game frees (four times a second, tracing or not), and the heap.
        self.assertIn("share::collect();", extension)
        self.assertIn("try { share::clear(); } catch(...) { return -10; } // outside the engine's lock", extension)
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("st.sharedBase=st.heap.allocate_range(share::kMaxShared);", tracer)
        self.assertIn("share::publish(st.d12.Get(), st.heap, st.sharedBase, st.consumer.d12.Get(), st.consumer.value+1);", tracer)
        self.assertLess(tracer.index("share::heap_lost();"), tracer.index("st.heap.reset(); st.decalSamplers.Reset(); st.sharedBase=kInvalid;"))
        # The plugin registers each traced material's colour, normal and mask textures beside their atlas tiles, and the
        # harness settles whether mip streaming makes a new texture.
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        self.assertEqual(capture.count("TextureShare.Register(texture, native);"), 3)
        self.assertIn("mat.NormalTexture = TextureShare.Normal(m, shaderName, out float normalScale);", capture)
        self.assertIn("mat.ColorTexture = TextureShare.Handle(m.GetTexture(albedo), TextureShare.FieldColor);", capture)
        # Mip streaming makes a new texture when it loads a texture's top mips: the plugin registers the new one and sends
        # the materials that read it again.
        self.assertIn("TextureShare.Refresh(materialList, materialsUploaded, tracer);", capture)
        self.assertIn("TextureShare.Bind(materialList.Count - 1);", capture)
        textureShare = self.text("src/TGPathTracing/TextureShare.cs")
        self.assertIn("IntPtr native = w.Texture.GetNativeTexturePtr();", textureShare)
        self.assertIn("foreach (int i in resend) tracer.SetMaterials((uint)i, new[] { materials[i] }, 1);", textureShare)
        # The shaders read them over the ray cone's footprint, with an anisotropic sampler, the atlas behind.
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("return t.SampleGrad(s_anisoWrap, st, gx, gy);", common)
        self.assertIn("h.footprint = g_coneWidth + (g_coneSpread >= 0.0 ? g_coneSpread : pixel_angle()) * length(hitPos - g_rayOrigin);", common)
        self.assertIn("base *= material_texture(m, st, gx, gy).rgb;", common)
        self.assertIn("s.normal = material_normal(h, m, st, gx, gy, h.normal);", common)
        self.assertIn("FILTER_ANISOTROPIC, maxAnisotropy=16, addressU=TEXTURE_ADDRESS_WRAP", self.text(PT + "shaders/pt_root.hlsli"))
        self.assertIn("public int mipStreaming = 0;", self.text("src/TGPathTracing.Harness/Request.cs"))
        self.assertIn("for(auto& m:made) {", self.text("tests/pathtracing_gpu_test.cpp"))

    def test_texture_sharing_is_safe_on_every_card(self):
        # PT-45 phase 3 (docs/agents/share-3.md). No vendor promises the legacy flag on compressed textures, so a self-test on
        # the player's own card shares and reads back each format the game uses before the hook goes in; the hook flags only
        # the formats that passed, and none passing keeps it out.
        share = self.text(PT + "src/texture_share.cpp")
        self.assertIn("template<class Desc> bool wants(const Desc& d) { return shareable(d) && plain(d) && format_passed(d.Format) && !host().internal(); }", share)
        # The hook goes in from the self-test's thread alone, after the verdict, and only if some format passed.
        self.assertEqual(share.count("install(game.Get(), "), 1)
        self.assertEqual(share.count("install("), 2)  # its definition and that one call
        body = share[share.index("void self_test(ComPtr<ID3D11Device> game"):share.index("void start(ID3D11Device* game")]
        self.assertLess(body.index("if(passedCount==0) {\n        hookState.store(4);"), body.index("install(game.Get(), "))
        # Through the tracer's own path: the legacy handle, the registry's view, the fence handoff; bytes and texels compared.
        self.assertIn("dxgi->GetSharedHandle(&handle)", body)
        self.assertIn("device->OpenSharedHandle(handle, IID_PPV_ARGS(&c.shared))", body)
        self.assertIn("const auto view=view_desc(c.shared->GetDesc());", body)
        self.assertIn("const auto view=view_desc(rd);", share[share.index("int32_t open(Registry& r"):])
        self.assertIn("check(queue->Wait(fence.d12.Get(), 1), \"wait for D3D11's textures\");", body)
        self.assertIn("c.why=\"D3D12 read different bytes at mip \"+std::to_string(m);", body)
        self.assertIn("if(c.why.empty() && n[0]) c.why=", body)
        self.assertIn("D3D11_RESOURCE_MISC_SHARED};", body)  # made with the legacy flag, as the hook makes them
        # Every format the game's textures come in is tested (the harness's textures.txt).
        for fmt in ["BC1_UNORM_SRGB", "BC3_UNORM", "BC4_UNORM", "BC5_UNORM", "BC6H_UF16", "BC7_UNORM_SRGB", "R8G8B8A8_UNORM_SRGB",
                    "A8_UNORM", "R16G16B16A16_FLOAT", "R32G32B32A32_FLOAT", "R16G16_FLOAT", "R16_UNORM"]:
            self.assertIn("{DXGI_FORMAT_" + fmt + ", ", share, fmt)
        # On a thread of its own: the game's start doesn't wait for it.
        self.assertIn("std::thread([g=ComPtr<ID3D11Device>(game), d=ComPtr<ID3D12Device>(device), w=std::string(when)] {", share)
        self.assertIn("}).detach();", share)
        # The video memory cap: a texture opens only while Windows' count for the game, the tracer's
        # second count included, stays under 97 % of the budget, and only if it takes a quarter of
        # what is left at most (the largest stop first).
        self.assertIn("constexpr double kBudgetShare=0.97;", share)
        self.assertIn("constexpr uint64_t kLargestShare=4;", share)
        self.assertIn("if(bytes*kLargestShare<=left) return true;", share)
        opening = share[share.index("int32_t open(Registry& r"):]
        self.assertLess(opening.index("if(!within_budget(r, "), opening.index("r.device->OpenSharedHandle(handle"))
        self.assertIn("QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)", share)
        # Hair's and eyes' maps follow mip streaming: the plugin watches them and swaps, the records follow.
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("            bake_eyes();\n            apply_texture_swaps();", tracer)
        self.assertEqual(tracer.count("st.scene.set_map_record(") - tracer.count("st.scene.set_map_record((uint32_t(tile)-kMaxTiles)"), 3)
        for maps in ["maps, 5);", "maps, 3);", "maps, 2);"]:
            self.assertIn(maps, tracer)
        self.assertIn("if((texture!=from || !from) && texture!=to) continue;", self.text(PT + "src/scene.cpp"))
        self.assertIn("TextureShare.Character(t, native);", self.text("src/TGPathTracing/SceneCapture.cs"))
        textureShare = self.text("src/TGPathTracing/TextureShare.cs")
        self.assertIn("if (w.Character) tracer.SwapSharedTexture(old, native);", textureShare)
        self.assertIn("else if (n < 0 && !kept.Contains(w)) kept.Add(w);", textureShare)
        # The offline proof: a ctest whose self-test reads one format back wrong on purpose.
        cmake = self.text("CMakeLists.txt")
        self.assertIn("add_test(NAME tgpt_share_selftest_fails_one COMMAND tgpt_gpu_test", cmake)
        self.assertIn("TGPT_TEST_SHARE_BREAK=99", cmake)

    def test_path_tracing_grass(self):
        # Grass and leaves as HDRP shades them (PT-03, docs/PT-VEGETATION.md; tgpt_gpu_test measures
        # each): a blade's reflectance is its diffusion profile's (0.0023 for the game's foliage,
        # where 0.04 made wet grass glint white), in the tracer and in Ray Reconstruction's guide; the
        # light through it is its material's own share (the plugin works it out from the profile the
        # material names: TGPT_Material.subsurface), from the sky and bounced light behind it too; its
        # material's occlusion stays; its rays leave from its own face; and it lets a light's shadow
        # ray through half the time, micromaps leaving its covered texels to that test.
        prepare = self.text(PT + "shaders/pt_prepare.hlsl")
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("const bool leaf = deferred && translucent;", prepare)
        self.assertIn("if (leaf && dot(geo, normal) < 0.5) geo = normal;", prepare)
        self.assertIn("const float profileF0 = profile_f0(leaf_profile(s, feature));", prepare)
        self.assertIn("if (abs(baked.r - 2048.0) < 1.0 && abs(baked.b - 1024.0) < 1.0) ambientOcclusion = saturate(baked.g);", prepare)
        self.assertIn("(hairLobes ? 64u : 0u) | (leaf ? 128u : 0u)", prepare)
        self.assertIn("if ((bits & 128u) != 0) { s.ambientOcclusion = spec.g; s.specularOcclusion = spec.b; s.f0 = srgb_to_linear(spec.rrr); }", common)
        self.assertIn("if ((m.flags & MATERIAL_TRANSLUCENT) != 0 && g_lightRay && !g_cameraRays && ray_chance(instanceId, geometry, primitive, 0x6C656166u) < TRANSMISSION) return false;", common)
        self.assertEqual(common.count("g_lightRay = true;"), 2)
        self.assertIn("g_lightRay = true;", self.text(PT + "shaders/pt_rtxdi.hlsli"))
        # A leaf, and thin cloth too (PT-39: a canopy under an open sky was black underneath): a deferred cloth pixel, not hair.
        self.assertIn("bool gathers_behind(Surface s) { return s.translucent && (s.through > CLOTH_TRANSMISSION + 0.01 || !s.forward); }", common)
        for name in ("pt_gi.hlsl", "pt_gi_spatial.hlsl", "pt_rtxdi.hlsli"):
            self.assertIn("gathers_behind(", self.text(PT + "shaders/" + name), name)
        self.assertEqual(self.text(PT + "shaders/pt_di_spatial.hlsl").count("*= backTint;"), 2)
        self.assertIn("if (!nrdAll && s.ambientOcclusion < 1.0) diffuse -= bounced * (1.0 - s.ambientOcclusion);", self.text(PT + "shaders/pt_compose.hlsl"))
        self.assertIn("added *= s.specularOcclusion;", self.text(PT + "shaders/pt_specular.hlsl"))
        self.assertIn("const float leafF0 = profileF0[profile >> 2u][profile & 3u];", self.text(PT + "shaders/d3d11.hlsl"))
        self.assertIn("std::memcpy(c.profileF0, e.profileF0.data(), sizeof(c.profileF0));", self.text(PT + "src/tracer.cpp"))
        passes = self.text("src/TGPathTracing/HdrpPasses.cs")
        self.assertIn("Session.Tracer.SetProfileReflectance(reflectanceTable, 16) == 0", passes)
        self.assertIn("var t = TransmissionOf(settings);", passes)
        self.assertIn("mat.SubsurfaceR = share.x; mat.SubsurfaceG = share.y; mat.SubsurfaceB = share.z;", passes)
        self.assertIn('baseColor = Vector3.Scale(baseColor, 0.5f * (LinearColor(m.GetColor("_HealthyColor")) + LinearColor(m.GetColor("_DryColor"))));',
                      self.text("src/TGPathTracing/SceneCapture.cs"))
        tests = self.text("tests/pathtracing_gpu_test.cpp")
        for check in ['"a blade of grass mirrors what its diffusion profile says (0.0023), not 0.04"',
                      '"Ray Reconstruction\'s specular guide takes a blade\'s reflectance from its profile"',
                      '"a blade lit from behind takes the light its material\'s own share lets through, tinted as HDRP\'s"',
                      '"a leaf gathers the sky\'s light from behind it, as much as it lets through"',
                      '"a blade\'s material occlusion takes its bounced and sky light down as HDRP\'s does"',
                      '"a blade\'s specular occlusion takes its reflections down"',
                      '"a blade whose normal faces away from the camera still gathers light"',
                      '"grass and leaves cast half shadows, as much as they let through"',
                      '"thin cloth lets a share of the light through, tinted by its color"',
                      '"cloth the wind sways away from where it is traced is still lit through"']:
            self.assertIn(check, tests)
        # Thin cloth the same way (PT-39: a red canopy left a candle corner black): a light's shadow ray passes its weave by
        # CLOTH_TRANSMISSION's chance, tinted by its color (glass_walk's crossings); the plugin makes every thin cloth
        # alpha-tested so that rays test it; a swaying cloth HDRP drew away from its traced place is still found as cloth.
        self.assertIn("if (ray_chance(instanceId, geometry, primitive, 0x636C6F74u) >= CLOTH_TRANSMISSION) return true;", common)
        self.assertIn("else through *= cloth_tint(inst, m, q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics());", common)
        self.assertIn("if ((m.flags & MATERIAL_THIN_CLOTH) != 0 && state == OMM_OPAQUE) state = OMM_UNKNOWN_OPAQUE;", self.text(PT + "shaders/pt_omm_bake.hlsl"))
        self.assertIn("if (!found && clothCandidate && scene_sways() && window < CLOTH_SWAY_REACH) {", prepare)
        self.assertIn("if ((mat.Flags & Tgpt.MaterialAlphaTest) == 0) { mat.AlphaCutoff = 0f; mat.Flags |= Tgpt.MaterialAlphaTest; }", self.text("src/TGPathTracing/SceneCapture.cs"))
        # PT-49: thin cloth is found by what it is (ClothRules: the game's TG/Clothes/ family, a
        # hanging sheet's name, a cloth drawn from both sides), checked without the game against the
        # census of the harness's scenes.
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        self.assertIn("else if (ClothRules.IsThinCloth(shaderName, m.name, (mat.Flags & Tgpt.MaterialDoubleSided) != 0))", capture)
        self.assertIn("if (shader.StartsWith(ClothFamily, StringComparison.OrdinalIgnoreCase)) return true;", self.text("src/TGPathTracing/ClothRules.cs"))
        self.assertIn('<Compile Include="..\\src\\TGPathTracing\\ClothRules.cs" Link="ClothRules.cs" />', self.text("tests/ClothRules.Tests.csproj"))
        self.assertIn("ClothRules.Tests", self.text("docs/TESTING.md"))
        # And the cloth the game sways (Cloth_Wind's vertex shader) is traced in the pose HDRP draws: the plugin sends the wind
        # of each such material and the time with every frame; the tracer moves its own copy of the mesh as the game's compiled
        # shader does (pt_sway), refits its BLAS, and traces it as still. The shader's two switches as it uses them.
        self.assertIn("SceneCapture.SendClothWind(tracer);\n                DecalCapture.Capture(hd); // after the game animates projector fades; all rays read the same records\n                int result = tracer.SubmitFrame(ref frame);", self.text("src/TGPathTracing/HdrpPasses.cs").replace("\r\n", "\n"))
        self.assertIn("tracer.SetClothWind(Time.time, clothWindArray, (uint)clothWindArray.Length);", capture)
        self.assertIn("Flags = (worldSpace != 0f ? ClothRules.WindVertexPaint : 0u) | (vertexPaint != 0f ? ClothRules.WindPositionUv : 0u),", capture)
        sway = self.text(PT + "shaders/pt_sway.hlsl")
        self.assertIn("const float n = simple_noise(uv + r.time * r.speed, r.noiseScale);", sway)
        self.assertIn("p += r.intensity * (n * local * mask);", sway)
        scene_cpp = self.text(PT + "src/scene.cpp")
        self.assertIn("if(!swayWork.empty()) sway(device, list, heap, ring, release, retireValue, swayWork, barriers);", scene_cpp)
        self.assertIn("if(c->built) { desc.Inputs.Flags|=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE; desc.SourceAccelerationStructureData=c->blas.va(); }", scene_cpp)
        self.assertLess(scene_cpp.index("if(!swayWork.empty()) sway("), scene_cpp.index("build_emitters(device, list, heap, ring, release, retireValue, origin, sources, barriers);"))
        self.assertIn("if (s.valid && s.translucent && s.through == CLOTH_TRANSMISSION) c = float3(0.65, 0.3, 1.0);", self.text(PT + "shaders/pt_compose.hlsl"))
        for check in ['"a swaying cloth is traced in the pose HDRP draws it, at two times, within a millimetre"',
                      '"a swaying cloth\'s shadow falls where its pose casts it, and moves with it"',
                      '"a swaying cloth\'s underside takes the sky\'s light through it, as thin cloth"',
                      '"the light through a red cloth is red"']:
            self.assertIn(check, tests)
        # And the sky's and bounced light (PT-39, round 2: the canopy stood in a building's shade, so only the sky lit it): the
        # rays that gather light for a diffuse surface pass thin cloth by the same chance, tinted (pt_gi's first ray, a path's
        # next ray and the cache's through trace_bounce). A bounced-light sample keeps its light over the chance its ray had of
        # getting there, and the ray that checks it is seen passes cloth by that chance and takes its tint, so a sample reused
        # across a canopy's edge is dimmed as the pixel's own (cloth left open to that ray leaked a quarter more light under
        # the harness's canopy, untinted). A cloth pixel gathers from behind as a leaf does. A leaf's direct light from behind
        # is HDRP's wrapped cosine.
        # Not a character's clothes (a material named like cloth on a body): a tunic is no canopy.
        self.assertIn("if ((m.flags & MATERIAL_THIN_CLOTH) != 0 && (g_lightRay || g_bounceRay) && !g_cameraRays && (inst.flags & 8u) == 0) {", common)
        self.assertIn("#define INSTANCE_CHARACTER 8", common)
        self.assertIn("if (g_bounceRay) ++g_clothMet;", common)
        self.assertEqual(common.count("trace_bounce("), 3)
        self.assertIn("if (found && cloth_stops(hit)) weight /= 1.0 - CLOTH_TRANSMISSION;", common)
        gi = self.text(PT + "shaders/pt_gi.hlsl")
        self.assertIn("const float held = pow(1.0 / CLOTH_TRANSMISSION, float(clothPassed)) * (found && cloth_stops(hit) ? 1.0 / (1.0 - CLOTH_TRANSMISSION) : 1.0);", gi)
        self.assertIn("const float3 arrives = clothPassed > 0 ? clothTint * pow(CLOTH_TRANSMISSION, float(clothPassed)) : float3(1.0, 1.0, 1.0);", gi)
        # The clamp before held (after it, the sky through a canopy was clamped to 0.21 of 0.30), and the check ray's own chance
        # at the cloth (g_raySalt: deciding as the ray that found the sample, it let 1.2 times the light through).
        self.assertIn("radiance = held * finite_radiance(shade_path(hit, dir, g.settings.z, rng, feed));", gi)
        self.assertIn("radiance = held * finite_radiance(sky_radiance(dir));", gi)
        self.assertNotIn("finite_radiance(held", gi)
        self.assertIn("pcg(asuint(dir.y) ^ pcg(g.size.z ^ g_raySalt))", common)
        self.assertIn("g_bounceRay = true; g_clothMet = 0; g_raySalt = 0x76697369u;", self.text(PT + "shaders/pt_gi_spatial.hlsl"))
        self.assertIn("g_bounceRay = false; g_raySalt = 0;", self.text(PT + "shaders/pt_gi_spatial.hlsl"))
        self.assertIn("if (g_clothMet > 0) clothTint = cloth_walk(origin, dir, 0.0, reach, g_clothMet, clothPassed);", self.text(PT + "shaders/pt_gi_spatial.hlsl"))
        self.assertNotIn("g_clothOpen", common)
        self.assertEqual(self.text(PT + "shaders/pt_di_spatial.hlsl").count("leaf_cos(s.normal, "), 2)
        self.assertIn("return saturate(c) + transmittance * saturate((w - c) / ((1.0 + w) * (1.0 + w)));", common)
        for check in ['"a leaf\'s light from behind is wrapped past the terminator as HDRP wraps it"',
                      '"thin cloth is lit by the sky behind it, as much as it lets through"',
                      '"the sky\'s light passes thin cloth to what lies under it, tinted by its color"']:
            self.assertIn(check, tests)

    def test_path_tracing_world_is_what_the_game_draws(self):
        # PT-63, PT-61. The tracer's own camera rays show only what is in the traced scene, so what is
        # traced is what the game draws, by the game's own rules (WorldRules, checked without the game
        # by tests/world_rules_tests.cs).
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        rules = self.text("src/TGPathTracing/WorldRules.cs")
        # Statically batched renderers are traced, once a combined mesh: never skipped again.
        self.assertNotIn("if (renderer.isPartOfStaticBatch) { ++batched; continue; }", capture)
        self.assertIn("batch.Members.Add((renderer.subMeshStartIndex, renderer.sharedMaterials));", capture)
        self.assertIn("if (!WorldRules.PlaceBatchMember(list, start, memberMaterials)) continue;", capture)
        self.assertIn("AddInstance(instances, indices, handle, batch.World, list, 0, batch.Layers);", capture)
        # The level of detail is the drawn one: HDRP's LOD bias and maximum level for the camera, Entities Graphics' rule
        # for entities and Drake, Unity's for LOD groups (culled past the last level: not traced), Medusa's and Leshy's own.
        self.assertIn("drawnLodBias = frame.GetResolvedLODBias(asset); drawnMaximumLod = frame.GetResolvedMaximumLODLevel(asset);", capture)
        self.assertEqual(capture.count("WorldRules.EntityLevelDrawn(range[i].LODMask, range[i].MinDist, range[i].MaxDist, d, drawnMaximumLod);"), 2)
        self.assertIn("int chosen = WorldRules.GroupLevel(g.Heights, size, distance, drawnFov, drawnLodBias, drawnMaximumLod);", capture)
        self.assertNotIn("the tracer keeps the coarsest for light", capture)
        self.assertIn("int lod = WorldRules.MedusaLevel(d2 * scaleSq, l => brg.LodDistanceSq(t, (byte)l));", capture)
        self.assertIn("if (lodMask < maximumMask) return false;", rules)
        self.assertIn("if (lodMask == maximumMask) minDist = 0f;", rules)
        self.assertIn("if (from <= distanceSq && to > distanceSq) return i;", rules)
        # No LOD bias read at any other time than HDRP's culling stands in for the camera's own (only as the fallback).
        self.assertEqual(capture.count("QualitySettings.lodBias"), 1)
        # Drake: an entity is traced while the game has it loaded for drawing (its own state, not the range worked out
        # again), and one with a LOD range and no reference point is traced (Entities Graphics draws it always).
        self.assertIn("bool LoadedForDrawing(in ArchetypeChunk chunk) => chunk.Has(ref loadedHandle) && !chunk.Has(ref disabledHandle);", capture)
        self.assertEqual(capture.count("if (!LoadedForDrawing(chunk)) { if (!follow) drakeUnloaded += chunk.Count; continue; }"), 3)
        self.assertIn("bool drawn = loaded && WorldRules.EntityLevelDrawn(", capture)
        self.assertNotIn("d2 < visible[i].value.x", capture)
        self.assertIn("None = new[] { ComponentType.ReadOnly<LODWorldReferencePoint>() }", capture)
        # The levels are chosen again within half a second of a metre of camera movement, or of another LOD scale.
        self.assertIn("private const float MovedScanInterval = 0.5f, MovedDistance = 1f;", capture)
        self.assertIn("WorldRules.LevelsMayHaveChanged((camera - lastScanCamera).sqrMagnitude, lastScanScale, scale, MovedDistance);", capture)
        # Leshy: its own level rule, and a tree past its last mesh traced as that mesh where the game draws a card.
        leshy = self.text("src/TGPathTracing/LeshyCapture.cs")
        self.assertIn("int lod = WorldRules.LeshyLevel(prefab.LodDistancesSq, d2);", leshy)
        self.assertIn("if (card) { lod = prefab.CardMeshLevel; if (lod < 0) continue; }", leshy)
        self.assertNotIn("? -1f : v * v", leshy)
        # The in-game run reads these counts from the plugin's status line.
        for text in ('"; LOD as drawn (bias "', '" statically batched renderers traced in "', '" Drake entities the game has not loaded for drawing (left out)"',
                     '" trees past their last mesh traced as that mesh (the game draws a card)"'):
            self.assertIn(text, capture)
        # No distance past which nothing is traced: the Scene Radius and Ray Distance rows and every
        # cut by them are gone, not set wide; what the game draws is traced however far off.
        settings = self.text("src/TGPathTracing/Settings.cs")
        rows = self.text("src/TGPathTracing/PathTracingRow.cs")
        for text in (settings, rows, capture, leshy, self.text("src/TGPathTracing/CharacterCapture.cs"), self.text("src/TGPathTracing/WaterCaustics.cs"),
                     self.text("src/TGPathTracing/VegetationCapture.cs")):
            for gone in ("SceneRadius", "RayDistance", "Scene radius", "Ray distance", "float radius", "> r2", "reachSq"):
                self.assertNotIn(gone, text)
        self.assertIn("MaxRayDistance = Tgpt.NoRayEnd,", self.text("src/TGPathTracing/HdrpPasses.cs"))
        self.assertIn("internal const float NoRayEnd = 1e6f;", self.text("src/TGPathTracing/NativeTracer.cs"))
        # Plants are never flagged as swaying things rays distrust: they are posed. Only cloth past its copies is.
        self.assertIn('internal static bool Sways(string shaderName) => shaderName.IndexOf("Cloth_Wind", StringComparison.OrdinalIgnoreCase) >= 0;', capture)
        # The old count-only workaround is gone: the projector list is captured before each traced frame.
        self.assertNotIn("CountDecals()", capture)
        self.assertIn("DecalCapture.Summary", capture)
        self.assertIn("DecalCapture.Capture(hd)", self.text("src/TGPathTracing/HdrpPasses.cs"))
        # Grass, leaves and trunks sway in the tracer as the game sways them: the game's three
        # vegetation shaders' vertex maths (read from their compiled code), every such instance posed
        # every frame in a group with the others of its cell, with no count or distance that leaves
        # one still.
        header = self.text(PT + "include/tgpt_api.h")
        self.assertIn("TGPT_EXPORT int32_t TGPT_CALL TGPT_SetPlantWind(const TGPT_PlantWindFrame* frame, const TGPT_PlantWind* winds, uint32_t count);", header)
        self.assertIn("TGPT_EXPORT int32_t TGPT_CALL TGPT_SetWindNoise(uint32_t key, uint32_t width, uint32_t height, uint32_t flags, const uint8_t* texels);", header)
        self.assertIn('static_assert(sizeof(TGPT_PlantWind)==48 && sizeof(TGPT_PlantWindFrame)==32, "TG Path Tracing plant wind ABI");', header)
        sway = self.text(PT + "shaders/pt_sway.hlsl")
        self.assertIn("if (pass.a.z != 0u) { plant(id.x); return; }", sway)
        self.assertIn("const float gust = max(noise_at(maps, w, uv2), noise_at(maps, w, uv1));", sway)
        self.assertIn("float k = grp.speed * w.strength * P.y * 0.0001 + 1.0;", sway)
        self.assertIn("const float theta = atan2(m.toObject[0].x, m.toObject[2].x);", sway)
        self.assertIn("const float fade = saturate((viewZ + w.fadeX) / (w.fadeX - w.fadeY));", sway)
        scene = self.text(PT + "src/scene.cpp")
        self.assertIn("if(grouped[source]) continue; // a plant in the wind: traced in its group's pose (below), not where it stands still", scene)
        self.assertIn("plan_plants(device, heap, release, retireValue, grouped, plantUses);", scene)
        self.assertIn("pose_plants(device, list, heap, ring, release, retireValue, plantUses, camera, barriers);", scene)
        self.assertIn("if(g.built) { desc.Inputs.Flags|=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE; desc.SourceAccelerationStructureData=g.blas.va(); }", scene)
        hpp = self.text(PT + "src/scene.hpp")
        self.assertIn("constexpr double kPlantCell=16.0;", hpp)
        # No count of plants and no distance: only how many groups are made a frame (the rest wait a frame, counted).
        self.assertNotIn("kMaxPlant", hpp)
        self.assertNotIn("kPlantReach", hpp)
        plant = self.text("src/TGPathTracing/PlantWind.cs")
        self.assertIn('internal const string GrassShader = "TG/Vegetation/Vegetation_Grass", LeavesShader = "TG/Vegetation/Vegetation_Leaves", TrunkShader = "TG/Vegetation/Vegetation_Trunk";', plant)
        self.assertIn('Time = Time.time, Speed = Shader.GetGlobalFloat("_VegetationWindSpeed"), Direction = Shader.GetGlobalFloat("_VegetationWindDirection"),', plant)
        self.assertIn("PlantWind.Add(m, shaderName, (uint)(materialList.Count - 1));", capture)
        self.assertIn("PlantWind.Send(tracer);", capture)
        managed = self.text("src/TGPathTracing/NativeTracer.cs")
        self.assertIn("Marshal.SizeOf(typeof(NativePlantWind)) != 48 || Marshal.SizeOf(typeof(NativePlantWindFrame)) != 32 || Marshal.SizeOf(typeof(NativeFogBox)) != 144)", managed)
        gpu = self.text("tests/pathtracing_gpu_test.cpp")
        self.assertIn('"grass, leaves and a trunk in the wind cast their shadows where the game\'s pose puts them, and the shadows move with them"', gpu)
        self.assertIn('"plants in the wind\'s pose are met by every ray from where it starts: no light passes them at their feet"', gpu)
        self.assertIn("static V3 grass_wind_reference(V3 p, const M4& world, const float toObject[9], float strength, float time, float speed, float direction, const PlantNoise& noise) {", gpu)
        project = self.text("tests/WorldRules.Tests.csproj")
        self.assertIn('<Compile Include="..\\src\\TGPathTracing\\WorldRules.cs" Link="WorldRules.cs" />', project)
        self.assertIn("WorldRules.Tests", self.text("docs/TESTING.md"))
        self.assertIn('"at 25 m the second level (20 to 60) is drawn"', self.text("tests/world_rules_tests.cs"))

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
        self.assertIn("throughput *= m.reflectance * m.occlusion;", common)
        self.assertNotIn("throughput *= m.albedo;", common)
        self.assertIn("s.f0 = lerp(0.04, base, finish.x);", common)
        self.assertNotIn("metallic * 0.8", common)
        self.assertIn("return float2(lerp(ends.x, ends.y, 1.0 - t.r), lerp(ends.z, ends.w, 1.0 - t.a));", common)
        prepare = self.text(PT + "shaders/pt_prepare.hlsl")
        self.assertIn("roughness = traced.roughness; f0 = traced.f0;", prepare)
        self.assertIn("reflection_boost(roughness, f0, forwardBoost);", prepare)
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
        self.assertIn('["TG/Clothes/Cloth_Wind"] = new MaskLayout { Texture = "_Mask_Map", SmoothnessChannel = 2, Occlusion = true },', capture)
        self.assertIn('Texture = "_MaskMap", Keyword = "_MASKMAP"', capture)
        self.assertIn("string mask = ReadFinish(m, shaderName, ref mat, true);", capture)
        # Shader Graphs that tile their maps through properties of their own are traced with that tiling.
        self.assertIn('mat.ScaleX = mat.ScaleY = m.GetFloat("_Base_Tiling");', capture)
        self.assertIn("            ShaderGraphTiling(m, shaderName, ref mat);", capture)

    def test_path_tracing_is_mono_only_and_packaged(self):
        helpers = self.text("tools/Package.Common.ps1")
        self.assertIn("function Get-TgModNames { @('TGFrameGeneration','TGBetterUpscaling','TGPathTracing','TGNeuralRendering') }", helpers)
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
        self.assertIn("if(dxr12 && serReorders) {", tracer)
        for name in ["prepare", "gi", "giSpatial", "di", "diSpatial", "specular", "temporal", "atrous", "compose"]:
            self.assertIn(f"TGPT_PASS(g, {name}, {name}Dxr12);", tracer, name)
            self.assertIn(f"TGPT_PASS(g, {name}, {name});", tracer, name)
        # A refused 6.9 set: the 6.6 set is made, without the reordering passes.
        self.assertIn("const size_t first=jobs.size(); set(false); pass_cache::total+=uint32_t(jobs.size()-first); run(first);", tracer)

    def test_path_tracing_hair_highlights(self):
        # Hair's highlights are HDRP's Kajiya-Kay hair (the game's RealHair is its Hair master stack): two lobes around the
        # strand's direction, shifted along the normal, their exponent from smoothness (capped: HDRP's sharpest is a line rays
        # never meet), tinted, times Fresnel at 0.0465 and a quarter; for the sun and every lamp sample, and hair's
        # reflections of its surroundings in its second lobe's tint. The strand comes from the flow map in the triangle's
        # texture frame (pt_prepare), stored a pixel in the hair texture with its material. Harness: a sunlit quad of hair
        # matches the formula pixel by pixel across its strands, and is ~19x fainter along them (a round lobe: 1x).
        header = self.text(PT + "include/tgpt_api.h")
        common = self.text(PT + "shaders/pt_common.hlsli").replace("\r\n", "\n")
        managed = self.text("src/TGPathTracing/NativeTracer.cs")
        self.assertIn("#define TGPT_MATERIAL_HAIR_LOBES 32768u", header)
        self.assertIn("#define MATERIAL_HAIR_LOBES 32768", common)
        self.assertIn("internal const uint MaterialHairLobes = 32768;", managed)
        self.assertIn('static_assert(sizeof(TGPT_Material)==204, "TG Path Tracing material ABI");', header)
        self.assertIn('static_assert(sizeof(TGPT_HairLobes)==48, "TG Path Tracing hair lobes ABI");', header)
        self.assertIn("Marshal.SizeOf(typeof(NativeMaterial)) != 204", managed)
        self.assertIn("Marshal.SizeOf(typeof(NativeHairLobes)) != 48", managed)
        self.assertIn("static const float HAIR_F0 = 0.0465;", common)
        self.assertIn("static const float HAIR_MAX_EXPONENT = 2000.0;", common)
        self.assertIn("return saturate(th + 1.0) * (exponent + 2.0) / (2.0 * PI) * pow(saturate(1.0 - th * th), 0.5 * exponent);", common)
        self.assertIn("const float3 t1 = normalize(s.strand + s.normal * m.hairShift), t2 = normalize(s.strand + s.normal * m.hairShift2);", common)
        self.assertIn("return 0.25 * fresnel_schlick((float3)HAIR_F0, dot(l, h)) * (s.hairLobe.x * m.hairTint) * lobes * nl;", common)
        # the per-texel strand: the flow map's direction in the tangent frame of the mesh's own coordinates
        prepare = self.text(PT + "shaders/pt_prepare.hlsl")
        self.assertIn("const float3 strand = f.x * t + f.y * b + sqrt(1.0 - min(dot(f, f), 1.0)) * normal;", prepare)
        self.assertIn("if (strands && (hm.flags & MATERIAL_HAIR_LOBES) != 0 && hm.hairLobeTile >= 0 && frame) {", prepare)
        self.assertIn("(hairLobes ? 64u : 0u)", prepare)
        self.assertIn("if ((bits & (4u | 64u)) != 0 && materialIndex == g.material.x && g.hair.x != INVALID) {", common)
        self.assertIn("tint = s.hairLobe.x * m.hairTint * m.hairStrength2;", self.text(PT + "shaders/pt_specular.hlsl"))
        # the game's shader reads flow, mask and specular at the mesh's own coordinates; green flipped on the unpacked value
        d3d11 = self.text(PT + "shaders/d3d11.hlsl")
        self.assertIn("flow.y += c1.z * (1.0 - 2.0 * flow.y);", d3d11)
        self.assertIn("TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterHairLobes(uint64_t key, const TGPT_HairLobes* lobes);", header)
        self.assertIn('registerHairLobes = Bind<RegisterHairLobesFn>(module, "TGPT_RegisterHairLobes");', managed)
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        self.assertIn("HairLobes(m, ref mat);", capture)
        self.assertIn("mat.HairSecondaryShift = mat.HairShift + FloatOr(m, \"_SecondarySpecularShift\", 0f);", capture)
        self.assertIn("if ((mat.Flags & Tgpt.MaterialHairLobes) != 0) { Linear(now.Specular, out mat.HairTintR, out mat.HairTintG, out mat.HairTintB, out _); changed = true; }", capture)
        harness = self.text("tests/pathtracing_gpu_test.cpp")
        for name in ["hair's highlight is HDRP's Kajiya-Kay lobe at its peak",
                     "hair's highlight lies along its strands (the half vector leaning into them dims it)"]:
            self.assertIn('"' + name + '"', harness, name)

    def test_pt_memory_unload_and_visible_refusal(self):
        session = self.text("src/TGPathTracing/Session.cs")
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("SceneManager.sceneUnloaded += SceneUnloaded", session)
        self.assertNotIn("GeometryBudget", session)
        self.assertIn("WantsFrames => CanCapture && !TextureShare.Incomplete", session)
        self.assertIn('Notices.Show("tgpt.compiling"', session)
        self.assertIn('Notices.Hide("tgpt.compiling")', session)
        self.assertIn("st.sceneResourcesDirty=true", tracer)
        self.assertIn("if(st.sceneResourcesDirty && st.alphaAtlas.t.uav)", tracer)
        self.assertNotIn("if(cleared && st.alphaAtlas.t.uav)", tracer)
        self.assertIn("if(st.scene.intake(&st.heap, st.release, st.consumer.value)) release_scene_extras();", tracer)

    def test_path_tracing_atlas_sides_held_only_around_a_bake(self):
        # PT-87 (docs/agents/memory-4.md): Windows counts an atlas both devices hold twice (345 MB with hair's strands),
        # so the game's device lets go of its side between bakes and opens it again by a kept handle. A bake function
        # that took a bake before its atlas's side is held would write through a view that is gone and lose the bake:
        # every one asks hold_sides first, under the engine's lock, before it reads its queue. The game's device never
        # lets go before it has opened the atlas a second time by that handle (the way back shown to work), a failed
        # opening leaves the bake in its queue, and a cleared scene's clears take both sides up first.
        tracer = self.text(PT + "src/tracer.cpp").replace("\r\n", "\n")
        self.assertIn("bool hold_sides(std::unique_lock<std::mutex>& lock, std::initializer_list<uint32_t> sides, bool sceneBound=true) {", tracer)
        for queue, sides in [("newTextures", "{kSideColour}"), ("newHairLobeBakes", "{kSideColour}"), ("newEyeBakes", "{kSideColour}"),
                             ("newAlphaTextures", "{kSideAlpha}")]:
            self.assertIn(f"if(!e.{queue}.empty() && (!hold_sides(lock, {sides}) || e.clearGeneration!=st.scene.clear_generation())) return;", tracer, queue)
        self.assertIn("const bool held=e.newHairBakes.empty() || hold_sides(lock, {kSideColour, kSideStrands});", tracer)
        self.assertIn("if(!held) n=0;", tracer)
        self.assertIn("if(!e.newCookies.empty() && !hold_sides(lock, {kSideCookies}, false)) return;", tracer)
        self.assertEqual(tracer.count("hold_sides(lock, {"), 6)
        # Every write of an atlas through the game's device: the bakes, a cleared scene's clears and the start's.
        hint = ": a new writer of this atlas holds its side first (hold_sides or take_up_side); then raise this count"
        self.assertEqual(tracer.count("st.atlas.t.uav.Get()"), 6, "the colour atlas" + hint)
        self.assertEqual(tracer.count("st.alphaAtlas.t.uav.Get()"), 3, "the alpha atlas" + hint)
        self.assertEqual(tracer.count("st.cookieAtlas.t.uav.Get()"), 2, "the cookie atlas" + hint)
        self.assertIn("if(st.sceneResourcesDirty) { take_up_side(kSideAlpha); take_up_side(kSideColour); }", tracer)
        self.assertIn("st.sceneResourcesDirty=!st.atlas.t.uav;", tracer)
        let_go = tracer[tracer.index("void let_go_side(uint32_t which, uint32_t idleThen) {"):tracer.index("void tend_sides() {")]
        self.assertIn("if(!a.t.d11 || !a.t.d12 || !s.handle || s.stay) return;", let_go)
        self.assertLess(let_go.index("const HRESULT r=open_side(which, false, second, view);"), let_go.index("a.t.d11.Reset();"))
        self.assertIn("keep_side_handle(kSideColour); keep_side_handle(kSideAlpha); keep_side_handle(kSideCookies);", tracer)
        self.assertIn("keep_side_handle(kSideStrands);", tracer)
        self.assertIn("for(uint32_t side=0;side<kSides;++side) close_side(side);", tracer)
        # By the clock (PT-98), and at once while no frame is traced: off, or held back for textures.
        self.assertIn("constexpr uint64_t kSideIdleMs=2000;", tracer)
        self.assertIn("tend_sides_quiet();", tracer)
        self.assertIn('#include "pathtracing_memory4_checks.inl"', self.text("tests/pathtracing_memory2_checks.inl"))
        checks = self.text("tests/pathtracing_memory4_checks.inl")
        self.assertIn("whether the game's device held the atlases or opened them again for the bakes", checks)
        self.assertIn("a bake whose atlas cannot be opened waits", checks)

    def test_path_tracing_passes_made_while_the_game_starts(self):
        # The driver compiles every pass (13 to 22 s after a new build in round 3) and the D3D11 passes are compiled from
        # source: on the first traced frame that froze the game. They are made on threads of their own from the device's
        # creation, published as TGPT_Status.starting before the device is ready, and the plugin hands HDRP's frames over
        # only once they are made; a frame handed over meanwhile goes untraced without counting as a failure.
        tracer = self.text(PT + "src/tracer.cpp").replace("\r\n", "\n")
        ready = tracer.split("void device_ready(", 1)[1].split("void __stdcall render_event(", 1)[0]
        self.assertLess(ready.index("start_passes(d5.Get(), dxr12, st.serReorders);"), ready.index("e.status.deviceReady=1;"))
        self.assertIn("{ auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); e.status.starting=1; }", tracer)
        self.assertIn("    if(passes_starting()) return;\n    const uint32_t parity=uint32_t(st.frame+1)&1u;", tracer)
        self.assertNotIn("if(made) passes=build_passes(st.d12.Get(), st.dxr12, st.serReorders);", tracer)
        self.assertIn("adopt_passes(*warm.passes);", tracer)
        self.assertIn("if(auto it=warm.d3d11.find(shader_key(macro, entry, profile, extra)); it!=warm.d3d11.end()) return it->second;", tracer)
        for macro in ["TGPT_COPY_ALBEDO", "TGPT_HAIR", "TGPT_EYE", "TGPT_GUIDES"]:
            self.assertIn('{"' + macro + '", "main", "cs_5_0"}', tracer, macro)
            self.assertIn('make_cs("' + macro + '")', tracer, macro)
        self.assertIn("uint32_t starting; float startSeconds;", self.text(PT + "include/tgpt_api.h"))
        self.assertIn("Status.Supported != 0 && Status.Starting == 0 && Status.Disabled == 0", self.text("src/TGPathTracing/Session.cs"))
        gpu_test = self.text("tests/pathtracing_gpu_test.cpp")
        self.assertIn('std::getenv("TGPT_TEST_STARTUP_SECONDS")', gpu_test)
        self.assertIn('std::getenv("TGPT_TEST_CANDLE_ONLY") ? 600 : 180', gpu_test)
        self.assertIn("for(int i=0;i<startupSeconds*10 && status.starting;++i) { Sleep(100); ReadStatus(&status); }", gpu_test)
        self.assertIn("if(status.starting) { std::printf(\"FAILED: the passes were still being made after", gpu_test)
        # When the GPU stops (the freeze of 2026-09-28 09:16, a driver reset with nothing in the logs): each device's reason
        # and how far the tracer's work got, from markers the queue writes as each stage completes.
        self.assertIn("    try { report_gpu_stop(hr); } catch(...) {}", tracer)
        self.assertIn("if(st.crumbsOn) { st.crumbsRecorded=(uint32_t(st.frame)<<8)|stage; crumb(list, 0, st.crumbsRecorded); }", tracer)
        self.assertIn("const D3D12_WRITEBUFFERIMMEDIATE_MODE mode=D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT;", tracer)
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("#define PT_RAYQUERY(flags) RayQuery<flags, RAYQUERY_FLAG_ALLOW_OPACITY_MICROMAPS>", common)
        for shader in ["pt_common.hlsli", "pt_compose.hlsl"]:
            self.assertNotRegex(self.text(PT + "shaders/" + shader), r"RayQuery<RAY_FLAG", shader)
        cmake = self.text("CMakeLists.txt")
        self.assertIn("-T cs_6_9 -E main -D PT_DXR12", cmake)
        self.assertIn("-T lib_6_9 -D PT_DXR12 -D PT_SER", cmake)
        # Micromaps: made for the materials of the first alpha-tested instance that can have them, once what its alpha
        # test reads is there (the game's own texture, or its atlas tiles baked); an instance whose materials differ, or
        # a frame that turns them off, traces with them disabled.
        scene = self.text(PT + "src/scene.cpp")
        self.assertIn("const bool omm=alpha && ommEnabled && omm_instance(*m, materialIndices_.data()+inst.materialBase, retireValue) && ommAllowed;", scene)
        self.assertIn("const bool omm=alpha && ommEnabled && omm_instance(*m, mats, retireValue) && ommAllowed;", scene) # a thing the game moves
        self.assertIn("if(omm_matches(mesh, materials)) { mesh.ommUsedFrame=swayFrame_; return true; }", scene)
        self.assertNotIn("D3D12_RAYTRACING_INSTANCE_FLAG_DISABLE_OMMS", scene) # (since PT-110's M5: the plain bottom level instead; test_cutouts_are_counted_...)
        self.assertNotIn("D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_DISABLE_OMMS", scene) # (the same)
        # Micromaps wait for the tile the alpha test reads: the 256x256 alpha tile when the material has one.
        self.assertIn("const int32_t tile=alphaTile ? mat.alphaTile : mat.albedoTile;", scene)
        self.assertIn("(size_t(tile)>=ready.size() || !ready[size_t(tile)])", scene)
        self.assertIn("st.scene.tile_settled(b.tile);", tracer)
        self.assertIn("st.scene.ommAllowed=!(f.options&TGPT_OPTION_NO_MICROMAPS);", tracer)
        bake = self.text(PT + "shaders/pt_omm_bake.hlsl")
        self.assertIn("OMM_SPECIAL_FULLY_TRANSPARENT = -1, OMM_SPECIAL_FULLY_OPAQUE = -2", bake)
        self.assertIn("if (m.albedoTile < 0) {", bake)
        # PT-08 (docs/agents/speed-2.md, 2026-10-08): a cut-out whose alpha test reads the game's own texture has its
        # micromaps baked from that texture, by the shader's own shared_cutout, its base alpha and the material's cutoff
        # (they were switched off for every such mesh, which in the game is every cut-out). They are never used with
        # another texture: the texture's handle is in the material's signature, and stale micromaps are made anew.
        self.assertIn("const bool own = shared_cutout(m);", bake)
        self.assertIn("const float a = m.baseColor.a * t.Load(int3(texel, 0)).a;", bake)
        self.assertIn("const float covered = m.alphaCutoff + OMM_MARGIN, bare = m.alphaCutoff - OMM_MARGIN;", bake)
        self.assertIn("if((m.flags&TGPT_MATERIAL_ALPHA_TEST) && shared_cutout(uint32_t(i))) { const uint32_t own=0x6F776E21u; mix(&own, 4); mix(&m.colorTexture, 4); }", scene)
        self.assertIn("if((omm_stale(mesh) || swayFrame_-mesh.ommUsedFrame>kOmmUnusedFrames) && omm_possible(mesh, materials)) release_omm(mesh, retireValue);", scene)
        self.assertNotIn("colorTexture>=0) { mesh.omm=Mesh::Omm::None; return; }", scene)
        self.assertIn("st.scene.sharedTextures=st.sharedBase!=kInvalid && !(f.options&TGPT_OPTION_ATLAS_TEXTURES);", tracer)
        self.assertIn('"micromaps baked from the game\'s own texture leave every pixel as the alpha test alone makes it"', self.text("tests/pathtracing_gpu_test.cpp"))
        # The scene stage (the same report): a frame's acceleration structures are built side by side in their stage's
        # scratch, a refit at a refit's size, one barrier after the last. Built one after another over one piece, with a
        # barrier after each, 59 to 82 characters took 9 to 14 ms of every frame.
        self.assertIn("desc.ScratchAccelerationStructureData=scratch_place(list, skinScratch_, offset, rebuild ? m->skinScratchBytes : m->skinUpdateBytes);", scene)
        self.assertIn("desc.ScratchAccelerationStructureData=scratch_place(list, plantScratch_, offset, g.built ? g.updateBytes : g.scratchBytes);", scene)
        self.assertIn("desc.ScratchAccelerationStructureData=scratch_place(list, swayScratch_, offset, c->built ? c->updateBytes : c->scratchBytes);", scene)
        self.assertNotIn("One scratch region, reused only after the preceding build.", scene)
        # TG Path Tracing ships Microsoft's Agility SDK runtime beside its native DLL, where the bridge looks.
        helpers = self.text("tools/Package.Common.ps1")
        self.assertIn("@{Folder='D3D12'; Signer='Microsoft Corporation'", helpers)
        self.assertIn("Files=@('D3D12Core.dll'); Notices=@('LICENSE-DirectX-Agility-SDK.txt')", helpers)
        self.assertIn("$vendors+=@{Folder='TGPathTracing\\D3D12'; Signer='Microsoft Corporation'", self.text("Stage-Runtime.ps1"))

    def test_path_tracing_gpu_lifetimes(self):
        # Descriptor slots, the atlas and sky, a start that fails part-way, and a list that is never submitted:
        # the GPU may still be reading, or the bookkeeping may describe work that never ran.
        gpu = self.text(PT + "src/gpu.hpp")
        # A slot given back waits for the GPU (retiring_), and one that is not handed out is never given back again
        # (2026-10-11: a slot in the free list twice would go to two owners).
        release = gpu[gpu.index("void release(uint32_t index"):gpu.index("void collect(uint64_t completed)")]
        self.assertIn("if(index>=capacity_ || state_[index]!=1) { note(index, false, file, line); return; }", release)
        self.assertIn("retiring_.emplace_back(retireAt_, index);", release)
        self.assertNotIn("free_.push_back", release)
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
        # reset forgets the terrain material. (ReSTIR GI's reuse across frames is pt_gi_reuse's since 2026-10-10.)
        self.assertIn("&& all(g.prevOriginDelta.xyz == 0.0)) {", self.text(PT + "shaders/pt_gi_reuse.hlsl"))
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
        # (One helper, pt_common indirect_color, for a camera pixel's bounced light and a reflection's: PT-46.)
        gi_spatial = self.text(PT + "shaders/pt_gi_spatial.hlsl")
        self.assertIn("result = indirect_color(result);", gi_spatial)
        indirect = self.text(PT + "shaders/pt_common.hlsli").split("float3 indirect_color(", 1)[1].split("\n}", 1)[0]
        self.assertIn("if (scale > 1.0 && c[i] < l) scale = max(1.0, min(scale, l / max(l - c[i], 1e-6)));", indirect)
        self.assertIn("return l + (c - l) * scale;", indirect)
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
        self.assertEqual(order, ["Path Tracing", "Denoising", "Exposure", "Dark Adaptation", "Rays Per Pixel", "Bounces", "Radiance Cache",
                                 "Indirect Color", "Reflections", "Caustics", "Fill Light Fog", "Spatial Samples", "Boiling Filter",
                                 "Emissive Lights", "Crevice Light", "Sky Intensity", "Reflection Boost", "Debug View"])
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

    def test_path_tracing_wet_ground(self):
        # PT-52 (docs/agents/water-edges.md): where HDRP's waves have been the ground is wet, by the optics of water on a rough
        # surface (Lekner and Dorf), not a tint, and it dries by its slope and the time since (Jeffreys' draining film, sand's
        # soaking, evaporation). One store of where the water has been, kept across frames in the world's cells, read by the
        # camera's surfaces and by every ray's hit alike (a mirror shows the same wet sand).
        common = self.text(PT + "shaders/pt_common.hlsli")
        wet = self.text(PT + "shaders/pt_wet.hlsl")
        prepare = self.text(PT + "shaders/pt_prepare.hlsl")
        tracer = self.text(PT + "src/tracer.cpp")
        api = self.text(PT + "include/tgpt_api.h")
        self.assertIn("float3 wet_albedo(float3 a) { return (1.0 - WATER_RE) * (1.0 - WATER_RI) * a / (1.0 - WATER_RI * a); }", common)
        self.assertIn("static const float WATER_RE = 0.0665;", common)
        self.assertIn("static const float WATER_RI = 0.4749;", common)
        self.assertIn("const float drain = WET_VISCOSITY * max(above, 0.0) / (WET_GRAVITY * sin2 * WET_GRAIN * WET_GRAIN);", common)
        self.assertIn("static const float WET_EVAPORATION = 0.2e-3 / 3600.0;", common)
        # Rays read it in hit_material; the camera's surfaces in pt_prepare, not where the water stands over them now.
        hit_material = common[common.index("SurfaceMaterial hit_material(Hit h) {"):]
        hit_material = hit_material[:hit_material.index("\n}\n")]
        self.assertIn("wet_state(h.position, h.normal)", hit_material)
        # Not for what a walk inside a subsurface material meets (a wick in wax, PT-40): nothing inside a solid thing has a
        # film of water on it. And with the store's read inside the walk's loop the reflections pass hung the GPU on its
        # first frame (RTX 5080, driver 32.0.16.1692; docs/agents/merge-3.md "The GPU hang came back"): keep it out of there.
        self.assertIn("if (!g_hitInside && wet_on() && h.position.y <= g.wetGrid.z) {", hit_material)
        walk = common.split("bool subsurface_walk(", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("g_hitInside = true;\n            const float3 inside = hit_material(h).reflectance;\n            g_hitInside = false;", walk)
        self.assertEqual(common.count("g_hitInside = true;"), 1)
        # One place makes a surface wet, hit_material: HDRP's G-buffer is no longer made wet in pt_prepare (PT-58).
        self.assertNotIn("wet_", prepare)
        self.assertIn("if (!(state.y > 0.0) || state.z > 0.5) return;", common)
        # The store reads the water where every ray meets it (the waves as triangles, PT-71 item 5: pt_water_surface water_over, a
        # ray straight down at them), and runs before the camera's surfaces are made.
        self.assertIn("if (water_over(xz, slot, height)) {", wet)
        self.assertNotIn("water_height", wet + self.text(PT + "shaders/pt_waves.hlsli"))
        self.assertLess(tracer.index("dispatch(list, st.wetPass.Get(), {0u}, b"), tracer.index("dispatch(list, st.prepare.Get(), {out.g0.uav, out.g1.uav}, b);"))
        # With the tracer's own camera rays (PT-59) the camera's surface is a hit like any other ray's: hit_material is what
        # makes it wet (pt_primary write_traced). What is not ground stays dry at a hit:
        # a material light walks inside (skin, wax), an eye, glass, hair.
        self.assertIn("const SurfaceMaterial traced = hit_material(hit);", self.text(PT + "shaders/pt_primary.hlsli"))
        self.assertIn("if ((m.flags & (MATERIAL_SUBSURFACE | MATERIAL_GLASS | MATERIAL_EYE | MATERIAL_CAMERA_ONLY | MATERIAL_STRANDS | MATERIAL_HAIR_LAYER)) == 0)\n"
                      "            state = wet_state(h.position, h.normal);", hit_material)
        self.assertIn('"wet rock is darker by a water film\'s internal reflection and dries in its time"', self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn('"a material light walks inside (skin, wax) is not made wet where the camera\'s own ray meets it"', self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn("std::memcpy(&k.wet[2], &st.wetClock, 4);", tracer)
        self.assertIn("#define TGPT_OPTION_DRY_GROUND 536870912u", api)
        for name in ("pt_common.hlsli", "pt_prepare.hlsl", "pt_wet.hlsl"):
            self.assertNotIn("wet tint", self.text(PT + "shaders/" + name).lower(), name)

    def test_path_tracing_water_surface(self):
        # PT-71 item 5: water is a surface of the traced scene that every ray meets, its waves real triangles in the
        # acceleration structure (scene_water.cpp, pt_water_mesh.hlsl, docs/agents/water-4.md): no walk of a height field for
        # every ray, no pass of its own over HDRP's picture, nothing that depends on the screen.
        shaders = PT + "shaders/"
        common = self.text(shaders + "pt_common.hlsli")
        surface = self.text(shaders + "pt_water_surface.hlsli")
        mesh = self.text(shaders + "pt_water_mesh.hlsl")
        path = self.text(shaders + "pt_water_path.hlsli")
        primary = self.text(shaders + "pt_primary.hlsli")
        specular = self.text(shaders + "pt_specular.hlsl")
        scene = self.text(PT + "src/scene_water.cpp")
        header = self.text(PT + "src/scene.hpp")
        tracer = self.text(PT + "src/tracer.cpp")
        # The old route is gone, with its passes, events, histories, exports and row.
        for old in ("pt_water.hlsl", "pt_water_under.hlsl", "pt_water_optics.hlsli"):
            self.assertFalse((ROOT / PT / "shaders" / old).exists(), old)
        for old in ("TGPT_WATER_SHADE", "TGPT_COPY_WATER_DEPTH", "TGPT_WATER)"):
            self.assertNotIn(old, self.text(shaders + "d3d11.hlsl"))
        for old in ("water_event(", "water_shade_event(", "waterHist", "waterUnderHist", "TGPT_EVENT_WATER"):
            self.assertNotIn(old, tracer)
        for old in ("TGPT_SetWater(", "TGPT_SetWaterShade(", "#define TGPT_EVENT_WATER"):
            self.assertNotIn(old, self.text(PT + "include/tgpt_api.h"))
        self.assertFalse((ROOT / "src/TGPathTracing/WaterShade.cs").exists())
        self.assertNotIn("WaterReflections", self.text("src/TGPathTracing/Settings.cs"))
        # And so is the walk: no ray steps through a lattice of its own.
        for old in ("water_patch(", "water_subdivision(", "water_box(", "water_triangle(", "g_firstCaustic", "g_firstKnown"):
            self.assertNotIn(old, common + surface + path, old)
        # One surface for every ray: its triangles in the scene's structure under a bit of MASK_ALL, each unit an instance.
        self.assertIn("#define MASK_WAVES 0x80u", common)
        self.assertIn("#define MASK_SOLID 0x77u", common)
        self.assertIn("#define INSTANCE_WAVES 1024", common)
        self.assertIn("constexpr uint32_t kInstanceWaves=0x400;", header)
        self.assertIn("d.InstanceMask=kMaskWaves;", scene)
        self.assertIn("records.push_back(InstanceRecordGpu{meshBase+uint32_t(k), materialBase, kInstanceWaves|(g.slot<<8), kInvalid});", scene)
        self.assertIn("if ((inst.flags & INSTANCE_WAVES) != 0) water_hit(h, inst, mesh, idx, bary);", common)
        # The queries that ask for the water alone: trace_water, water_medium, water_over, and water_straight (the Caustics row
        # off: a light's straight ray passes the surface, and its parts under water are found crossing by crossing).
        self.assertEqual(surface.count("q.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_WAVES, ray);"), 4)
        self.assertIn("if (trace_water(g.cameraPos.xyz, dir, length(nearAt), w.stopped ? w.stop.t : g.params.y, waterHit)) {", primary)
        # Its position is the game's wave data's and the camera's position's alone: a lattice point slides from its parents'
        # line onto the waves as its parent edge grows past the cell wanted at its distance. The CPU cuts on the safe side.
        self.assertIn("return saturate(len / h - 1.0);", mesh)
        self.assertIn("const float h = max(cell, length(ac - ab * along) * CELL_PER_METRE);", mesh)
        self.assertIn("precise float3 p = c0 * w + c1 * u + c2 * v;", mesh)
        self.assertIn("precise float3 middle = (a + b) * 0.5;", mesh)
        self.assertIn("const float len = onEdge ? length(baseB - baseA) : longest * (float(2u * s) * exp2(-float(level)));", mesh)
        for name in ("size", "pixel", "viewProj", "invViewProj", "g.forward"):
            self.assertNotIn(name, mesh.split("[RootSignature(PT_ROOT_SIGNATURE)]", 1)[1], name)
        self.assertIn("static const float CELL_PER_METRE = 1.0 / 64.0;     // scene.cpp kWaterCellPerMetre", mesh)
        self.assertIn("constexpr double kWaterCellPerMetre=1.0/64.0;   // pt_water_mesh.hlsl CELL_PER_METRE", header)
        self.assertIn("const double h=0.99*std::max(cell, distance*kWaterCellPerMetre);", scene)
        self.assertIn("slots[i].cell=finest>0.f && c.displacement.size ? 0.5f/(finest*float(c.displacement.size)) : 0.f;", tracer)
        # Built once, refitted every frame after; posed before the top level is built; no count of units.
        self.assertIn("if(g.built) { desc.Inputs.Flags|=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE; desc.SourceAccelerationStructureData=blas; }", scene)
        record = self.text(PT + "src/scene.cpp")
        # PT-99 (docs/agents/waterblas.md, scene.hpp WaterGroup): the units are traced in GROUPS, a mesh of a submesh a unit:
        # one bottom level, one refit call and one instance a group (it was one a unit: 1,400 to 1,800 calls a frame).
        # No pass that traces knows of groups: a hit finds its unit as it finds any submesh, by the instance's mesh
        # record, the geometry's index and the material at the base plus that index.
        self.assertIn("MeshRecord mesh = meshes[inst.mesh];", common)
        self.assertIn("GeometryRecord geo = geometries[mesh.geometryBase + geometry];", common)
        self.assertIn("h.material = materialIndices[inst.materialBase + geometry];", common)
        self.assertNotIn("WaterGroup", common + surface + path + primary + specular)
        # So the frame's material list holds an entry of the water's one material for every unit a group can have,
        self.assertIn("if(waterWanted) combinedIndices_.insert(combinedIndices_.end(), kWaterGroupMembers, uint32_t(materials_.size()));", record)
        self.assertIn("if(!batch.empty() && (batch.size()>=mostMembers || batchTriangles+triangles_of(*u)>mostTriangles)) { place(batch); batch.clear(); batchTriangles=0; }", scene)
        self.assertIn("if(triangles+g->usedTriangles()>room || members+g->used>kWaterGroupMembers) break;", scene)
        # the shared pattern a group's worth a level, and a unit's geometry record names its own stretch of it: the
        # indices of its base triangles there are its vertices' numbers in its group's block.
        self.assertIn("constexpr uint32_t kWaterLevelIndices=kWaterGroupTriangles*3; // the shared pattern's indices a level", header)
        self.assertIn("count=kWaterGroupTriangles>>(2*m), vertices=kWaterLatticeVertices[m];", scene)
        self.assertIn("g.records.push_back(GeometryRecordGpu{g.level*kWaterLevelIndices+m.first*indicesEach, m.count*indicesEach, 0, 0});", scene)
        self.assertIn("waterGeometries_.insert(waterGeometries_.end(), g.records.begin(), g.records.end());", scene)
        self.assertIn("tri.at=g.vertexAt+(m.first+t)*perTriangle*12;", scene)
        self.assertIn("tri.span=g.vertexCount*12;", scene)
        self.assertIn("plantGeometries_.insert(plantGeometries_.end(), waterGeometries_.begin(), waterGeometries_.end());", record)
        # A unit its group holds and the frame does not want is collapsed to one point by the pose pass, never left as it
        # was (it would be traced) and never a reason to build the group anew; its mark is one bit, the same on both sides.
        self.assertIn("constexpr uint32_t kWaterTriCollapse=128;", header)
        self.assertIn("static const uint COLLAPSE = 128u;", mesh)
        self.assertIn("if ((tri.node & COLLAPSE) != 0u) {", mesh)
        self.assertIn("tri.node=kWaterTriCollapse|(g.level<<8); tri.span=g.vertexCount*12; tri.target=waterVertexPool_.pages[g.vertexPage].uav;", scene)
        self.assertIn("if(m.unit && m.unit->usedFrame==waterFrame_) { m.collapsed=false; continue; }", scene)
        # What is kept of a group cannot go stale (PT-125): a unit that leaves the map is taken out of its group's entry
        # at both places it leaves; a collapsed unit's point is written again when the scene's origin moves; and every
        # frame's records are laid out from the groups in use, none kept.
        self.assertIn("water_unit_gone(it->second); waterUnitIndex_.erase(it->first); it=waterUnits_.erase(it);", scene)
        self.assertIn("water_unit_gone(*u);\n            waterUnits_.erase(unitKey); waterUnitIndex_.erase(unitKey);", scene)
        self.assertIn("for(auto& g:waterGroups_) for(auto& m:g->members) m.collapsed=false;", scene)
        self.assertIn("waterGroupUses_.clear(); waterGeometries_.clear();", scene)
        # A group's frame (its units the frame wants) is counted anew every frame, in one pass over the frame's units; a
        # group every unit of which is wanted, and none of which is marked collapsed, needs no look at its members.
        self.assertIn("for(auto& g:waterGroups_) { g->used=0; g->usedBase=0; g->use=kInvalid; }", scene)
        self.assertIn("if(WaterGroup* g=u->group) { ++g->used; g->usedBase+=u->triangles; }", scene)
        self.assertIn("if(g.used==g.members.size() && !g.anyCollapsed) continue;", scene)
        self.assertIn("g.used=uint32_t(units.size()); g.usedBase=g.baseTriangles; // (this frame's count: every one of them is wanted)", scene)
        self.assertIn("fail(\"a group's count of the frame is not its members'\");", scene)
        self.assertIn('fail("a group holds a collapsed unit and does not say so");', scene)
        self.assertIn("if(recordCheck_) water_check();", scene)
        # The pools (scene.hpp WaterPool): a group's blocks are of every size, and a walk makes and lets go of some every
        # frame. So a page keeps its free stretches: a block is cut from the first stretch of the first page that holds
        # it, and one given back is one with the stretches it touches. (A block given back used to wait for the next
        # asked for of exactly its size.)
        self.assertIn("struct Page { Buffer buffer; uint32_t uav=kInvalid; uint32_t live=0; uint64_t idleSince=0; std::map<uint32_t, uint32_t> free; };", header)
        self.assertIn("for(auto stretch=candidate.free.begin();stretch!=candidate.free.end();++stretch) {", scene)
        self.assertIn("if(before->first+before->second==at) { start=before->first; bytes+=before->second; p.free.erase(before); }", scene)
        self.assertIn("if(after!=p.free.end() && at+block==after->first) { bytes+=after->second; p.free.erase(after); }", scene)
        self.assertNotIn("pool.free", scene)
        self.assertNotIn("pool->free", scene)
        self.assertIn('fail("a page\'s blocks and free stretches do not add up to the page");', scene)
        self.assertIn("for(size_t k=0;listed && k<most;++k) listed=combinedIndices_[materialBase+k]==uint32_t(materials_.size());", scene)
        # The old way is the TEST's alone (a group a unit, nothing put together): an environment variable, never a setting.
        self.assertIn('const bool ungrouped=GetEnvironmentVariableA("TGPT_TEST_WATER_UNGROUPED", nullptr, 0)>0;', scene)
        for name in ("src/TGPathTracing/Settings.cs", PT + "include/tgpt_api.h"):
            self.assertNotIn("UNGROUPED", self.text(name).upper(), name)
        self.assertIn('if(seaRuns("groups")) {', self.text("tests/water_sea_gpu.inl"))
        self.assertIn('"the water\'s pieces in groups trace the picture they traced one bottom level a piece, bit for bit"', self.text("tests/water_sea_gpu.inl"))
        self.assertLess(record.index("pose_water(device, list, heap, ring, release, retireValue"), record.index("// The top level: rebuilt each frame"))
        self.assertNotIn("kWaterMaxUnits", header + scene)
        self.assertIn("TGPT_PASS(1, sway, sway); TGPT_PASS(1, waterMesh, waterMesh);", tracer)
        self.assertIn("waterMesh:pt_water_mesh", self.text("CMakeLists.txt"))
        # The look is the game's wave normal at the hit (HDRP's gradient read), the side the triangle's.
        self.assertIn("water_displace(slot, h.waterBase, level, position, outward, lift, steep);", surface)
        self.assertIn("h.rounded = all(isfinite(outward)) ? (h.front ? outward : -outward) : h.normal;", surface)
        # One boundary for glass and water; one water path a pass, shade_path's.
        interface = self.text(shaders + "pt_interface.hlsli")
        self.assertIn("Interface dielectric_interface(float3 direction, float3 facing, float ni, float nt) {", interface)
        self.assertIn("const Interface boundary = dielectric_interface(direction, normalize(view + mirrored), ni, nt);", path)
        self.assertIn("const Interface boundary = dielectric_interface(-toLight, n, 1.0, WATER_IOR);", self.text(shaders + "pt_caustics.hlsl"))
        every = "".join(self.text(shaders + f.name) for f in (ROOT / PT / "shaders").iterdir() if f.name.endswith((".hlsl", ".hlsli")))
        self.assertEqual(every.count("float fresnel_dielectric("), 1)
        self.assertEqual(every.count("static const float WATER_IOR"), 1)
        self.assertEqual((common + specular + path).count("water_path("), 2)  # its definition and shade_path's one call
        self.assertEqual(specular.count("shade_seen("), 1)
        self.assertIn("specular[p] = float4(shown / g.radiance.x, g_waterGuide);", specular)
        # Under water the lamps' and the sun's light is the caustics', read where it landed.
        self.assertIn("if (medium >= 0 && !foam) radiance += weight * m.albedo * caustic_light(hit.position, hit.normal);", path)
        self.assertIn("st.release.add(st.consumer.value+1, b->resource.Get());", tracer)
        # Water the tracer has no slot for stays HDRP's to draw; the rest HDRP neither draws nor lights.
        rendering = self.text("src/TGPathTracing/WaterSurfaceRendering.cs")
        self.assertIn('"ShouldRenderSurface"', rendering)
        self.assertIn('"RenderWaterLighting"', rendering)
        self.assertIn("internal static bool Traced(WaterSurface s)", self.text("src/TGPathTracing/WaterCaustics.cs"))
        # The checks (tests/water_surface_gpu.inl, TGPT_TEST_WATER_SURFACE=1): the optics, and the surface itself.
        fixture = self.text("tests/water_surface_gpu.inl")
        for check in ("water radiance crossing carries 1/n squared with Fresnel",
                      "undenoised water mean has no isolated bright dots",
                      "water absorption belongs to each ray's travelled segment",
                      "underwater camera attenuates the camera-to-bed segment",
                      "water viewed from below totally internally reflects beyond the critical angle",
                      "a mirror sees the same water boundary and transmitted bed as the camera",
                      "water mirrors the sky at grazing angles with exact Fresnel",
                      "camera rays meet the actual waves including their sideways displacement",
                      "undenoised wavy pool converges to its many-sample transport reference",
                      "a mirror sees the same displaced waves and transport as the camera",
                      "waves meet a sloping shore at the nearest surface without a flat waterline step",
                      "the surface a camera ray meets is the game's wave surface, to 2 mm near the camera",
                      "far from the camera the surface stays within what its coarser lattice allows of the wave data",
                      # The shadow ray's surface, by an opaque liquid with the Caustics row ON (water 4b: it was judged with
                      # clear water and the row off, while "off" still stopped the light and brought none through; with
                      # clear water and the row on the sun arrives bent through the wave and fills that shadow, on either
                      # meaning of "off": 0.62 of the open floor's light there, printed and NOT judged, no reference yet).
                      "a shadow ray meets the same waves: the sun's shadow of an opaque liquid's edge on dry ground has the wave's outline",
                      "with the Caustics row off a light's straight ray passes clear water: its edge casts no shadow"):
            self.assertIn('"' + check + '"', fixture)
        # The three pictures stay three: the opaque liquid (judged), clear water with the row off (judged), and clear water
        # with the row on, whose number is printed so that it is seen, with the words that say nobody has judged it.
        self.assertIn("liquid(2000.f);", fixture)
        self.assertIn("const auto meanOff=shot(options), meanClear=shot(rowOn);", fixture)
        self.assertIn("clear water, the row on (NOT JUDGED, no reference)", fixture)
        # PT-81: the sea a pale sheet. The foam's Jacobian is summed over every band, as HDRP's threshold is made for (4, 8
        # and 12 times a factor near 1 for one, two and three bands; water 6 read it in the game's own HDRP build): over two
        # of a three-band sea's it stood 3.35 to 4.23 over the sum at rest, full cover everywhere. The sea's own fixture
        # (TGPT_TEST_WATER_SEA=1 with the water's switch) holds the game's numbers and judges it; the plugin logs them.
        foam = self.text(shaders + "pt_water_foam.hlsli")
        self.assertIn("for (uint band = 0; band < uint(info.x); ++band) {", foam)
        self.assertNotIn("min(uint(info.x), 2u)", foam)
        self.assertIn('#include "water_sea_gpu.inl"', fixture)
        sea = self.text("tests/water_sea_gpu.inl")
        for check in ("a three-band sea is not a sheet of foam: foam covers under a tenth of it",
                      "the sea's foam follows its waves: some of its pixels have foam, most have none",
                      "the open sea near the shore is darker than the sunlit sand beside it",
                      "more of the surface's foam amount is more foam",
                      "with the foam's intensity 0 the sea has no foam"):
            self.assertIn('"' + check + '"', sea)
        self.assertIn("auto threshold=[](double amount) { return float(12.0*std::pow(0.8+0.28*amount, 0.25)); };", sea)
        self.assertIn('const bool waterSea=waterOnly && !waterCost && GetEnvironmentVariableA("TGPT_TEST_WATER_SEA", nullptr, 0)>0;', self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn("if (v.Frame >= Time.frameCount - 1 && numbersLogged.Add(s.GetInstanceID()))", self.text("src/TGPathTracing/WaterCaustics.cs"))
        # PT-81, PT-85 (water 7, docs/agents/water-7.md). 1: the sun is asked for at every point where a path scatters inside
        # the water, through the waves (pt_water_sun.hlsli: a patch of the caustics' own light mesh made at the point, the
        # sun's disc as its footprint), and counted once: the chance hit of the disc that was the only way (one path in a
        # hundred thousand: the green dots) is dropped where a vertex's own question covers it.
        sun = self.text(shaders + "pt_water_sun.hlsli")
        self.assertIn('#include "pt_water_sun.hlsli"', path)
        self.assertIn('"${PT_SHADER_DIR}/pt_water_sun.hlsli"', self.text("CMakeLists.txt"))
        self.assertIn("radiance += weight * water_sun(uint(medium), origin, rayWidth, rng) / (4.0 * PI);", path)
        # PT-131 (docs/agents/campfix.md): the cell of a patch made at a point is no finer than half the width of the
        # path's own ray there, in every build (the water's path carries the width), and a coarser cell is summed as
        # every other: a triangle's sun is the area of its three moved corners across the light, which is that piece of
        # water's own. (Until 2026-10-12 10:30 the hidden Speed trial 64 turned the rule off for a run's comparison; it came
        # out when the game had answered: the same light, the beach's tracer 121 ms to 73.) (Two more things of the same day came
        # out. "Each corner its own facet's sun" for a coarser cell: the sea fixture's width part found the sun's share
        # 0.6 to 1.1 % up with it and the pattern changed, and the plain sum within 0.05 %. And "the shadow first", one
        # small ray against the ground before a patch is made: it gained nothing in the game. No line of either stays.)
        self.assertIn("float3 water_sun(uint medium, float3 p, float width, inout Rng rng) {", sun)
        self.assertIn("bool sun_cell_by_width() { return true; }", sun)  # (the rule with no switch since 2026-10-12)
        self.assertNotIn("speed_trial() & 64u", sun)
        self.assertIn("if (byWidth > cellWanted) { cellWanted = byWidth; coarse = true; }", sun)
        self.assertEqual(sun.count("const float across = max(0.0, -0.5 * dot(cross(cb.place.xyz - c00.place.xyz, cc.place.xyz - c00.place.xyz), l));"), 2)  # a tile's triangle and a patch's, whatever its cell
        for gone in ("ownSun", "levelShare", "facing / max(dot(normal, up)", "waterGeometry.w >> 31"):
            self.assertNotIn(gone, sun)
        self.assertNotIn("TGPT_TEST_SUN_PLAIN", self.text(PT + "src/tracer.cpp") + self.text("tests/water_sea_gpu.inl"))
        self.assertIn("if(ws.bandsHeld) expect(bandsSame, ", self.text("tests/water_sea_gpu.inl"))  # (the check that caught it holds the bands now)
        for gone in ("sun_shadow_first", "sun_ground_shade", "SUN_DARK", "MASK_GROUND", "kept * sure"):
            self.assertNotIn(gone, sun)
        for gone in ("SPEED_M3_SUN_SHADOW_FIRST", "g_scSunFirstDark"):  # (the measuring build's own form of it, the third page's 1024)
            self.assertNotIn(gone, common + sun)
        self.assertEqual(sun.count("sun_rays("), 3)  # its definition, the row's off, the chosen cell's: no copy for a first look
        self.assertIn("float rayWidth = max(startWidth, 0.0), raySpread = g_coneSpread >= 0.0 ? g_coneSpread : pixel_angle();", path)
        self.assertNotIn("g_speedSunFootprint", common + path + sun)  # (the measuring build's trial of it: the rule now)
        self.assertEqual((path + sun).count("water_sun("), 2)  # its definition and the scatter point's one call
        self.assertIn("if (delta && !glinted && !(asked && crossings == 1u && crossed) && g.sunColor.w > 0.0 && dot(direction, g.sunDir.xyz) >= g.sunDir.w)", path)
        self.assertIn("asked = medium >= 0 && !foam; crossings = 0; crossed = false; glinted = false;", path)
        self.assertIn("water_displace(slot, base, up, position, normal, lift, steep);", sun)
        self.assertEqual(sun.count("water_displace("), 1)  # one place a corner is made: the patch's and its middle's alike
        self.assertEqual(sun.count("const float covered = min(disc_triangle(ya, yb, yc), area);"), 2)  # the patch's, and a tile's triangle
        self.assertIn("kept = min(kept, SUN_BUDGET / corners);", sun)
        self.assertIn("float3 light = sum / kept;", sun)
        self.assertIn("if (water_passes_light()) return g.sunColor.rgb * sun_rays(", sun)
        self.assertNotIn("clamp(light", sun)
        # PT-81 (water 8, docs/agents/water-8.md): the wave corners kept, in tiles. A slot's corners are made once a frame
        # (pt_sun_grid.hlsl: water_displace's own answer at each place of a grid on the slot's level plane), with a record
        # a tile of what its corners' rays can do, a record a row and one for the whole grid. A point asks the records
        # which tiles a ray can reach it through and sums those tiles' triangles (more than SUN_PICK: that many by chance,
        # counted for the rest). The one water_displace call above stays for a patch the tiles cannot serve. What keeps
        # the result: the test only leaves a tile out when every corner's landing place lies to one side of the sun's
        # footprint (the tilt bounded for every normal: sun_tilt_slack), a tile is summed whole, and what is drawn by
        # chance is counted by its chance (`kept`). The fixture runs the same sea both ways, and with 2 and 8 tiles.
        grid = self.text(shaders + "pt_sun_grid.hlsl")
        self.assertIn("water_displace(slot, float3(xz.x, first.z, xz.y), float3(0.0, 1.0, 0.0), position, normal, lift, steep);", grid)
        self.assertIn("corners.Store4(address, asuint(float4(position, normal.x)));", grid)
        self.assertIn("const float3 ray = refract(-l, normal, 1.0 / WATER_IOR);", grid)
        self.assertIn("sun_record_store(corners, slotAt + SUN_WHOLE_AT, record);", grid)
        self.assertIn("static const uint SUN_CORNERS = 1024;", sun)
        self.assertIn("static const uint SUN_CORNER_BYTES = 24;", sun)
        self.assertIn("static const uint SUN_TILE = 4;", sun)
        self.assertIn("static const uint SUN_RECORD_BYTES = 64;", sun)
        self.assertIn("constexpr uint32_t kSunCorners=1024, kSunCornerBytes=24, kSunGrids=2;", tracer)
        self.assertIn("constexpr uint32_t kSunTile=4, kSunTiles=kSunCorners/kSunTile, kSunRecordBytes=64;", tracer)
        self.assertIn("constexpr uint32_t kSunSlotBytes=kSunCorners*kSunCorners*kSunCornerBytes+kSunTiles*kSunTiles*kSunRecordBytes+kSunTiles*kSunRecordBytes+kSunRecordBytes;", tracer)
        self.assertIn("static const uint SUN_WHOLE_AT = SUN_ROWS_AT + SUN_TILES * SUN_RECORD_BYTES;", sun)
        self.assertIn("TGPT_PASS(1, sunGrid, sunGrid);", tracer)
        # (2026-10-11: each stage for every slot, then one barrier; the stages in this order, their groups as before.)
        self.assertIn("const uint32_t groups[4][2]={{kSunCorners/8, kSunCorners/8}, {kSunTiles/8, kSunTiles/8}, {kSunTiles/8, 1}, {1, 1}};", tracer)
        self.assertIn("values[0]=i; values[1]=stage;", tracer)
        self.assertIn('GetEnvironmentVariableA("TGPT_TEST_SUN_GRID", mode, sizeof(mode));', tracer)
        self.assertIn("sunGrid:pt_sun_grid", self.text("CMakeLists.txt"))
        self.assertIn('#include "pt_sun_grid.h"', self.text(shaders + "pt_shaders.hpp.in"))
        self.assertIn("const float corners = 0.785 * float((n + 1u) * (n + 1u));", sun)
        self.assertIn("if (up.y > 0.99999 && abs(lift) < 2e-3 && gridCell <= cellOfWaves && slack < 0.1 && alongHi > 0.05 && abs(turned) > 1e-12 && abs(upAt.z) > 0.05) {", sun)
        # A tile's corner that stands behind the point's plane is followed back to it (its reach under 0), where the patch
        # made at the point gives such a corner up and so needs cells of half the point's way to the surface: the tiles
        # serve a point just under the waves from the same cells as any other (docs/agents/water-8.md: against a patch of
        # very fine cells on the CPU, x0.995 to x1.004 from 2 mm under the surface on).
        self.assertIn("const float cellOfWaves = cellWanted;", sun)
        self.assertIn("if (dot(normal, q.l) > 1e-4 && cosIn > 0.0 && along > 0.05 && all(isfinite(position)) && all(isfinite(t))) {", sun)
        self.assertIn("if (facing > 1e-4 && along > 0.05 && reach > 0.0 && all(isfinite(position)) && all(isfinite(t))) {", sun)  # (the patch's own corner: as it was)
        self.assertIn("const float reachLo = overLo >= 0.0 ? overLo / tileAlongHi : overLo / alongLeast;", sun)
        self.assertIn("const float reachLeast = min(overLeast / alongLo, 0.0);", sun)
        self.assertIn("const float slack = sun_tilt_slack(alpha, tilt, whole.least);", sun)
        self.assertIn("if (any(landLo > margin) || any(landHi < -margin)) continue;", sun)
        self.assertIn("kept = float(max(tiles, 1u)) / (float(max(left, 1u)) * float(every));", sun)
        # The patch made at the point is the code it was before the tiles (its loop runs when the tiles did not serve the
        # point); a tile's corners are kept two rows at a time, as the patch keeps its own (a third corner carried from
        # one turn of the loop to the next in a variable sent nothing: half the light, 2026-10-09).
        self.assertIn("[loop] for (uint turn = made ? 40u : 0u; turn < 40u; ++turn) {", sun)
        self.assertIn("const Interface boundary = dielectric_interface(-l, normalize(l + ggx_sample(normal, l, alpha, tilt)), 1.0, WATER_IOR);", sun)
        self.assertIn("const SunBent c00 = tileRows[(j - 1u) & 1u][i - 1u], c10 = tileRows[(j - 1u) & 1u][i], c01 = tileRows[j & 1u][i - 1u];", sun)
        self.assertNotIn("SUN_QUEUE", sun)
        self.assertNotIn("TGPT_TEST_SUN_BITS", tracer + sun)  # (a measuring switch of the job's: gone)
        self.assertIn('"the sun\'s light from the kept wave corners\' tiles is the light of patches made at their points (within a hundredth)"', sea)
        # 2: the water's own smoothness (HDRP's start and end values and the range of distances between them): a facet of
        # GGX's lobe mirrors or lets through, the sun's glint is asked for through that lobe, and the sun's ray into the
        # water is bent at a facet so tilted.
        api = self.text(PT + "include/tgpt_api.h")
        self.assertIn("#define TGPT_WATER_SMOOTHNESS 1u", api)
        self.assertIn("float smoothnessFade[2];", api)
        self.assertIn("static_assert(sizeof(TGPT_WaterCaustics)==288", api)
        self.assertIn("const bool smooth=(d.flags&TGPT_WATER_SMOOTHNESS)!=0;", tracer)
        self.assertIn("float water_smoothness(uint slot, float3 position) {", surface)
        self.assertIn("const float3 mirrored = ggx_sample(facing, view, alpha, rng.next2());", path)
        self.assertIn("const float lobe = fresnel_dielectric(dot(view, h), nt / ni) * ggx_d(saturate(dot(facing, h)), a2) * smith_g1(nl, a2) * smith_g1(nv, a2) / (4.0 * nv);", path)
        self.assertIn("delta = true; glinted = !transmitted;", path)
        self.assertIn("normalize(l + ggx_sample(normal, l, alpha, tilt))", sun)
        self.assertIn("const float3 facet = sun_facet(normal, q.l, q.alpha, q.tiltAt);", sun)  # (a tile's corner: ggx_sample's own half vector, the same facet)
        caustics_plugin = self.text("src/TGPathTracing/WaterCaustics.cs")
        self.assertIn("d.SmoothnessNear = s.startSmoothness; d.SmoothnessFar = s.endSmoothness;", caustics_plugin)
        self.assertIn("internal float SmoothnessFadeStart, SmoothnessFadeDistance;", self.text("src/TGPathTracing/NativeTracer.cs"))
        # 3: our own foam: where it forms is the simulation's squeeze past the surface's threshold;
        # how much and what it looks like is written from what the game's foam texture holds and from
        # how foam behaves, not from Unity's shader. The lines that were a port of Unity's foam
        # erosion are gone.
        for old in ("eroded", "pow(age, 32.0)", "smoothstep(0.1, 0.9", "lerp(5.0, 0.1, life)", "0.225 * (sample.a + sample.r)", "frac(phase + 0.5)"):
            self.assertNotIn(old, foam, old)
        self.assertIn("static const float FOAM_ONSET = 0.45;", foam)
        # Water-10: shore state is shared by all rays, and a disabled trial keeps the prior crest value exactly.
        self.assertIn("q.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_SOLID, ray);", foam)
        self.assertIn("ray.TMax = 1e30", foam)
        self.assertIn("g_cameraRays = false; g_primaryRays = false;", foam)
        self.assertIn("g_raySeed = seed; g_rayOrigin = rayOrigin;", foam)
        self.assertIn("g_coneWidth = width; g_coneSpread = spread; g_candidateT = candidate;", foam)
        self.assertIn("water_shore_foam_on() ? 1.0 - (1.0 - crestCover) * (1.0 - shoreCover) : crestCover", foam)
        lights10 = self.text(shaders + "pt_water_lights.hlsli")
        self.assertIn("pdf = 0.05 / (4.0 * PI);", lights10)  # every connection has support, even a root the aim missed
        self.assertIn("lists_candidate(lists, i, choice, invPdf)", lights10)
        self.assertNotIn("g.viewProj", lights10)
        self.assertNotIn("g.caustics", lights10)
        self.assertIn("interface_radiance(boundary)", lights10)
        self.assertIn("-cosine * through * tint", lights10)
        self.assertIn("-direction, m.through, m.tint, rng)", path)
        self.assertIn("lampsAsked && (crossings == 0u || (crossings == 1u && crossed))", path)
        settings10 = self.text("src/TGPathTracing/Settings.cs")
        for key10 in ("Water shore foam", "Water body lamps", "Water light without view"):
            self.assertIn('file.Bind(Advanced, "' + key10 + '", false, Hidden(', settings10)

        self.assertIn("const float crestCover = saturate((past - FOAM_ONSET) / (1.0 - FOAM_ONSET)) * saturate(settings.y) * saturate(mask);", foam)
        self.assertIn("const float3 kept = pow(here, float3(0.6, 0.35, 0.05));", foam)
        self.assertEqual(foam.count("SampleLevel(s_linearRepeat"), 1)  # one read of the foam texture: no second, half-offset one
        self.assertIn("private static void LogSqueeze(string name, Texture gradient, Waves v, int bands)", caustics_plugin)
        for check in ("the sun lights still deep water's body as the closed form for one scattering says (a further 2 % at most from the next ones)",
                      "waves move the sun's light about inside the water and take none: the wavy sea's body is as bright as still water's",
                      "the sea's colour in 16 frames is its colour in 256: no rare bright sample carries it",
                      "no sample in the water is far over the picture's mean: no path carries the whole sun",
                      "with the Caustics row off the sun lights the body by its straight ray, and no path carries the whole sun",
                      "still water's image of the sun carries Fresnel's share of the sun's light",
                      "the water's own smoothness spreads the sun's glint and keeps its light",
                      "at the game's smoothness the sun's glint is softer and wider than a mirror's",
                      "no sample of the sun's glint is far over its pixel's mean",
                      "the sea's foam is thin threads and lace, not clouds: nearly every foam pixel has open water two pixels away"):
            self.assertIn('"' + check + '"', sea)
        # A corner of the caustics' grid no light reaches traces its two rays on only beside one that is lit (the same
        # light lands; the open sea's level under the whole land cost 12 ms a frame in a forest).
        caustic_pass = self.text(shaders + "pt_caustics.hlsl")
        self.assertIn("gs_lit[index] = any(c.carried > 0.0) ? 1u : 0u;", caustic_pass)
        self.assertIn("static const int2 beside[6] = { int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1), int2(1, 1), int2(-1, -1) };", caustic_pass)
        self.assertNotIn("trace_corner(", caustic_pass)
        # And its shadow ray, which aims at a point inside the light's emitter ball, ends where it enters that ball: the
        # emitter's own body (a lantern's opaque glass, a crystal) is not in its own light's way.
        self.assertIn("tmax = min(tmax, max(along - sqrt(max(l.radius * l.radius - dot(aside, aside), 0.0)) - 0.01, 0.0));", caustic_pass)
        self.assertIn('"a lamp inside its own opaque emitter throws the same light through the water"', sea)

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
        # NRD as its README sets it up (PT-127): RELAX (REBLUR on trial) for the diffuse and the
        # specular light, all the light a surface gives, demodulated by NRD_MaterialFactors, with real
        # hit distances, the shading normal and 2.5D motion. Until then RELAX took the bounced light
        # alone against the triangle's normal with a hit distance of 1 and the tracer's own filters
        # the rest: the gravel came out milky (run 20261011-150230: the region's detail 6.7, as
        # documented 8.8, with none 15.5) and the tavern's wood grain and small shadows blurred.
        nrd = self.text(PT + "src/nrd_denoiser.cpp")
        self.assertIn("const nrd::DenoiserDesc denoiser{kRelax, denoiser_of(kind)};", nrd)
        self.assertIn("nrd::Denoiser denoiser_of(uint32_t kind) { return kind==2 ? nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR : nrd::Denoiser::RELAX_DIFFUSE_SPECULAR; }", nrd)
        self.assertNotIn("RELAX_DIFFUSE_SH", nrd)
        self.assertIn("relax.diffusePrepassBlurRadius=0.f;", nrd)
        self.assertIn("c.motionVectorScale[0]=c.motionVectorScale[1]=c.motionVectorScale[2]=1.f;", nrd)
        self.assertIn("c.accumulationMode=restart_ ? nrd::AccumulationMode::CLEAR_AND_RESTART : f.reset ? nrd::AccumulationMode::RESTART : nrd::AccumulationMode::CONTINUE;", nrd)
        self.assertIn('#include <NRD.h>', nrd)
        prep = self.text(PT + "shaders/pt_nrd_prep.hlsl")
        self.assertIn("const float materialClass = (s.strands || s.hair) ? 1.0 : (psr_on() && all(s.albedo == 0.0) ? 2.0 : 0.0);", prep)
        self.assertIn("normalRoughness[p] = NRD_FrontEnd_PackNormalAndRoughness(s.normal, s.roughness, materialClass);", prep)
        self.assertIn("NRD_MaterialFactors(s.normal, normalize(g.cameraPos.xyz - s.position), saturate(s.albedo), s.f0, s.roughness, diffFactor, specFactor);", prep)
        self.assertIn("float3 light = max((d.rgb + b.rgb * occlusion + caustics) * scale, 0.0) * diffuse_to_nrd(s, diffFactor);", prep)
        self.assertIn("float3 spec = max(r.rgb * scale, 0.0) / specFactor;", prep)
        self.assertIn("float3 back = spec * specFactor * asfloat(pass.b.x);", prep)
        for gone in ("bump_share", "guide_plane", "RELAX_FrontEnd_PackSh", "direction_from_code", "SMOOTH_PLANE"):
            self.assertNotIn(gone, prep, gone)
        self.assertIn("motion[p] = float4(-motion_at(p), motion_depth_change(p, s.position), 0.0);", prep)  # (2.5D: PT-127)
        self.assertIn("diffuse[p] = RELAX_FrontEnd_PackRadianceAndHitDist(light, hitDiffuse, true);", prep)
        # (PT-19, PT-75: RELAX's directional form with the normal map's share divided out and put back, bump_share: gone with
        # the old route on 2026-10-11. The shading normal is NRD's guide now, as its README asks.)
        spatial = self.text(PT + "shaders/pt_gi_spatial.hlsl")
        self.assertNotIn("resultFlat", spatial)
        self.assertIn("diffuse[p] = float4(to_storage(max(result, 0.0)), hitDistance);", spatial)
        # The bounce's hit distance as NRD's README asks for ReSTIR: the sample's through the lobe's lens, else the pixel's
        # own cosine-drawn ray's (pt_gi's alpha).
        self.assertIn("if (!(u < lobe) && ownDistance > 0.0) hitDistance = ownDistance;", spatial)
        self.assertIn("own[p] = float4(to_storage(mean / float(rays)), firstFound ? met.t : HIT_DISTANCE_MISS);", self.text(PT + "shaders/pt_gi.hlsl"))
        # The last frame's filtered direction is NOT fed back for the normal map's share: fed back, the bounced light of bark
        # and leaves swung from dark to white in a frozen world (the forest, 2026-10-08: a trunk's blocks wandered 6.5 levels,
        # 0.06 without). Settled in the game: do not feed it back again.
        self.assertIn("    k.denoise1[1]=kInvalid;", self.text(PT + "src/tracer.cpp"))
        self.assertNotIn("nrdDirectionKept", self.text(PT + "src/tracer.cpp"))
        # PT-75, PT-85: RELAX's firefly suppression is ON. Off, it kept 5 of 100 parts more of a
        # closed room's bounced light, and in the game every scene was sprinkled with dots that
        # linger. Settled in the game: do not turn it off again to gain light; make the rare bright
        # paths less rare in the tracer.
        self.assertIn("relax.enableAntiFirefly=true;", nrd)
        self.assertNotIn("enableAntiFirefly=false", nrd)
        # NRD's units follow the bounced light's own median (a histogram read back), not the brightest light in the scene:
        # with a light 40,000 times brighter far away, NRD's boil went from 0.28 to 0.49 levels and its error from 6.3 to 37.
        self.assertIn("st.nrdMedian=std::exp2(log2Median)*st.nrdSlotScale[index];", self.text(PT + "src/tracer.cpp"))
        self.assertIn("if(restart) st.nrdScale=st.nrdMedian>0.0 ? 2.0*st.nrdMedian : 0.01*st.reference;", self.text(PT + "src/tracer.cpp"))
        self.assertIn("k.settings2[1]=(rrg ? f.options : (f.options&~TGPT_OPTION_RAY_REGENERATION))&~TGPT_OPTION_NRD; k.settings2[2]=", self.text(PT + "src/tracer.cpp"))
        test = self.text("tests/pathtracing_gpu_test.cpp")
        self.assertIn("\"NRD keeps the normal map's shading in the bounced light (under the ceiling)\"", test)
        self.assertIn('"NRD denoises the room the same with a light 40,000 times brighter far away"', test)
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
        # the same footprint; RR guides now preserve actual material reflectance (PT-127).
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
        # RR 18: material reflectance has neither a floor nor emission mixed into it.
        self.assertNotIn("diffuse += 0.05", self.text(PT + "shaders/d3d11.hlsl"))
        self.assertNotIn("stable_mix", self.text(PT + "shaders/d3d11.hlsl"))
        self.assertIn('"micromaps leave every pixel as the alpha test alone makes it"', self.text("tests/pathtracing_gpu_test.cpp"))

    def test_path_tracing_amd_portability(self):
        # From the review for GPUs without NVIDIA's extras (a Radeon RX 7900 XTX): what a driver may lack is checked, not
        # assumed; nothing depends on behaviour the specifications leave undefined.
        tracer = self.text(PT + "src/tracer.cpp")
        # Typed UAV loads: the extra formats (a driver has them together) and R32G32_UINT (on its own), checked at start.
        self.assertIn("if(reason.empty() && !o.TypedUAVLoadAdditionalFormats)", tracer)
        self.assertIn("D3D12_FEATURE_DATA_FORMAT_SUPPORT fs{DXGI_FORMAT_R32G32_UINT};", tracer)
        # The D3D11 passes that read HDRP's color back through a UAV only where the format loads there, as float4.
        self.assertEqual(tracer.count("uavFormat!=DXGI_FORMAT_UNKNOWN && float_format(uavFormat) && typed_uav_load(uavFormat)"), 1)
        # The Ray Reconstruction hit-distance texture and the tracer's guides are released with the others when the size changes.
        # (And the hair layer's own outputs since PT-53.)
        self.assertIn("for(Shared* s:{&st.outputs.a, &st.outputs.b, &st.outputs.c, &st.outputs.g0, &st.outputs.g1, &st.outputs.dm, &st.outputs.mv}) release_shared(*s);", tracer)
        # SVGF's moments in 32-bit floats and its variance carried as the standard deviation (half precision overflowed).
        for name in ("diffuseMoments", "directMoments", "specMoments"):
            self.assertRegex(tracer, r"st\." + name + r"\[i\]=make_texture\(d, heap, w, h, DXGI_FORMAT_R32G32B32A32_FLOAT,")
        self.assertIn("historyOut[p] = float4(color, sqrt(variance));", self.text(PT + "shaders/pt_temporal.hlsl"))
        atrous = self.text(PT + "shaders/pt_atrous.hlsl")
        # PT-85: the pass does NOT keep the light of rare bright samples.
        self.assertIn("sum += c.rgb * w; variance += c.a * c.a * w * w; weights += w;", atrous)
        # PT-75's cost stays for now: against the centre's own noise, and divided by its own weights' sum, a bright, noisy
        # pixel takes its dim neighbours' light and they refuse its own (a candle-lit floor's direct light keeps 0.75 of its
        # mean, a metal's reflections 0.68). The light comes back by making those samples less rare in the tracer, not by
        # this pass. The planes' term still takes both pixels' values.
        self.assertIn("const float sigma = 4.0 * sqrt(max(variance3, 1e-10)) + 1e-4;", atrous)
        self.assertIn("output[p] = float4(sum / weights, sqrt(variance) / weights);", atrous)
        self.assertIn("float wp = exp(-max(abs(dot(s.geoNormal, apart)), abs(dot(sq.geoNormal, apart))) / ((0.02 + 0.005 * (s.distance + sq.distance)) * float(step)));", atrous)
        self.assertNotIn("center.rgb * kept + sum", atrous)
        # PT-80: a reflection is filtered by what it shows. pt_specular leaves each pixel the normal and the reflectance of
        # the surface its ray met; on a mirror a neighbour counts only as far as it shows the same surface (normal, plane,
        # reflectance to within a few hundredths), and the first iteration is not fed back into its history; the history is
        # fetched where the shown point was (pt_temporal). Before, a mirror's picture was filtered as noise on a flat surface
        # (0.09 of the reference's detail in game with NRD, 0.22 in tgpt_gpu_test's mirrorFilters).
        # Never divide a reflection's light by the shown surface's reflectance to filter it while the surface's own lamp
        # highlights are in the same signal: measured, that doubled a mirror's light (docs/agents/filters.md).
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("g_seenNormal = m.normal; g_seenReflectance = mirrored && roughness < SEEN_PICTURE_ROUGHNESS ? 0.0 : max(m.albedo + lobe, 1e-4);", common)
        specular = self.text(PT + "shaders/pt_specular.hlsl")
        self.assertIn("if (traced && found) seenGuide[p] = pack_seen(SEEN_REFLECTED, g_seenNormal, g_seenReflectance);", specular)
        self.assertIn("if (shade) seenGuide[p] = pack_seen(SEEN_THROUGH, g_seenNormal, g_seenReflectance);", specular)
        self.assertIn("seenGuide[p] = pack_seen(SEEN_WATER, 0.0, 0.0);", specular)
        self.assertIn("ws = lerp(1.0, same, picture);", atrous)
        self.assertIn("exp(-SAME_REFLECTANCE * max(differ.r, max(differ.g, differ.b)));", atrous)
        self.assertIn("history[p] = float4(lerp(center.rgb, before.rgb, shown.picture), before.a);", atrous)
        self.assertNotIn("tint", atrous)
        self.assertIn("if (specular && water_rests(shown, s, step)) { output[p] = center; return; }", atrous)
        self.assertIn("if ((shown.kind == SEEN_WATER) != (o.kind == SEEN_WATER) || water_rests(o, sq, step)) continue;", atrous)
        self.assertNotIn("(specular && s.roughness < 0.15 && step > 1)", atrous)
        temporal = self.text(PT + "shaders/pt_temporal.hlsl")
        self.assertIn("if (length(showed - shownAt) > 0.05 + 0.02 * (s.distance + shownDistance)) continue;", temporal)
        self.assertIn("momentsOut[p] = float4(m, len, shownDistance);", temporal)
        self.assertIn("k.material[2]=st.specSeen.uav;", tracer)
        self.assertIn("st.specMoments[prev].uav, 16u, 1u, 1u}, b);", tracer)
        # A retry past a pass-through surface never sets TMin past TMax (nor past what stands still's shorter end).
        common_retry = self.text(PT + "shaders/pt_common.hlsli")
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
        # The row's 0 is "no sky light": the native side takes a 0 as "not set" (1), so the smallest value sent is 1e-6.
        self.assertIn("SkyIntensity = DebugCapture.SkyOff || SceneLights.Interior ? 1e-6f : Mathf.Max(settings.SkyIntensity.Value, 1e-6f)", passes)
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
        self.assertIn("sun[p] = float4(to_storage(sunDiffuse + finite_radiance(localDiffuse)), 1.0);", spatial)
        self.assertNotIn("g.signals.y", spatial) # the bounced light's signal is not written here
        self.assertIn("float3 diffuse = nrdAll ? bounced : bounced + sun[p].rgb + caustics_at(s);", self.text(PT + "shaders/pt_compose.hlsl"))
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
        # by one over its own probability; every light once when there are no more than the candidates; and one from the
        # far lists for the lights the cell does not list (PT-50).
        self.assertIn("uint i = lists_candidate(lists, c, u, invPdf);", di)
        self.assertIn("LightRecord light = load_light(picked);", common)  # (lists_candidate first; a weight-0 candidate skipped, 2026-10-11)
        self.assertIn("const uint i = cell_candidate(s.first, s.count, s.near, c, u, invPdf);", common)
        self.assertIn("float w = target * invPdf;", common)
        self.assertIn("if (count <= n) { invPdf = float(count); return entries[first + (min(uint(u * count), count - 1) + c) % count].x; }", common)
        scene = self.text(PT + "src/scene.cpp")
        # (The cells' lists are light_grid.hpp's since 2026-10-12, PT-99: the same weight, in its builder and in the loop
        # the builder is held against; the entries go up through upload_light_lists.)
        grid = self.text(PT + "src/light_grid.hpp")
        self.assertIn("w.weight.push_back(float(lightPower/std::max(d2, 1.0)));", grid)
        self.assertIn("weights[entry]=float(power/std::max(d2, 1.0));", grid)
        self.assertIn("light_grid::build_cells(shape, corner, lights_.data(), lights_.size(), clusterLights_.data(), clusterLights_.size(), clusterBase,", scene)
        self.assertIn("upload_light_lists(device, list, heap, ring, release, retireValue, barriers);", scene)
        # Rays from the G-buffer's surface pass out through a traced mesh they start inside (a back face close by);
        # paths from traced hits use the plain functions.
        self.assertIn("bool passes_through(uint instanceId, uint geometry, bool frontFace, float t, float nearBack) {", common)
        # The material decides whether a back face is a real hit, not the object: hair made a head's inside stop the sun.
        self.assertIn("return (materials[materialIndices[inst.materialBase + geometry]].flags & MATERIAL_ALPHA_TEST) == 0;", common)
        self.assertNotIn("INSTANCE_ALPHA_TEST", common)
        self.assertIn('"rays starting inside an object with alpha-tested parts pass out through its opaque faces"', self.text("tests/pathtracing_gpu_test.cpp"))
        for name, call in (("pt_gi.hlsl", "trace_from_surface(origin, dir, 0.0, g.params.y, surface_near_back(s), hit)"),
                           ("pt_specular.hlsl", "query_from_surface(origin, l, 0.0, g.params.y, surface_near_back(s), met)"),
                           ("pt_di_spatial.hlsl", "transmittance_from_surface(sunOrigin, l, 0.0, g.params.y, nearBack)"),
                           ("pt_gi_spatial.hlsl", "occluded_from_surface(origin, dir, 0.0, sky ? g.params.y : max(dist - 0.02, 0.0), surface_near_back(s))")):
            self.assertIn(call, self.text(PT + "shaders/" + name), name)
        self.assertIn('"rays starting inside the traced mesh pass out through it"', self.text("tests/pathtracing_gpu_test.cpp"))
        # Shadow-only meshes are stand-ins rays from a drawn surface pass through close by; translucent surfaces
        # (HDRP's transmission) are lit through from behind unless their diffusion profile is thick (skin: HDRP's
        # Regular transmission mode, read per profile and handed over as a bit mask); characters carry their texture
        # coordinates.
        self.assertIn("if (inst.flags & INSTANCE_STAND_IN) return true;", common)
        self.assertIn("#define TGPT_INSTANCE_STAND_IN 4u", self.text(PT + "include/tgpt_api.h"))
        self.assertIn("AddMesh(standIn.sharedMesh, renderer.sharedMaterials, renderer.localToWorldMatrix, Tgpt.InstanceStandIn, renderer.renderingLayerMask)", self.text("src/TGPathTracing/SceneCapture.cs"))
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
        self.assertIn("float nl = surface_cos(s, surface.view, ls.dir);", self.text(PT + "shaders/pt_rtxdi.hlsli"))
        self.assertIn("character.Handle = Session.Tracer.RegisterSkinnedMesh(key, reads.Indices, vertexCount, reads.SubmeshStarts, reads.Uvs);", self.text("src/TGPathTracing/CharacterCapture.cs"))
        for check in ('"a translucent surface (grass, leaves) is lit through from behind"', '"a character\'s cut-outs let light through, texel by texel"',
                      '"a surface whose diffusion profile is thick (skin) is not lit through from behind"',
                      '"rays from a drawn surface pass through a stand-in close by, whichever way it faces"'):
            self.assertIn(check, self.text("tests/pathtracing_gpu_test.cpp"))
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("const uint32_t atrousIterations=denoise ? 5 : 0;", tracer)
        self.assertIn("k.denoise1[0]=denoiseOwnDirect ? st.directOut.uav : st.sunRaw.uav;", tracer)  # (NRD as documented, on trial, takes the direct light: PT-127)
        self.assertIn("dispatch(list, st.atrous.Get(), {st.directTmp.uav, st.directOut.uav, 2u, 0u, st.directHist[cur].uav}, b);", tracer)
        self.assertIn("dispatch(list, st.compose.Get(), {(st.nrdActive ? 2u : 0u), out.c.uav, nrdToStorage, out.g0.uav, out.g1.uav, out.dm.uav, out.mv.uav, fogUav}, b);", tracer)
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
        self.assertIn("params_->Set(mode, unsigned(NVSDK_NGX_RayReconstruction_Hint_Render_Preset_Default));", self.text("native/upscaling/src/rr_upscaler.cpp"))
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
        for token in ["static_assert(sizeof(FrameConstants)==9024", "k.diTemporal.enablePermutationSampling=1;",
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
        # ReSTIR GI's reuse is RTXDI's too: its temporal reuse in pt_gi_reuse (a compute pass of its own in the screen's
        # order right after pt_gi, the ray generation shader with Shader Execution Reordering; since 2026-10-10,
        # docs/agents/rule-2.md), its boiling filter in a compute pass of its own (it synchronizes a thread group), its
        # spatial reuse in pt_gi_spatial, over one reservoir buffer with both frames' arrays.
        gi = self.text(PT + "shaders/pt_gi.hlsl")
        reuse = self.text(PT + "shaders/pt_gi_reuse.hlsl")
        self.assertIn("r = RTXDI_GITemporalResampling(p, surface, rtxdi_motion(p, s), g.rtxdi.w, r, rtxdiRng, g.rtxdiRuntime, g.rtxdiReservoirs, tparams);", reuse)
        self.assertIn("RTXDI_StoreGIReservoir(r, g.rtxdiReservoirs, p, g.rtxdi.z);", reuse)
        self.assertNotIn("RTXDI_GIBoilingFilter", gi)
        self.assertNotIn("RTXDI_GIBoilingFilter", reuse)
        self.assertIn("const float bound = boiling_bound(local, g.giBoiling.boilingFilterStrength, weight);",
                      self.text(PT + "shaders/pt_gi_boil.hlsl"))
        # A GI reservoir that isn't finite is dropped before it's stored; rays a normal map points into the surface count
        # as samples that found no light (mirrored out, their density doubled without their weight saying so).
        self.assertIn("if (!(r.weightSum < 1e30) || !all(r.radiance < 1e30)) r = RTXDI_EmptyGIReservoir();", reuse)
        self.assertIn("const bool firstFound = dot(firstDir, firstBack ? -s.geoNormal : s.geoNormal) > 0.0 &&", gi)
        self.assertIn("found = above && trace_from_surface(", gi)
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
        self.assertEqual(rows.count("boilingFilter, emissiveLights, creviceLight, sky"), 2)
        self.assertIn("boilingFilter.Enabled, emissiveLights.Enabled, creviceLight.Enabled, sky.Value", rows)
        harness = self.text("tests/pathtracing_gpu_test.cpp")
        self.assertIn('"with Crevice Light, ReSTIR\'s bounced light reaches the opening of a dark pocket (reuse counts a pixel only for samples it sees)"', harness)
        self.assertIn('"with Crevice Light, ReSTIR\'s bounced light deep in a dark pocket is neither lost to the boiling filter nor inflated by restarted histories"', harness)

    def test_path_tracing_shadow_terminator(self):
        # Opaque surfaces start shadow rays outside (only a translucent leaf switches to the light's side), the light fades
        # towards the geometric terminator (Chiang et al. 2019) on skin alone, and near the surface the nearest hit decides
        # whether a ray is passed through (with the first hit accepted a nearer real blocker could be skipped). The fade went
        # face by face: on everything else (a jug of 12 to 16 sides, cups, shoulders, cloth folds) it left each face near the
        # shadow line its own flat light, a step at every face's edge (PT-04); their shadow rays start outside anyway.
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("return position + geoNormal * (!translucent || dot(geoNormal, dir) >= 0.0 ? offset : -offset);", common)
        self.assertIn("if (s.translucent) return 1.0;", common)
        self.assertIn("if (!s.skin && (g.settings2.y & OPTION_FACETED) == 0) return 1.0;", common)
        self.assertIn("s.skin = color.a > 0.125 && color.a < 0.5;", common)
        prepare = self.text(PT + "shaders/pt_prepare.hlsl")
        self.assertIn("const bool skin = deferred && !forward && solid_transmission(s, feature);", prepare)
        self.assertIn("mat[p] = uint2(pack_rgba8(float4(color, forward ? 1.0 : skinOnMesh ? 0.4375 : skin ? 0.25 : 0.0)),", prepare)
        self.assertIn("return g * (1.0 + g * (1.0 - g));", common)
        self.assertIn("const float nearEnd = min(tmax, nearBack * 3.0);", common)
        self.assertIn("PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH) f;", common)
        self.assertNotIn("skin_shadow_origin", common)
        # A translucent surface the camera sees starts on the light's side only where its shading normal turns from the light
        # too (PT-04, PT-40): by its flat face alone, the face before the shadow line of closed, alpha-tested wax started
        # inside its own mesh and went black.
        self.assertIn("const bool lightSide = s.translucent && (s.strands || (g.settings2.y & OPTION_FACETED) != 0 || dot(s.normal, dir) < 0.0);", common)
        spatial = self.text(PT + "shaders/pt_di_spatial.hlsl")
        self.assertIn("float3 sunOrigin = shadow_origin(s, l, offset);", spatial)
        self.assertEqual(self.text(PT + "shaders/pt_rtxdi.hlsli").count("float3 origin = shadow_origin(s, ls.position - s.position, offset);"), 2)
        self.assertIn("sunDiffuse = g.sunColor.rgb * nl * terminator_softening(s, l) / PI;", spatial)
        self.assertIn("diffuse = ls.irradiance * nl * terminator_softening(s, ls.dir) / PI;", self.text(PT + "shaders/pt_rtxdi.hlsli"))

    def test_path_tracing_face_on_its_mesh(self):
        # PT-43, PT-44. A character's skin whose own triangle the camera ray finds under HDRP's
        # surface, and an eye, are lit from that triangle (onMesh): their rays leave a millimetre
        # above it and every back face stops them, so the lids, brow and nose shadow them; from
        # ray_offset's 2 cm out (and with back faces within near_back passed) the eye was lit edge to
        # edge. Skin keeps its material's ambient occlusion (GBuffer3's tag, as a leaf's) for its
        # bounced and sky light.
        common = self.text(PT + "shaders/pt_common.hlsli")
        prepare = self.text(PT + "shaders/pt_prepare.hlsl")
        self.assertIn("float surface_offset(Surface s) { return s.onMesh ? 0.001 + s.distance * 0.0002 : ray_offset(s.distance); }", common)
        self.assertIn("float surface_near_back(Surface s) { return s.onMesh ? 0.0 : near_back(s.distance); }", common)
        self.assertIn("float ray_offset(float distanceFromCamera) { return 0.02 + distanceFromCamera * 0.002; }", common)
        self.assertIn("s.onMesh = (s.eye) || (s.skin && color.a > 0.375);", common)
        self.assertIn("if (s.skin) { s.ambientOcclusion = spec.g; s.f0 = srgb_to_linear(spec.rrr); }", common)
        self.assertIn("if (eyeSurface) { rel = hit.position - g.cameraPos.xyz; eye = true; }", prepare)
        self.assertIn("if ((skinInstances[hit.instance].flags & INSTANCE_CHARACTER) != 0) { rel = hit.position - g.cameraPos.xyz; geo = hit.normal; skinOnMesh = true; }", prepare)
        self.assertIn("if (leaf || skinOcclusion) {", prepare)
        self.assertIn(": skin ? float3(linear_to_srgb(saturate(f0)).r, ambientOcclusion, 1.0)", prepare)
        # Every ray from the camera's surface leaves by surface_offset and passes back faces by surface_near_back (pt_hdrp_shadow
        # shades HDRP's own surfaces, which are never on the traced mesh).
        for name in ("pt_gi.hlsl", "pt_gi_spatial.hlsl", "pt_di_spatial.hlsl", "pt_specular.hlsl", "pt_rtxdi.hlsli"):
            text = self.text(PT + "shaders/" + name)
            self.assertNotIn("ray_offset(s.distance)", text, name)
            self.assertNotIn("near_back(s.distance)", text, name)
            self.assertIn("surface_offset(", text, name)
        gpu = self.text("tests/pathtracing_gpu_test.cpp")
        for check in ('"a lid 1.5 cm tall shadows the eye in front of it (its rays leave from just above the eye)"',
                      '"a lid\'s back face shadows the eye too (no back face passed from the traced mesh)"',
                      '"a character\'s skin drawn into the GBuffer is shadowed by a lid 1.5 cm tall (its rays leave from its traced triangle)"',
                      '"skin\'s own ambient occlusion darkens its bounced and sky light"'):
            self.assertIn(check, gpu)

    def test_path_tracing_round_surfaces(self):
        # PT-04. What the tracer draws from its own mesh (see-through glass, props HDRP draws forward,
        # a bottle's far side) is shaded by the mesh's vertex normals (Hit.rounded), turned to face
        # the viewer at the outline; rays still start from the triangle's own.
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("float3 rounded;    // the mesh's own smooth normal there", common)
        self.assertIn("if (mesh.normals != INVALID && (g.settings2.y & OPTION_FACETED) == 0) {", common)
        self.assertIn("const float3 world = mul(float3x3(cross(r1, r2), cross(r2, r0), cross(r0, r1)), local);", common)
        self.assertIn("if (worldLength > 1e-12) h.rounded = (dot(world, h.normal) < 0.0 ? -world : world) / worldLength;", common)
        self.assertIn("return c >= 0.05 ? n : normalize(n + toViewer * (0.05 - c));", common)
        prepare = self.text(PT + "shaders/pt_prepare.hlsl")
        self.assertIn("pack_normal22(hit.normal)", self.text(PT + "shaders/pt_primary.hlsli"))
        self.assertIn("if (!eyeSurface && !strands) normal = facing_normal(hit.rounded, -dir);", prepare)
        self.assertIn("geo = hit.normal; // towards the camera; GBuffer2 holds nothing of its own", prepare)
        # The built-in denoiser stops at an edge only where the smooth normal turns too: by each face's orientation alone it
        # filtered every face of a round pot on its own into one tone (the Direct, Indirect and Specular views' faces).
        atrous = self.text(PT + "shaders/pt_atrous.hlsl")
        self.assertIn("const float edge = (g.settings2.y & OPTION_FACETED) != 0 ? faces : max(faces, pow(saturate(dot(s.normal, sq.normal)), 64.0));", atrous)
        # NRD's guide is the shading normal its README asks for (PT-127; the geometric one was the old route's).
        self.assertIn("NRD_FrontEnd_PackNormalAndRoughness(s.normal, s.roughness,", self.text(PT + "shaders/pt_nrd_prep.hlsl"))
        # The A/B for testing: the hidden "Smooth round surfaces" off shades them by their faces as before.
        self.assertIn("#define TGPT_OPTION_FACETED 33554432u", self.text(PT + "include/tgpt_api.h"))
        self.assertIn("#define OPTION_FACETED 33554432", common)
        self.assertIn("internal const uint OptionFaceted = 33554432;", self.text("src/TGPathTracing/NativeTracer.cs"))
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertIn('SmoothSurfaces = file.Bind(Advanced, "Smooth round surfaces", true, Hidden(', settings)
        self.assertIn("(SmoothSurfaces.Value ? 0u : Tgpt.OptionFaceted) |", settings)
        harness = self.text("tests/pathtracing_gpu_test.cpp")
        for name in ("see-through glass is shaded by its mesh's smooth normals, not by its flat faces",
                     "a prop HDRP draws forward is shaded by its mesh's smooth normals",
                     "a round surface's direct light falls off smoothly to the shadow line, without a step at each face",
                     "skin keeps its fade towards the terminator",
                     "a closed translucent surface (candle wax) is lit to its shadow line, without a black face before it",
                     "the built-in denoiser filters a round thing across its faces"):
            self.assertIn('"' + name + '"', harness)

    def test_path_tracing_detail_tile_pool(self):
        # PT-35, PT-13: the 512x512 detail tiles (see-through glass, the eyes, hair) are a pool. There were 32, a tile for
        # every clone of a hair material: a dozen characters took them all, and 29 of 53 hair materials in one run, jars' and
        # eyes' textures fell back to 64x64 tiles (blocks on the jars, hair off the tracer's hair layer). Now 128 (16 rows of
        # the atlas, 1 MB a tile), shared by hair of the same maps and values, given back when a hair material is gone or
        # colored anew, and hair leaves the last 16 to glass and the eyes.
        runtime = self.text(PT + "src/runtime.hpp")
        self.assertIn("kDetailRows=16, kMaxDetailTiles=kDetailTilesPerRow*kDetailRows;", runtime)
        self.assertIn("constexpr uint32_t kDetailReserve=16;", runtime)
        self.assertIn('static_assert(kAtlasHeight<=16384, "a Direct3D texture is at most 16384 texels tall");', runtime)
        rows = int(re.search(r"kDetailRows=(\d+)", runtime).group(1))
        common = self.text(PT + "shaders/pt_common.hlsli")
        # The shaders' count is the native one (4096 / 512 tiles a row), and the strand rows are two a tile on both sides.
        self.assertIn(f"static const uint DETAIL_TILES = {rows * 8};", common)
        self.assertIn("float4 strandTiles[2 * DETAIL_TILES];", common)
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertEqual(tracer.count("strandTiles[2*kMaxDetailTiles][4]"), 2)
        self.assertIn("if (d >= DETAIL_TILES) return t;", self.text(PT + "shaders/pt_primary.hlsli"))
        self.assertNotIn("d >= 32u", self.text(PT + "shaders/pt_primary.hlsli"))
        # The old water submission and its separate constant buffer no longer exist.
        self.assertNotIn("kWaterConstantsStride", tracer)
        extension = self.text(PT + "src/extension.cpp")
        # One allocator for all three: a tile given back first, hair never the last kDetailReserve.
        self.assertIn("if(held>=kMaxDetailTiles-(hair ? kDetailReserve : 0u)) return -1;", extension)
        self.assertEqual(extension.count("take_detail_tile(e, false)"), 2)   # see-through glass, the eyes
        self.assertEqual(extension.count("take_detail_tile(e, true)"), 2)    # hair: new, and colored anew
        self.assertNotIn("e.detailTiles.size()<kMaxDetailTiles", extension)
        self.assertNotIn("kMaxTiles+e.detailTiles.size()", extension)
        # Hair by what it is made of; colored anew, a tile it shares is not baked over; the last material gives the tile back,
        # with its waiting bake dropped and its strand row cleared before the next hair's is written.
        self.assertIn("const auto shared=e.hairContents.find(content);", extension)
        self.assertIn("} else if(old!=e.hairContents.end() && old->second.users==1) {", extension)
        self.assertIn("    e.detailTilesFree.push_back(use.tile);\n    e.detailTilesGivenBack.push_back(use.tile);", extension.replace("\r\n", "\n"))
        self.assertIn("e.newHairBakes.erase(std::remove_if(e.newHairBakes.begin(), e.newHairBakes.end(), [&](const HairBake& b) { return b.tile==use.tile; }), e.newHairBakes.end());", extension)
        self.assertIn("givenBack.swap(e.detailTilesGivenBack);", tracer)
        self.assertIn("e.detailTilesMade=0; e.detailTilesFree.clear(); e.detailTilesGivenBack.clear(); e.hairContents.clear(); e.hairKeys.clear();", extension)
        self.assertIn("TGPT_EXPORT int32_t TGPT_CALL TGPT_ReleaseHairColor(uint64_t key);", self.text(PT + "include/tgpt_api.h"))
        # The plugin: hair that is gone (its material destroyed, or not traced for 600 frames) gives its tile back when hair or
        # glass finds none; a material whose tile changed follows it.
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        self.assertIn("if (!held && tile >= 0 && tile < Tgpt.FirstDetailTile && ReleaseIdleHairTiles(id))", capture)
        self.assertIn("if (tile < 0 && native != IntPtr.Zero && ReleaseIdleHairTiles(0)) tile = Session.Tracer.RegisterDetailTexture((ulong)(uint)id, native);", capture)
        self.assertIn("if (pair.Key != keep && (pair.Value.Material == null || now - pair.Value.Seen >= HairTileIdleFrames))", capture)
        self.assertIn("for (int variant = 0; variant < 4; variant++) characterMaterials.Remove(unchecked(id * 4 + variant));", capture)
        self.assertIn("if (held && tile >= Tgpt.FirstDetailTile) FollowHairTile(id, tile);", capture)
        self.assertIn("            HairTileSeen(m);", capture)
        # A cleared scene starts the native pool over, so the plugin forgets who held a tile (a hold left over made the
        # material look served, and it never asked gone hair for a tile).
        self.assertIn("detailTiles.Clear(); hairHolds.Clear();", capture)
        # Hair colored anew: its tile first (it may be another one now, put into the traced material), the material read after.
        # Read before, the copy RefreshHairColor writes back for the highlights' tint put the old, shared tile into it again.
        self.assertIn("            if (baked) HairTile(m);\n            var mat = materialList[(int)index];\n", capture.replace("\r\n", "\n"))
        harness = self.text("tests/pathtracing_gpu_test.cpp")
        for name in ("hair materials of the same maps and values share one detail tile",
                     "hair leaves exactly the last 16 detail tiles alone",
                     "the tile a hair material shared keeps its colors when that material is colored anew",
                     "a tile given back is baked with the next hair's colors",
                     "the pool holds well over 32 hair colors (128 detail tiles)",
                     "see-through glass and an eye registered after the hair still get detail tiles",
                     "tiles given back are handed out again",
                     "the layer card's tile lies past the first 32 detail tiles, and its strands' row with it"):
            self.assertIn('"' + name + '"', harness)

    def test_path_tracing_glass_is_a_shell(self):
        # PT-71 supersedes the old HDRP sheet blending: every real interface is traced.
        optical = self.text(PT + "shaders/pt_dielectric.hlsli")
        spec = self.text(PT + "shaders/pt_specular.hlsl")
        self.assertNotIn("SHEETS", spec)
        self.assertNotIn("see_through_shell", spec)
        self.assertNotIn("hdrp_material_at", spec)
        self.assertIn("glassFound = dielectric_follow(seen, seenDir, material, glassWeight, rng, glassDistance, glassLight);", spec)
        self.assertIn("while ((materials[hit.material].flags & MATERIAL_GLASS) != 0)", optical)
        self.assertIn("weight /= survive;", optical)
        # One boundary for glass and water (pt_interface.hlsli): its Fresnel term, its refraction, the radiance's index ratio squared.
        self.assertIn("Interface boundary = dielectric_interface(dir, halfVector, ni, nt);", optical)
        self.assertIn("if (!reflection && solid) weight *= interface_radiance(boundary);", optical)
        self.assertIn("weight *= segment_transmittance(absorption, distance);", optical)
        self.assertIn("r.transmitted = r.reflectance >= 1.0 ? 0.0 : refract(direction, facing, r.eta);", self.text(PT + "shaders/pt_interface.hlsli"))
        self.assertIn("r.reflectance = ni == nt ? 0.0 : fresnel_dielectric(dot(direction, facing), nt / ni);", self.text(PT + "shaders/pt_interface.hlsli"))

    def test_path_tracing_jars_are_camera_surfaces(self):
        primary = self.text(PT + "shaders/pt_primary.hlsli")
        self.assertIn("bool glass = (flags & MATERIAL_GLASS) != 0;", primary)
        self.assertNotIn("roughness = 0.03", primary)
        self.assertIn("glass ? length(hit.position - g.cameraPos.xyz) : distance", primary)
        checks = self.text("tests/pathtracing_glass_checks.inl")
        for name in ("eighty glass interfaces retain the expected light without a pane cap",
                     "glass tint follows Beer-Lambert distance through both panes",
                     "material IOR controls Fresnel reflection and refraction",
                     "glass guides contain its own roughness and zero diffuse albedo",
                     "a jar behind two panes remains visible with its own tint",
                     "a glass rim darkens by the longer traced distance through its wall",
                     "an eye beneath a refracting cornea has the same colour and light in a mirror",
                     "an Unlit forward surface emits its own colour without HDRP shading or a light"):
            self.assertIn(name, checks)
        self.assertIn('"pathtracing_glass_checks.inl"', self.text("tests/pathtracing_gpu_test.cpp"))

    def test_path_tracing_glass_is_what_the_game_draws_see_through(self):
        # PT-73 f (run 20261007-224654: the tavern's bottles black shapes, their colors and labels gone). The game draws those
        # bottles OPAQUE (HDRP/Lit, surface type 0: the glaze, the label and the cork in one texture); the tracer took them for
        # glass by their names alone, and glass has had no color of its own since the glass migration. Glass is only what the
        # game itself draws see-through; an opaque bottle is the opaque, painted surface the game shows.
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        finish = self.text("src/TGPathTracing/GlassFinish.cs")
        self.assertNotIn("if (!unlitSurface && GlassFinish.IsGlass(m))", capture)
        self.assertIn("mat.Flags |= Tgpt.MaterialAlphaTest | Tgpt.MaterialGlass | Tgpt.MaterialSeeThrough;", capture)
        self.assertEqual(capture.count("Tgpt.MaterialGlass"), 1)
        self.assertIn("else if (!unlitSurface) GlassFinish.ExplainOpaque(m, shaderName);", capture)
        # What the game's shader shows of its optics: HDRP Lit reads _Ior, _TransmittanceColor and _ATDistance only with a
        # refraction model on (the jars' _Ior 1.01 was never drawn with; "_AbsorptionDistance" is no property of HDRP's).
        self.assertIn('m.HasProperty("_RefractionModel") && m.GetFloat("_RefractionModel") > 0.5f', finish)
        self.assertIn('if (refracts && m.HasProperty("_TransmittanceColor") && m.HasProperty("_ATDistance"))', finish)
        self.assertNotIn('HasProperty("_AbsorptionDistance")', finish)
        self.assertIn('mat.Ior = refracts && m.HasProperty("_Ior") ? m.GetFloat("_Ior") : IsCornea(m) ? 1.376f : 1.5f;', finish)
        # Paint on see-through glass (PT-35, lost in the glass migration): the game's alpha is the share of a texel its color
        # covers (a jar's label, its lid), a lit surface a ray meets with that chance; a light's ray is stopped by it.
        common = self.text(PT + "shaders/pt_common.hlsli")
        optical = self.text(PT + "shaders/pt_dielectric.hlsli")
        primary = self.text(PT + "shaders/pt_primary.hlsli")
        self.assertIn("return (materials[hit.material].flags & MATERIAL_SEE_THROUGH) != 0 ? see_through_cover(hit).a : 0.0;", common)
        self.assertIn("if (cover > 0.0 && ray_chance(hit.instance, hit.geometry, hit.primitive, GLASS_PAINT_SALT) < cover) return true;", optical)
        self.assertIn("if (cover > 0.0 && ray_chance(hit.instance, hit.geometry, hit.primitive, GLASS_PAINT_SALT) < cover) glass = false;", primary)
        self.assertIn("g_glassDrawn = true;", self.text(PT + "shaders/pt_specular.hlsl"))
        # (PT-76: what the paint leaves of a light's ray at a crossing, glass_open; glass's base color is no filter any more.)
        self.assertIn("if ((m.flags & MATERIAL_SEE_THROUGH) == 0) return 1.0;", common)
        self.assertIn("return 1.0 - saturate(a);", common)
        self.assertNotIn("float3 glass_color(", common)
        self.assertNotIn("lerp(1.0, c, saturate(a))", common)
        # Glass has an inside only where its mesh closes a volume (the whole GPU test's "clear glass casts no shadow" failed on
        # a box without a bottom: entered, never left, its light kept at 1 / index^2). An open sheet is a thin pane.
        scene = self.text(PT + "src/scene.cpp")
        self.assertIn("void mark_closed(MeshData& d) {", scene)
        self.assertEqual(scene.count("mark_closed(d);"), 2)
        self.assertIn("struct GeometryRecordGpu { uint32_t indexStart, indexCount, layers, flags; };", self.text(PT + "src/scene.hpp"))
        self.assertIn("struct GeometryRecord { uint indexStart; uint indexCount; uint layers; uint flags; };", common)
        self.assertIn("const bool solid = firstSolid >= 0 ? firstSolid == 1 : glass_solid(hit.instance, hit.geometry, hit.primitive);", optical)
        self.assertIn("if (!solid) F = 2.0 * F / (1.0 + F);", optical)
        # Through the boundary glass shares with water (pt_interface.hlsli): its Fresnel term, its bent ray, its index ratio squared.
        self.assertIn("float F = boundary.reflectance;", optical)
        self.assertIn("const float3 next = reflection ? reflected : solid ? boundary.transmitted : dir;", optical)
        self.assertIn("if (!reflection && solid) weight *= interface_radiance(boundary);", optical)
        self.assertNotIn("refract(", optical)
        self.assertNotIn("fresnel_dielectric(", optical)
        self.assertIn("if (!glass_solid(inst, q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex())) continue;", optical)
        self.assertIn("if (!glass_solid(v0.instance, v0.geometry, v0.primitive) || !glass_solid(v1.instance, v1.geometry, v1.primitive)) return straight;", common)
        # Hair's card is lit through from behind where any ray meets it, as the camera's own pixel of it is.
        self.assertIn("(m.flags & (MATERIAL_THIN_CLOTH | MATERIAL_STRANDS)) != 0 ? CLOTH_TRANSMISSION : 0.0;", common)
        checks = self.text("tests/pathtracing_glass_checks.inl")
        for name in ("paint on see-through glass covers its share of it: a lit surface there, the glass beside it",
                     "an open sheet of glass is a thin pane: what is behind it stays as bright as behind a pane",
                     "an eye under a cornea's open shell is as bright as the shell lets it be seen",
                     "a lit card of hair sends light on to what is under it, as much as its own lit-through share"):
            self.assertIn(name, checks)

    def test_path_tracing_materials_as_the_games_shaders_show_them(self):
        # PT-64: Read original game textures and material values without reduced-resolution private
        # copies.
        header = self.text(PT + "include/tgpt_api.h")
        common = self.text(PT + "shaders/pt_common.hlsli")
        programs = self.text(PT + "shaders/pt_materials.hlsli")
        plugin = self.text("src/TGPathTracing/MaterialPrograms.cs")
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        # Each shader's program by one number on both sides, its record's rows sent apart from the materials.
        for name, number in (("DETAIL", 1), ("ARCHITECTURE", 2), ("CLIFF", 3), ("GRASS", 4), ("LEAVES", 5), ("TRUNK", 6), ("TERRAIN", 7), ("TRIPLANAR", 8), ("SKIN", 9)):
            self.assertIn("#define TGPT_PROGRAM_%s %du" % (name, number), header, name)
            self.assertIn("#define PROGRAM_%s %du" % (name, number), programs, name)
        self.assertIn("internal const uint Lit = 0, Detail = 1, Architecture = 2, Cliff = 3, Grass = 4, Leaves = 5, Trunk = 6, Terrain = 7, Triplanar = 8, Skin = 9;", plugin)
        self.assertIn("#define TGPT_PROGRAM_COUNT 10u", header)
        self.assertIn('case "TG/Character/RealSkin":', plugin)
        self.assertIn('m.IsKeywordEnabled("BOOLEAN_IS_HEAD_ON")', plugin)
        self.assertIn('"_MNAOMap"', plugin)
        self.assertIn('"_NormalStrenght", "_MaskMap", C(m, "_BaseTint")', plugin)
        self.assertIn("program_sample(p, 7u, h.uv, gx, gy, MAP_WHITE).gba", programs)
        self.assertIn('"pathtracing_skin_material_checks.inl"', self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn("TGPT_EXPORT int32_t TGPT_CALL TGPT_SetMaterialPrograms(uint32_t first, const uint32_t* rows, uint32_t count);", header)
        self.assertIn("uint program; uint programRow; int emissiveTexture; float ior;", common)
        self.assertIn("uint32_t program, programRow;", header)
        self.assertIn("internal uint Program, ProgramRow;", self.text("src/TGPathTracing/NativeTracer.cs"))
        # The shaders by their names, and their properties by the game's own spelling.
        for shader in ("TG/Architecture/KF_Architecture", "TG/Environment/Cliffs/Cliff", "TG/Vegetation/Vegetation_Grass", "TG/Vegetation/Vegetation_Leaves",
                       "TG/Vegetation/Vegetation_Trunk", "TG/Environment/MeshTerrain", "TG/Architecture/TriplanarArchitecture2"):
            self.assertIn('"%s"' % shader, plugin, shader)
        for name in ("_Base_Tint", "_Base_NormalStrenght", "_L0_NormalMapSterngth", "_L2_WSTextureMask", "_HealthyColor", "_DryColor", "_GroundBaseColor", "_TrunkBarkMask",
                     "_SplatResolution", "_HeightTransition", "BaseColorRemapMax", "_DetailMap"):
            self.assertIn(name, plugin, name)
        # A program runs only with every texture its material has open; until then HDRP Lit's way from the material's fields.
        self.assertIn("p.ready = (head.w & ~open) == 0u;", programs)
        self.assertIn("if (!p.ready) return false;", common)
        self.assertIn("if (material_program(h, m, program, st, gx, gy, made)) {", common)
        # What the programs need of a hit and of a mesh: the instance's transform, the footprint's axes, a fourth set of
        # texture coordinates, the vertex colors.
        self.assertIn("float3x4 toWorld;", common)
        self.assertIn("float2 uv2; float2 uv2GradX; float2 uv2GradY;", common)
        self.assertIn("uint32_t uv2Stream, uv2Offset, uv2Format, uv2Dimension;", header)
        self.assertIn("mesh.HasVertexAttribute(VertexAttribute.TexCoord3)", capture)
        self.assertIn("mesh.HasVertexAttribute(VertexAttribute.Color)", capture)
        # The game's noise, hue turn and saturation, as its compiled shaders compute them.
        self.assertIn("v.y ^= 1103515245u; v.x += v.y; v.x *= v.y; v.x ^= v.x >> 5; v.x *= 0x27d4eb2du;", programs)
        self.assertIn("frame.Forward[3] = Shader.GetGlobalFloat(GlobalSaturation);", self.text("src/TGPathTracing/HdrpPasses.cs"))
        # Each material's own values: glass's index of refraction, the mask map's occlusion range, the normal map's scale as
        # Unity applies it (and as Shader Graph's Normal Strength does), a back face in HDRP's Flip mode, the emissive map
        # itself, the game's glowing graphs' own arithmetic.
        self.assertIn("float material_ior(MaterialRecord m) { return m.ior >= 1.0 ? m.ior : GLASS_IOR; }", common)
        self.assertIn("GlassFinish.ReadOptics(m, ref mat);", capture)
        self.assertIn('m.HasProperty("_AORemapMin") && m.HasProperty("_AORemapMax")', capture)
        self.assertIn("const float z = max(1e-16, sqrt(1.0 - saturate(dot(xy, xy))));", common)
        self.assertIn("if ((m.textureFlags & TEXTURE_FLIP_BACK) != 0u && !h.front) tn.xy = -tn.xy;", common)
        self.assertIn("if (has_shared(m, m.emissiveTexture)) {", common)
        self.assertIn('["TG/Character/Kandra_Lit"] = new Glow { Map = "_EmissiveColorMap", Intensity = "_EmissionIntensity" },', capture)
        # The tangent is the mesh's own direction of u whatever the tiling's sign (the game's shaders take the mesh's tangent).
        self.assertNotIn("m.uvST.x < 0.0 ? -1.0 : 1.0", common)
        # The yardstick says how each material differs, the plugin's line with it.
        self.assertIn('" (place "', capture)
        test = self.text("tests/pathtracing_gpu_test.cpp")
        for check in ('"KF_Architecture\'s base normal map is read (its name is not HDRP Lit\'s)"',
                      '"the Cliff\'s layer 1 (the rock up close) takes over from the formation\'s own color toward the camera"',
                      '"grass is its map times its healthy color, toward its dry color by the game\'s noise over the world"',
                      '"a trunk\'s bark is laid by its mask and read by the mesh\'s fourth set of texture coordinates"',
                      '"a terrain\'s layers are laid flat over the world by their tiling and offset, not by the mesh\'s coordinates"',
                      '"HDRP Lit\'s detail map lightens the base color as HDRP\'s Lit does"',
                      '"a normal map\'s scale is applied as Unity applies it (z from the map, then x and y scaled)"',
                      '"the emissive map itself is read at a hit (no atlas tile of it)"',
                      '"small textures are shared as any other (none of these maps is read from an atlas tile)"',
                      '"TriplanarArchitecture2\'s layers are laid over the world, not by the mesh\'s coordinates"'):
            self.assertIn(check, test, check)


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
        self.assertLess(common.index("if ((m.flags & MATERIAL_STRANDS) != 0 && g_cameraRays && !(g_primaryRays && (m.flags & MATERIAL_STRANDS) != 0))"), strands)
        self.assertLess(strands, common.index("return a >= (!originalCutout"))
        self.assertIn("if(m.flags&(TGPT_MATERIAL_CAMERA_ONLY|TGPT_MATERIAL_GLASS|TGPT_MATERIAL_STRANDS|TGPT_MATERIAL_HAIR_LAYER)) return false;", scene)
        self.assertIn("if ((mat.Flags & Tgpt.MaterialAlphaTile) != 0) mat.Flags |= Tgpt.MaterialStrands;", self.text("src/TGPathTracing/SceneCapture.cs"))
        self.assertIn('"hair\'s shadow lets light through between its strands, as much as they leave uncovered"', harness)
        # Light through thin skin is the walk's (pt_walk: a random walk inside the traced mesh comes out on an ear's far side).
        # HDRP's thin-object transmission by its thickness map, which stood in for it while the pixel's surface was HDRP's
        # G-buffer, is gone with its table (PT-58), in the shaders, the tracer, the API and the plugin.
        for gone in ("skin_transmission", "through_skin", "transmitted_unoccluded", "skin_transmittance", "float4 transmission[32];", "THIN_SKIN", "s.transmittance"):
            self.assertNotIn(gone, common + self.text(PT + "shaders/pt_rtxdi.hlsli") + self.text(PT + "shaders/pt_di_spatial.hlsl") + self.text(PT + "shaders/pt_prepare.hlsl"))
        for gone in ("TGPT_SetSkinTransmission(const", "TGPT_SetDiffusionProfiles(const"):
            self.assertNotIn(gone, self.text(PT + "include/tgpt_api.h") + self.text(PT + "src/extension.cpp"))
        for gone in ("SetSkinTransmission", "SetDiffusionProfiles"):
            self.assertNotIn(gone, self.text("src/TGPathTracing/NativeTracer.cs") + self.text("src/TGPathTracing/HdrpPasses.cs") + self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertNotIn("transmission[128]", tracer)
        self.assertIn("const float w = 0.258819; // cos(90 - 15 degrees)", common)
        self.assertNotIn("s.seeThrough", common)
        # Reflection Boost leaves skin as the game made it (a neck mirrored the chin above it in sharp patches).
        self.assertIn("float boost = solid_transmission(s, feature) ? 0.0 : g.cameraPos.w;", self.text(PT + "shaders/pt_prepare.hlsl"))
        # Ray Reconstruction's guides describe skin as the tracer shades it (the thick profiles reach the D3D11 pass too).
        self.assertIn("const float boost = deferred && !skin && !transmits ? cameraDelta.w : 0.0;", self.text(PT + "shaders/d3d11.hlsl"))
        # Nor grass and leaves (PT-03: boosted blades mirrored the sky, white beside dark); the guides say the same.
        self.assertIn("if (feature == 2u || feature == 3u) boost = 0.0;", self.text(PT + "shaders/pt_prepare.hlsl"))
        self.assertIn("const bool transmits = (features & 7u) >= 2u && (features & 7u) <= 3u;", self.text(PT + "shaders/d3d11.hlsl"))
        self.assertIn("c.extras[1]=frame.thickProfiles;", self.text(PT + "src/tracer.cpp"))
        self.assertIn('"Reflection Boost leaves skin\'s reflections as the game made them"', self.text("tests/pathtracing_gpu_test.cpp"))
        # Its mirror share goes to metal: a non-metal only gets glossier. One rule for the deferred
        # surface, the forward one and Ray Reconstruction's guides.
        self.assertIn("f0 = lerp(f0, f0 / max(top, 1e-3), 0.7 * boost * metal);", common)
        self.assertIn("roughness = lerp(roughness, 0.05, saturate(boost * (1.0 + metal)));", common)
        self.assertEqual(self.text(PT + "shaders/pt_prepare.hlsl").count("reflection_boost(roughness, f0, "), 2)
        self.assertNotIn("lerp(f0, 0.6,", self.text(PT + "shaders/pt_prepare.hlsl"))
        self.assertNotIn("lerp(f0, 0.6,", self.text(PT + "shaders/d3d11.hlsl"))
        self.assertIn("f0 = lerp(f0, f0 / max(top, 1e-3), 0.7 * boost * metal);", self.text(PT + "shaders/d3d11.hlsl"))
        self.assertIn('"Reflection Boost makes a metal mirror more, and more than stone"', self.text("tests/pathtracing_gpu_test.cpp"))
        rtxdi = self.text(PT + "shaders/pt_rtxdi.hlsli")
        # A ray to an emissive triangle's point ends just short of it in both, as triangle_shadow_ray's.
        self.assertIn("return ls.emitter ? max(dist - max(1e-3 * dist, 2e-3), 0.0) : max(dist - ls.ownReach - (ls.atEmitter ? 0.0 : offset), 0.0);", rtxdi)
        self.assertEqual(rtxdi.count("light_ray_end(ls, dist, offset)"), 2)
        self.assertNotIn("float tmax = max(dist - 0.05 - offset, 0.0);", rtxdi)
        passes = self.text("src/TGPathTracing/HdrpPasses.cs")
        for field in ['"transmissionTintAndFresnel0"', '"shapeParamAndMaxScatterDist"', '"worldScaleAndFilterRadiusAndThicknessRemap"',
                      ]:
            self.assertIn(field, passes)
        self.assertNotIn("thin skin lit from behind gets the light HDRP", harness)
        self.assertIn('"a thin slab of wax (an ear) passes the light a reference random walk gives for its thickness"', harness)
        # Every forward surface is traced; no HDRP shading-share pass or history remains.
        self.assertFalse((ROOT / PT / "shaders/pt_hdrp_shadow.hlsl").exists())
        self.assertNotIn("hdrpShadow", tracer)
        self.assertNotIn("s.hdrpShaded", common)
        compose = self.text(PT + "shaders/pt_compose.hlsl")
        self.assertNotIn("s.hdrpShaded", compose)
        # Spatial Samples reaches 16 in the tracer as in the row (the row went to 16 while the tracer still stopped at 8).
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("k.diSpatial.numSamples=std::min(f.spatialSamples, 16u);", tracer)
        self.assertIn("k.giSpatial.numSamples=std::min(f.spatialSamples, 16u);", tracer)
        lights = self.text("src/TGPathTracing/SceneLights.cs")
        self.assertIn("if (HdrpShadows(light, hd, (p - camera).magnitude)) n.Flags |= Tgpt.LightHdrpShadows;", lights)
        self.assertIn("frame.SunColor[3] = sun.HdrpShadows && ShadowMaps ? 2f : 1f;", passes)
        settings = self.text("src/TGPathTracing/Settings.cs")
        # No Hair Shadows row: hair and eyes are the tracer's; what HDRP still shades itself always takes the traced shadows.
        self.assertNotIn("HairShadows", settings)
        self.assertNotIn("hairShadows", self.text("src/TGPathTracing/PathTracingRow.cs"))
        # The fill lights the tracer leaves out keep lighting the game's fog (an interior's haze) unless Fill Light Fog is off.
        self.assertIn('FillLightFog = file.Bind(Section, "Fill light fog", true, Row(order++, "Fill Light Fog", null,', settings)
        self.assertIn('fillLightFog = new ToggleOption(IdPrefix + "FillLightFog", "Fill Light Fog", true, false);', self.text("src/TGPathTracing/PathTracingRow.cs"))
        self.assertIn("bool fog = entry.volumetric && (settings == null || settings.FillLightFog.Value);", self.text("src/TGPathTracing/SceneLights.cs"))
        # Characters as far as the traced scene reaches (PT-50: no 100 m cut), their acceleration structures refitted to each
        # frame's pose and rebuilt every fourth.
        characters = self.text("src/TGPathTracing/CharacterCapture.cs")
        self.assertNotIn("MaxDistance", characters)
        self.assertIn("if (!math.all(math.isfinite(sphere))) continue;", characters)
        self.assertIn("private const int MaxRenderers = 256;", characters)
        self.assertIn("#define TGPT_MAX_SKINNED 256u", self.text("native/pathtracing/include/tgpt_api.h"))
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
        # No radiance clamps since PT-50: only light that is not a number is dropped. The clamps' old reference (the sun's,
        # or the brightest light's 25 cm away) is still computed in tracer.cpp; no shader reads it.
        self.assertIn("return luminance(radiance) < 1e30 ? max(radiance, 0.0) : 0.0;", restir)
        self.assertIn("double ref=std::max(sun, lamp), clampRef=std::max(sun, 16.0*lamp);", self.text(PT + "src/tracer.cpp"))
        self.assertNotIn("exposure()", restir)
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
        self.assertNotIn("ShadowDetailPass", passes)
        self.assertNotIn("TGPT_EVENT_SHADOW_DETAIL", self.text(PT + "src/tracer.cpp"))
        self.assertNotIn("ShadowDetail", self.text("src/TGPathTracing/Settings.cs"))
        self.assertIn("private const float Highlight = 0.5f;", passes)
        settings = self.text("src/TGPathTracing/Settings.cs")
        # 35 since 2026-10-08: the room as a whole at the game's own level (23.3 against 23.5), a
        # candle's surroundings 1.3 times the game's (2.8 at 50).
        self.assertIn('DarkAdaptation = file.Bind(Section, "Dark adaptation", 35, Row(order++, "Dark Adaptation", new AcceptableValueRange<int>(0, 100),', settings)
        self.assertIn('var legacy = file.Bind(Advanced, "Auto exposure compensation", 0f, Hidden("Replaced by Exposure."));', settings)
        self.assertIn("float most = Mathf.Clamp(gameEv - settings.ExposureLowest.Value, 0f, 12f);", passes)
        self.assertIn("TracedGain = TracedGain,", passes)
        # The traced light's whole gain, in what the composite shows and what the meter measures alike (until 2026-10-11 its
        # automatic part faded as HDRP's fog, laid over the composite, hid a surface: the fog is the tracer's now).
        self.assertIn("const float exStored0 = ex * reference_radiance() * traced_gain();", self.text(PT + "shaders/pt_compose.hlsl"))
        meter = self.text(PT + "shaders/pt_meter.hlsl")
        self.assertIn("float v = traced_ungained(luminance(light), traced_gain());", meter)
        self.assertNotIn("/ traced_gain();", meter)
        common_text = self.text(PT + "shaders/pt_common.hlsli")
        # PT-73: Highlight hold applies full automatic gain in darkness and none at HIGHLIGHT_KEPT, with unit
        # slope at the threshold. Apply after NRD; metering solves the same curve backwards. Keep the accepted
        # threshold 1 in both shader paths.
        self.assertIn("static const float HIGHLIGHT_KEPT = 1.0;", common_text)
        self.assertIn("if (!(automatic > 1.0)) return 1.0;", common_text)
        self.assertIn("return (1.0 + b * (1.0 - x) * (1.0 - x) / (1.0 + b * x * (1.0 - x))) / automatic;", common_text)
        ungained = "return 2.0 * shown / ((a + 1.0 + sqrt(max((a + 1.0) * (a + 1.0) - 4.0 * a * y, 0.0))) * player);"
        self.assertIn(ungained, common_text)
        self.assertIn("exStored *= highlight_hold(luminance(max(diffuse * s.albedo + specular, 0.0)) * exStored, traced_gain());",
                      self.text(PT + "shaders/pt_compose.hlsl"))
        self.assertNotIn("highlight_hold", self.text(PT + "shaders/pt_nrd_prep.hlsl"))
        self.assertIn('"the traced light\'s gain is all there in the dark, none from the kept brightness up, and keeps the order of brightness"', self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn("const double kept=1.0; // pt_common.hlsli HIGHLIGHT_KEPT", self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn('"the meter reports the traced light without its gain where the gain holds back too"', self.text("tests/pathtracing_gpu_test.cpp"))
        # The log line says whose gain the number is.
        self.assertIn('" where it is dark, less where bright ("', self.text("src/TGPathTracing/HdrpPasses.cs"))
        # PT-37, PT-46: a reflected or refracted ray's way crosses the traced medium (pt_medium traced_fog_on_way; since
        # 2026-10-11, when HDRP's formula, its gain fading and Fog Adaptation went with HDRP's fog).
        for gone in ("traced_gain_through", "traced_gain_at", "fog_gain", "fog_color", "fog_sky", "OPTION_FOG_ADAPTS", "fogged_after"):
            self.assertNotIn(gone, common_text)
        self.assertNotIn("TGPT_FOG", self.text(PT + "shaders/d3d11.hlsl"))
        self.assertNotIn("TGPT_SetFog(", self.text(PT + "include/tgpt_api.h"))
        self.assertNotIn("FogPass", self.text("src/TGPathTracing/HdrpPasses.cs"))
        self.assertNotIn("FogAdaptation", self.text("src/TGPathTracing/Settings.cs"))
        self.assertIn("radiance = fogged(radiance, origin, l, found ? length(hit.position - origin) : HIT_DISTANCE_MISS, rng);", self.text(PT + "shaders/pt_specular.hlsl"))
        self.assertIn("return (g.size.w & FLAG_TRACED_FOG) != 0u ? traced_fog_on_way(radiance, from, dir, dist, rng) : radiance;", common_text)
        self.assertIn("const bool scatters = fog_scatter_point(from, dir, reach, boxReach, through, true, rng, x, weight);", self.text(PT + "shaders/pt_medium.hlsli"))
        self.assertIn("bool volumetric = fog.enableVolumetricFog.value && GamePasses.GameVolumetrics;", passes)
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
        self.assertIn("var c = new Class { placed = named.placed, effect = !named.placed && !named.kept && !named.flame, parent = named.parent, kind = named.kind };", lights)
        # A light kept for a glowing thing (PT-47) is the thing's: HDRP's range, as an effect's.
        self.assertIn("if (at >= 0) { c.placed = false; c.effect = true; c.glow = true;", lights)
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
        # Per-output switches replace the camera override, and restore on Off.
        effects = self.text("src/TGPathTracing/EffectPasses.cs")
        self.assertIn('FindPassTagValue(i, LightMode)', effects)
        self.assertIn('m.SetShaderPassEnabled(Distortion, on ? false : s.DistortionEnabled)', effects)
        self.assertIn('EffectPasses.Hold(on);', passes)
        self.assertNotIn('SuppressDistortion', passes)
        self.assertIn("dispatch(list, st.probe.Get(), {st.probeUav}, b, 1, 1);", self.text(PT + "src/tracer.cpp"))
        self.assertIn("\"the sky the camera sees is measured against the open sky's\"", self.text("tests/pathtracing_gpu_test.cpp"))
        # The sky only: counting the lamps' light the paths found made it rise in a lit room as if the sky were there.
        self.assertIn("if (!trace_closest(g.cameraPos.xyz, dir, 0.3, g.params.y, hit)) seen += open;", self.text(PT + "shaders/pt_probe.hlsl"))
        # Eyes and HDRP's opaque Unlit decals are in the traced scene for camera rays only: other rays pass through them, but
        # a mirror's (PT-46: what the camera there would see; not the hair layer's shells).
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("if ((m.flags & MATERIAL_CAMERA_ONLY) != 0 && !g_cameraRays && !(g_mirrorRay && (m.flags & MATERIAL_HAIR_LAYER) == 0)) return false;", common)
        # its declaration: set by pt_specular alone, for a mirror-smooth surface. And, ON TRIAL (docs/agents/flame-near.md), on
        # and off again round the one ray lamp_around_light traces back to such a pixel, by the flag pt_specular hands it.
        self.assertEqual(common.count("g_mirrorRay = "), 3)
        self.assertIn("g_mirrorRay = mirror;", common)
        self.assertIn("if(m.flags&(TGPT_MATERIAL_CAMERA_ONLY|TGPT_MATERIAL_GLASS|TGPT_MATERIAL_STRANDS|TGPT_MATERIAL_HAIR_LAYER)) return false;", self.text(PT + "src/scene.cpp"))
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        self.assertIn('bool unlitSurface = unlit && !transparent;', capture)
        self.assertIn('bool eye = shaderName.StartsWith("TG/Character/Eye", StringComparison.OrdinalIgnoreCase);', capture)
        self.assertIn('"a surface seen by camera rays only casts no shadow"', self.text("tests/pathtracing_gpu_test.cpp"))
        # Surfaces HDRP draws forward (hair, beards, some props) are found along the camera ray in the traced scene and
        # lit by the tracer; the composite replaces HDRP's shading there with light already multiplied by their color.
        self.assertIn("const bool found = trace_closest(g.cameraPos.xyz, dir, max(dist - window, 0.0), dist + window, hit);", self.text(PT + "shaders/pt_prepare.hlsl"))
        self.assertIn("outB[p] = float4(rr_clamp(max(specular * exStored * fog.a, 0.0)), 1.0);", self.text(PT + "shaders/pt_compose.hlsl"))
        d3d11 = self.text(PT + "shaders/d3d11.hlsl")
        # (Since PT-84 the composite takes such a texel whole, as B's alpha marks it: test_path_tracing_edges_carry_the_shift.)
        self.assertIn("if (tb.a > 0.5) return float4(max(ta.rgb + tb.rgb, 0.0), 0.0);", d3d11)
        self.assertIn('"a surface HDRP draws forward is lit by the tracer like a deferred one"', self.text("tests/pathtracing_gpu_test.cpp"))
        # The traced level of detail is the drawn one (PT-61; until 2026-10-07 the finest within 30 m whatever the game drew),
        # and a LOD with a registered mesh stands in until the drawn one's is (test_path_tracing_world_is_what_the_game_draws).
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        self.assertNotIn("NearDetail", capture)
        self.assertEqual(capture.count("int standing = WorldRules.TracedLevels(levels, drawnLevels, nearLevels,"), 2)
        self.assertEqual(capture.count("if (!follow) lodStandIns += standing;"), 2)
        # params.x is the traced light's own gain (the only exposure-like factor, applied in compose), never HDRP's exposure.
        self.assertIn("k.params[0]=std::isfinite(f.tracedGain) && f.tracedGain>0.f ? std::clamp(f.tracedGain, 1.f/64.f, 4096.f) : 1.f;", tracer)
        self.assertIn("const float exStored0 = ex * reference_radiance() * traced_gain();", self.text(PT + "shaders/pt_compose.hlsl"))
        self.assertIn("history *= rescale;", self.text(PT + "shaders/pt_temporal.hlsl"))
        for name, write in (("pt_gi_spatial.hlsl", "diffuse[p] = float4(to_storage(max(result, 0.0)), hitDistance);"),
                            ("pt_di_spatial.hlsl", "sun[p] = float4(to_storage(sunDiffuse + finite_radiance(localDiffuse)), 1.0);"),
                            ("pt_specular.hlsl", "finite_radiance(weight * radiance)")):
            self.assertIn(write, self.text(PT + "shaders/" + name), name)
        passes = self.text("src/TGPathTracing/HdrpPasses.cs")
        self.assertNotIn("ClampExposure", passes)
        self.assertIn("if (pipeline != null) TrackExposure(pipeline, hd);", passes)
        self.assertIn('"the traced light doesn\'t depend on HDRP\'s exposure"', self.text("tests/pathtracing_gpu_test.cpp"))
        d3d11 = self.text(PT + "shaders/d3d11.hlsl")
        # The composite shows the frame's own traced light (PT-84), pre-exposed with the very exposure HDRP's color of that
        # frame has: no ratio between two frames' exposures, and no light of another frame's texel for a pixel without one
        # (until 2026-10-08: the frame before's light, brought to this frame's exposure, and for a pixel no texel matched
        # one nearby, picked at random).
        self.assertNotIn("exposureTraced", d3d11)
        self.assertNotIn("en / et", d3d11)
        self.assertNotIn("st.inputs[prev.parity]", self.text(PT + "src/tracer.cpp"))
        self.assertNotIn("closest = error; best = n;", d3d11)
        self.assertNotIn("frame.frameIndex&0xFFFFu", self.text(PT + "src/tracer.cpp"))
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
        # PT-47: a light the names leave out is kept where a glowing thing no flame's or lamp's light
        # claims stands at it (LightRules, checked without the game by tests/light_rules_tests.cs),
        # with HDRP's range, and every left-out and kept-for-a-glow name is logged with what is at it.
        self.assertIn("int at = LightRules.GlowAt(p.x, p.y, p.z, things, out float distance, out _);", lights)
        self.assertIn("if ((p - camera).sqrMagnitude > r2 || ByNames(light).placed || !LightsWorld(light.GetComponent<HDAdditionalLightData>(), lightLayers)) continue;\n                LightRules.Claim(p.x, p.y, p.z, things);", lights.replace("\r\n", "\n"))
        self.assertIn("internal static bool IsEffectLight(Light light) => Classify(light).effect;", lights)
        self.assertIn("\"\\\" left out (placed to fake light: no flame, lamp or effect in its names, \" + WhatIsAt(light)", lights)
        rules = self.text("tests/LightRules.Tests.csproj")
        self.assertIn('<Compile Include="..\\src\\TGPathTracing\\LightRules.cs" Link="LightRules.cs" />', rules)
        self.assertIn("LightRules.Tests", self.text("docs/TESTING.md"))
        # Each traced light is the size of what emits it, measured from the emitting thing at it or
        # its kind's size; HDRP's radius (an artist's value for soft shadows) only for a mod's own
        # light.
        self.assertNotIn("Mathf.Clamp(radius, 0.02f, fire ? 1f : 0.3f)", lights)
        self.assertIn("n.Radius = size.radius;", lights)
        self.assertIn("if (at < 0) s.radius = LightRules.DefaultRadius(kind);", lights)
        self.assertIn("s.radius = LightRules.MeasuredRadius(kind, t.HalfX, t.HalfY, t.HalfZ); s.measured = true;", lights)
        # Lights round 2 (the tavern's candles white-hot): a flame's or a lamp's light is traced at its measured flame's centre,
        # not where HDRP has it (a candle's in its wax 10 cm under the flame), and its shadow rays are tested up to its own size
        # and a centimetre there (TGPT_LIGHT_AT_EMITTER), not the 5 cm round a light inside its lamp's mesh: the wax's top then
        # shadows the wax's sides (tests/pathtracing_gpu_test.cpp 2b). HDRP's place and radius only with the hidden key off.
        self.assertIn("if (LightRules.FlameCentre(kind, t)) { s.atEmitter = true; s.local = light.transform.InverseTransformPoint(new Vector3(t.X, t.Y, t.Z)); }", lights)
        self.assertIn("var at = size.atEmitter ? t.TransformPoint(size.local) : p;", lights)
        self.assertIn("n.PositionX = at.x; n.PositionY = at.y; n.PositionZ = at.z;", lights)
        self.assertIn("if (size.atEmitter) n.Flags |= Tgpt.LightAtEmitter;", lights)
        self.assertIn("if (!asEmitters) s.radius = LightRules.HdrpRadius(", lights)
        self.assertIn('LightsAsEmitters = file.Bind(Advanced, "Lights sized as their emitters", true, Hidden(', self.text("src/TGPathTracing/Settings.cs"))
        self.assertIn("internal const uint LightAtEmitter = 0x40000;", self.text("src/TGPathTracing/NativeTracer.cs"))
        self.assertIn("#define TGPT_LIGHT_AT_EMITTER 0x40000u", self.text(PT + "include/tgpt_api.h"))
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("#define LIGHT_AT_EMITTER 0x40000u // TGPT_LIGHT_AT_EMITTER", common)
        self.assertIn("float light_own_reach(LightRecord l) { return (l.flags & LIGHT_AT_EMITTER) != 0u ? min(l.radius + 0.01, 0.05) : 0.05; }", common)
        self.assertIn("tmax = max(dist - light_own_reach(light), 0.0); // the lamp's own mesh around the light does not block it", common)
        self.assertIn("r.ownReach = light_own_reach(l);", self.text(PT + "shaders/pt_rtxdi.hlsli"))
        # And a surface lit through from behind passes back faces on the way to a lamp within a thin thing's 1 cm only, not
        # near_back's 15 cm: a candle's wax is no leaf.
        self.assertIn("return lightSide ? THIN_THROUGH : near_back(s.distance);", common)
        self.assertEqual(self.text(PT + "shaders/pt_rtxdi.hlsli").count("light_ray_end(ls, dist, offset), near_back_towards(s, v / dist));"), 2)
        self.assertIn("\"a candle's light at its flame no longer lights its own wax from inside: the wax's top shadows its sides\"", self.text("tests/pathtracing_gpu_test.cpp"))
        # A light kept by a flame's or a lamp's name needs its flame, lamp or glowing thing at it,
        # else it is a fill light, logged.
        self.assertIn("if (LightRules.EmitterAt(p.x, p.y, p.z, things, out _) >= 0) emitterSeen[id] = named.parent;", lights)
        self.assertIn("else { c.placed = true; c.noEmitter = true; }", lights)
        self.assertIn("\"\\\" left out (\" + NoEmitterText(light)", lights)
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
        # Except hair well in front of HDRP's depth at a forward pixel (R4): a card HDRP cut away there (lashes over the eye).
        # One line since the hair layer (2026-09-28): its block's own `if (g_candidateT < g_strandsFrom) return false;` became
        # the `g_candidateT >= g_strandsFrom` of its return, the same rule for strands (what HDRP shades still stops the ray).
        self.assertIn("if ((m.flags & MATERIAL_STRANDS) != 0 && g_cameraRays && !(g_primaryRays && (m.flags & MATERIAL_STRANDS) != 0))", self.text(PT + "shaders/pt_common.hlsli"))
        # And, R4, by its own texel's alpha at the forward pixel (HDRP drew whole cards held opaque: the game has no alpha-cut
        # build of its hair and brows drawn so); what lies behind a card it cut through is the pixel's, the tracer's. Since
        # 2026-09-28 (with the hair layer) by how much of the pixel the card covers (strand_coverage, 3x3 samples of its
        # footprint): below half, what the ray meets behind it shows (behindShown), blended with the card's color; it replaced
        # the cut at a single texel (STRANDS_CAMERA_CUT, g_strandsPassed, the ray traced on from HDRP's depth).
        prepare = self.text(PT + "shaders/pt_prepare.hlsl")
        self.assertIn("const float coverage = cm.albedoTile >= 0 ? strand_coverage(cm, hit.uv, dpdu, dpdv, frame, pixel) : 1.0;", prepare)
        self.assertIn("if (coverage < 0.999 && trace_closest(g.cameraPos.xyz, dir, tCard + max(1e-4, 2e-5 * tCard), g.params.y, behind) &&", prepare)
        self.assertIn("if (coverage < 0.5) {\n                        hit = behind; behindShown = true;", prepare.replace("\r\n", "\n"))
        self.assertIn("normal = (eyeSurface || (strands && !behindShown)) && dot(normal, hit.normal) > 0.5 ? normal : hit.normal;", prepare)
        self.assertIn('"hair HDRP drew as a whole card is cut by its own alpha, the surface behind shows through"', self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn("        g_candidateT = q.CandidateTriangleRayT();", self.text(PT + "shaders/pt_common.hlsli"))
        self.assertLess(prepare.index("g_strandsFrom = dist - (0.003 + 0.001 * dist);"), prepare.index("bool found = trace_closest(g.cameraPos.xyz, dir, max(dist - window, 0.0), dist + window, hit);"))
        # The rule ends with that ray: the rays looking behind a card take every strand they meet.
        self.assertLess(prepare.index("bool found = trace_closest(g.cameraPos.xyz, dir, max(dist - window, 0.0), dist + window, hit);"), prepare.index("g_strandsFrom = -1e30;"))
        self.assertLess(prepare.index("g_strandsFrom = -1e30;"), prepare.index("Hit behind;"))
        self.assertLess(prepare.index("Hit behind;"), prepare.index("g_cameraRays = false;"))
        # Hair drawn as a layer (pt_hair) takes its alpha as RealHair does (2026-10-06, read from its compiled shader): remapped
        # by _AlphaPower and _AlphaRemap, and the whole pixel where that is at least the depth prepass. With the texture's alpha
        # alone, each texel let the scalp in the hair's shade show through: black gaps between flat clumps (PT-01).
        # (Since 2026-10-07 the pixel's own camera ray stops at such a card with that chance, pt_primary: no layer.)
        hair = self.text(PT + "shaders/pt_primary.hlsli")
        self.assertIn("float strand_cover(StrandTile t, float a) { return t.prepass > 0.0 && a >= t.prepass ? 1.0 : a; }", hair)
        self.assertIn("if (!(a > 0.0) || a < t.clip) continue;", hair)
        self.assertIn("const float c = strand_cover(t, a);", hair)
        self.assertIn("return hair_game_alpha(m, m.baseColor.a * a / float(taps));", hair)
        # Read anisotropically, as the game's quality setting forces on every texture (2026-10-07, PT-01): at the geometric mean of
        # the footprint's sides the strands of a card turned from the camera ran together into an even veil.
        self.assertIn("static const uint STRAND_ANISO = 9;", hair)
        self.assertIn("f.lod = log2(max(max(major / float(f.taps), minor), 1e-6)) + asfloat(g.hair.w);", hair)
        # No fixed count of cards changes what the picture converges to: past the cards lit one by one
        # the ray goes on and stops at a card by chance, as its cover gives. (Every card so now: none
        # is lit "one by one" first, there is no count at all.)
        self.assertNotIn("HAIR_LAYERS", hair)
        self.assertIn("if (ray_chance(inst, geo, prim, PRIMARY_SALT) < c) { cover = c; hair = true; q.CommitNonOpaqueTriangleHit(); }", hair)
        # Each strand's light by the game's normal map (RealHair: Lambert about its normal-mapped normal; the highlights shifted
        # about it), as a lean on the tracer's strand light that averages to 1.
        self.assertIn("return sqrt(saturate(1.0 - tl * tl)) * hair_bump(s.normal, nv, l);", self.text(PT + "shaders/pt_common.hlsli"))
        self.assertIn("r.normal = dot(tu, tu) > 0.0 ? hair_normal(m, r.hit, tu, b, nv) : nv;", hair)
        for check in ["\"a thick stack of hair cards over a bright background stays as opaque as the game's blend (no veil past the eighth card)\"",
                      "\"strands at a slant stay apart, read anisotropically as the game reads them (no even veil)\"",
                      "\"hair's strands take the light by the game's normal map: those tilted toward a lamp lighter\""]:
            self.assertIn(check, self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn("if (baked >= Tgpt.FirstDetailTile) { mat.Flags |= Tgpt.MaterialHairLayer; mat.HairAlpha = HairAlpha(m); }", self.text("src/TGPathTracing/SceneCapture.cs"))
        # And hidden from HDRP by Kandra's culling where its renderer holds nothing else (a head of hair, a beard, with their
        # scalp and beard masks): with every pass of their materials off and the cut past 1, HDRP still drew Ron's hair and
        # beard (2026-10-06, round 2). The game's mask comes back when tracing stops, only into the slot it was taken from.
        capture = self.text("src/TGPathTracing/CharacterCapture.cs")
        # (From 2026-10-07 to 10-11 HDRP drew such hair's depth, not its colour (HoldLayerDepth). Since PT-112 (A) HDRP draws
        # no opaque object while tracing and the tracer writes the frame's depth: hair is hidden from HDRP as brows are. What
        # gives a hidden renderer back stays for a session that hid one.)
        self.assertIn("                                HideLayerHair(c.renderer, materials[s]);", capture)
        self.assertNotIn("HoldLayerDepth(c.renderer", capture)
        self.assertIn("if (!on) { RestoreLayerHair(true); RestoreRenderers(true); RestoreCuts(); }", capture)
        self.assertIn("if (KandraRendererManager.USlot(r.RenderingId) != hold.Slot || hold.Slot >= culling.layerMasks.Length) continue;", capture)
        self.assertIn("if (culling.layerMasks[hold.Slot] == 0u) culling.layerMasks[hold.Slot] = hold.Original;", capture)
        # Round 3. The brows sit on the head's renderer, which cannot be taken out of the culling: their material's three cuts
        # are raised past 1 (the depth passes cut by _DepthPrepass, not by _AlphaClip: with the clip alone HDRP wrote the
        # brows' depth and nothing lit them, thick black bars), given back exactly, and the tracer keeps the game's values.
        self.assertIn('private static readonly string[] LayerCutProperties = { "_AlphaClip", "_DepthPrepass", "_DepthPostpass" };', capture)
        self.assertIn("for (int c = 0; c < LayerCutProperties.Length; ++c) if (!float.IsNaN(taken[c])) m.SetFloat(LayerCutProperties[c], taken[c]);", capture)
        scene = self.text("src/TGPathTracing/SceneCapture.cs")
        self.assertIn('Mathf.Clamp01(CharacterCapture.GameCut(m, "_DepthPrepass", 0.5f)) : 0f;', scene)
        # The scalp and beard masks are the tracer's too, the hair layer's lowest layer (the hide took HDRP's away with the
        # hair's renderer: a crown was strands over bare skin): seen by that layer alone, taken a little past HDRP's surface
        # (a mask lies on the skin), behind as many cards as a crown stacks.
        self.assertIn("Flags = Tgpt.MaterialAlphaTest | Tgpt.MaterialCameraOnly | Tgpt.MaterialHairLayer });", scene)
        self.assertIn("leftOut[s] = SceneCapture.CharacterLeftOut(materials[s]) && !SceneCapture.LayerShell(materials[s]);", capture)
        # (Since 2026-10-07 the hair's renderer is drawn again, and HDRP draws the mask into its G-buffer as it draws skin; the
        # ray that looks for hair alone passes it, the tracer's own ray for every pixel takes it by its own alpha test.)
        self.assertIn("if (!IsShell(materials[s]) && SceneCapture.IsHairLayer(materials[s], character.Textured))", capture)
        self.assertIn("if ((m.flags & MATERIAL_CAMERA_ONLY) != 0) continue;", hair)
        self.assertIn("return a >= m.alphaCutoff && ray_chance(inst, geo, prim, PRIMARY_SALT) < a;", hair)
        for check in ['"a new draw every frame: a partly covered pixel is now the hair, now what lies behind it"',
                      '"the mean of the frames is the game\'s blend of the five cards over the wall"',
                      '"hair covering the pixel hides the GBuffer3 of the surface behind it (no pale veil over a crown)"']:
            self.assertIn(check, self.text("tests/pathtracing_gpu_test.cpp"))
        # Round 4. Brows the tracer draws as a layer stay transparent while tracing: held opaque, HDRP drew their whole cards
        # into its depth before the tracer took it (RealHair has no opaque build that cuts them), black bars. Transparent, HDRP
        # draws them after the capture, through the builds that cut. Traced either way (not left out while transparent).
        self.assertIn("if (SceneCapture.LayerBrow(m)) continue; // Brows, once the frame's materials are read", capture)
        self.assertIn("Brows(c.renderer, materials, n, character.Textured);", capture)
        self.assertIn("if (m == null || LayerBrow(m) || GlassFinish.IsCornea(m)) return false;", scene)
        self.assertIn("bool transparent = !LayerBrow(m) && (", scene)
        # And the hair covers the GBuffer3 of the surface behind it as it covers its light (a crown's pale veil: the skin's
        # GBuffer3 added whole under hair covering 98 % of the pixel). Since PT-53 the composite lays the hair over the whole
        # of the surface's light, its GBuffer3 included.
        composite = self.text("native/pathtracing/shaders/d3d11.hlsl")
        # (Since 2026-10-07 hair is a surface of the picture: what the tracer's own ray stopped at covers the GBuffer3 of the
        # G-buffer's surface behind it by its share of the pixel's light.)
        # (Since 2026-10-08, PT-84, each pixel takes its own texel of the frame's own light: one the tracer's own ray made is
        # the pixel's whole light, its emission in it; HDRP's GBuffer3 is added only on the route before, to a surface of the
        # G-buffer.)
        self.assertIn("if (tb.a > 0.5) return float4(max(ta.rgb + tb.rgb, 0.0), 0.0);", composite)
        self.assertIn("return float4(max(gbuffer3_emission(p) + decode(a.rgb) * ta.rgb + tb.rgb, 0.0), 0.0);", composite)
        self.assertNotIn("ownShare", composite)
        # A character's forward pixels no longer stay HDRP's whatever the camera ray meets (the rule: nothing keeps HDRP's
        # shading while tracing); characters' instances stay marked.
        # Until the tracer has it, what HDRP draws forward on a character that the traced scene lacks (far hair) stays HDRP's.
        self.assertIn("\"a character's face drawn forward is shaded by the tracer, not left to HDRP: an eye and a plain material alike\"", self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn("(alpha ? TGPT_INSTANCE_ALPHA_TEST : 0u)|TGPT_INSTANCE_CHARACTER", self.text(PT + "src/scene.cpp"))
        # Glass (by name): shadow rays pass through by chance as bright as its color, micromaps skip it (they would make
        # its texels opaque to every ray), and while tracing it has a glass's finish (the game's values come back after).
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("if ((m.flags & MATERIAL_GLASS) != 0) {", common)
        # every traversal: shadow, sky and bounced light, the tinted shadow rays and the caustics' segments
        self.assertEqual(common.count("ray_seed(origin, dir);"), 7)
        self.assertIn("if(m.flags&(TGPT_MATERIAL_CAMERA_ONLY|TGPT_MATERIAL_GLASS|TGPT_MATERIAL_STRANDS|TGPT_MATERIAL_HAIR_LAYER)) return false;", self.text(PT + "src/scene.cpp"))
        # The game's materials are left as they are: the tracer gives glossy glass texels a mirror finish itself.
        self.assertNotIn("GlassFinish.Set(", self.text("src/TGPathTracing/HdrpPasses.cs"))
        # Light inside subsurface materials (PT-40, PT-43): a random walk inside the traced mesh by
        # HDRP's diffusion profiles (its own conversion to a medium), lit where it comes out, for the
        # camera's pixels (pt_walk) and where paths and reflections meet such a material
        # (subsurface_vertex). HDRP's screen-space scattering (pt_sss) is gone: no blur of the light
        # on screen.
        self.assertFalse((ROOT / (PT + "shaders/pt_sss.hlsl")).exists())
        self.assertNotIn("pt_sss", self.text("CMakeLists.txt"))
        walk = self.text(PT + "shaders/pt_walk.hlsl")
        self.assertIn("if (subsurface_walk(entry, s.albedo, shape, rng, e)) {", walk)
        self.assertIn("const float3 gain = e.throughput / max(s.albedo, 1e-3);", walk)
        self.assertIn("keeps = float3(walk_albedo(a.r), walk_albedo(a.g), walk_albedo(a.b));", common)
        self.assertNotIn("WALK_WATCHDOG", common)
        self.assertIn("sigmaT = shape / (1.9 - a + 3.5 * (a - 0.8) * (a - 0.8));", common)
        # No walk is cut short: it leaves the mesh or dies by weighted roulette.
        self.assertIn("if (step >= WALK_LATE && (step & 127u) == 127u) { if (rng.next() >= 0.5) break; throughput *= 2.0; }", common)
        self.assertIn("if (!(q > 0.0) || rng.next() >= q) break;", common)
        # its definition, the cache's sample, a path's vertex, a reflection's; and, ON TRIAL (docs/agents/flame-near.md), the
        # point a lamp's own ray lands on (lamp_around_light: the same walk, from the lit side)
        self.assertEqual(common.count("subsurface_vertex("), 5)
        self.assertIn("if(walkOwn && st.walk && !(f.options&TGPT_OPTION_NO_SUBSURFACE)) dispatch(list, st.walk.Get(), {}, b);", self.text(PT + "src/tracer.cpp"))
        # The walk starts at the camera ray's own hit (PT-59, PT-58), told by the material it
        # met as a path's vertex is (subsurface_material). HDRP's G-buffer is not asked, and its route to the walk (HDRP's
        # subsurface features, a profile by G-buffer index, a mesh looked for under HDRP's surface) is gone: the material is the one
        # the pixel's ray stopped at (own_material), the pass runs while the scene holds such a material, and the exit's normal
        # map is the tracer's own record's.
        self.assertIn("if (!walk_own(p, s, pixel, entry, shape)) return;", walk)
        for gone in ("walk_pixel", "walk_entry", "inputs0", "inputs1", "gb2", "decode_hdrp_normal", "sssProfiles"):
            self.assertNotIn(gone, walk)
        own = walk.split("bool walk_own(uint2 p, Surface s, float pixel, out Hit entry, out float3 shape) {", 1)[1].split("\n}", 1)[0]
        self.assertIn("if (!own_material(p, material) || !subsurface_material(material, shape)) return false;", own)
        self.assertIn("entry.position = s.position; entry.normal = s.geoNormal;", own)
        self.assertIn("material = yard[p] >> 12;", common)
        self.assertIn("&& st.scene.stats().subsurfaceMaterials>0;", self.text(PT + "src/tracer.cpp"))
        self.assertIn("for(const auto& m:materials_) if(m.flags&TGPT_MATERIAL_SUBSURFACE) ++subsurfaceMaterials_;", self.text(PT + "src/scene.cpp"))
        # Who walks is the game's own rule (PT-40, PT-73 c; docs/agents/who-walks.md): a material whose subsurface feature is
        # on (not one that names a profile: 688 clothes and armour materials and the ground did, and walked for it), as
        # often as its mask says at the point (an unbiased choice at the hit, the same for a path's vertex and the camera's
        # pixel), and where it is drawn from both sides only on a piece of the mesh that closes a volume.
        passes = self.text("src/TGPathTracing/HdrpPasses.cs")
        self.assertIn('bool feature = KeywordOn(m, "_MATERIAL_FEATURE_SUBSURFACE_SCATTERING");', passes)
        self.assertIn("return keyword.isValid && m.IsKeywordEnabled(keyword);", passes)
        self.assertIn("mat.SubsurfaceMask = Mathf.Clamp01(ownMask);", passes)
        self.assertIn('case "TG/Environment/MeshTerrain":', passes)
        self.assertNotIn("const uint noInside = Tgpt.MaterialDoubleSided", passes)
        header = self.text(PT + "include/tgpt_api.h")
        self.assertIn("    float subsurfaceMask;\n    uint32_t opticalReserved[2];", header.replace("\r\n", "\n"))
        self.assertIn("TGPT_EXPORT int32_t TGPT_CALL TGPT_GetWalkPixels(uint32_t* frameIndex, uint32_t* onSubsurface, uint32_t* walked);", header)
        self.assertIn("internal float SubsurfaceMask;", self.text("src/TGPathTracing/NativeTracer.cs"))
        self.assertIn("float3 absorption; uint surfaceModel; float subsurfaceMask; uint2 opticalReserved;", common)
        self.assertIn("static const uint MATERIAL_NO_WALK = MATERIAL_TRANSLUCENT | MATERIAL_THIN_CLOTH | MATERIAL_STRANDS | MATERIAL_GLASS | MATERIAL_CAMERA_ONLY | MATERIAL_HAIR_LAYER;", common)
        self.assertIn("return (m.flags & MATERIAL_DOUBLE_SIDED) == 0 || (h.front && piece_solid(h.instance, h.geometry, h.primitive));", common)
        self.assertIn("if (!(m.subsurface > 0.0) || !subsurface_hit(hit, shape)) return false;", common)
        self.assertIn("if (m.subsurface < 1.0 && rng.next() >= m.subsurface) return false;", common)
        self.assertIn("s.subsurface = (m.flags & MATERIAL_SUBSURFACE) != 0 ? saturate(g_pointSubsurface >= 0.0 ? g_pointSubsurface : m.subsurfaceMask) : 0.0;", common)
        self.assertIn("if (same && !subsurface_bounds(met, h)) { tmin = th + eps; continue; }", common)
        self.assertIn("if (!own_walks(p)) return false;", own)
        primary = self.text(PT + "shaders/pt_primary.hlsli")
        self.assertIn("ray_chance(hit.instance, hit.geometry, hit.primitive, WALK_SALT) < traced.subsurface;", primary)
        self.assertIn("y[p] = code | (g_ownWalks ? YARD_WALKS : 0u);", primary)
        self.assertIn("g_pointSubsurface = saturate(region * restScatter.w * program_sample(p, 12u, h.uv, gx, gy, MAP_WHITE).r);", self.text(PT + "shaders/pt_materials.hlsli"))
        self.assertIn('T(m, "_TattooMaskMap"), T(m, "_TattooNormalMap"), T(m, "_SSSMap") };', self.text("src/TGPathTracing/MaterialPrograms.cs"))
        # Closed is asked piece by piece (a candle's wax body and its wick's card share a submesh): the solid pieces'
        # triangles first, their count in the flags' upper bits, before anything reads the triangles' order.
        scene = self.text(PT + "src/scene.cpp")
        self.assertIn("g.flags|=uint32_t(std::min<size_t>(solid, kGeometrySolidMax))<<kGeometrySolidShift;", scene)
        self.assertIn("mark_closed(d); // before anything reads the triangles' order\n    compute_clusters(d);", scene.replace("\r\n", "\n"))
        self.assertIn("constexpr uint32_t kGeometrySolidShift=8, kGeometrySolidBits=0xFFFFFF00u, kGeometrySolidMax=0xFFFFFFu;", self.text(PT + "src/scene.hpp"))
        self.assertIn("return primitive < (geometries[meshes[instances[instanceId].mesh].geometryBase + geometry].flags >> GEOMETRY_SOLID_SHIFT);", common)
        self.assertIn("if ((code & YARD_WALKS) != 0u) InterlockedAdd(counters[13], 1u);", self.text(PT + "shaders/pt_yardstick.hlsl"))
        test = self.text("tests/pathtracing_gpu_test.cpp")
        self.assertIn('#include "pathtracing_walk_checks.inl"', test)
        checks = self.text("tests/pathtracing_walk_checks.inl")
        for name in ('"a mask of 0.5 gives the mean of the walk and the plain surface, in each channel"',
                     '"a closed wax cylinder drawn from both sides is lit as the one-sided one"',
                     '"a closed body with an open card in its submesh still walks (closed is asked piece by piece)"',
                     '"no pixel of an open sheet drawn from both sides walks"',
                     '"a metal plate with a mask of 0 takes no walk"',
                     '"skin walks as often as its subsurface map says at the point"'):
            self.assertIn(name, checks)
        self.assertEqual(test.count("subsurfaceMask=1.f;"), 5)  # a zeroed material does not walk: the walk's other checks say 1
        # A leaf the camera's own ray meets passes light by its material's own share too (thin_through); HDRP's GBuffer2 is not asked.
        leaf = common.split("bool leaf_transmittance(uint2 p, Surface s, out float3 t) {", 1)[1].split("\n}", 1)[0]
        self.assertIn("!own_material(p, material)) return false;", leaf)
        self.assertIn("return thin_through(materials[material], t);", leaf)
        self.assertNotIn("gb2", leaf)
        self.assertIn('"a leaf the camera\'s own ray meets passes the light from behind by its material\'s own share, tinted"', self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn("v = new Vector4(shape.x * mmPerUnit, shape.y * mmPerUnit, shape.z * mmPerUnit, radius / mmPerUnit);", self.text("src/TGPathTracing/HdrpPasses.cs"))
        self.assertIn('"light scattered under skin keeps the light\'s total"', self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn('"a wax cylinder lit from one side: its far side takes the light a reference random walk gives"', self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn('"a deep fold in a wax lump is lit by the light that went in elsewhere, redder than it went in"', self.text("tests/pathtracing_gpu_test.cpp"))
        # Thin things pass light by their own material where a path meets them (TGPT_Material.subsurface with
        # TGPT_MATERIAL_TRANSLUCENT); the grey half only where the material says nothing.
        self.assertIn("if (thin_through(m, own)) { s.through = max(luminance(own), 1e-4); s.tint = own / s.through; }", common)
        self.assertIn('"a leaf a path meets passes the light from behind by its material\'s own share, tinted"', self.text("tests/pathtracing_gpu_test.cpp"))
        # See-through glass: pt_prepare marks the smooth texels of glass (the camera ray finds its material), pt_specular
        # refracts through it (solid to that path) to what lies behind, and it has no diffuse light.
        # Glass HDRP draws transparent (the alchemist's jars) is the tracer's: its own mask bit, a camera ray that meets such glass
        # alone in front of HDRP's surface, HDRP's passes hidden while tracing, and the composite matching it by HDRP's depth.
        self.assertIn("#define TGPT_MATERIAL_SEE_THROUGH 2048u", self.text(PT + "include/tgpt_api.h"))
        # (Since the hair layer, 2026-09-28, 0x10 is hair drawn as a layer's own: still instances without such hair 0xE1, with it
        # 0xF1 as before.)
        self.assertIn("constexpr uint32_t kMaskSways=0x02, kMaskSeeThrough=0x04, kMaskWater=0x08, kMaskStrands=0x10, kMaskStill=0x61, kMaskWaves=0x80;", self.text(PT + "src/scene.hpp"))
        # In front of the sky too: its reflection traced, HDRP's sky laid under it through its color (B's alpha -2).
        self.assertNotIn("hdrpDepth", self.text(PT + "shaders/pt_specular.hlsl"))
        self.assertIn("if (g_through.x >= 0.0) { c.rgb = c.rgb * g_through + o.rgb; target[p] = c; }", self.text(PT + "shaders/d3d11.hlsl"))
        self.assertIn("GlassFinish.HideSeeThrough(on);", self.text("src/TGPathTracing/HdrpPasses.cs"))
        # Its texture from a 512x512 detail tile, filtered (the 64x64 tile showed a jar's stains as blocks), below the 64x64 tiles.
        self.assertIn("mat.AlbedoTile = seeThrough ? DetailTile(m.GetTexture(albedo)) : Tile(m.GetTexture(albedo));", self.text("src/TGPathTracing/SceneCapture.cs"))
        self.assertIn("st.atlas=make_shared(kAtlasSize, kAtlasHeight, DXGI_FORMAT_R8G8B8A8_UNORM, false);", self.text(PT + "src/tracer.cpp"))
        self.assertIn("if (uint(tile) >= MAX_TILES) {", self.text(PT + "shaders/pt_common.hlsli"))
        # Hair takes its shader's color, texel by texel (RealHair's gradient, highlights, occlusion), baked into its own tile.
        self.assertIn("baked = HairTile(m);", self.text("src/TGPathTracing/SceneCapture.cs"))
        # Hair, eyebrows and lashes stay opaque while tracing (the game's transparent layers were HDRP-shaded over the image).
        self.assertIn("CharacterCapture.HoldOpaque(on);", self.text("src/TGPathTracing/HdrpPasses.cs"))
        # Caustics through glass: the sun's and the lamps' shadow rays through one closed glass object are bent and focused.
        self.assertIn("if (g_glassMet >= 2 && any(through > 0.0)) through = caustic_through(sunOrigin, s.normal, s.geoNormal, l, 0.0, true, through);", self.text(PT + "shaders/pt_di_spatial.hlsl"))
        self.assertIn("return caustic_through(origin, s.normal, s.geoNormal, v / dist, dist, false, straight);", self.text(PT + "shaders/pt_rtxdi.hlsli"))
        self.assertIn("NormalStream = Tgpt.NoStream };", self.text("src/TGPathTracing/SceneCapture.cs"))
        self.assertIn('"glass focuses the sun into a bright spot, with a darker ring round it (caustics)"', self.text("tests/pathtracing_gpu_test.cpp"))
        # Caustics (PT-14): the sun's and the lamps' light traced onto HDRP's water as its real waves
        # shape it, reflected and refracted there and traced on; where it lands, its light over the
        # area its cell's corners land round (pt_caustics.hlsl). HDRP's water in the traced scene
        # under a mask bit of its own, which only the water queries ask for (every other ray asks for
        # MASK_ALL, never 0xFF). The water's own surface, its waves as triangles (PT-71 item 5), is in
        # MASK_ALL under a bit of its own (MASK_WAVES): a ray that must pass it (the caustics' own:
        # water_mask) or looks for it apart (the camera's first walk: MASK_SOLID) leaves that bit out.
        shaders = {name: self.text(PT + "shaders/" + name) for name in sorted(f.name for f in (ROOT / PT / "shaders").iterdir()) if name.endswith((".hlsl", ".hlsli"))}
        # MASK_STRANDS (instances holding hair the tracer's own camera rays find, since 2026-09-28) is asked for by the camera
        # ray that looks for hair alone (pt_primary.hlsli primary_walk: its `mask` is that or MASK_ALL, nothing else).
        for name, text in shaders.items():
            for mask in re.findall(r"TraceRayInline\(g_scene, [^,]+, ([^,]+),", text):
                self.assertIn(mask.strip(), ["MASK_ALL", "MASK_STILL", "MASK_SWAYS", "MASK_SEE_THROUGH", "MASK_WATER", "mask", "MASK_WAVES", "MASK_SOLID",
                                             "MASK_GROUND",  # water_medium: the ground between a point and water over it (PT-131)
                                             "water_mask(MASK_ALL)", "water_mask(mask)", "light_mask(MASK_ALL)", "light_mask(MASK_STILL)",
                                             # (round 20: a light's ray meets leaves and grass in a pass of its own)
                                             "mask & ~foliage", "light_mask(MASK_ALL) & (MASK_FOLIAGE | MASK_SWAYS)"], name)
        self.assertIn("const uint mask = everything ? MASK_SOLID : MASK_STRANDS;", shaders["pt_primary.hlsli"])
        for name, text in shaders.items():
            self.assertNotIn("mask = 0xFFu", text, name)
        common_masks = shaders["pt_common.hlsli"]
        self.assertIn("#define MASK_WATER 0x08u", common_masks)
        self.assertIn("#define MASK_ALL 0xF7u", common_masks)
        self.assertIn("#define MASK_STILL 0xF5u", common_masks)
        caustics = shaders["pt_caustics.hlsl"] + shaders["pt_waves.hlsli"]
        compose = shaders["pt_compose.hlsl"]
        # The stand-ins are gone: no mirror image of the sun or the lamps in a flat plane, no HDRP caustic pattern.
        for name, text in shaders.items():
            for stand_in in ("water_caustic(", "water_caustic_at(", "water_reflected_sun(", "water_reflected_lamps(", "water_lamp_light(", "s_linearRepeat, c.xz"):
                self.assertNotIn(stand_in, text, name)
        self.assertNotIn("GetCausticsBuffer", self.text("src/TGPathTracing/WaterCaustics.cs"))
        # HDRP's real waves, as its water shader moves its surface: each band's displacement and surface gradient (bicubic), its
        # amplitude and its fade with the distance, the horizontal move times HDRP's choppiness.
        self.assertIn("static const float CHOPPINESS = 2.25;   // HDRP's WATER_SYSTEM_CHOPPINESS", shaders["pt_waves.hlsli"])
        self.assertIn("move += float3(-CHOPPINESS * raw.y, raw.x, -CHOPPINESS * raw.z);", shaders["pt_waves.hlsli"])
        self.assertIn("slope += bicubic(gradient, uv, float(b), info.y).xy * fade;", shaders["pt_waves.hlsli"])
        self.assertIn("float fade = saturate(distance * e.y + e.z); fade *= fade; fade = fade * fade * (3.0 - 2.0 * fade);", shaders["pt_waves.hlsli"])
        self.assertIn('constantsField = AccessTools.Field(system, "m_ShaderVariablesPerSurfaceArray");', self.text("src/TGPathTracing/WaterCaustics.cs"))
        self.assertIn('B0 = (Vector4)Constant(cb, "_Band0_ScaleOffset_AmplitudeMultiplier")', self.text("src/TGPathTracing/WaterCaustics.cs"))
        self.assertIn("c.waves=copy_waves(from[i].displacement, c.displacement) && copy_waves(from[i].normals, c.normals) &&", self.text(PT + "src/tracer.cpp"))
        # Light traced onto them: reflected and refracted (index 1.333, the exact Fresnel split), each ray traced on; the light
        # through a water triangle (a lamp's intensity times its solid angle, the sun's irradiance times its area across the light)
        # over the area its corners land round.
        self.assertIn("const float reflected = boundary.reflectance;", caustics)
        self.assertIn("const float3 t = boundary.transmitted;", caustics)
        self.assertIn("share = dot(up, y - xa) > 0.0 ? solid_angle(xa - y, xb - y, xc - y) : 0.0;", caustics)
        self.assertIn("const float density = share / (0.5 * twice) / PI;", caustics)
        self.assertIn("sum += asfloat(light.Load3(at)) * (1.0 - w.x - w.y) + asfloat(light.Load3(at + 12)) * w.x + asfloat(tail.xyz) * w.y;", common_masks)
        # Where it landed is found by an acceleration structure of the landed triangles, built every frame (the caustics-3
        # agent, 2026-10-07): the world-space hash it replaces walked 8 buckets' lists at every level for each point, 40 to
        # 50 ms of the composite at the game's cave lake (run 20261007-140305), 33.7 ms on the test's lake against 4.4 ms.
        # No walk limit is left, and the light is kept as floats (a half clamped a focused line's light).
        self.assertIn("static const uint CAUSTIC_CORNERS = 48;", common_masks)
        self.assertIn("RaytracingAccelerationStructure landed = ResourceDescriptorHeap[g.caustics.y];", common_masks)
        self.assertIn("PT_RAYQUERY(RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) q;", common_masks)
        self.assertIn("if (abs(dot(p - a, nt)) > asfloat(tail.w) * len) continue;", common_masks)
        for gone in ("CAUSTIC_WALK", "caustic_bucket(", "f32tof16(min(la, 65000.0))"):
            self.assertNotIn(gone, common_masks + caustics, gone)
        tracer_caustics = self.text(PT + "src/tracer.cpp")
        self.assertIn("kCausticCorners=48, kCausticLight=48, kCausticCounters=256", tracer_caustics) # (128 until PT-114: the tiles' counts)
        self.assertIn("g.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION;", tracer_caustics)
        self.assertIn("const D3D12_RAYTRACING_GEOMETRY_DESC geometry=caustic_geometry(st.causticBuilt);", tracer_caustics)
        self.assertIn("dispatch(list, st.causticPass.Get(), {2u}, b, (st.causticBuilt+255)/256, 1);", tracer_caustics)
        self.assertIn("corners.Store(i * CAUSTIC_CORNERS, nan); corners.Store(i * CAUSTIC_CORNERS + 16, nan); corners.Store(i * CAUSTIC_CORNERS + 32, nan);", caustics)
        self.assertIn('"the caustics and their lookup stay affordable where 25 lamps light a lake (under 10 ms here; the hash took 33.7 ms)"', self.text("tests/pathtracing_gpu_test.cpp"))
        # The proof that HDRP's waves arrive (until 2026-10-07 no slot got them: the plugin read an RTHandle "as Texture"):
        # the plugin takes the texture behind the handle; the tracer says in its log what the waves are where it reads them
        # (0 cm and flat: still water) and one grid point's surface; and the GPU test hands over buffers as HDRP makes them
        # (texture arrays with mips, three bands, a turned surface) and holds that point against HDRP's formula.
        self.assertIn('AccessTools.Property(handle.GetType(), "rt")?.GetValue(handle, null) as Texture', self.text("src/TGPathTracing/WaterCaustics.cs"))
        self.assertIn("counters.InterlockedCompareExchange((4 + slot) * 4, 0u, 1u, was);", caustics)
        self.assertIn('"%s slot %u waves up to %.1f cm high, slopes to %.3f (at x %.2f z %.2f the surface is at %.3f %.3f %.3f, normal %.3f %.3f %.3f)"', tracer_caustics)
        self.assertIn('GetEnvironmentVariableA("TGPT_LOG_WATER_SAMPLES", nullptr, 0)>0', tracer_caustics)
        self.assertIn('"HDRP\'s waves reach the tracer as HDRP makes them: at a grid point the tracer\'s surface and normal are HDRP\'s formula\'s (three bands, a turned surface, the horizontal move, the fade)"',
                      self.text("tests/pathtracing_gpu_test.cpp"))
        # Every lamp counts (a budget may cost noise, never the picture): a tile's two strongest lights and one of the rest in
        # proportion to its light, divided by its chance; over all the lights by the group's threads, never per pixel.
        self.assertIn("if (chosen != CAUSTIC_NONE && sum > 1e-6 * (sum + all) && chosenWeight > 0.0) { gs_light[2] = chosen; gs_scale[2] = sum / chosenWeight; }", caustics)
        self.assertIn("for (uint k = index; k <= count; k += TILE * TILE) {", caustics)
        self.assertNotIn("light_cell(", compose)
        # The light under the water came through its surface: a light's straight shadow ray that meets the water stops there,
        # as at anything else (its surface is in the acceleration structure, PT-71 item 5: no query of its own for it), except
        # the caustics' own rays, which start on it (g_passWater: water_mask leaves its bit out).
        # With the Caustics row off nothing traces that light, and the row's "off" is the straight light, for water as for
        # glass (its tooltip: "water casts no pattern"; the GPU test: "with TGPT_OPTION_NO_CAUSTICS the water stops no ray and
        # the light under it is the straight light"): a light's straight ray passes the surface then (light_mask, the shadow
        # rays' queries alone: a camera's or a bounce's ray always meets the water), and the water takes its share over each
        # part of the ray that lies in it (water_straight). Without it the bed lost the sun with the row off.
        common = shaders["pt_common.hlsli"]
        self.assertNotIn("crosses_water", common)
        any_hit = common.split("bool occluded_any(float3 origin", 1)[1].split("\n}", 1)[0]
        # (Round 20: through any_blocks, which meets leaves and grass in a pass of its own for a light's ray.)
        self.assertIn("return any_blocks(origin, dir, tmin, tmax, light_mask(MASK_ALL));", any_hit)
        surface_rays = common.split("bool occluded_from_surface_rays(", 1)[1].split("\n}", 1)[0]
        self.assertIn("q.TraceRayInline(g_scene, g_lightRay ? RAY_FLAG_FORCE_NON_OPAQUE : RAY_FLAG_NONE, light_mask(MASK_STILL), ray);", surface_rays)
        self.assertIn("return any_blocks(origin, dir, max(nearEnd, tmin), tmax, light_mask(MASK_ALL));", surface_rays)
        closest = common.split("bool trace_closest(float3 origin", 1)[1].split("\n}", 1)[0]
        self.assertIn("q.TraceRayInline(g_scene, RAY_FLAG_NONE, water_mask(MASK_ALL), ray);", closest)
        water_surface = shaders["pt_water_surface.hlsli"]
        self.assertIn("uint water_mask(uint mask) { return g_passWater ? mask & ~MASK_WAVES : mask; }", water_surface)
        self.assertIn("bool water_passes_light() { return (g.settings2.y & OPTION_NO_CAUSTICS) != 0; }", water_surface)
        self.assertIn("uint light_mask(uint mask) { return g_passWater || water_passes_light() ? mask & ~MASK_WAVES : mask; }", water_surface)
        self.assertIn("if (water_passes_light()) return water_straight(origin, dir, tmin, tmax, medium);", common)
        self.assertIn("inside = q.CommittedTriangleFrontFace() ? int((instances[q.CommittedInstanceID()].flags >> 8) & 3u) : -1;", water_surface)
        for name in ["transmittance(", "transmittance_from_surface("]:
            self.assertIn("float3 waterThrough = water_through(origin, dir, tmin, tmax);", common.split("float3 " + name, 1)[1].split("\n}", 1)[0])
        self.assertIn('"with TGPT_OPTION_NO_CAUSTICS the water stops no ray and the light under it is the straight light"', self.text("tests/pathtracing_gpu_test.cpp"))
        # Who sees it: every surface the camera sees (after the direct light's filter), and the bed seen through the water.
        self.assertIn("float3 d = s.valid && s.deferred ? sun[p].rgb + caustics_at(s) : 0.0;", compose)
        self.assertIn("float3 caustics_at(Surface s) { return to_storage(caustic_light(s.position, s.geoNormal)); }", compose)
        self.assertIn("if(k.caustics[0]!=kInvalid) {", self.text(PT + "src/tracer.cpp"))
        self.assertIn('"scene", "sky", "caustics", "prepare", "bounced light"', self.text(PT + "src/tracer.cpp"))  # the caustics before every pass that reads them (PT-130): NRD takes them (PT-127), the water's paths read them
        self.assertIn('"radiance cache", "denoising", "fog cells", "fog walk", "fog lamps", "composite"};', self.text(PT + "src/tracer.cpp"))  # the fog has its own measured passes (PT-118)
        # The checks: still water is the mirror's limit (the sun's and a lamp's, the floor under it the water's transmittance);
        # waves only move light; a sine wave focuses the sun into lines where the CPU's rays put them; every lamp adds up.
        gpu = self.text("tests/pathtracing_gpu_test.cpp")
        for check in ("under still water the floor takes the sun through the surface: the water's Fresnel transmittance of it, no straight light",
                      "waves only move the light: the floor's mean under them is the still water's",
                      "the sine wave's crests focus the sun into lines where the CPU's rays put them, as bright",
                      "the lines move with the waves",
                      "a wall facing still water takes the sun's mirror image in it, by the water's Fresnel reflectance",
                      "the waves split the reflected sun into bright lines and dark gaps",
                      "no reflected light from water the sun doesn't reach",
                      "a wall facing still water takes the lamp's mirror image in it, by the water's Fresnel term and the whole path's falloff",
                      "no reflected lamplight where something stands between the water and the lamp",
                      "three lamps' light reflected off the water adds up to each one's alone",
                      "four lamps' reflected light adds up on average, the two weakest sampled for each other"):
            self.assertIn('"' + check + '"', gpu)
        # The F8 report says how many times the crosshair's glass is crossed (2: its caustics are solved).
        self.assertIn("const uint crossings = glass_crossings(g.cameraPos.xyz, dir);", self.text(PT + "shaders/pt_probe.hlsl"))
        self.assertIn('"the F8 report counts the crosshair\'s glass crossings (2 closed, 4 hollow)"', gpu)
        self.assertIn('"; the camera ray crosses the first glass along it "', self.text("src/TGPathTracing/DebugCapture.cs"))
        self.assertIn("if(inst.flags&TGPT_INSTANCE_WATER) {", self.text(PT + "src/scene.cpp"))
        self.assertIn("d.InstanceMask=kMaskWater;", self.text(PT + "src/scene.cpp"))
        # (HDRP's own caustics need no holding back any more: HDRP does not draw or light the water the tracer draws, PT-71 item 5.
        # Water past the tracer's slots stays HDRP's whole, with its own caustics.)
        water = self.text("src/TGPathTracing/WaterCaustics.cs")
        self.assertNotIn("causticsIntensity", water)
        rendering = self.text("src/TGPathTracing/WaterSurfaceRendering.cs")
        self.assertIn("if (!Active(hdCamera) || !WaterCaustics.Traced(currentWater)) return true;", rendering)
        # PT-114: in an interior the map's water is neither traced nor drawn; an interior's own water always is.
        self.assertIn("if (Active(hdCamera) && WaterCaustics.OutsideAnInterior(currentWater)) { __result = false; return false; }", rendering)
        self.assertIn('SceneLights.InteriorLoaded && !s.gameObject.scene.name.StartsWith("Interior", StringComparison.OrdinalIgnoreCase);', water)
        self.assertIn("if (LeftOutside(s))", water)
        self.assertNotIn("Interior: trace the water outside", self.text("src/TGPathTracing/Settings.cs"))
        self.assertIn("private static bool Lighting(HDCamera hdCamera) => !Active(hdCamera) || WaterCaustics.AnyUntraced;", rendering)
        self.assertIn("try { water = WaterCaustics.Capture(camera, AddWater); }", self.text("src/TGPathTracing/SceneCapture.cs"))
        self.assertIn("AddInstance(sinkInstances, sinkIndices, handle, world, list, Tgpt.InstanceWater | ((uint)slot << 8));", self.text("src/TGPathTracing/SceneCapture.cs"))
        self.assertIn("WaterCaustics.Tick();", self.text("src/TGPathTracing/Session.cs"))
        # The eyes are the tracer's: their shader's color baked, lit as a wet surface; HDRP's cornea hidden while tracing.
        self.assertIn("Flags = Tgpt.MaterialAlphaTest | Tgpt.MaterialEye });", self.text("src/TGPathTracing/SceneCapture.cs"))
        self.assertNotIn("Tgpt.MaterialCameraOnly | Tgpt.MaterialHdrpShaded", self.text("src/TGPathTracing/SceneCapture.cs"))
        self.assertIn('if (m.name.IndexOf("Cornea", StringComparison.OrdinalIgnoreCase) >= 0) { HideLayer(m); continue; }', self.text("src/TGPathTracing/CharacterCapture.cs"))
        self.assertIn('"an eye takes its shader\'s sclera, iris and limbal ring"', self.text("tests/pathtracing_gpu_test.cpp"))
        # (Fog Adaptation, PT-48, went on 2026-10-11 with HDRP's fog: the fog is the tracer's.)
        # HDRP's fog dims the traced sun and lamps on their way to a surface, as it dims HDRP's own (PT-05, PT-38: sunlit
        # ground two to three times the game's, a night lit by a full moon): every use of the sun's light takes sun_fog.
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("float sun_fog(float3 position) { return directional_fog(position, g.sunDir.y); }", common)
        self.assertIn("return exp(-k * depth / max(cosZenith, 0.001));", common)
        self.assertIn("return l.color * (atten * exp(-height_fog_depth(position, l.position))) * light_cookie(l, -dir);", common)
        self.assertIn("const float3 sun = g.sunColor.rgb * transmittance(position + n * (dot(n, l) >= 0.0 ? offset : -offset), l, 0.0, g.params.y) * sun_fog(position);", common)
        self.assertIn("through *= sunFog;", self.text(PT + "shaders/pt_di_spatial.hlsl"))
        for name in ("pt_specular.hlsl", "pt_compose.hlsl", "pt_di_spatial.hlsl"):
            source = self.text(PT + "shaders/" + name)
            for line in source.splitlines():
                if "g.sunColor.rgb" in line and "sun_fog" not in line and "sunFog" not in line:
                    # The pixel's own sun (pt_di_spatial) is dimmed through `through` and `sunFog` on the lines around it.
                    self.assertTrue(name == "pt_di_spatial.hlsl", name + ": the sun's light without HDRP's fog: " + line.strip())
        self.assertIn('"the traced sun is dimmed by HDRP\'s fog above the surface, as HDRP dims its own"', self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn('"a lamp\'s light is dimmed by HDRP\'s fog over the way to the surface"', self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn("if (holdOpaque) { RememberQueues(__instance); transparent = false; blendshapes = false; }", self.text("src/TGPathTracing/CharacterCapture.cs"))
        # The campaign map's torches and candles stand under no prop with generic names: a light under no prop is a flame by
        # its look (warm, with its flicker cookie, or warm beside one); a warm light alone stays out (no fake lights).
        lights = self.text("src/TGPathTracing/SceneLights.cs")
        self.assertIn("if (n.placed && light.transform.parent == null && FlameByLook(light)) { n.placed = false; n.flame = true; n.kind = EmitterKind.FlameByLook; }", lights)
        self.assertIn("private static bool Flickers(Light light) => light.type == LightType.Point && light.cookie != null && Warm(light);", lights)
        self.assertIn("GatherFlickering(all);\n                GatherGlows(all, camera, radius, lightLayers);\n                LogInventory(all);", lights.replace("\r\n", "\n"))
        # The hold gives each material the render queue it had: HDRP's ValidateMaterial derives one from the queue it finds,
        # and hair "before refraction" (2750) came back as plain transparent (3000) after Off (PT-02, the in-game harness).
        self.assertIn("if (m != null && !heldQueues.ContainsKey(m)) heldQueues[m] = m.renderQueue;", self.text("src/TGPathTracing/CharacterCapture.cs"))
        self.assertIn("heldQueues.Clear();", self.text("src/TGPathTracing/CharacterCapture.cs"))
        # Under the material's own key (the native side shares a tile between hair of the same maps and values: the pool's check).
        self.assertIn("ulong key = unchecked((ulong)(uint)id);", self.text("src/TGPathTracing/SceneCapture.cs"))
        self.assertIn("int tile = tracer.RegisterHairColor(key, ref d);", self.text("src/TGPathTracing/SceneCapture.cs"))
        self.assertIn("#elif defined(TGPT_HAIR)", self.text(PT + "shaders/d3d11.hlsl"))
        self.assertIn('"hair takes its shader\'s root-to-end gradient and highlight band"', self.text("tests/pathtracing_gpu_test.cpp"))
        spec = self.text(PT + "shaders/pt_specular.hlsl")
        self.assertIn("glassFound = dielectric_follow(seen, seenDir, material, glassWeight, rng, glassDistance, glassLight);", spec)
        self.assertIn("if (s.glass || s.water || s.underwater) diffuse = 0.0;", self.text(PT + "shaders/pt_compose.hlsl"))
        # The Denoising tooltip: one short line per denoiser.
        self.assertLess(len(re.search(r'DenoiserDescription =\s*((?:"[^"]*"\s*\+?\s*)+);', settings_text := self.text("src/TGPathTracing/Settings.cs")).group(1)), 260)
        self.assertNotIn("DOOM", settings_text)
        self.assertIn("if (!on) RestoreAll();", lights)
        self.assertIn("Interior = !Sun.Valid || InteriorScene();", lights)
        for token in ['private static readonly string[] AlwaysPlacedNames = { "HeroLight", "Volumetric", "Fake" };',
                      "for (int depth = 0; t != null && depth <= 5; depth++, t = t.parent)", "if (Names(t.name, SourceNames)) n.placed = false;",
                      "var cls = Classify(light);\n                        if (cls.placed)"]:
            self.assertIn(token, lights)
        for source in ['"Fire"', '"Candle"', '"Candela"', '"Brazier"', '"Lamp"', '"Lantern"', '"Torch"', '"VFX"']:
            self.assertIn(source, lights)
        self.assertIn("SceneLights.IsPlacedLight(light)", self.text("src/TGPathTracing/Probe.cs"))
        # The player's own Conversation Lighting Overhaul portrait light stays.
        self.assertIn('private static readonly string[] KeptNames = { "TGConversationLightingOverhaul" };', lights)
        # The dialogue light keeps the brightness its mod set at the game's exposure: the traced light's automatic gain is
        # taken back out of it (the player's Exposure stays).
        self.assertIn("if (Names(light.name, KeptNames)) color *= HdrpPasses.AutomaticGainInverse;", lights)
        # (PT-73 e: the gain holds back where a pixel is bright, so the factor is the curve's own inverse at a mid tone, a third
        # of HIGHLIGHT_KEPT; 1 / the gain would leave a lit face too dark. Never above 1.)
        self.assertIn("float automatic = Mathf.Pow(2f, tracedLog2Gain - player);", self.text("src/TGPathTracing/HdrpPasses.cs"))
        self.assertIn("if (!(automatic > 1f)) return 1f;", self.text("src/TGPathTracing/HdrpPasses.cs"))
        self.assertIn("return 2f / (a + 1f + Mathf.Sqrt(Mathf.Max((a + 1f) * (a + 1f) - 4f * a * midTone, 0f)));", self.text("src/TGPathTracing/HdrpPasses.cs"))
        self.assertIn("kept = Names(light.name, KeptNames) };", lights)
        self.assertIn("if (n.kept) n.placed = false;", lights)

    def test_caustics_grid_trial_changes_no_triangle(self):
        # PT-114, ON TRIAL (docs/agents/caustgrid.md): the caustics' stage cost 4 to 7 ms of every outdoor frame, most of it
        # where none of its light landed. Measured (the sea fixture's grid part): the cost was the lights' pick, in which the
        # first of a tile's 256 threads joined their shares alone, twice, while 255 waited; and the tiles that ran for
        # nothing (a river's extent is a rectangle round its meshes: mostly dry land). The trial, behind frame flag bit 19
        # and the plugin's hidden "Caustics grid trial" (0: everything as before), in three steps: 1 the tiles the pass would
        # run are listed on the CPU and the lights' pick has a pass of its own (a thread a tile, the same sums and draws in
        # the same order); 2 (bit 25 with it) only the tiles with their slot's water under them, and no corner that no cell
        # needs; 3 (bit 26 with both) a light whose rays cannot land in view is not traced. NO LIMIT: what is written is the
        # same, triangle for triangle (the GPU test compares the written triangles' checksum frame by frame).
        api = self.text(PT + "include/tgpt_api.h")
        self.assertIn("#define TGPT_FRAME_CAUSTICS_GRID (1u << 19)", api)
        self.assertIn("#define TGPT_FRAME_CAUSTICS_GRID_WATER (1u << 25)", api)
        self.assertIn("#define TGPT_FRAME_CAUSTICS_GRID_VIEW (1u << 26)", api)
        tracer = self.text(PT + "src/tracer.cpp")
        # Off: the dispatch as it was, every tile of every level and slot, the pass picking for itself.
        self.assertIn("const bool cut=(f.flags&TGPT_FRAME_CAUSTICS_GRID) && caustic_tile_list(index, k, levels, !(cutsOff&32u), listSrv, listed);", tracer)
        self.assertIn("if(!(f.flags&TGPT_FRAME_CAUSTICS_GRID_WATER)) cutsOff|=2u|32u;", tracer)
        self.assertIn("if(!(f.flags&TGPT_FRAME_CAUSTICS_GRID_VIEW) || (cutsOff&32u)) cutsOff|=1u;", tracer)
        self.assertIn("else if(!cut) list->Dispatch(tiles, tiles, levels*TGPT_MAX_WATER);", tracer)
        self.assertIn("else if(listed) list->Dispatch(listed, 1, 1);", tracer)
        # The list leaves out only what the pass's own first tests end, and tiles with no triangle of the slot's level water
        # under them (a proof by the water meshes' triangles; nothing is left out where the CPU does not hold them all).
        self.assertIn("if(water && !((rows[tz]>>tx)&1ull)) { ++dry; continue; }", tracer)
        water = self.text(PT + "src/scene_water.cpp")
        self.assertIn("if(slot>=4 || !waterCoverKnown_[slot] || !(tile>0.0) || !std::isfinite(x0) || !std::isfinite(z0) || !(margin>=0.0)) { std::fill(rows, rows+n, all); return false; }", water)
        self.assertIn("if(!water_triangles(device, list, *mesh, release, retireValue, barriers)) { waterCoverKnown_[record.slot&3u]=false; continue; }", water)
        self.assertIn("for(int slot=0;slot<4;++slot) { waterCover_[slot].clear(); waterCoverKnown_[slot]=false; }", self.text(PT + "src/scene.cpp"))
        shader = self.text(PT + "shaders/pt_caustics.hlsl")
        self.assertIn("const bool listed = pass.a.z != INVALID;", shader)
        # The pick's own pass does what the group's threads did: the shares in the threads' order, the join's draws with the
        # first share's numbers going on.
        self.assertIn("TilePick pick_tile(uint slot, float2 lo, float2 hi, uint tile) {", shader)
        self.assertIn("if (t2 == 0u) first = r; // (the first thread's numbers go on in the join)", shader)
        self.assertIn("if (first.next() * joined < rest) { chosen = pick; chosenWeight = pickWeight; }", shader)
        # A light is left untraced only where no triangle of it can pass in_view (the rule a landed triangle is written
        # by), with room to spare; never with the split trial, whose parts land off the corners' rays.
        self.assertIn("viewCut = (pass.a.w & 1u) == 0u && (g.settings2.y & OPTION_SPLIT) == 0u;", shader)
        self.assertIn("const float SLACK = 1e-4, k = 1.5; // (k: in_view's)", shader)
        self.assertIn("if (!in_view(pa, pb, pc)) return;", shader)
        # No measuring switch is left on in a build for players.
        self.assertNotIn("\n#define PT_CAUSTICS_PARTS", shader.replace("\r\n", "\n"))
        self.assertIn("#if defined(PT_SPEED_MEASURE) || defined(PT_CAUSTICS_PARTS)", shader)
        # The plugin's hidden switch and the frame's bits. All three steps are the default since 2026-10-12 (the game's run
        # 20261012-114808: the stage 5.3 -> about 3 ms in a town and a forest, the beach's picture the same); 0 is the
        # tests' yardstick.
        self.assertIn("internal const uint FrameCausticsGrid = 1u << 19, FrameCausticsGridWater = 1u << 25, FrameCausticsGridView = 1u << 26;", self.text("src/TGPathTracing/NativeTracer.cs"))
        self.assertIn('CausticsGrid = file.Bind(Advanced, "Caustics grid trial", 3, Hidden(', self.text("src/TGPathTracing/Settings.cs"))
        self.assertNotIn('"Caustics grid trial", 0, Hidden(', self.text("src/TGPathTracing/Settings.cs"))
        passes = self.text("src/TGPathTracing/HdrpPasses.cs")
        for line in ("if (grid >= 1) frame.Flags |= Tgpt.FrameCausticsGrid;", "if (grid >= 2) frame.Flags |= Tgpt.FrameCausticsGridWater;", "if (grid >= 3) frame.Flags |= Tgpt.FrameCausticsGridView; }"):
            self.assertIn(line, passes)
        # The harness's recipes: off, the trial (all of it for the picture, step by step for the time), off again, and the
        # default (3) back at their end.
        recipes = json.loads(self.text("tests/ingame/scenarios.json"))["recipes"]
        for name, values in (("caustics-grid", ["0", "3", "0", "3"]), ("caustics-grid-speed", ["0", "1", "2", "3", "0", "3"])):
            steps = recipes[name]
            trial = [s["value"] for step in steps for s in step.get("set", []) if s.get("key") == "Caustics grid trial"]
            self.assertEqual(trial, values, name)
            self.assertTrue(all(s.get("section") == "Advanced" for step in steps for s in step.get("set", []) if s.get("key") == "Caustics grid trial"), name)
        # The check: the same triangles (count, largest tolerance, checksum) and the same pixels, the trial off and on.
        self.assertIn('#include "water_grid_gpu.inl"', self.text("tests/water_sea_gpu.inl"))
        grid = self.text("tests/water_grid_gpu.inl")
        self.assertIn('"with the grid\'s trial on every frame writes the same caustic triangles: their count, their largest tolerance and their checksum"', grid)
        self.assertIn('"with the grid\'s trial on the frames are the same pixel for pixel, within what two runs of the same frames differ by"', grid)

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
                      "for (int j = span.start; j < span.end; j++) added |= Add(pairs[j], v, entity);",
                      "if (settings.ShadowCastingMode == ShadowCastingMode.ShadowsOnly) { if (!follow) drakeShadowOnly += chunk.Count; if (!standIns) return false; standIn = true; }",
                      "if (settings.ShadowCastingMode == ShadowCastingMode.ShadowsOnly) { if (!follow) entityShadowOnly += chunk.Count; continue; }"]:
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
        self.assertIn("k.settings2[1]=(rrg ? f.options : (f.options&~TGPT_OPTION_RAY_REGENERATION))&~TGPT_OPTION_NRD; k.settings2[2]=", tracer)
        # AMD's backend binds its own heap and root signature; the tracer's are bound again before compose.
        after = tracer[tracer.index("st.rrgActive=st.rrg.dispatch(list, r, error);"):]
        self.assertLess(after.index("list->SetDescriptorHeaps(2, heaps);"), after.index("dispatch(list, st.compose.Get()"))
        self.assertIn("list->SetComputeRootSignature(st.root.Get());", after)
        self.assertIn("dispatch(list, st.compose.Get(), {(st.nrdActive ? 2u : 0u), out.c.uav, nrdToStorage, out.g0.uav, out.g1.uav, out.dm.uav, out.mv.uav, fogUav}, b);", tracer)
        # AMD 6/14: both directions use the same specular factor and stable radiance scale, before compose/meter.
        guides = self.text(PT + "shaders/pt_rrg_guides.hlsl")
        self.assertIn("const float3 specularFactor = max(spec, 1e-3);", guides)
        self.assertIn("restore ? reflection.rgb * specularFactor / scale : reflection.rgb * scale / specularFactor", guides)
        self.assertIn("gi.rgb / scale : gi.rgb * scale, gi.a", guides)
        self.assertIn("di.rgb / scale : di.rgb * scale, di.a", guides)
        self.assertNotIn("specular *= specular_guide_albedo", self.text(PT + "shaders/pt_compose.hlsl"))
        self.assertIn("r.frameIndex=f.frameIndex; r.reset=restart;", tracer)
        self.assertIn("f.frameIndex!=lastDispatch_+1u", rrg)
        self.assertIn("havePrevious_=false; // any failed dispatch invalidates history", rrg)
        self.assertIn("std::async(std::launch::async", rrg)
        self.assertIn("FFX_DENOISER_ENABLE_DEBUGGING|FFX_DENOISER_ENABLE_VALIDATION", rrg)
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
        # (direct_light: direct_diffuse's light sampling, with a specular lobe for what a reflection shows, PT-46.)
        direct = common.split("float3 direct_light(", 1)[1].split("float3 shade_path(", 1)[0]
        self.assertIn("const float3 sun = g.sunColor.rgb * transmittance(position + n * (dot(n, l) >= 0.0 ? offset : -offset), l, 0.0, g.params.y) * sun_fog(position);", direct)
        self.assertIn("return direct_light(position, n, shading, albedo, 0.0, 0.0, n, offset, rng, through);", direct)
        # A translucent vertex (grass, leaves, thin cloth) is lit from behind too, as the camera's translucent pixels are, by
        # the share its material lets through.
        # (The cosines with the shading normal, a normal map's at a hit: PT-46; the triangle's for an opaque surface's side.)
        self.assertIn("float nl = through > 0.0 || dot(n, l) > 0.0 ? translucent_cos(shading, l, through) : 0.0;", direct)
        self.assertIn("float target = through > 0.0 || dot(n, l) > 0.0 ? luminance(e) * translucent_cos(shading, l, through) : 0.0;", direct)
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
                      "for (uint crossing = 0; crossing < met && t0 < tmax; ++crossing) {",
                      "& MATERIAL_GLASS) != 0) q.CommitNonOpaqueTriangleHit();",
                      "through *= glass_open(inst, m, q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics());",
                      # PT-76: glass's own tint is its material's absorption: over the way inside closed glass, a thin pane's by its thickness.
                      "through *= segment_transmittance(m.absorption, t - (insideOf == glassId ? insideFrom : tmin));",
                      "through *= pane_transmittance(m, triangle_slant(glassId, q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedObjectToWorld3x4(), dir));",
                      "t0 = t + max(1e-4, t * 1e-5);"]:
            self.assertIn(token, walk)
        self.assertNotIn("NO_DUPLICATE_ANYHIT_INVOCATION", self.text(PT + "src/scene.cpp"))
        for name in ["transmittance(", "transmittance_from_surface("]:
            body = common.split("float3 " + name, 1)[1].split("\n}", 1)[0]
            self.assertIn("g_glassTint = true; g_glassMet = 0;", body)
            self.assertIn("g_glassTint = false;", body)
            self.assertIn("return waterThrough * (g_glassMet > 0 ? glass_walk(origin, dir, tmin, tmax, g_glassMet) : 1.0);", body)
        # The denoisers take see-through glass as specular only: NRD leaves it alone as the sky, AMD's Ray Regeneration gets
        # no diffuse albedo, a specular albedo covering what it lets through, and a material type of its own, and the
        # composite corrects Ray Reconstruction's guides there (B's alpha -1 marks it; forward stays > 0.5).
        self.assertIn("bool nrd_takes(Surface s) { return s.valid && s.deferred && !s.glass && !s.water && !s.underwater; }", self.text(PT + "shaders/pt_nrd_prep.hlsl"))
        rrg = self.text(PT + "shaders/pt_rrg_guides.hlsl")
        self.assertIn("normals[p] = float4(octahedral(s.normal), s.roughness, s.glass ? 1.0 / 3.0 : 0.0);", rrg)
        self.assertIn("diffuseAlbedo[p] = float4(s.glass ? 0.0 : sqrt(saturate(s.albedo)), 1.0);", rrg)
        self.assertIn("if (s.glass) a += (1.0 - fresnel_schlick(s.f0, nv));", common)
        self.assertIn("outB[p] = float4(rr_clamp(max(specular * exStored, 0.0)), s.glass ? -1.0 : 0.0);", self.text(PT + "shaders/pt_compose.hlsl"))
        d3d11 = self.text(PT + "shaders/d3d11.hlsl")
        # Since 2026-10-08 (PT-84) each pixel takes its own texel of the frame's own light, by what tracedB's alpha says it
        # holds: 1 the pixel's whole light, -2 glass in front of the sky (A the share of HDRP's sky it lets through), 0 and -1
        # the route before the tracer's own camera rays (a surface of the G-buffer; its see-through glass). No taps around a
        # place found through motion vectors, no kinds to keep apart, no search, no random pick.
        self.assertIn("if (tb.a > 0.5) return float4(max(ta.rgb + tb.rgb, 0.0), 0.0);", d3d11)
        self.assertIn("if (tb.a < -1.5) { g_through = saturate(ta.rgb); return float4(max(tb.rgb, 0.0), dot(g_through, 1.0 / 3.0)); }", d3d11)
        self.assertIn("g_hitDistance = tracedC.Load(int3(p, 0)).a;", d3d11)
        for gone in ("texel_kind", "sky_surface", "composite_surface", "prevUv", "kinds["):
            self.assertNotIn(gone, d3d11, gone)
        self.assertNotIn("glass_guides", d3d11)
        self.assertEqual(d3d11.count("float3 env_brdf_approx2("), 1)
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("out.c.t.srv.Get(), out.g0.t.srv.Get(), out.g1.t.srv.Get()};", tracer)
        # Where the tracer knows the pixel better than HDRP's GBuffer (a surface drawn forward: an eye; hair drawn as a layer),
        # its own guides over the guides pass's (PT-01, 2026-10-06: under the layer the GBuffer holds the skin or the wall, and
        # Ray Reconstruction smoothed the strands away as noise on it; an eye had a grey albedo). The hair layer writes them
        # first, compose lays a forward surface under them, the composite reads them from the texel whose light it took.
        # Since PT-53 the hair layer's go to outputs of its own (hg0, hg1), which the composite lays over the rest from the
        # texel the hair itself is moved from (hair_guides).
        # Since 2026-10-07 (pt_primary) pt_prepare writes the guides of the hair each pixel's own ray crossed into the same
        # outputs, and there is no layer with outputs of its own.
        self.assertIn("out.g0.t.srv.Get(), out.g1.t.srv.Get()};", tracer)
        self.assertIn("dispatch(list, st.prepare.Get(), {out.g0.uav, out.g1.uav}, b);", tracer)
        self.assertIn("const std::initializer_list<Shared*> writes={&out.a, &out.b, &out.c, &out.g0, &out.g1};", tracer)
        self.assertIn("        traced_guides(p);\n", d3d11)
        self.assertIn("g_guideTexel = int2(p);", d3d11)
        self.assertIn("    const float3 rrSpecular = traced_guides(p, s);\n", self.text(PT + "shaders/pt_compose.hlsl"))
        self.assertIn("extras ? st.guides[0].uav.Get() : nullptr, extras ? st.guides[1].uav.Get() : nullptr, extras ? st.guides[2].uav.Get() : nullptr};", tracer)
        self.assertIn("st.c11->CSSetUnorderedAccessViews(0, 6, u, nullptr);", tracer)
        # Its hit distance is the distance to what is seen behind it, where it lets through more than it reflects.
        spec = self.text(PT + "shaders/pt_specular.hlsl")
        self.assertIn("glassFound ? glassDistance : HIT_DISTANCE_MISS", spec)
        optical = self.text(PT + "shaders/pt_dielectric.hlsli")
        self.assertIn("light += weight * reflected_lamps", optical)
        self.assertIn("light += weight * g.sunColor.rgb * sun_fog(from)", optical)

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
        self.assertIn("ScenePostProcess.Register(ScenePostStage.FrameGenerationCapture, Execute)", hudless)
        self.assertIn("CustomPassInjectionPoint.AfterPostProcess", self.text("src/TGGraphicsBridge/ScenePostProcess.cs"))
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
        self.assertIn("VERSION 1.2.4", cmake)
        for path in re.findall(r"(?:native|tests)/[\w/.-]+\.(?:cpp|hpp|hlsl|in)", cmake):
            self.assertTrue((ROOT / path).is_file(), path)
        self.assertNotIn("resource_tracker.cpp", cmake)
        self.assertIn("tgfg_shader_tests", cmake)
        self.assertIn("interop_shader.hpp.in", cmake)
        self.assertLess(cmake.index("if(TGFG_BUILD_NATIVE)"), cmake.index("tgfg_shader_tests"))
        # MinHook's own install rules stay out of the staged mods: one install rule per mod DLL, and the notices of what
        # TGPathTracing.Native.dll compiles in beside it.
        self.assertEqual(len(re.findall(r"install\(TARGETS [^)]*COMPONENT TGNative\)", cmake)), 5)
        # (The fourth notice is NVAPI's, MIT, installed only by a build with TGPT_MEGA_GEOMETRY, which links its library:
        # PT-109, on trial, OFF by default. A default build's package holds the three it held. The fifth, since
        # 2026-10-11 (docs/agents/megageo-4.md), meshoptimizer's, MIT, with it: its clusterizer is linked in beside NVAPI.)
        self.assertEqual(len(re.findall(r"install\(FILES [^)]*DESTINATION TGPathTracing/Notices [^)]*COMPONENT TGNative\)", cmake)), 5)
        self.assertEqual(cmake.count("COMPONENT TGNative"), 11)
        self.assertIn('  if(TGPT_MEGA_GEOMETRY)\n    # NVAPI\'s library is linked into it (MIT: its notice goes with it).\n    install(FILES "${TG_NVAPI}/License.txt"', cmake)
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

    def test_path_tracing_moving_things_traced_where_they_are(self):
        # PT-71 item 4: since the tracer's own camera rays draw every pixel, a thing the game moves is
        # placed every frame where HDRP draws it then, with where it was the frame before for its own
        # motion, and the scan's copy of it is not traced. The flag that said "moved since the last
        # scan, distrust it" is gone with the reason for it.
        header = self.text(PT + "include/tgpt_api.h")
        self.assertNotIn("TGPT_INSTANCE_MOVING", header)
        self.assertIn("TGPT_EXPORT int32_t TGPT_CALL TGPT_SetMoving(const TGPT_Moving* things, uint32_t count, const uint32_t* materialIndices, uint32_t materialIndexCount,", header)
        self.assertIn('static_assert(sizeof(TGPT_Moving)==116, "TG Path Tracing moving instance ABI");', header)
        for path in ["shaders/pt_common.hlsli", "shaders/pt_primary.hlsli", "src/scene.cpp", "src/tracer.cpp", "src/extension.cpp"]:
            self.assertNotIn("INSTANCE_MOVING", self.text(PT + path), path)
        managed = self.text("src/TGPathTracing/NativeTracer.cs")
        self.assertNotIn("InstanceMoving", managed)
        self.assertIn('setMoving = Bind<SetMovingFn>(module, "TGPT_SetMoving");', managed)
        self.assertIn("Marshal.SizeOf(typeof(NativeMoving)) != 116", managed)
        # A frame is traced with the scene list it was submitted with (the moving things' replaced indices name that list's).
        extension = self.text(PT + "src/extension.cpp")
        self.assertIn("slot.scene=e.scene; slot.moving=e.moving;", extension)
        self.assertNotIn("instancesDirty", extension)
        self.assertIn("st.scene.set_lists(sceneList, moving);", self.text(PT + "src/tracer.cpp"))
        scene = self.text(PT + "src/scene.cpp")
        # Each moving thing an instance of its mesh's own BLAS (no rebuild), with its previous transform's row; the scene's
        # copies it stands in for keep their records (the emitters' table is by record) and lose their place in the TLAS.
        self.assertIn("d.AccelerationStructure=linked ? m->ommBlasVa : m->blasVa;", scene)
        self.assertIn("movingPrevious.insert(movingPrevious.end(), before, before+12);", scene)
        self.assertIn("if(replaced_[source]) {", scene)
        self.assertIn("if(glowHash!=replacedGlowHash_) { replacedGlowHash_=glowHash; emittersDirty_=true; }", scene)
        self.assertNotIn("InstanceRecordGpu{uint32_t(s.mesh), characterBase[i], (alpha ? TGPT_INSTANCE_ALPHA_TEST : 0u)|TGPT_INSTANCE_CHARACTER, 0}", scene)
        # A hit's own motion: a moving thing's point taken back into its own space and forward by the transform it had, as a
        # move added to the hit (round 20: placed anew it carried the roundings of coordinates kilometres from the origin).
        primary = self.text(PT + "shaders/pt_primary.hlsli")
        self.assertIn("} else if (record.previous != INVALID && g.primary.w != INVALID) {", primary)
        self.assertIn("was = position + (mul(before, float4(p, 1.0)) - mul(h.toWorld, float4(p, 1.0)));", primary)
        # A character's (and a swaying cloth's) by each corner's move, a difference of two nearby floats: exactly nothing for
        # what stands still (the town's soldier moved a few hundredths of a pixel, and DLSS dropped its history on him).
        self.assertIn("(asfloat(pb.Load3(idx.x * 12)) - asfloat(cb.Load3(idx.x * 12))) * (1.0 - h.bary.x - h.bary.y) +", primary)
        self.assertNotIn("was = mul(h.toWorld, float4(p, 1.0));", primary)
        self.assertIn("instances[instance].previous != INVALID ? YARD_KIND_MOVING : YARD_KIND_STILL;", primary)
        self.assertIn("k.primary[3]=st.scene.movingPreviousSrv;", self.text(PT + "src/tracer.cpp"))
        # The plugin: placed right before the frame is handed over, every GameObject renderer that is not statically batched,
        # Drake's movable entities and any listed chunk found moving; the scan lists none of them.
        passes = self.text("src/TGPathTracing/HdrpPasses.cs")
        self.assertLess(passes.index("SceneCapture.Follow(tracer, camera);"), passes.index("int result = tracer.SubmitFrame(ref frame);"))
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        self.assertIn("if (!renderer.isPartOfStaticBatch)", capture)
        self.assertNotIn("Moved(renderer.GetInstanceID(), world)", capture)
        self.assertIn("bool placedEveryFrame = MovingRules.PlacedEveryFrame(true, chunk.Has(ref staticHandle), foundMoving);", capture)
        self.assertIn("if (lastInstances == null || listed.Count == 0 || !chunk.DidChange(ref ltwHandle, followVersion)) return false;", capture)
        self.assertIn("var p = previous.Was(identity, m);", capture)
        self.assertEqual(capture.count("if (follow) Place(handle, r.Matrix, list,"), 2)
        rules = self.text("src/TGPathTracing/MovingRules.cs")
        self.assertIn("internal static bool PlacedEveryFrame(bool drake, bool hasStaticTag, bool foundMoving) => (drake && !hasStaticTag) || foundMoving;", rules)
        self.assertIn("internal static long RendererIdentity(int instanceId) => long.MinValue | (uint)instanceId;", rules)
        self.assertIn('<Compile Include="..\\src\\TGPathTracing\\MovingRules.cs" Link="MovingRules.cs" />', self.text("tests/MovingRules.Tests.csproj"))
        gpu = self.text("tests/pathtracing_gpu_test.cpp")
        for check in ['"a moving thing is traced where HDRP draws it this frame"', '"the scan\'s copy of a moving thing is not traced where it was"',
                      '"a moving thing\'s pixels move by its own previous transform, as HDRP\'s motion vectors"',
                      '"a hidden moving thing and its scan\'s copy are traced nowhere"']:
            self.assertIn(check, gpu)

    def test_path_tracing_emissive_lights(self):
        # Emissive surfaces as ReSTIR DI's lights (the Emissive Lights row, on by default): the option, flags and view as the
        # coordinator assigned them, mirrored in C#.
        header = self.text(PT + "include/tgpt_api.h")
        for token in ["#define TGPT_OPTION_EMISSIVE_LIGHTS 131072u", "#define TGPT_MATERIAL_EMISSIVE_ALBEDO 128u",
                      "#define TGPT_VIEW_EMISSIVE 10u", "#define TGPT_VIEW_COUNT 12u"]:
            self.assertIn(token, header)
        managed = self.text("src/TGPathTracing/NativeTracer.cs")
        for token in ["internal const uint OptionEmissiveLights = 131072;", "internal const uint MaterialEmissiveAlbedo = 128;", "Emissive = 10, Yardstick = 11 }"]:
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
        # (The emission where the ray met the surface; its direct light where a walk inside a subsurface material comes out: PT-40.)
        self.assertIn("const float3 emitted = path_emission(hit, m.emissive, from, gathers);", common)
        self.assertIn("radiance += throughput * (emitted + direct_diffuse(hit.position, hit.normal, m.normal, m.reflectance, offset, rng, m.through, m.tint));", common)
        self.assertIn("from = hit.position; gathers = true;", common)
        # its definition, shade_path's use, the radiance cache's samples, which gather for the vertex they leave, and what a
        # reflection shows (shade_seen, PT-46: weighed for the reflection's own origin, as shade_path's first vertex)
        self.assertEqual(len(re.findall(r"\bpath_emission\(", common)), 4)
        self.assertIn("float3 radiance = path_emission(hit, m.emissive, g_pathFrom, g_pathGathers);", common.split("float3 shade_seen(", 1)[1])
        # (times the tint of thin cloth the cache's ray passed on its way: trace_bounce, PT-39)
        self.assertIn("const float3 emitted = path_emission(next, m.emissive, hit.position, true);", common)
        self.assertIn("return interfaces + tint * (emitted + direct_diffuse(next.position, next.normal, m.normal, m.reflectance, nextOffset, rng, m.through, m.tint) +", common)
        self.assertIn("g_pathFrom = RAB_GetSurfaceWorldPos(surface); g_pathGathers = true;", self.text(PT + "shaders/pt_gi.hlsl"))
        self.assertIn("g_pathFrom = s.position; g_pathGathers = false;", self.text(PT + "shaders/pt_specular.hlsl"))
        rtxdi = self.text(PT + "shaders/pt_rtxdi.hlsli")
        self.assertIn("if (dot(s.normal, ls.dir) > 0.0 && !ls.emitter && !ls.diffuseOnly)\n        specular = ls.irradiance * (s.hair ? hair_specular(s, surface.view, ls.dir) : ggx_specular(", rtxdi.replace("\r\n", "\n"))
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
        # Emitters: the scene list's instances only (not those a moving thing stands in for, not the moving things, not
        # stand-ins, not characters), brightest first, within a triangle budget; the records' pass is built and dispatched.
        scene = self.text(PT + "src/scene.cpp")
        self.assertIn("if((inst.flags&TGPT_INSTANCE_STAND_IN) || (sources[r]<replaced_.size() && replaced_[sources[r]])) continue;", scene)
        self.assertIn("if(!m || m->skinned || m->data.geometryClusters.size()!=m->data.geometries.size()+1) continue;", scene)
        self.assertIn("if(triangles+c.triangles>kEmitterBudget) { ++overBudget; continue; }", scene)
        self.assertIn("constexpr uint32_t kEmitterBudget=1u<<18;", self.text(PT + "src/scene.hpp"))
        self.assertIn("emissive:pt_emissive", self.text("CMakeLists.txt"))
        self.assertIn('#include "pt_emissive.h"', self.text(PT + "shaders/pt_shaders.hpp.in"))
        # HDRP's own shaders glow by _EmissiveColor whatever the Baked Emission flag (EmissiveIsBlack) says: read as "no glow",
        # no emissive submesh was ever traced in game (every harness run to 2026-10-06; the mineral cave's crystals, PT-34).
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        # (PT-64: and the game's own glowing graphs, whatever their baked flag: GraphGlow.)
        self.assertIn('if (m.HasProperty("_EmissiveColor") && (!notBaked || glow != null || shaderName.StartsWith("HDRP/", StringComparison.Ordinal)))', capture)
        self.assertNotIn('if (m.HasProperty("_EmissiveColor") && (m.globalIlluminationFlags & MaterialGlobalIlluminationFlags.EmissiveIsBlack) == 0)', capture)
        # A glowing surface a kept light stands for (PT-47: the light the game placed at a crystal) is that light's own surface
        # however far the light sits from it (TGPT_MATERIAL_OWN_SURFACE on the instance's emissive materials), never drawn as a
        # light: its light is not counted twice.
        self.assertIn("#define TGPT_MATERIAL_OWN_SURFACE 131072u", header)
        self.assertIn("internal const uint MaterialOwnSurface = 131072;", managed)
        self.assertIn("#define MATERIAL_OWN_SURFACE 131072", common)
        self.assertIn("if ((m.flags & MATERIAL_OWN_SURFACE) != 0) flags |= EMITTER_PAIRED;", emissive)
        self.assertIn("const bool sampled=emissiveLights && !(materials_[c.material].flags&TGPT_MATERIAL_OWN_SURFACE) &&", scene)
        self.assertIn("if (mm.EmissiveR + mm.EmissiveG + mm.EmissiveB > 0f) indices[(int)first + i] = OwnSurfaceVariant(material);", capture)
        self.assertIn("if (c.glow && c.thing.Kind == LightRules.GlowSurface) own.Add(c.thing.Key);", self.text("src/TGPathTracing/SceneLights.cs"))
        self.assertIn('"a glowing surface marked as a kept light\'s own adds no light of its own, however far the light sits from it"', self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn("st.scene.record_emitters(list, st.emissive.Get());", self.text(PT + "src/tracer.cpp"))
        # HDRP's emission as the camera sees it: its exposure weight and Albedo Affects Emissive. (What the game moves is placed
        # every frame and met by paths, not sampled: test_path_tracing_moving_things_traced_where_they_are.)
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        # (PT-64: the game's own glowing graphs weigh by the exposure itself, GraphExposure.)
        for token in ['exposureWeight = glow != null ? GraphExposure : m.HasProperty("_EmissiveExposureWeight") ? Mathf.Clamp01(m.GetFloat("_EmissiveExposureWeight")) : 1f;',
                      "return weight >= 0.999f ? 1f : Mathf.Lerp(1f / Mathf.Max(CurrentExposure(), 1e-12f), 1f, weight);",
                      "if (weight == GraphExposure) weight = Mathf.Clamp01(CurrentExposure()); // the game's graphs: the exposure itself is the weight",
                      'if (m.HasProperty("_AlbedoAffectEmissive") && m.GetFloat("_AlbedoAffectEmissive") > 0.5f) mat.Flags |= Tgpt.MaterialEmissiveAlbedo;']:
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

    def test_path_tracing_no_limits_that_change_the_picture(self):
        # PT-50.
        common = self.text(PT + "shaders/pt_common.hlsli")
        lights = self.text("src/TGPathTracing/SceneLights.cs")
        # Lights: no count of 1,024, no reach past which a light stops (only an effect's light keeps HDRP's window), and the
        # far lists sample every light its cell does not list, each light counted once.
        self.assertIn("if (!IsEffectLight(light)) n.Flags |= Tgpt.LightNoRange;", lights)
        self.assertIn("private const int MaxLights = 65536;", lights)
        self.assertIn("constexpr uint32_t kMaxLights=65536;", self.text(PT + "src/runtime.hpp"))
        self.assertIn("#define TGPT_LIGHT_NO_RANGE 0x80000u", self.text(PT + "include/tgpt_api.h"))
        self.assertIn("const float window = (l.flags & LIGHT_NO_RANGE) != 0u ? 1.0 : smooth_window(", common)
        self.assertIn("invPdf = cell_lists(s.first, s.count, i) ? 0.0 : invPdf * float(s.candidates) / float(s.far);", common)
        # (Hair took its lights so too while it was a layer, pt_hair.hlsl; since 2026-10-07 it is a surface of the picture and
        # takes them as every other, through pt_di.)
        self.assertIn("lists_candidate(lists, c, u, invPdf)", self.text(PT + "shaders/pt_di.hlsl"))
        self.assertEqual(common.count("lists_candidate(lists, c, u, invPdf)"), 1)
        scene = self.text(PT + "src/scene.cpp")
        # The far lists hold every such light whatever its range, and are kept from build to build (9 ms of the render thread
        # with the game's 985 lights, on the quarter of the frames that make the grid anew): made again only when a light
        # came, went, changed its kind or moved 4 m.
        self.assertIn("if(usable(l)!=f.usable || falls_off(l)!=f.fallsOff) stale=true;", scene)
        self.assertIn("stale=moved>kFarStale*kFarStale;", scene)
        # (They follow the cells' entries in the buffer the passes read, copied there on the GPU from an upload of their
        # own that is written when they are made: upload_light_lists, PT-99. Until 2026-10-12 they were appended to the
        # cells' entries and uploaded with them at every build.)
        self.assertIn("if(farCount) list->CopyBufferRegion(target.buffer.resource.Get(), uint64_t(own)*stride, farUpload_.resource.Get(), 0, uint64_t(farCount)*stride);", scene)
        self.assertIn("farUploadDirty_=true; // (upload_light_lists: the far lists' own upload is made anew)", scene)
        self.assertNotIn("gridIndices_.insert(", scene)
        self.assertNotIn("farthest", scene)
        # No radiance clamps, no 25 cm floor, no caustic focus cap; NRD's overflow is added back, not cut.
        shaders = "".join(self.text(PT + "shaders/" + f.name) for f in (ROOT / PT / "shaders").iterdir() if f.suffix in (".hlsl", ".hlsli"))
        for gone in ("clamp_radiance", "LIMIT_DIRECT", "LIMIT_REFLECTION", "LIMIT_INDIRECT", "CAUSTIC_MAX", "near / 0.0625", "HAIR_REACH", "HDRP_SHADOW_RANGE"):
            self.assertNotIn(gone, shaders, gone)
        self.assertIn("if (handed < 1.0 && all(traced < 1e30)) bounced += traced * (1.0 - handed);", self.text(PT + "shaders/pt_compose.hlsl"))
        self.assertIn("if (channel_max(light) > ceiling) { handed = ceiling / channel_max(light); light *= handed; }", self.text(PT + "shaders/pt_nrd_prep.hlsl"))
        # What is traced at all: characters, grass and swaying cloth as far as the traced scene reaches.
        self.assertNotIn("GrassDistance", self.text("src/TGPathTracing/LeshyCapture.cs"))
        self.assertNotIn("kSwayReach", self.text(PT + "src/scene.hpp"))
        self.assertIn("constexpr uint32_t kMaxSwayCopies=4096, kSwayVertexBudget=1u<<24;", self.text(PT + "src/scene.hpp"))
        self.assertIn("if (!deferred) {", self.text(PT + "shaders/pt_prepare.hlsl"))
        self.assertNotIn("SHEETS", self.text(PT + "shaders/pt_specular.hlsl"))
        self.assertIn("kAlphaAtlasHeight=4*kAtlasSize", self.text(PT + "src/runtime.hpp"))
        harness = self.text("tests/pathtracing_gpu_test.cpp")
        for check in ("\"a lamp's light goes on past the light grid's reach for it (the far lists): the same light as listed near everywhere\"",
                      '"the 1,101st light is traced (no cap of 1,024)"',
                      '"the far lists follow a light that changes its kind (kept from grid to grid otherwise)"',
                      '"alpha-tested texture registered for the alpha atlas, the 301st (past the old 256)"',
                      '"the 128th character is traced (PT-50: no cap of 64 renderers or 524,288 vertices)"',
                      # The clamps' own case, a night lit by candles standing on the floor (nrdStudy): the same light with and
                      # without the old option. (NRD's limit is not reached there after ReSTIR's reuse: the line above alone
                      # holds the composite to adding back what NRD cannot hold.)
                      "\"no radiance clamp is left: the candle-lit room holds the same light with and without the old clamps' option\"",
                      '"NRD\'s picture of the candle-lit room holds the light the built-in denoiser\'s does"'):
            self.assertIn(check, harness)
        # The caps that stay are buffer sizes or another struct's, each with a log line that says when a scene meets it: the
        # evidence for raising it (docs/agents/caps.md).
        self.assertIn('Session.Log.LogInfo("Alpha atlas full: "', self.text("src/TGPathTracing/SceneCapture.cs"))
        self.assertIn('" water surfaces within the traced scene; the nearest "', self.text("src/TGPathTracing/WaterCaustics.cs"))
        self.assertIn('" submeshes; the tracer takes the first "', self.text("src/TGPathTracing/CharacterCapture.cs"))
        # (The log says where a glass's index of refraction comes from: its own _Ior where its shader refracts by it, else the
        # index of the 4 % reflection its shader shows, with the unused _Ior named.)
        self.assertIn('optics.Ior.ToString("0.###") + (refracts && m.HasProperty("_Ior") ? " (material)" : IsCornea(m) ?', self.text("src/TGPathTracing/GlassFinish.cs"))

    def test_traced_world_arrives_by_the_clock(self):
        # PT-98. The tracer ran at 5 to 10 frames a second, and whatever was counted by the frame or
        # by the scan came 6 to 12 times later than at 60. What a frame takes in goes by the clock.
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        # Meshes are registered for a share of the frame's time, counted after the frame's first mesh: the first call into
        # Unity waits for the render thread, which at 8 frames a second ate the whole share (so it was still 6 a frame).
        self.assertIn("for (int n = 0; (n < MeshesPerFrame || meshClock.Elapsed.TotalMilliseconds - spentBefore < allowed) && meshQueue.Count > 0; n++)", capture)
        self.assertIn("if (registeredNow++ == 0) { firstMs = meshClock.Elapsed.TotalMilliseconds; if (!asBefore) spentBefore = firstMs; }", capture)
        # A registered mesh's instances come with the next scan: that scan comes soon while any wait, and scans take a
        # quarter of the time at most.
        self.assertIn("bool toPlace = !asBefore && Arrival.ToPlace > 0 && now >= lastScan + Mathf.Max(ArrivalScanInterval, ArrivalScanRest * lastScanSeconds);", capture)
        self.assertIn("if (now < nextScan && !moved && !toPlace)", capture)
        # For measuring, the plugin's three rules can be put back (hidden, 0 by default: by the clock), and a change starts
        # the traced scene over, so that the recipe `arrival` shows one scene's world arrive both ways in one run.
        self.assertIn('ArrivalAsBefore = file.Bind(Advanced, "Arrival as before", 0, Hidden(', self.text("src/TGPathTracing/Settings.cs"))
        self.assertIn("private static bool ArrivalAsBefore => Session.Settings != null && Session.Settings.ArrivalAsBefore.Value != 0;", capture)
        self.assertIn('Arrival.SceneCleared("the hidden \\"Arrival as before\\" changed and the traced scene started over");', self.text("src/TGPathTracing/Session.cs"))
        recipe = json.loads(self.text("tests/ingame/scenarios.json"))["recipes"]["arrival"]
        ways = [s["value"] for step in recipe for s in step["set"] if s.get("key") == "Arrival as before"]
        self.assertEqual(ways, ["1", "0", "1", "0"])  # before, by the clock, before again (the order's own share), and left at 0
        # The textures are looked at between scans too (a swap to a sharp texture is followed within a quarter second).
        self.assertIn("TextureShare.Refresh(materialList, materialsUploaded, Session.Tracer, false);", capture)
        self.assertIn("private const float TextureLookInterval = 0.25f;", capture)
        # The native side's budgets "a frame" are multiplied by the frames of 60 a second the last frame's time holds.
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("st.scene.set_clock(frame_clock(started));", tracer)
        self.assertIn("frameClock=last.time_since_epoch().count() ? std::clamp(std::chrono::duration<double>(now-last).count()*60.0, 1.0, kClockMost) : 1.0;", tracer)
        for queue, a_frame in [("newTextures", 24), ("newHairBakes", 8), ("newHairLobeBakes", 8), ("newEyeBakes", 4), ("newCookies", 8), ("newAlphaTextures", 8)]:
            self.assertIn(f"std::min<size_t>(e.{queue}.size(), by_clock({a_frame}));", tracer, queue)
        scene = self.text(PT + "src/scene.cpp")
        self.assertIn("const uint64_t builds=by_clock(kBuildsPerFrame), buildTriangles=uint64_t(double(kTrianglesPerFrame)*std::min(clock_, kMemoryClockMost));", scene)
        self.assertIn("const uint64_t readBudget=uint64_t(double(kReadBudget)*std::min(clock_, kMemoryClockMost));", scene)
        self.assertIn("if(builds>=by_clock(kPlantBuildsPerFrame) ||", scene)
        # A mesh's read-back is mapped by the clock too, not only two frames later.
        self.assertIn("(frame>mesh->copiedFrame && std::chrono::duration<double, std::milli>(now-mesh->copiedAt).count()>=kReadMapMs)", scene)
        # No budget leaves a thing out: what does not fit a frame stays in its queue (the checks above are of `<` and `>=`
        # on counts of a queue, never a cap on the queue itself).
        self.assertNotIn("pending_.resize(", scene)
        # What is still to come is counted where it waits, and the notice says all of it (it said meshes only).
        header = self.text(PT + "include/tgpt_api.h")
        self.assertIn("uint32_t tilesToBake;", header)
        self.assertIn("uint32_t instancesWaiting;", header)
        self.assertIn("e.status.instancesWaiting=s.instancesWaiting; e.status.plantsWaiting=s.plantWaiting;", tracer)
        # The status record grew at its end with TGPT_ABI as it was: either side still works with the other's older build.
        self.assertIn("(out->size!=sizeof(TGPT_Status) && out->size!=TGPT_STATUS_SIZE_BEFORE_ARRIVAL)", self.text(PT + "src/extension.cpp"))
        self.assertIn("if (whole != -1) return false;", self.text("src/TGPathTracing/NativeTracer.cs"))
        session = self.text("src/TGPathTracing/Session.cs")
        self.assertIn('Notices.Show("tgpt.building", "TG Path Tracing", Arrival.NoticeText, 1f);', session)
        self.assertNotIn("meshes to go)", session)
        arrival = self.text("src/TGPathTracing/Arrival.cs")
        self.assertIn("ToCome = queue[ToRegister] + Mathf.Max(queue[ToBuild], queue[ToPlaceQ]) + queue[Characters] + queue[KeptBack] + queue[Loading] + queue[Tiles];", arrival)
        self.assertIn("if (!Owed() && ToCome <= 0 && scans > 0) { Finish(now); return; }", arrival)
        self.assertIn('"Arrival whole after "', arrival)

    def test_kept_numbers_cannot_go_stale(self):
        # PT-125 (2026-10-12: a place in a buffer that is laid out anew every frame, kept across frames, reset the GPU),
        # and what an audit by reading found of the same kind (docs/agents/stale.md).
        sway = self.text("native/pathtracing/shaders/pt_sway.hlsl")
        # PT-125 itself: a vertex keeps its range's number WITHIN ITS GROUP, and a number past the group's ranges is none.
        self.assertIn("data.Store(grp.rangeAt + (m.vertexBase + v) * 4u, lo);", sway)
        self.assertNotIn("data.Store(grp.rangeAt + (m.vertexBase + v) * 4u, grp.rangeFirst + lo);", sway)
        self.assertIn("if (rangeLocal < grp.rangeCount) {", sway)
        scene = self.text("native/pathtracing/src/scene.cpp")
        decals = scene[scene.index("void GpuScene::upload_decals("):scene.index("void GpuScene::clear(")]
        # F1: a mesh decal's own top level names its mesh's bottom level by its ADDRESS, which moves when the mesh is
        # compacted. The address it was built with is checked against the mesh's every frame, BEFORE the early way out.
        check = "mesh->blasVa!=cache.instance.AccelerationStructure) { moved=true; break; }"
        early = "if(!decalsDirty_ && !moved && generation==decalShareGeneration_"
        self.assertIn(check, decals)
        self.assertLess(decals.index(check), decals.index(early))
        self.assertIn("d.reserved[2]=cache.srv; cache.mesh=handle;", decals)
        self.assertIn("decalMeshes_[i].mesh=0;", decals)
        # F2: the emitters' instance table is by this frame's record numbers, which move when plants' groups are made:
        # laid out again whenever the frame's records are not the ones it was laid out for.
        emitters = scene[scene.index("void GpuScene::build_emitters("):scene.index("void GpuScene::record_emitters(")]
        self.assertIn("if(!rebuild && !emitterPlaces_.empty() && sources!=emitterTableSources_) {", emitters)
        self.assertIn("emitterPlaces_.push_back(EmitterPlace{sources[c.record], c.geometry, first[i], uint32_t(c.mesh->data.geometries.size())});", emitters)
        self.assertLess(emitters.index("sources!=emitterTableSources_"), emitters.index("if(!rewrite) return;"))
        # F3: a mesh let go gives back every view it was given (its tangents' and decal colors' stayed until 2026-10-12).
        retire = scene[scene.index("void GpuScene::retire_meshes("):]
        for view in ("positionsSrv", "uvsSrv", "colorsSrv", "indicesSrv", "normalsSrv", "uvs2Srv", "tangentsSrv", "decalColorsSrv"):
            self.assertIn("m->" + view, retire[:retire.index("heap.release(srv);")])
        # The offline checks of the first two (docs/agents/fixt.md): tgpt_gpu_test's TGPT_TEST_PLANT_SHIFT_ONLY (a group of
        # plants keeps its pose when another comes and goes before it in the frame's order) and TGPT_TEST_DECAL_MOVE_ONLY
        # (a mesh decal's top level follows its mesh's structure when that is compacted in a quiet frame). Each alone,
        # right after the warm-up, before anything the whole test runs, and each ends at its first failed stage: on a
        # tracer with the fault the stages after would read past the frame's records, or through memory given back.
        gpu = self.text("tests/pathtracing_gpu_test.cpp")
        for switch, name in (("TGPT_TEST_PLANT_SHIFT_ONLY", "pathtracing_plant_shift_checks.inl"),
                             ("TGPT_TEST_DECAL_MOVE_ONLY", "pathtracing_decal_move_checks.inl")):
            wired = 'if(GetEnvironmentVariableA("' + switch + '", nullptr, 0)>0)\n#include "' + name + '"'
            self.assertIn(wired, gpu)
            self.assertLess(gpu.index(wired), gpu.index('#include "pathtracing_sky_checks.inl"'))
            self.assertIn("The run ends here", self.text("tests/" + name))
        # What they read, the scene says under the test's own switch, and saying is all that switch does: it is read
        # once a frame, and each of its two blocks ends in a log line and records nothing.
        self.assertEqual(scene.count('orderTest=GetEnvironmentVariableA("TGPT_TEST_SCENE_ORDER", nullptr, 0)>0;'), 1)
        blocks = scene.split("if(orderTest) {")[1:]
        self.assertEqual(len(blocks), 2)
        for block in blocks:
            said = block[:block.index("if(said!=told) { told=said; log(said); }")]
            for word in ("list->", "device->", "upload_", "heap.", "release.", "barriers.", "Dirty_", "=true;", "moved"):
                self.assertNotIn(word, said)

    def test_ingame_harness(self):
        # The in-game test runner (src/TGPathTracing.Harness, docs/PT-HARNESS.md) is a developer's tool: no release zip
        # names it, and it does nothing unless a request file exists when the game starts.
        self.assertNotIn("Harness", self.text("tools/Package.Common.ps1"))
        self.assertNotIn("Harness", self.text("Package.ps1"))
        self.assertNotIn("Harness", self.text("Build.ps1"))
        plugin = self.text("src/TGPathTracing.Harness/Plugin.cs")
        self.assertIn('if (!File.Exists(path)) { Logger.LogInfo(Name + ": no request, so it does nothing this session."); return; }', plugin)
        self.assertLess(plugin.index("if (!File.Exists(path))"), plugin.index("Runner.Start("))
        # A request is taken once: the next start plays as normal.
        self.assertIn("File.Move(path, taken);", plugin)
        project = self.text("src/TGPathTracing.Harness/TGPathTracing.Harness.csproj")
        self.assertNotIn("ProjectReference", project)
        self.assertEqual(ET.fromstring(project).find(".//TargetFrameworks").text, "net472")
        # Nothing is saved during a run: the game's own blocker and a prefix on LoadSave.Save, and the driver compares
        # the save files before and after.
        game = self.text("src/TGPathTracing.Harness/Game.cs")
        self.assertIn('AccessTools.Method(typeof(LoadSave), "Save", new[] { typeof(SaveSlot), typeof(bool) })', game)
        self.assertIn("World.Add(new SaveBlocker(BlockerId));", game)
        driver = self.text("tools/harness/Run-PtHarness.ps1")
        self.assertIn("$changed = @(Compare-Object -ReferenceObject $savesBefore -DifferenceObject $savesAfter)", driver)
        # The player's configs come back after a run, also after one that was cut short.
        self.assertIn("Restore-Configs", driver)
        self.assertIn("[void](Set-ConfigValue (Join-Path $configDir 'com.wessberg.tgpathtracing.cfg') 'Path Tracing' 'Mode' 'Off')", driver)
        # It drives the installed TG Path Tracing by the names of its config keys and internal members: the tree still
        # has every one it reads (Traced.cs, Runner.cs).
        settings = self.text("src/TGPathTracing/Settings.cs")
        for key in ['"Mode"', '"Denoising"']:
            self.assertIn("file.Bind(Section, " + key, settings)
        for name in ["None", "NRD", "Ray Regeneration", "Ray Reconstruction"]:
            self.assertIn('"' + name + '"', settings)
        session = self.text("src/TGPathTracing/Session.cs")
        for member in ["internal static NativeTracer Tracer;", "internal static NativeStatus Status;", "internal static bool HaveStatus;"]:
            self.assertIn(member, session)
        tracer = self.text("src/TGPathTracing/NativeTracer.cs")
        status = tracer[tracer.index("internal struct NativeStatus"):tracer.index("internal struct NativeGuides")]
        for field in ["GpuMs", "FrameMs", "D3d11Ms", "RenderWidth", "RenderHeight", "Composited", "MeshesQueued", "LastError", "FramesTraced",
                      "FramesFailed", "Features", "Disabled", "Supported"]:
            self.assertRegex(status, r"\b" + field + r"\b", field)
        self.assertIn("internal string StageTimes()", tracer)
        for constant in ["FeatureNrdActive", "FeatureRayRegenerationActive"]:
            self.assertRegex(tracer, r"internal const uint [^;]*\b" + constant + r" = \d+")
        passes = self.text("src/TGPathTracing/HdrpPasses.cs")
        for member in ["internal static string ExposureText", "internal static float TracedGain", "internal static bool Failed", "internal static string Detail"]:
            self.assertIn(member, passes)
        self.assertIn("internal static bool Active", self.text("src/TGPathTracing/RayReconstruction.cs"))
        self.assertIn("internal static string Summary", self.text("src/TGPathTracing/SceneCapture.cs"))
        self.assertIn("internal static string Summary", self.text("src/TGPathTracing/CharacterCapture.cs"))
        lights = self.text("src/TGPathTracing/SceneLights.cs")
        self.assertIn("internal static bool Interior", lights)
        self.assertRegex(lights, r"internal static int Count\b")
        # PT-98: a traced step's pictures are taken when the traced world is whole, by the plugin's own word for it
        # (Arrival), not by the native queue of meshes alone (empty between two waves, and blind to instances, textures
        # and characters: run 20261009-150909 took a town's pictures with nothing traced on 62 % of the screen).
        arrival = self.text("src/TGPathTracing/Arrival.cs")
        for member in ["internal static bool Whole", "internal static int ToCome", "internal static float LastSeconds", "internal static string NoticeText"]:
            self.assertIn(member, arrival)
        runner = self.text("src/TGPathTracing.Harness/Runner.cs")
        self.assertIn('bool whole = arrival ? Traced.Flag("Arrival", "Whole") : Traced.Status("MeshesQueued") == 0;', runner)
        self.assertIn("bool settled = step.traced ? on && whole : !on;", runner)
        # Every scenario names a save and a recipe the plan has, and its region lies on the screen.
        plan = json.loads(self.text("tests/ingame/scenarios.json"))
        self.assertIn(plan["defaults"]["recipe"], plan["recipes"])
        keys = [s["key"] for s in plan["scenarios"]]
        self.assertEqual(len(keys), len(set(keys)))
        for scenario in plan["scenarios"]:
            # Scenario save names match the game save menu; the where field describes the camera position.
            self.assertTrue(scenario["save"].startswith("PT ") and scenario["tests"] and scenario["where"], scenario["key"])
            self.assertIn(scenario.get("recipe", plan["defaults"]["recipe"]), plan["recipes"], scenario["key"])
            left, top, right, bottom = scenario["roi"]
            self.assertTrue(0 <= left < right <= 1 and 0 <= top < bottom <= 1, scenario["key"])
        for name, steps in plan["recipes"].items():
            # A recipe starts and ends with path tracing off: the first frames are the game's own, and the next load is too.
            self.assertEqual([p["value"] for p in steps[0]["set"] if p["key"] == "Mode"], ["Off"], name)
            self.assertEqual([p["value"] for p in steps[-1]["set"] if p["key"] == "Mode"], ["Off"], name)
        targets = json.loads(self.text("tests/ingame/targets.json"))
        for section in ["performance", "restore", "stability"]:
            self.assertIn(section, targets)

    def test_own_camera_rays_and_hair_as_a_surface(self):
        # PT-57 and PT-59. Hair is no layer added after the denoiser any more: the pixel's own camera
        # ray stops at a card with the chance its texel covers, a new draw every frame, and what it
        # stops at is the pixel's surface. And so for every pixel: its surface is what its own ray
        # finds in the traced scene (pt_primary primary_all); HDRP's G-buffer is the route before it
        # (TGPT_OPTION_GBUFFER_SURFACES) and what the yardstick compares the hit with.
        shaders = ROOT / PT / "shaders"
        self.assertFalse((shaders / "pt_hair.hlsl").exists(), "the hair layer's pass is gone")
        primary = self.text(PT + "shaders/pt_primary.hlsli")
        prepare = self.text(PT + "shaders/pt_prepare.hlsl")
        common = self.text(PT + "shaders/pt_common.hlsli")
        composite = self.text(PT + "shaders/d3d11.hlsl")
        tracer = self.text(PT + "src/tracer.cpp")
        header = self.text(PT + "include/tgpt_api.h")
        gpu = self.text("tests/pathtracing_gpu_test.cpp")
        self.assertIn("if (ray_chance(inst, geo, prim, PRIMARY_SALT) < c) { cover = c; hair = true; q.CommitNonOpaqueTriangleHit(); }", primary)
        self.assertIn("if (own_rays()) { primary_all(p, depth); return; }", prepare)
        self.assertIn("if (primary_pixel(p, depth, deferred)) return;", prepare)
        # The whole scene as the camera draws it: no shadow-only stand-in, no back of a one-sided surface; a cut-out by chance.
        self.assertIn("(!q.CandidateTriangleFrontFace() && (m.flags & (MATERIAL_DOUBLE_SIDED | MATERIAL_GLASS)) == 0))) continue;", primary)
        self.assertIn("if (!originalCutout && (m.flags & MATERIAL_ALPHA_TILE) != 0 && (!g_cameraRays || g_primaryRays)) return ray_chance(instanceId, geometry, primitive, 0x616C7068u) < a;", common)
        self.assertIn("if (shared_cutout(m)) a *= shared_sample(m.colorTexture, st, 0.0, 0.0).a;", common)
        self.assertIn("return a >= (!originalCutout && (m.flags & MATERIAL_ALPHA_TILE) != 0 ? 0.5 : m.alphaCutoff);", common)
        # The record keeps HDRP's distance at the pixel (the composite places the light by HDRP's depth of the frame shown).
        self.assertIn("surf[p] = float4(h.hit.position, distance);", primary)
        self.assertIn("surf[p] = float4(hit.position, glass ? length(hit.position - g.cameraPos.xyz) : distance);", primary)
        # Nothing of the layer is left in the composite; a pixel of the G-buffer takes the texel the tracer's own ray made.
        for gone in ("hair_at(", "hair_guides(", "hairLight", "hairViewProj"):
            self.assertNotIn(gone, composite.replace("found again each frame by where its points had moved to: hair_at.", ""), gone)
        self.assertNotIn("hairPass", tracer)
        self.assertNotIn("out.hl", tracer)
        # Its own motion (a character's by its previous skinned positions) for the tracer's reuse, filters and NRD.
        self.assertIn("if (own_rays()) { RWTexture2D<float4> own = ResourceDescriptorHeap[g.primary.x]; return own[p].xy; }", common)  # (z: the depth's change, PT-127)
        self.assertIn("previous.Store3(id.x * 12u, pass.b.y == 1u ? asuint(p) : positions.Load3(id.x * 12u));", self.text(PT + "shaders/pt_skin.hlsl"))
        for name in ("pt_rtxdi.hlsli", "pt_temporal.hlsl", "pt_nrd_prep.hlsl"):
            self.assertIn("motion_at(p)", self.text(PT + "shaders/" + name), name)
        # The yardstick: a view, counters, a log line and numbers for the plugin, which names the materials.
        self.assertIn("#define TGPT_VIEW_YARDSTICK 11u", header)
        self.assertIn("#define TGPT_OPTION_GBUFFER_SURFACES 2147483648u", header)
        self.assertIn("#define OPTION_GBUFFER_SURFACES 0x80000000u", common)
        self.assertIn('static_assert(sizeof(TGPT_Yardstick)==632, "TG Path Tracing yardstick ABI");', header)
        self.assertIn("TGPT_EXPORT int32_t TGPT_CALL TGPT_GetYardstick(TGPT_Yardstick* yardstick);", header)
        self.assertIn("write_yardstick(p, yardstick_code(p, depth, w.stopped, w.stop.t, own, motion.xy, w.stop.instance, w.stopped && w.hair, material));", primary)
        # Six counters a material since PT-64: its pixels, those that differ, and how (place, colour, normal, roughness).
        self.assertIn("static const uint YARD_COUNTERS = 64u, YARD_MATERIALS = 4096u, YARD_PER_MATERIAL = 6u;", common)
        self.assertIn("constexpr uint32_t kYardCounters=64, kYardMaterials=4096, kYardPerMaterial=6, kYardCount=kYardCounters+kYardPerMaterial*kYardMaterials;", tracer)
        self.assertIn("struct { uint32_t material, pixels, differing, place, colour, normal, roughness; } materials[TGPT_YARDSTICK_MATERIALS];", header)
        managed = self.text("src/TGPathTracing/NativeTracer.cs")
        self.assertIn("internal const uint OptionGBufferSurfaces = 2147483648;", managed)
        self.assertIn("sizeof(NativeYardstick) != 632", managed)
        self.assertIn("(OwnCameraRays.Value ? 0u : Tgpt.OptionGBufferSurfaces) |", self.text("src/TGPathTracing/Settings.cs"))
        self.assertIn('"Yardstick 10s (the tracer\'s own first hit against the game\'s G-buffer; the picture from "', self.text("src/TGPathTracing/HdrpPasses.cs"))
        # The plugin: hair hidden from HDRP while tracing, as brows are (PT-112 A: HDRP draws no opaque object; the tracer
        # writes the frame's depth and motion).
        characters = self.text("src/TGPathTracing/CharacterCapture.cs")
        self.assertNotIn("HoldLayerDepth(c.renderer", characters)
        self.assertNotIn("HideRenderer(manager, c.renderer, materials, n);", characters)
        for check in ['"hair in the drawn depth stays where it is while the camera moves (PT-53: its own depth and motion, not the wall\'s behind it)"',
                      '"the same hair hidden from the drawn depth stays where it is too (nothing places the traced light by HDRP\'s motion vectors)"',
                      '"cards that cover part of a pixel stop its ray as often as they cover it (the game\'s blend on average, in every frame)"',
                      '"hair before a surface of the G-buffer shows as hair in the picture (the pixel takes its own ray\'s light)"',
                      '"the yardstick finds what only the traced scene has (the tracer\'s hit nearer)"',
                      '"the yardstick finds what the traced scene lacks (the tracer\'s hit farther)"',
                      '"a character\'s pixels move by its own previous positions (the skinning\'s of the frame before)"',
                      '"a still world\'s own motion under a moving camera is the drawn motion vectors\'"',
                      '"the picture from the tracer\'s own camera rays is as bright as the one from the drawn G-buffer where the two agree"']:
            self.assertIn(check, gpu)

    def test_path_tracing_edges_carry_the_shift(self):
        # PT-84. The game's upscaler is told a new sub-pixel shift every frame and lays each frame's
        # samples down where they were taken. So the traced picture a frame hands it must be that
        # frame's own, sampled at that frame's shift. Two things made it otherwise: A. The camera's
        # rays carried no shift at all: the plugin read HDRP's matrices when its capture pass
        # executed, after HDRP (while still RECORDING the frame) had taken the shift out of its camera
        # again. It keeps them from where HDRP records its opaque passes now (armedView), and says
        # once a session whether they hold the shift.
        passes = self.text("src/TGPathTracing/HdrpPasses.cs")
        self.assertIn("armedView = hdCamera.mainViewConstants;", passes)
        self.assertIn("var view = armedView;", passes)
        self.assertNotIn("var view = hd.mainViewConstants;", passes)
        self.assertIn("SayShift(hd, in view);", passes)
        #    And the tracer holds on to it itself: what the handed matrices hold is measured against the told jitter, and
        #    matrices holding none while the frame has one get it put in (so the offline test can hand them as the game did).
        shift = self.text(PT + "src/camera_shift.hpp")
        self.assertIn("inline CameraShiftCheck ensure_camera_shift(TGPT_Frame& f) {", shift)
        self.assertIn("const float ta=float(-2.0*told[0]/w), tb=float(2.0*told[1]/h);", shift)
        self.assertIn("if(std::fabs(r.held[0])>0.01f || std::fabs(r.held[1])>0.01f) { r.state=CameraShift::Other; return r; }", shift)
        self.assertIn("const CameraShiftCheck shift=ensure_camera_shift(slot.frame);", self.text(PT + "src/extension.cpp"))
        api = self.text(PT + "include/tgpt_api.h")
        self.assertIn("WITH the frame's sub-pixel", api)
        # B. The light shown in a frame was the frame before's, moved by HDRP's motion vectors and matched by its depth: under
        #    another frame's shift, and a frame late. A frame's composite waits on the GPU for that frame's own trace and takes
        #    each pixel's own texel: no motion vectors, no distance key, no taps, no neighbour search, no random fill, no ratio
        #    of two frames' exposures, no second set of output textures.
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("if(st.consumer.d12->GetCompletedValue()<now.retire) { check(st.c11->Wait(st.consumer.d11.Get(), now.retire), \"D3D11 wait for the traced light\"); ++st.compositeWaits; }", tracer)
        self.assertIn("struct CompositeWork { Result traced; uint32_t parity=0; bool inputs=false; };", tracer)
        self.assertIn("    Outputs outputs; // the frame's traced light: written by its trace, read by its composite", tracer)
        self.assertIn("constexpr UINT kCompositeSrvs=8;", tracer)
        for gone in ("st.latest", "work.previous", "prev.retire", "std::array<Outputs, 2>", "c.cameraDelta[i]=float(double(frame.cameraPosition[i])"):
            self.assertNotIn(gone, tracer, gone)
        d3d11 = self.text(PT + "shaders/d3d11.hlsl")
        composite = d3d11.split("#elif defined(TGPT_COMPOSITE) || defined(TGPT_COMPOSITE_RASTER)")[1].split("#elif defined(TGPT_GUIDES)")[0]
        self.assertIn("const float4 ta = tracedA.Load(int3(p, 0));", composite)
        self.assertIn("const float4 tb = tracedB.Load(int3(p, 0));", composite)
        self.assertIn("if (!(ta.a > 0.0)) return float4(0, 0, 0, 1);", composite)
        for gone in ("motion.Load", "Texture2D<float2> motion", "mul(invViewProj", "cameraDelta.", "tolerance", "exposureNow", "normalsIn", "offsets[", "prevUv"):
            self.assertNotIn(gone, composite, gone)
        #    A record's distance is its own hit's (it was HDRP's depth at the pixel, the key the composite matched texels by).
        primary = self.text(PT + "shaders/pt_primary.hlsli")
        self.assertIn("write_hair(p, h, w.stop.t);", primary)
        self.assertIn("own = write_traced(p, hit, dir, w.stop.t);", primary)
        self.assertNotIn("matchDistance", primary)
        self.assertNotIn("const float match = sky ? w.stop.t : surface;", primary)
        compose = self.text(PT + "shaders/pt_compose.hlsl")
        self.assertIn("overSky = !(hdrpDepth.Load(int3(p, 0)) > 0.0);", compose)
        self.assertNotIn("if (d > 0.0) distance = length(camera_relative_position(p, d));", compose)
        # The check (tests/pathtracing_edges_checks.inl, TGPT_TEST_EDGES_ONLY=1): a thin slanted bar and a card with holes cut
        # out, sixteen frames each with another shift, every pixel against what a ray through its shifted centre meets,
        # worked out on the CPU from the camera alone. With the matrices holding the shift, handed without it as the game
        # handed them, and with the camera moving.
        gpu = self.text("tests/pathtracing_gpu_test.cpp")
        self.assertIn('#include "pathtracing_edges_checks.inl"', gpu)
        self.assertIn("if(lateMatrices) { unity_layout(inverse(cam.nonJitteredViewProj), f.invViewProj); unity_layout(cam.nonJitteredViewProj, f.viewProj); }", gpu)
        checks = self.text("tests/pathtracing_edges_checks.inl")
        self.assertIn('GetEnvironmentVariableA("TGPT_TEST_EDGES_ONLY", nullptr, 0)>0', checks)
        self.assertIn("const double sx=x+0.5+jx, sy=y+0.5+jy;", checks)
        for check in ['"each frame\'s picture is the scene sampled at that frame\'s own sub-pixel shift (not the frame before\'s, not none)"',
                      '"handed the camera\'s matrices without the shift, the tracer\'s rays carry the told shift all the same"',
                      '"the tracer\'s log says how the matrices came: holding the shift, and without it"',
                      '"with the camera moving, each frame\'s picture is that frame\'s own: its camera, its shift"']:
            self.assertIn(check, checks)

    def test_path_tracing_small_lamps_on_trial_default_off(self):
        # PT-85 and PT-40 (docs/agents/flame-specks.md), ON TRIAL: with each lamp in its flame at its
        # flame's size the tavern shows twice the specks of lamps at the game's place and size. Four
        # hidden switches, one a suspect (A the near field, B the reflected ball, C the highlight, D
        # the shadow's disc), ALL 0 by default: off, every lamp is as it was. They never move a lamp
        # out of its flame. No option bit, no size, no ABI: TGPT_Frame.jitter's w,
        # FrameConstants.wetGrid's w, and HDRP's radius in a light's flags' top 12 bits.
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertRegex(settings, r'LampNearSize = file\.Bind\(Advanced, "Lamp near size", 15, Hidden\(')
        self.assertNotIn("Lamp trial near light", settings)
        self.assertIn("return Mathf.Clamp(s.LampNearSize.Value, 0, 255) + 256 * Mathf.Clamp(s.LampTrialReflected.Value, 0, 2) +", self.text("src/TGPathTracing/SceneLights.cs"))
        for key in ("LampTrialReflected", "LampTrialHighlight", "LampTrialShadow"):
            self.assertRegex(settings, key + r' = file\.Bind\(Advanced, "Lamp trial [a-z ]+", 0, Hidden\(')
        self.assertIn("frame.Jitter[3] = SceneLights.LampTrial(settings);", self.text("src/TGPathTracing/HdrpPasses.cs"))
        lights = self.text("src/TGPathTracing/SceneLights.cs")
        self.assertIn("bool hdrpRadiusAsked = trial.LampTrialReflected.Value == 1 || trial.LampTrialHighlight.Value == 1 || trial.LampTrialShadow.Value == 1;", lights)
        self.assertIn("if (hdrpRadiusAsked)", lights)
        tracer = self.text(PT + "src/tracer.cpp")
        # (22 bits since the job after took bits 14-21 for its own switch: test_path_tracing_flame_surroundings_on_trial_default_off.)
        self.assertIn("const float asked=std::isfinite(f.jitter[3]) && f.jitter[3]>0.f ? std::min(f.jitter[3], 4194303.f) : 0.f;", tracer)
        self.assertIn("k.wetGrid[3]=float(trial);", tracer)
        self.assertIn('static_assert(sizeof(FrameConstants)==9024, "frame constants layout");', tracer)  # 7920 + the water's sunGrid (water 8) + the rain's row (PT-108)
        self.assertIn("#define TGPT_LIGHT_HDRP_RADIUS_SHIFT 20u", self.text(PT + "include/tgpt_api.h"))
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("uint lamp_trial() { return uint(max(g.wetGrid.w, 0.0) + 0.5); }", common)
        self.assertIn("if (mode == 0u || l.type > 1u) return l.radius;", common)
        # The near size (bits 0-7) is no trial any more: within it a lamp's light stops growing, from
        # two of it on nothing changes; a lamp larger than it, and 0, are as before. The size is that
        # of a lamp of a tavern candlestick's strength and grows with the square root of a lamp's own,
        # so the most any lamp puts on a surface is the same (a wall candelabra three times as strong
        # still burnt its wax).
        self.assertIn("#define LAMP_NEAR_STRENGTH 13500.0", common)
        self.assertIn("const float n2 = n * n * max(luminance(l.color), 0.0) * (1.0 / LAMP_NEAR_STRENGTH);", common)
        self.assertIn("return n2 > own && l.type <= 1u ? rsqrt(d2 * d2 + n2 * n2) : 1.0 / max(d2, own);", common)
        self.assertIn("float atten = window * light_falloff(l, d2);", common)
        self.assertIn("float3 term = e / (light_falloff(l, dist * dist) * PI * ball * ball);", common) # reflected_lamps, the walk through the lamps' tree
        rtxdi = self.text(PT + "shaders/pt_rtxdi.hlsli")
        self.assertIn("r.highlightDir = r.dir;", rtxdi)
        self.assertIn("if (lamp_trial_mode(LAMP_TRIAL_SHADOW) != 0u) {", rtxdi)
        self.assertIn("if (lamp_trial_mode(LAMP_TRIAL_HIGHLIGHT) != 0u) {", rtxdi)

    def test_path_tracing_flame_surroundings_on_trial_default_off(self):
        # PT-85 and PT-40 (docs/agents/flame-near.md), ON TRIAL. The game showed the tavern's specks and its burnt-white
        # candles to be one thing: what a flame puts on surfaces within 10 cm of it. Two hidden switches, BOTH 0 by default
        # (off: the picture as before), neither of which moves a lamp or takes light from a surface:
        #  1. "Lamp trial lit surroundings" (cm): the rays that gather light find what stands by a lamp on purpose, weighed
        #     against the chance hit so each point's light counts once (pt_common lamp_around; LampTrial's bits 14-21).
        #  2. "Highlight shoulder knee" / "most": what is far past white rolls off above a knee in the composite, and the
        #     meter and the fog's histogram take it back out (TGPT_Frame.frameReserved, FrameConstants.radiance's z).
        # No option bit, no size, no ABI.
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertRegex(settings, r'LampTrialAround = file\.Bind\(Advanced, "Lamp trial lit surroundings", 0, Hidden\(')
        self.assertRegex(settings, r'HighlightShoulderKnee = file\.Bind\(Advanced, "Highlight shoulder knee", 0f, Hidden\(')
        self.assertRegex(settings, r'HighlightShoulderMost = file\.Bind\(Advanced, "Highlight shoulder most", 0f, Hidden\(')
        self.assertIn("16384 * Mathf.Clamp(s.LampTrialAround.Value, 0, 255);", self.text("src/TGPathTracing/SceneLights.cs"))
        passes = self.text("src/TGPathTracing/HdrpPasses.cs")
        self.assertIn("FrameReserved = HighlightShoulder(settings),", passes)
        self.assertIn("return most > 0f ? knee + 4096f * most : 0f;", passes)
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("k.radiance[2]=float(shoulder);", tracer)
        self.assertIn('static_assert(sizeof(FrameConstants)==9024, "frame constants layout");', tracer)  # 7920 + the water's sunGrid (water 8) + the rain's row (PT-108)
        common = self.text(PT + "shaders/pt_common.hlsli")
        # 1: off, nothing is armed and nothing is added; on, the two ways share one weighing (near_split) and the chance hit
        # gives up its share only where the lamp's own ray could have found the point (near_first).
        self.assertIn("float lamp_around() { return float((lamp_trial() >> LAMP_TRIAL_AROUND) & 0xFFu) * 0.01; }", common)
        self.assertIn("if (!(around > 0.0) || ray.alpha == 0.0 || l.type > 1u) return;", common)
        self.assertIn("if (ns > 0.0 && near_first(chosen, position)) kept = 1.0 - ns * na / (na + nb);", common)
        self.assertIn("result = leaving * (s * bz / (a + b) * invPdf);", common)
        self.assertIn("g_nearArmed.alpha = 0.0;", common)
        for site in (common, self.text(PT + "shaders/pt_gi.hlsl"), self.text(PT + "shaders/pt_specular.hlsl")):
            self.assertIn("lamp_around() > 0.0", site)
            self.assertIn("lamp_around_light(", site)
        # 2: off or under the knee the factor is exactly 1; the way back is in traced_ungained and in d3d11.hlsl's copy.
        self.assertIn("if (!(k.y > 0.0) || !(lit > k.x)) return 1.0;", common)
        self.assertIn("shown = highlight_unrolled(shown);", common)
        self.assertIn("exStored *= highlight_rolled(luminance(max(diffuse * s.albedo + specular, 0.0)) * exStored);", self.text(PT + "shaders/pt_compose.hlsl"))
        recipes = json.loads(self.text("tests/ingame/scenarios.json"))["recipes"]
        self.assertIn("lamp-near", recipes)

    def test_path_tracing_reuse_follows_the_shift_on_trial(self):
        # PT-85 (docs/agents/reuse-shift.md). Since PT-84 the camera's rays carry another sub-pixel shift every frame, and a
        # pixel's motion is HDRP's kind, between the two pictures WITHOUT their shifts. What a pass reuses of the frame
        # before was fetched by that motion alone: up to a render pixel beside the point, on another surface at thin things
        # and edges, where the history was refused and the raw sample showed (the game's Direct layer: 10 specks a frame
        # without the shift, 64 with it). The previous place is made in ONE function, with the two shifts' difference.
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("float2 previous_place(uint2 p, float2 motion) {", common)
        self.assertIn("return float2(p) + 0.5 - motion * float2(g.size.xy) + g.look.yz;", common)
        # Every fetch of the frame before by a pixel's place goes through it: ReSTIR's reuse across frames (direct and
        # bounced), the tracer's own histories (a mirror's picture too), NRD's kept direction. No other formula is left.
        rtxdi = self.text(PT + "shaders/pt_rtxdi.hlsli")
        self.assertIn("previous_place(p, motion_at(p)) - (float2(p) + 0.5)", rtxdi)
        temporal = self.text(PT + "shaders/pt_temporal.hlsl")
        self.assertIn("float2 prevPixel = previous_place(p, mv) - 0.5;", temporal)
        for name in ("pt_temporal.hlsl", "pt_nrd_prep.hlsl"):
            self.assertNotIn("/ float2(g.size.xy) - m", self.text(PT + "shaders/" + name), name)
        # The history's second try (SVGF's): the nine texels round the place when none of the four showed the surface,
        # each still checked against its own surface.
        self.assertIn("const int2 o = second ? int2((i - 4) % 3, (i - 4) / 3) - 1 : int2(i & 1, i >> 1);", temporal)
        self.assertIn("if (!sp.valid || !sp.deferred || dot(sp.geoNormal, s.geoNormal) < 0.85) continue;", temporal)
        # The difference is read from the matrices the rays are made of (camera_shift.hpp), frame against frame.
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("inline bool held_camera_shift(const TGPT_Frame& f, float held[2]) {", self.text(PT + "src/camera_shift.hpp"))
        self.assertIn("const bool have=held_camera_shift(f, held);", tracer)
        self.assertIn("st.cameraShift[0]=held[0]; st.cameraShift[1]=held[1]; st.haveCameraShift=have;", tracer)
        # ON TRIAL: behind TGPT_Frame.jitter's z, 0 by default, which the plugin's hidden setting
        # sets; no option bit, no size, no ABI. Off, the difference stays 0 and the history's second
        # try does not run: the tracer as it was. 1: the tracer's own histories follow the shifts and
        # ReSTIR's reuse keeps its fetch (its own search finds its surface; with its fetch moved too
        # the direct light boiled 19 % more inside flat surfaces and the edges gained nothing). 2:
        # ReSTIR's as well, for finding causes.
        self.assertIn("if(trial && have && st.haveCameraShift && !reset) { k.look[1]=held[0]-st.cameraShift[0]; k.look[2]=held[1]-st.cameraShift[1]; }", tracer)
        self.assertIn("k.look[3]=trial==1 ? 2.f : trial==2 ? 1.f : 0.f;", tracer)
        self.assertIn("if (weightSum > 0.01 || !(g.look.w > 0.5)) break;", temporal)
        self.assertIn("g.look.w > 1.5 ? -motion_at(p) * float2(g.size.xy)", rtxdi)
        self.assertIn('static_assert(sizeof(FrameConstants)==9024, "frame constants layout");', tracer)  # 7920 + the water's sunGrid (water 8) + the rain's row (PT-108)
        settings = self.text("src/TGPathTracing/Settings.cs")
        # On (1) since 2026-10-09 00:35: in the game (tavern-room, NRD, 16 frames) the picture's specks fell from 15.7 to 5.8 a
        # frame and the Direct view's from 62.9 to 3.4, the mean unchanged.
        self.assertIn('ReuseFollowsShift = file.Bind(Advanced, "Reuse follows camera shift", 1, Hidden(', settings)
        self.assertIn("frame.Jitter[2] = Mathf.Clamp(settings.ReuseFollowsShift.Value, 0, 2);", self.text("src/TGPathTracing/HdrpPasses.cs"))
        # The check (tests/pathtracing_reuse_checks.inl, TGPT_TEST_REUSE_ONLY=1): thin things under near and far lamps,
        # sixteen shifts in turn, a frame compared with the frames of the SAME shift only; the switch off, on, and no shift.
        gpu = self.text("tests/pathtracing_gpu_test.cpp")
        self.assertIn('#include "pathtracing_reuse_checks.inl"', gpu)
        self.assertIn("f.jitter[2]=reuseTrial;", gpu)
        checks = self.text("tests/pathtracing_reuse_checks.inl")
        self.assertIn('GetEnvironmentVariableA("TGPT_TEST_REUSE_ONLY", nullptr, 0)>0', checks)
        for check in ['"the switch leaves each layer\'s mean light as it is (against no shift: direct within 3 percent, reflections within 5; against the switch off: 2 and 3)"',
                      '"with the switch off this scene shows what the game shows: the Direct layer jumps several times as much under the shift, its light on the pixel grid"',
                      '"with the switch on the denoised light follows the camera\'s shift (no steps at the pixels\' borders: within 15 percent)"',
                      '"with the switch on the Direct layer jumps under 0.6 times as much as with it off, the Specular layer under 0.8 times"',
                      '"with the switch on the light inside flat surfaces changes no more from round to round than with it off (no more boiling)"']:
            self.assertIn(check, checks)

    def test_path_tracing_character_normals_on_trial(self):
        # PT-92, ON TRIAL behind the hidden "Character normals" (0 by default: the picture as before;
        # docs/agents/ char-normals.md). A character reached the tracer as skinned positions alone, so
        # each of its triangles was shaded flat. The game's own skinning leaves every vertex's skinned
        # normal beside its position (Kandra's CompressedVertex, byte 12): at 1 it travels with the
        # position, in the same two passes and the shared texture's alpha, into a buffer the
        # character's mesh record names, and a hit's Hit.rounded is smooth for every ray. Nothing is
        # drawn or skinned twice, nothing is read from the screen.
        api = self.text(PT + "include/tgpt_api.h")
        self.assertIn("#define TGPT_SKINNED_STRIDE_MASK 0xFFFFu", api)
        self.assertIn("#define TGPT_SKINNED_NORMAL_SHIFT 16u", api)
        self.assertIn('static_assert(sizeof(TGPT_SkinnedInstance)==100, "TG Path Tracing skinned instance ABI");', api)  # no ABI change
        self.assertIn("if(count && normalAt && (normalAt<12 || normalAt%4 || normalAt+4>bytes)) return refuse(-2);", self.text(PT + "src/extension.cpp"))
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn('{normalAt ? "SKIN_NORMAL" : nullptr, normalAt ? normal.c_str() : nullptr}', tracer)
        self.assertIn("st.skinNormals[parity]=normalAt!=0;", tracer)
        self.assertIn("st.skinNormals[parity], smooth[i].uav, smooth[i].buffer);", tracer)
        # The copy on the game's device: the packed normal as the game wrote it, with bit 30 set so that the float channel
        # holds an ordinary float (no valid packed normal has the bit); positions alone, alpha 1, without the switch.
        d3d11 = self.text(PT + "shaders/d3d11.hlsl")
        self.assertIn("float4(load_position(c0.x + id.x), asfloat(load_normal(c0.x + id.x) | 0x40000000u));", d3d11)
        self.assertIn("target[uint2(t % 4096u, t / 4096u)] = float4(load_position(c0.x + id.x), 1.0);", d3d11)
        skin = self.text(PT + "shaders/pt_skin.hlsl")
        self.assertIn("static const uint SKIN_NORMAL_MARK = 0x40000000u;", skin)
        self.assertIn("const float2 e = saturate(float2(f16tof32(packed & 0xFFFFu), f16tof32(packed >> 16))) * 2.0 - 1.0;", skin)
        self.assertIn("normals.Store(id.x * 4u, pack_normal(skinned_normal(asuint(texel.w))));", skin)
        # Its record names the normals only in the frames they were written: the switch back at 0 is the old picture at once.
        scene = self.text(PT + "src/scene.cpp")
        self.assertIn("const uint32_t named=normals ? m->skinNormalsSrv : kInvalid;", scene)
        self.assertIn("if(meshRecords_[size_t(handle)].normals!=named) { meshRecords_[size_t(handle)].normals=named; meshRecordsDirty_=true; }", scene)
        self.assertIn("m->skinNormalsUav,m->skinNormalsSrv}) heap.release(srv);", scene)
        # Hair cards keep their flat faces (the brief: hair has its own normal), and a character's hit without a normal map
        # takes its smooth normal for the rays after the camera's too: a mirror shows it as round as the camera sees it.
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("if ((hitFlags & MATERIAL_STRANDS) != 0 || ((hitFlags & MATERIAL_HAIR_LAYER) != 0 && (hitFlags & MATERIAL_CAMERA_ONLY) == 0)) h.rounded = h.normal;", common)
        self.assertIn("if (all(s.normal == h.normal) && any(h.rounded != h.normal)) {", common)
        # The plugin takes the normal's place from the game's own type and checks the packing against the game's own encoder.
        capture = self.text("src/TGPathTracing/CharacterCapture.cs")
        self.assertIn("uint stride = (uint)output.stride | (SkinnedNormalAt(output.stride) << Tgpt.SkinnedNormalShift);", capture)
        self.assertIn("if (Session.Settings.CharacterNormals.Value <= 0) return 0;", capture)
        self.assertIn("Unity.Collections.LowLevel.Unsafe.UnsafeUtility.GetFieldOffset(field)", capture)
        self.assertIn("new Awaken.Kandra.Data.CompressedVertex(float3.zero, n, new float3(1f, 0f, 0f));", capture)
        settings = self.text("src/TGPathTracing/Settings.cs")
        # ON by default since the game said yes (run 20261009-084003); NRD's smooth guide needs no option bit any more:
        # it holds wherever a character brought its normals (bit 4194304 is free again).
        self.assertIn('CharacterNormals = file.Bind(Advanced, "Character normals", 1, Hidden(', settings)
        # At 2, NRD's guide: the mesh's smooth normal WITHOUT the normal map (the measured reason for the geometric normal
        # stands: per-pixel bumps made RELAX's history fail), in two words of the hair texture no pass read for such a pixel.
        for text in (api, common, self.text("src/TGPathTracing/NativeTracer.cs"), settings):
            self.assertNotIn("SMOOTH_GUIDE", text)
            self.assertNotIn("OptionSmoothGuide", text)
        # (NRD's guide was the mesh's smooth normal without the normal map, in two words of the hair texture, SMOOTH_PLANE,
        # until 2026-10-11: NRD takes the shading normal now, as its README asks, PT-127.)
        for name in ("pt_primary.hlsli", "pt_nrd_prep.hlsl"):
            self.assertNotIn("SMOOTH_PLANE", self.text(PT + "shaders/" + name), name)
        # The game's answer: one run of two scenes, the switch at 0, 1 and 2 under NRD, 16 frames each; no neural step.
        import json
        recipe = json.loads(self.text("tests/ingame/scenarios.json"))["recipes"]["char-normals"]
        values = [s["value"] for step in recipe for s in step["set"] if s.get("key") == "Character normals"]
        self.assertEqual(values[:3], ["0", "1", "2"])
        self.assertEqual(values[-1], "0")
        for step in recipe:
            self.assertNotIn("neural", step)

    def test_path_tracing_grass_shadow_on_trial_default_off(self):
        # PT-93, ON TRIAL (docs/agents/grass-boil.md): grass lit by a fire on the ground flickers in every frame, because a
        # light's shadow ray flips a coin at every blade it meets and a level ray meets many. The trial takes the product
        # of what each blade lets through: the coin's own mean. Hidden, off by default, and off it changes no ray.
        common = self.text(PT + "shaders/pt_common.hlsli")
        tracer = self.text(PT + "src/tracer.cpp")
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertIn('GrassShadowTrial = file.Bind(Advanced, "Grass shadow trial", 0, Hidden(', settings)
        self.assertIn("frame.FogMip[3] = settings.GrassShadowTrial.Value == 1 ? 1f : 0f;", self.text("src/TGPathTracing/HdrpPasses.cs"))
        # It travels in a field no caller filled (TGPT_Frame.fogMip's w) into bit 22 of the lamps' number, which stays as it was.
        self.assertIn("const uint32_t grass=std::isfinite(f.fogMip[3]) && f.fogMip[3]>0.5f ? 1u : 0u;", tracer)
        self.assertIn("if(grass) k.wetGrid[3]=float(trial|(1u<<22));", tracer)
        self.assertIn("bool leaf_product_on() { return ((uint(max(g.wetGrid.w, 0.0) + 0.5) >> 22) & 1u) != 0u; }", common)
        # Only the final shading's shadow rays take the product; the coin stays for every other ray, and with the trial off.
        opaque = common.split("bool alpha_opaque(", 1)[1].split("\n}", 1)[0]
        self.assertIn("if (g_leafProduct && g_lightRay && !g_cameraRays && (m.flags & (MATERIAL_TRANSLUCENT | MATERIAL_THIN_CLOTH | MATERIAL_STRANDS)) == MATERIAL_TRANSLUCENT) { ++g_leafMet; return false; }", opaque)
        self.assertEqual(common.count("g_leafProduct = leaf_product_on();"), 2)
        self.assertEqual(common.count("g_leafProduct = false;"), 3)  # its declaration, and after each of the two rays
        for name in ["transmittance(", "transmittance_from_surface("]:
            body = common.split("float3 " + name, 1)[1].split("\n}", 1)[0]
            self.assertIn("g_leafProduct = leaf_product_on(); g_leafMet = 0;", body)
            self.assertIn("g_glassTint = false; g_lightRay = false; g_leafProduct = false;", body)
            self.assertIn("if (g_leafMet > 0) { g_leafThrough = leaf_walk(origin, dir, tmin, tmax, ", body)
            self.assertIn("waterThrough *= g_leafThrough; }", body)
        self.assertNotIn("g_leafProduct", self.text(PT + "shaders/pt_rtxdi.hlsli"))  # the reuse's yes-or-no test keeps the coin
        self.assertIn("return caustic_tint(v0, v1, light, directional) * scale * g_leafThrough;", common)  # the bent light in the straight ray's place takes the same share
        # The share is the coin's mean, term by term: TRANSMISSION where the blade covers, by its tile's coverage or its cutoff.
        share = common.split("float leaf_pass(", 1)[1].split("\n}", 1)[0]
        self.assertIn("const float cover = !shared_cutout(m) && (m.flags & MATERIAL_ALPHA_TILE) != 0 ? saturate(a) : (a >= m.alphaCutoff ? 1.0 : 0.0);", share)
        self.assertIn("return 1.0 - (1.0 - TRANSMISSION) * cover;", share)
        # Each crossing once, nearest first (the visibility test may report a triangle twice), the wind's things as that test meets them.
        walk = common.split("float leaf_walk(", 1)[1].split("\n}", 1)[0]
        for token in ["PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_OPAQUE) q;",
                      "for (uint crossing = 0; crossing < met + 2u && t0 < tmax; ++crossing) {",
                      "if (q.CandidateTriangleRayT() < swayFrom && (instances[ci].flags & INSTANCE_SWAYS) != 0) continue;",
                      "if (lets < 1.0) { nearest = lets; q.CommitNonOpaqueTriangleHit(); }",
                      "through *= nearest;"]:
            self.assertIn(token, walk)
        self.assertIn('"grass-product": [', self.text("tests/ingame/scenarios.json"))

    def test_path_tracing_texture_follow_on_trial_default_off(self):
        # PT-95, ON TRIAL (docs/agents/kandra-lit.md): a guard in blocks of colour, his armour without relief. The game's
        # character shader (TG/Character/Kandra_Lit) has no detail layer: the tracer reads all it samples. His materials
        # read their 64x64 tiles because the textures they name were replaced by the game's streaming and not followed.
        # Hidden, off by default, the plugin alone: 1 looks at every texture and names those in the log, 2 follows them.
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertIn('TextureFollow = file.Bind(Advanced, "Texture follow", 0, Hidden(', settings)
        share = self.text("src/TGPathTracing/TextureShare.cs")
        self.assertIn("int follow = Session.Settings != null ? Session.Settings.TextureFollow.Value : 0;", share)
        self.assertIn("try { Audit(materials, uploaded, tracer, follow, ref resend); }", share)  # its trouble never stops the scan
        audit = share.split("private static void Audit(", 1)[1].split("\n        }", 1)[0]
        # What the game holds now, and the handle the tracer has for that very object, against the one the materials name.
        self.assertIn("IntPtr native = w.Texture.GetNativeTexturePtr();", audit)
        self.assertIn("int n = tracer.RegisterSharedTexture(native, out var info);", audit)
        self.assertIn("if (info.Handle == held) { stale.Remove(w.Id); continue; }", audit)
        # At 1 nothing is written: the picture as before. At 2 the materials take the new handle, as for a swap Refresh saw.
        self.assertLess(audit.index("if (mode < 2) continue;"), audit.index("Follow(w, native, materials, uploaded, tracer, ref resend);"))
        # No copy and no stand-in comes with it, and nothing of it travels to the native side.
        for token in ["new Texture2D", "Graphics.CopyTexture", "Graphics.Blit"]:
            self.assertNotIn(token, audit)
        self.assertNotIn("TextureFollow", self.text("src/TGPathTracing/HdrpPasses.cs"))
        import json
        recipe = json.loads(self.text("tests/ingame/scenarios.json"))["recipes"]["texture-follow"]
        values = [s["value"] for step in recipe for s in step["set"] if s.get("key") == "Texture follow"]
        self.assertEqual(values, ["1", "2", "0"])
        for step in recipe:
            self.assertNotIn("neural", step)

    def test_path_tracing_reflection_relief_on_trial_default_off(self):
        # PT-95, PT-19, ON TRIAL (docs/agents/nrd-relief.md): a guard's hammered iron helmet is smooth bright metal with
        # NRD, where the frame with no denoiser has the game's relief (run 20261009-103336: its face kept 0.59 of 10.51 of
        # the detail). NRD is handed the bounced diffuse light alone; a metal has none; its reflections go through the
        # tracer's own filter (pt_atrous), whose normals' term is loose on anything but a near mirror and whose feedback
        # keeps the history smooth. Hidden, off by default, and off it changes no weight and no history.
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertIn('ReflectionReliefTrial = file.Bind(Advanced, "Reflection relief trial", 0, Hidden(', settings)
        # The word is shared with PT-99's speed trial (the rest of it, divided by 8): each reads its own part.
        self.assertIn("frame.PrevCameraPosition[3] = Mathf.Clamp(settings.ReflectionReliefTrial.Value, 0, 7) + 8 * Mathf.Clamp(settings.SpeedTrial.Value, 0, 65535);", self.text("src/TGPathTracing/HdrpPasses.cs"))
        # It travels in a field no caller filled (TGPT_Frame.prevCameraPosition's w, bits 0 to 2) to the four passes of the
        # reflections' filter as their sixth constant: no frame constant, no option bit, and the direct light's filter and
        # the bounced light's never see it.
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("const uint32_t relief=std::isfinite(f.prevCameraPosition[3]) && f.prevCameraPosition[3]>0.f ? uint32_t(std::min(f.prevCameraPosition[3], 524287.f)+0.5f)&7u : 0u;", tracer)
        self.assertEqual(tracer.count("1u, kInvalid, relief, onlyThrough}, b);") + tracer.count("1u, st.specHist[cur].uav, relief, onlyThrough}, b);"), 4)
        self.assertEqual(tracer.count("relief, onlyThrough}, b);"), 4)  # (onlyThrough: glass and water alone while NRD runs)
        atrous = self.text(PT + "shaders/pt_atrous.hlsl")
        self.assertIn("static const uint RELIEF_LOBE = 1u, RELIEF_REFLECTANCE = 2u, RELIEF_HISTORY = 4u;", atrous)
        self.assertIn("const uint relief = specular && pass.b.y != INVALID && g.material.x != INVALID ? pass.b.y : 0u;", atrous)
        self.assertIn("const bool plain = relief != 0u && s.valid && s.deferred && plain_reflector(word, shown.kind);", atrous)
        # Bit 4: a plain reflecting surface's history keeps its own samples (the first iteration is not fed back into
        # it, as on a mirror); the feedback itself stays in the pass as it was for everything else and with the trial off.
        self.assertIn("if (pass.b.x != INVALID && !(plain && (relief & RELIEF_HISTORY) != 0u)) {", atrous)
        self.assertIn("history[p] = float4(lerp(center.rgb, before.rgb, shown.picture), before.a);", atrous)
        # The weights as before stay in the pass, and the trial's come after them, between two plain reflecting surfaces
        # only: glass, water and hair with its highlights keep their filter.
        before = "float wn = edge * pow(saturate(dot(s.normal, sq.normal)), normalPower);"
        lobe = "if ((relief & RELIEF_LOBE) != 0u) wn = edge * lobe_held(s.normal, sq.normal, 0.5 * (s.roughness + sq.roughness));"
        self.assertLess(atrous.index(before), atrous.index("if (plain) {"))
        self.assertLess(atrous.index("if (plain_reflector(other, o.kind)) {"), atrous.index(lobe))
        self.assertIn("if ((relief & RELIEF_REFLECTANCE) != 0u) ws *= same_reflecting(word, other);", atrous)
        self.assertIn("bool plain_reflector(uint2 word, uint kind) { return ((word.y >> 24) & 66u) == 0u && kind != SEEN_WATER && kind != SEEN_THROUGH; }", atrous)
        # The lobe itself: GGX's distribution about the one normal, read at the other, over its peak; the pair's mean
        # roughness, so the weight is the same seen from either pixel (it exchanges light, it does not take it).
        held = atrous.split("float lobe_held(", 1)[1].split("\n}", 1)[0]
        self.assertIn("const float a = roughness_alpha(roughness), a2 = a * a;", held)
        self.assertIn("const float held = a2 / max(dot(x, x) + a2 * c * c, 1e-12);", held)
        import json
        recipe = json.loads(self.text("tests/ingame/scenarios.json"))["recipes"]["relief"]
        values = [s["value"] for step in recipe for s in step["set"] if s.get("key") == "Reflection relief trial"]
        self.assertEqual(values, ["0", "4", "1", "5", "7", "0"])
        self.assertEqual([s["value"] for step in recipe for s in step["set"] if s.get("key") == "Denoising"][:2], ["None", "NRD"])
        for step in recipe:
            self.assertNotIn("neural", step)

    def test_path_tracing_glass_on_trial(self):
        # PT-13, PT-35 (docs/agents/glass-3.md; the inspection of 2026-10-08: the Crypt's jars cloudy with dark blotches where the
        # game's are clear). ON TRIAL behind the hidden "Glass trial" (0 by default: the picture as before), a bit each: 1, what
        # goes through see-through glass crosses the mesh's own smooth face where the game's shader neither bends nor blurs what
        # is behind (the jars' roughness of 0.36 to 0.40 frosted the view at each of four faces); 2, its paint only on the faces
        # the game draws. No option bit (all 32 are used): TGPT_Frame.cameraPosition's w to the frame flags' bits 8-15.
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertIn('GlassTrial = file.Bind(Advanced, "Glass trial", 0, Hidden(', settings)
        self.assertIn("frame.CameraPosition[3] = Mathf.Clamp(settings.GlassTrial.Value, 0, 15) + 16 * ((settings.WaterShoreFoam.Value ? 1 : 0) |", self.text("src/TGPathTracing/HdrpPasses.cs"))
        tracer = self.text(PT + "src/tracer.cpp")
        self.assertIn("const float asked=std::isfinite(f.cameraPosition[3]) && f.cameraPosition[3]>0.f ? float(uint32_t(std::min(f.cameraPosition[3], 127.f))&15u) : 0.f;", tracer)
        # The same word carries the water's three trials times 16 (water10): each side takes its own bits.
        self.assertIn("k.prevCameraPos[3]=std::isfinite(f.cameraPosition[3]) ? float((uint32_t(std::clamp(f.cameraPosition[3], 0.f, 127.f))>>4)&7u) : 0.f;", tracer)
        self.assertIn("k.size[3]|=trial<<8;", tracer)
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("uint glass_trial() { return (g.size.w >> 8) & 0xFFu; }", common)
        self.assertIn("static const uint GLASS_TRIAL_CLEAR = 1u, GLASS_TRIAL_DRAWN = 2u, GLASS_TRIAL_PIECES = 4u, GLASS_TRIAL_BOUNDARY = 8u;", common)
        # 4: the Crypt's jar is one submesh, a closed body and an open lid; its body has an inside by its own piece.
        self.assertIn("return (glass_trial() & GLASS_TRIAL_PIECES) != 0u ? piece_solid(instanceId, geometry, primitive) : glass_solid(instanceId, geometry);", common)
        # Glass whose shader in the game does refract by its finish (HDRP Lit with a refraction model) keeps it.
        self.assertIn("return (glass_trial() & GLASS_TRIAL_CLEAR) != 0u && (m.flags & MATERIAL_SEE_THROUGH) != 0 && (m.opticalReserved.x & OPTICAL_REFRACTS) == 0u;", common)
        self.assertIn("if (refracts) mat.OpticalReserved1 |= 1u;", self.text("src/TGPathTracing/GlassFinish.cs"))
        self.assertIn("return (glass_trial() & GLASS_TRIAL_DRAWN) == 0u || front || (m.flags & MATERIAL_DOUBLE_SIDED) != 0;", common)
        self.assertIn("if (!glass_painted(materials[hit.material], hit.front)) return 0.0;", common)
        optical = self.text(PT + "shaders/pt_dielectric.hlsli")
        self.assertIn("const bool clear = glass_clear(m);", optical)
        self.assertIn("const float3 throughNormal = solid && (glass_trial() & GLASS_TRIAL_BOUNDARY) != 0u ? hit.normal : facing_normal(hit.rounded, v);", optical)
        self.assertIn("const Interface face = dielectric_interface(dir, throughNormal, ni, nt);", optical)
        checks = self.text("tests/pathtracing_glass_checks.inl")
        for name in ("on trial: what is seen through a see-through jar stays sharp, whatever roughness its highlight has",
                     "on trial: a jar's film is on the faces the game draws, once a wall"):
            self.assertIn('"' + name + '"', checks)
        self.assertIn("f.cameraPosition[3]=glassTrial+16.f*waterTrial;", self.text("tests/pathtracing_gpu_test.cpp"))
        # The game's answer: one run of the jars' scene, no denoiser with the switch at 0, 7 and 15; no NRD steps.
        import json
        recipe = json.loads(self.text("tests/ingame/scenarios.json"))["recipes"]["glass-trial"]
        values = [s["value"] for step in recipe for s in step["set"] if s.get("key") == "Glass trial"]
        self.assertEqual(values, ["0", "7", "15", "0"])
        self.assertEqual([s["value"] for step in recipe for s in step["set"] if s.get("key") == "Denoising"], ["None"])
        for step in recipe:
            self.assertNotIn("neural", step)
        # The camera's own glass pixel: its walk starts from the surface record, which keeps neither the submesh nor the
        # triangle, so the record says whether the triangle met has an inside (bit 30), and with bit 1 on trial its normal is
        # the mesh's smooth one (what the view is bent by), the normal-mapped one in the words' 16 spare bits.
        primary = self.text(PT + "shaders/pt_primary.hlsli")
        self.assertIn("(glass && glass_solid(hit.instance, hit.geometry, hit.primitive) ? 0x40000000u : 0u), hit.material);", primary)
        self.assertIn("nrm[p] = uint2(pack_normal(clearGlass ? facing_normal(hit.rounded, -dir) : own.normal),", primary)
        spec = self.text(PT + "shaders/pt_specular.hlsl")
        self.assertIn("seen.material = ids.y & 0xFFFFFu; seen.instance = ids.x & 0xFFFFFFu; seen.front = (ids.x & 0x80000000u) != 0;", spec)
        self.assertIn("g_glassFirst = (ids.x & 0x40000000u) != 0 ? 1 : 0;", spec)
        self.assertIn("if ((ids.x & 0x20000000u) != 0) material.normal = unpack_normal16(((ids.x >> 24) & 0xFu) | ((ids.y >> 20) << 4));", spec)
        self.assertIn('"on trial: a jar whose open lid shares its submesh is glass with an inside all the same, its film once a wall"', checks)

    def test_path_tracing_glass_tints_by_its_absorption(self):
        # PT-76 (docs/agents/glass-3.md): the whole GPU test's coloured-glass checks failed since glass's color means paint
        # on it. Two things. (1) A lamp's or the sun's light through glass was counted twice at every vertex that gathers
        # light: once by its shadow ray (tinted), once more by its bounce ray's walk through the glass (not tinted): a floor
        # under clear glass 1.885 times an open one. The walk now counts a lamp only where it reflected off glass on its way.
        optical = self.text(PT + "shaders/pt_dielectric.hlsli")
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("static bool g_walkGathers = false;", optical)
        self.assertIn("const bool gathers = g_walkGathers;", optical)
        self.assertIn("if (reflection) mirrored = true;", optical)
        self.assertIn("if (!gathers || mirrored) light += weight * reflected_lamps(from, origin, dir, found ? distance : 1e30);", optical)
        self.assertIn("if ((!gathers || mirrored) && g.sunColor.w > 0.0 && dot(dir, g.sunDir.xyz) >= g.sunDir.w)", optical)
        self.assertIn("g_walkGathers = gathers; // the vertex the ray left samples the lamps and the sun itself (pt_dielectric)", common)
        self.assertIn("g_walkGathers = true; // the cache's cell takes the lamps' and the sun's own light where its vertices are lit", common)
        # (2) Glass's own tint is its material's absorption (HDRP's transmittance color over its absorption distance): over the
        # way inside closed glass for a radiance ray, a light's ray and a caustic's path; an open sheet is a thin pane as thick
        # as its material says. Its base color is no filter (the offline test's old glass was tinted by it, for shadow rays alone).
        self.assertIn("float3 pane_transmittance(MaterialRecord m, float cosIncident) {", common)
        self.assertIn("const float thickness = asfloat(m.opticalReserved.y);", common)
        self.assertIn("if (!reflection && !solid) weight *= pane_transmittance(m, abs(dot(dir, hit.normal)));", optical)
        self.assertIn("return enter * leave * open0 * open1 * inside0;", common)
        self.assertNotIn("glass_color", common)
        finish = self.text("src/TGPathTracing/GlassFinish.cs")
        self.assertIn("mat.OpticalReserved2 = BitConverter.ToUInt32(BitConverter.GetBytes(thickness), 0);", finish)
        harness = self.text("tests/pathtracing_gpu_test.cpp")
        self.assertIn("for(float& v:materials[9].absorption) v=float(-std::log(0.36)/0.8);", harness)
        # The room's coloured-glass checks live in a file of their own: the whole test includes it where they stood, the
        # glass-only run (TGPT_TEST_GLASS_ONLY) after the glass fixtures.
        self.assertEqual(harness.count('#include "pathtracing_glass_tint_checks.inl"'), 2)
        tint = self.text("tests/pathtracing_glass_tint_checks.inl")
        for name in ("light through green glass reaches path vertices green (bounced light)",
                     "grey glass leaves the bounced light's color",
                     "grey glass lets through as much of every channel as its absorption leaves over the way inside it"):
            self.assertIn('"' + name + '"', tint)
        checks = self.text("tests/pathtracing_glass_checks.inl")
        for name in ("a lamp's light through clear glass reaches a floor once: by the floor's own shadow ray, not again by its bounce ray",
                     "an open sheet of glass tints a lamp's light by its absorption over its own thickness (a thin pane)",
                     "closed glass tints a lamp's light by its absorption over the way inside it"):
            self.assertIn('"' + name + '"', checks)

    def test_path_tracing_speed_on_trial_default_off(self):
        # PT-99, ON TRIAL: speed that leaves the picture as it is, behind the hidden "Speed trial" (0
        # by default: every ray as before). Bit 1: a shadow ray that nothing stopped asked which water
        # its start lies in with one more ray, straight up, wherever the scene held water; with the
        # bit only a point under the water's highest point asks.
        common = self.text(PT + "shaders/pt_common.hlsli")
        water = self.text(PT + "shaders/pt_water_surface.hlsli")
        tracer = self.text(PT + "src/tracer.cpp")
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertIn('SpeedTrial = file.Bind(Advanced, "Speed trial", 0, Hidden(', settings)
        # The word's bits 0 to 2 are PT-95's relief trial: the speed trial is the rest, divided by 8.
        self.assertIn("frame.PrevCameraPosition[3] = Mathf.Clamp(settings.ReflectionReliefTrial.Value, 0, 7) + 8 * Mathf.Clamp(settings.SpeedTrial.Value, 0, 65535);", self.text("src/TGPathTracing/HdrpPasses.cs"))
        # It travels in a field no caller filled (TGPT_Frame.prevCameraPosition's w) into the frame flags' bits 16-31, above
        # the reset bits and glass's eight.
        self.assertIn("const float asked=std::isfinite(f.prevCameraPosition[3]) && f.prevCameraPosition[3]>0.f ? std::floor(std::min(f.prevCameraPosition[3], 524287.f)/8.f) : 0.f;", tracer)
        self.assertIn("k.size[3]|=trial<<16;", tracer)
        self.assertIn("uint speed_trial() { return g.size.w >> 16; }", common)
        self.assertIn("uint glass_trial() { return (g.size.w >> 8) & 0xFFu; }", common)
        # The height is the one the wet ground's question already looks down from (water_over), and the test comes after
        # the two that say "no water in the scene": with the bit off the function is as it was.
        medium = water.split("int water_medium(float3 p) {", 1)[1].split("\n}", 1)[0]
        self.assertIn("if ((speed_trial() & SPEED_WATER_CEILING) != 0u && p.y > water_ceiling()) return -1;", medium)
        self.assertIn("trial|=1u;", tracer)  # (round 20: bit 1 the rule, every frame)
        ceiling = water.split("float water_ceiling() {", 1)[1].split("\n}", 1)[0]
        self.assertIn("const float top = max(max(max(g.waterTop.x, g.waterTop.y), max(g.waterTop.z, g.waterTop.w)), g.waterCeiling.x);", ceiling)  # (round 20: every slot's water)
        self.assertIn("* (2.0 * CHOPPINESS + 1.0) + 0.05;", ceiling)
        # The plugin's highest point is taken at its word only in a frame whose own water triangles bear it out.
        self.assertIn("waterHighest=std::max(waterHighest, surface.ranges[0].hi[1]);", self.text(PT + "src/scene_water.cpp"))
        self.assertIn("ceilingHolds=!(highest>-1e299) || (top>-1e29f && float(highest-st.origin[1])<=top+1e-3f);", tracer)
        self.assertIn("if(!ceilingHolds) trial&=~1u;", tracer)
        # The measuring build's switches (they leave work and light out, to time a part of a pass) are compiled in by
        # CMake's TGPT_SPEED_MEASURE alone, which is OFF: every line that names one lies between #ifdef PT_SPEED_MEASURE
        # and its #else or #endif, in every shader.
        cmake = self.text("CMakeLists.txt")
        self.assertIn('option(TGPT_SPEED_MEASURE "TG Path Tracing: compile the passes\' timing switches in (never for a build that is deployed)" OFF)', cmake)
        self.assertEqual(cmake.count("${PT_SPEED_DEFINES}"), 3)
        # The "Lamp trial lit surroundings" trial (PT-85) is compiled out of every pass by default. The game's own logs
        # (docs/agents/speed-1.md): the tavern's bounced light went from 29.4 to 61.9 ms in the build that brought the
        # trial's code in, with its setting at 0. ON brings it back for a build that is to try it; the tracer's log says
        # when the setting is asked of a build without it. The trial's own line stays as it was, for that build.
        self.assertIn("option(TGPT_LAMP_AROUND \"TG Path Tracing: compile the 'Lamp trial lit surroundings' trial into the passes\" OFF)", cmake)
        self.assertIn("list(APPEND PT_SPEED_DEFINES -D PT_NO_LAMP_AROUND)", cmake)
        self.assertIn("target_compile_definitions(TGPathTracing.Native PRIVATE TGPT_NO_LAMP_AROUND=1)", cmake)
        around = common.split("#ifdef PT_NO_LAMP_AROUND", 1)[1].split("#endif", 1)[0]
        self.assertIn("float lamp_around() { return 0.0; }", around)
        self.assertIn("float lamp_around() { return float((lamp_trial() >> LAMP_TRIAL_AROUND) & 0xFFu) * 0.01; }", around)
        self.assertIn("#ifdef TGPT_NO_LAMP_AROUND", tracer)
        # Shader Execution Reordering's call is one line, with no branch around it (a measuring switch that had one was
        # taken out: speed-1.md), and the tracer's bit 8 runs the two passes as plain compute passes only where those
        # were made (TGPT_TEST_PLAIN_PASSES at the start).
        self.assertEqual(common.count("dx::MaybeReorderThread("), 1)
        self.assertIn("#define PT_REORDER(key) dx::MaybeReorderThread(uint(key) & 0xFFFFu, 16)", common)
        self.assertIn("st.plainPasses=(trial&8u)!=0 && st.gi && st.specular;", tracer)
        self.assertIn("if(!rays || st.plainPasses) { dispatch(list, pso, {}, barriers); return; }", tracer)
        # The speed-1 mode ends itself over 8 GB of its own memory (its one start was stopped by hand at 33 GB).
        test = self.text("tests/pathtracing_gpu_test.cpp")
        self.assertIn("else if(speed1Only) memoryCeilingMb=8192.0;", test)
        self.assertIn("if(HANDLE watch=CreateThread(nullptr, 0, memory_watch, nullptr, 0, nullptr)) CloseHandle(watch);", test)
        for name in ["pt_common.hlsli", "pt_water_surface.hlsli", "pt_gi.hlsl", "pt_gi_reuse.hlsl", "pt_specular.hlsl"]:
            inside = False
            for line in self.text(PT + "shaders/" + name).splitlines():
                stripped = line.strip()
                if stripped.startswith("#ifdef PT_SPEED_MEASURE"):
                    inside = True
                elif stripped.startswith("#else") or stripped.startswith("#endif"):
                    inside = False
                elif not stripped.startswith("//"):
                    self.assertTrue(inside or ("speed_measure(" not in line and "SPEED_M_" not in line and "g_speedNoRay" not in line), name + ": " + stripped)
        # Its own offline check (the same picture with the switch on, three views, 16 frames) and the game's answer: one
        # run of one scene, the switch off then on under NRD and with no denoiser; no neural step.
        checks = self.text("tests/pathtracing_speed1_checks.inl")
        for name in ("the speed switches leave the picture as it is (no pixel differs by more than two runs without them do)",
                     "the picture check sees a picture that did change"):
            self.assertIn('"' + name + '"', checks)
        self.assertIn('#include "pathtracing_speed1_checks.inl"', self.text("tests/pathtracing_gpu_test.cpp"))
        self.assertIn("f.prevCameraPosition[3]=reliefTrial+8.f*speedTrial;", self.text("tests/pathtracing_gpu_test.cpp"))
        import json
        recipe = json.loads(self.text("tests/ingame/scenarios.json"))["recipes"]["speed"]
        values = [s["value"] for step in recipe for s in step["set"] if s.get("key") == "Speed trial"]
        self.assertEqual(values, ["0", "1", "0", "0"])
        for step in recipe:
            self.assertNotIn("neural", step)

    def test_path_tracing_ser_split_is_the_rule(self):
        # PT-99 and PT-110. The game answered on 2026-10-10 (run 20261010-130621, the same picture, no
        # denoiser; the bounced light's stage, ms, a tavern / a town / a beach): its pass split in
        # two, the reuse across frames a compute pass of its own in the screen's order, and ONE
        # regrouping with no hint of our own after the pixel's first ray, outside the rays' loop: 9.5
        # / 22.7 / 28.9, where the pass as it shipped (the call inside that loop) took 41.9 / 57.7 /
        # 61.5 and the plain compute pass 13.7 / 27.6 / 64.6. That is the only form, in every build;
        # the compute passes take it too, without the call. The reflections' pass stays as it shipped
        # (the trial's 3-bit key for it was worse: the town 33.8 against 24.7 ms, the beach 101
        # against 93). The reordering study, the split trial, their switches, passes and recipes are
        # gone.
        cmake = self.text("CMakeLists.txt")
        tracer = self.text(PT + "src/tracer.cpp")
        common = self.text(PT + "shaders/pt_common.hlsli")
        gi = self.text(PT + "shaders/pt_gi.hlsl")
        own = self.text(PT + "shaders/pt_gi_own.hlsli")
        reuse = self.text(PT + "shaders/pt_gi_reuse.hlsl")
        specular = self.text(PT + "shaders/pt_specular.hlsl")
        shaders = self.text(PT + "shaders/pt_shaders.hpp.in")
        # No switch and no pass of the trials is left, anywhere.
        for name in ("pt_ser_split.hlsli", "pt_ser_study.hlsli"):
            self.assertFalse((ROOT / (PT + "shaders/" + name)).exists(), name)
        texts = {"CMakeLists.txt": cmake, "tracer.cpp": tracer, "pt_common.hlsli": common, "pt_gi.hlsl": gi, "pt_gi_reuse.hlsl": reuse,
                 "pt_specular.hlsl": specular, "pt_shaders.hpp.in": shaders}
        for word in ("SER_SPLIT", "SER_STUDY", "PT_SEEN_ONCE", "split_regroup_seen", "gi_pixel_split", "pt_ser_split", "pt_ser_study"):
            for name, text in texts.items():
                self.assertNotIn(word, text, name + ": " + word)
        for word in ("giSplit", "specularSplit", "serSplit", "giStudy", "specularStudy", "serStudy", "log_split_stack", "log_ser_stack",
                     "Split trial", "Reordering study"):
            self.assertNotIn(word, tracer)
        import json
        recipes = json.loads(self.text("tests/ingame/scenarios.json"))["recipes"]
        for name in ("ser-split", "ser-study", "ser-study-keys"):
            self.assertNotIn(name, recipes)
        # The bounced light's pass: one function for every form (gi_pixel), the ray generation entry and the compute
        # passes alike. Its ONE regrouping call is under PT_SER, with no hint of our own (the 16-bit material key cost it
        # 20 ms and more: docs/agents/ser-study-run.md), after the pixel's first ray is traced and before its hit is
        # fetched, outside the loop over the pixel's rays (the call inside that loop kept 2,592 bytes a thread by the
        # driver's count; this one 0). The pixel and its surface are read again after it.
        self.assertEqual(gi.count("dx::MaybeReorderThread("), 1)
        self.assertNotIn("PT_REORDER(", gi)
        self.assertNotIn("HitObject", gi)
        self.assertEqual(gi.count("void gi_pixel("), 1)
        self.assertEqual(gi.count("PT_ENTRY("), 1)
        pixel = gi.split("void gi_pixel(uint2 p) {", 1)[1]
        # (ON TRIAL, round 20: the key of the material the first ray met, 16 bits, behind the hidden Speed trial 64; the
        # call with no hint otherwise. The study's 20 ms were with 2,560 bytes a thread carried across the call; it carries
        # 0 since rule-2. The block of PT_SER is not closed before the call: its #if lines inside are matched.)
        call = pixel.index("    dx::MaybeReorderThread(regroup_key(material, cache_feeds(p, 0u)) & 0xFFFFu, 16);\n    p = DispatchRaysIndex().xy;\n    surface = RAB_GetGBufferSurface(int2(p), false); s = surface.s;\n")
        self.assertEqual(pixel.count("#ifdef PT_SER\n"), 1)
        guarded = pixel[pixel.index("#ifdef PT_SER\n") + len("#ifdef PT_SER\n"):call]
        self.assertEqual(guarded.count("#if"), guarded.count("#endif"))
        self.assertLess(pixel.index("query_from_surface("), call)
        self.assertLess(call, pixel.index("const uint rays = clamp(g.grid.w, 1u, 16u);"))
        self.assertLess(call, pixel.index("firstHit = fetch_hit(met.instance"))
        self.assertLess(pixel.index("firstHit = fetch_hit(met.instance"), pixel.index("for (uint k = 0; k < rays; ++k) {"))
        self.assertNotIn("MaybeReorderThread", pixel.split("for (uint k = 0; k < rays; ++k) {", 1)[1])
        self.assertEqual(pixel.count("shade_path("), 1)
        # The first ray is drawn as the loop's first turn would draw it: the same random numbers, in the same order.
        self.assertLess(pixel.index("if (behind > 0.0) firstBack = rng.next() < backShare;"),
                        pixel.index("const float3 firstDir = cosine_sample(firstBack ? -s.normal : s.normal, rng.next2());"))
        # query_from_surface is trace_from_surface up to its fetch: the same queries and the same rules (KEEP THE TWO SIDE
        # BY SIDE; one function for both would change the code of the reflections' pass too, which stays as it shipped).
        query_text = self.text(PT + "shaders/pt_query.hlsli")  # (moved out of pt_gi.hlsl 2026-10-11: the reflections read it too)
        query = query_text.split("bool query_from_surface(", 1)[1].split("\n}\n", 1)[0]
        trace = common.split("bool trace_from_surface(", 1)[1].split("\n}\n", 1)[0]
        for line in ("ray_seed(origin, dir);", "for (uint pass = 0; pass < 5 && t0 < tmax; ++pass) {",
                     "q.TraceRayInline(g_scene, PT_RAY_FLAGS, water_mask(mask), ray);",
                     "if (mask == MASK_STILL) { mask = MASK_ALL; t0 = max(t0, min(tmax, SWAY_REACH)); continue; }",
                     "if (mask != MASK_STILL && t < SWAY_REACH && (instances[q.CommittedInstanceID()].flags & INSTANCE_SWAYS) != 0) { mask = MASK_STILL; continue; }",
                     "if (pass < 4 && passes_through(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedTriangleFrontFace(), t, nearBack)) { t0 = t + 1e-3; continue; }",
                     "if (pass < 4 && first_person_inside(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedTriangleFrontFace())) { t0 = t + 1e-3; continue; }",
                     "if (speed_measure(SPEED_M_ONE_QUERY)) "):
            self.assertIn(line, query, line)
            self.assertIn(line, trace, line)
        self.assertIn("KEEP THE TWO SIDE BY SIDE", query_text)
        self.assertIn('#include "pt_query.hlsli"', gi)
        self.assertIn("pt_gi.hlsl query_from_surface", common)
        # The trial that stays and the measuring build keep their lines in the one function: the lamp trial (compiled out
        # by default) under PT_NO_LAMP_AROUND's #ifndef, drawn before the first ray as before the loop; the measuring
        # switches under PT_SPEED_MEASURE.
        self.assertEqual(pixel.count("#ifndef PT_NO_LAMP_AROUND"), 4)
        self.assertLess(pixel.index("lamp_around_light(around, offset, surface_near_back(s), false, false, rng, nearDir, nearPoint, nearNormal)"),
                        pixel.index("firstBack = rng.next() < backShare;"))
        self.assertIn("!speed_measure(SPEED_M_NO_RAY) && !speed_measure2(SPEED_M2_NO_RAY) &&\n        query_from_surface(", pixel)
        self.assertIn("!speed_measure2(SPEED_M2_NO_REUSE)", reuse)
        # Nothing in the screen's order after the regrouping: ReSTIR GI's reuse across frames is pt_gi_reuse's, a compute
        # pass in both shader models, which the tracer dispatches right after pt_gi in every frame, inside the bounced
        # light's stage. The frame's own sample travels whole (pt_gi_own.hlsli).
        self.assertNotIn("RTXDI_GITemporalResampling", gi)
        self.assertIn("    own_sample_store(p, r);\n}\nPT_ENTRY(gi_pixel, gi)\n", gi)
        self.assertIn("packed.packed_radiance = asuint(r.radiance.x); packed.unused = r.radiance.y; packed.packed_miscData_age_M = asuint(r.radiance.z);", own)
        self.assertIn("r.radiance = float3(asfloat(packed.packed_radiance), packed.unused, asfloat(packed.packed_miscData_age_M));", own)
        for line in ("RTXDI_GIReservoir r = own_sample_load(p);",
                     "RTXDI_RandomSamplerState rtxdiRng = RTXDI_InitRandomSampler(p, g.size.z, 5);",
                     "if (s.complex) tparams.enablePermutationSampling = false;",
                     "r = RTXDI_GITemporalResampling(p, surface, rtxdi_motion(p, s), g.rtxdi.w, r, rtxdiRng, g.rtxdiRuntime, g.rtxdiReservoirs, tparams);",
                     "if ((g.size.w & FLAG_RESET) == 0 && (g.settings2.y & OPTION_NO_TEMPORAL) == 0 && all(g.prevOriginDelta.xyz == 0.0)) {",
                     "if (!(r.weightSum < 1e30) || !all(r.radiance < 1e30)) r = RTXDI_EmptyGIReservoir();\n    RTXDI_StoreGIReservoir(r, g.rtxdiReservoirs, p, g.rtxdi.z);"):
            self.assertIn(line, reuse)
        self.assertIn('#include "pt_gi_own.hlsli"', gi)
        self.assertIn('#include "pt_gi_own.hlsli"', reuse)
        self.assertIn('"${PT_SHADER_DIR}/pt_gi_own.hlsli"', cmake.split("set(PT_SHADER_DEPENDS", 1)[1].split(")", 1)[0])
        self.assertIn("foreach(entry prepare:pt_prepare probe:pt_probe gi:pt_gi giReuse:pt_gi_reuse giSpatial:pt_gi_spatial", cmake)
        for name in ("pt_gi_reuse.h", "pt_gi_reuse_dxr12.h"):
            self.assertIn('#include "' + name + '"', shaders)
        self.assertNotIn("#if", shaders)
        self.assertIn("if(sm69) TGPT_PASS(g, giReuse, giReuseDxr12); else TGPT_PASS(g, giReuse, giReuse);", tracer)
        self.assertIn("st.gi=p.gi; st.giReuse=p.giReuse;", tracer)
        self.assertIn("st.gi.Reset(); st.giReuse.Reset();", tracer)
        self.assertIn("dispatch_pass(list, st.giSer.Get(), 0, st.gi.Get(), b);\n    dispatch(list, st.giReuse.Get(), {}, b);\n    b.flush(list); mark(5);", tracer)  # (mark 5 since the caustics' stage moved before it, PT-130; 4 since the sky's, PT-112)
        self.assertIn("st.serTable=make_buffer(st.d12.Get(), 2*D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT,", tracer)
        # In every build: the driver's own count of what a thread of each reordering pass needs of stack, once a start
        # (the coordinator, 2026-10-10: "it is cheap and it is the measure").
        self.assertIn("log(\"Shader Execution Reordering: the driver's stack a thread, the bounced light's pass \"+std::to_string(gi->GetShaderStackSize(L\"gi\"))+", tracer)
        # The reflections' pass: its key by the material and the cache's feeders, or with no hint in a frame the tracer finds
        # that faster (R19, State::ReflectionsSort: the frame word's bit 4), ONE call with its bit count worked out (DXC
        # copies a call whose bits are another constant on another path), inside PT_SER; its guide cleared at its start.
        self.assertEqual(specular.count("MaybeReorderThread("), 1)
        self.assertIn("#ifdef PT_SER\n    // With SER: the shading of what is seen", specular.replace("\r\n", "\n"))
        self.assertIn("dx::MaybeReorderThread(regroup_key(keyMaterial, cache_feeds(p, 2u)) & 0xFFFFu, select((g.size.w & 4u) != 0u, 0u, 16u));", specular)

        self.assertNotIn("PT_REORDER(", specular)
        self.assertIn("    seenGuide[p] = uint2(0u, 0u);\n    Rng rng = make_rng(p, g.size.z, 5);", specular)
        self.assertEqual(common.count("dx::MaybeReorderThread("), 1)
        self.assertIn("if(noHint) k.size[3]|=4u;", tracer)
        self.assertIn("sort_measured(index, double(t[kReflectionsStage+1]-t[kReflectionsStage])/st.ticksPerMs,", tracer)
        self.assertEqual(tracer.count("\"reflections\", \"radiance cache\""), 1) # (stage 5 is the reflections')
        self.assertIn("#ifdef PT_SER\n#define PT_REORDER(key) dx::MaybeReorderThread(uint(key) & 0xFFFFu, 16)\n", common)
        # The plain compute passes stay a yardstick for tests ("Speed trial" 8, made beside the reordering ones only with
        # TGPT_TEST_PLAIN_PASSES or in a measuring build): never a choice that turns reordering off (AGENTS.md).
        self.assertIn("const bool plainToo=GetEnvironmentVariableA(\"TGPT_TEST_PLAIN_PASSES\", nullptr, 0)>0;", tracer)
        self.assertIn("#ifdef TGPT_SPEED_MEASURE\n    const bool plainToo=true;", tracer)

    def test_path_tracing_rain_wetness_on_trial_default_off(self):
        # PT-108 and PT-104, ON TRIAL. When it rains the game's ScreenSpaceWetness pass rewrites the
        # roughness HDRP lights its G-buffer with (and nothing else: no normal, no colour), by its
        # moisture and a noise picture laid over the world. The tracer makes the same roughness at
        # every hit, for every kind of ray. Hidden, off by default, and compiled into the passes only
        # by CMake's TGPT_RAIN_WETNESS (OFF): a trial costs frame time switched off.
        common = self.text(PT + "shaders/pt_common.hlsli")
        tracer = self.text(PT + "src/tracer.cpp")
        header = self.text(PT + "include/tgpt_api.h")
        cmake = self.text("CMakeLists.txt")
        settings = self.text("src/TGPathTracing/Settings.cs")
        plugin = self.text("src/TGPathTracing/RainWetness.cs")
        self.assertIn('RainWetness = file.Bind(Advanced, "Rain wetness", 0, Hidden(', settings)
        self.assertIn('option(TGPT_RAIN_WETNESS "TG Path Tracing: compile the rain\'s wetness (on trial) into the passes" OFF)', cmake)
        self.assertIn("list(APPEND PT_SPEED_DEFINES -D PT_RAIN_WETNESS)", cmake)
        self.assertIn("target_compile_definitions(TGPathTracing.Native PRIVATE TGPT_RAIN_WETNESS=1)", cmake)
        self.assertIn("#ifndef TGPT_RAIN_WETNESS", tracer)
        # Every line of it in the shaders lies between #ifdef PT_RAIN_WETNESS and its #endif: the function and its one call.
        self.assertEqual(common.count("#ifdef PT_RAIN_WETNESS"), 2)
        inside = False
        for line in common.splitlines():
            stripped = line.strip()
            if stripped.startswith("#ifdef PT_RAIN_WETNESS"):
                inside = True
            elif stripped.startswith("#endif"):
                inside = False
            elif not stripped.startswith("//") and not stripped.startswith("float4 rain;"):
                self.assertTrue(inside or ("rain_roughness(" not in line and "rain_on()" not in line and "g.rain" not in line), stripped)
        # It travels in a record of its own (TGPT_SetRain: no field of TGPT_Frame, no option bit), with the frame it is for.
        self.assertIn("TGPT_EXPORT int32_t TGPT_CALL TGPT_SetRain(const TGPT_Rain* rain);", header)
        self.assertIn('static_assert(sizeof(TGPT_Rain)==64, "TG Path Tracing rain ABI");', header)
        self.assertIn("slot.rain=e.rain;", self.text(PT + "src/extension.cpp"))
        self.assertIn("st.rain=slot.rain;", tracer)
        self.assertIn("Marshal.SizeOf(typeof(NativeRain)) != 64", self.text("src/TGPathTracing/NativeTracer.cs"))
        self.assertIn("RainWetness.Send(tracer, settings);", self.text("src/TGPathTracing/HdrpPasses.cs"))
        # The noise is the game's own texture, opened with no copy and resolved each frame; without it the frame is dry.
        self.assertIn("if(asked) share::resolve(&noise, 1); else noise=-1;", tracer)
        self.assertIn("k.rain[0]=on ? r.moisture : 0.f;", tracer)
        self.assertIn("TextureShare.Handle(noise); TextureShare.WatchStandalone(noise);", plugin)
        self.assertIn("bool rain_on() { return g.rain.x > 0.0 && asuint(g.rain.w) != INVALID && g.textures.x != INVALID; }", common)
        # The game's state where the game leaves it: its controller's moisture, 0 while its pass is off.
        self.assertIn('typeof(PrecipitationController).GetField("_moisture", Own)', plugin)
        self.assertIn("bool on = pass.enabled && volume != null && volume.isActiveAndEnabled && shelter != null && shelter.IsInitialized;", plugin)
        # The game's own rule, by the hit's place in the world and the camera's distance: nothing of the screen.
        body = common.split("float rain_roughness(", 1)[1].split("\n}", 1)[0]
        for token in ["const float2 world = hit_world(h).xz;",
                      "picture.SampleGrad(s_linearRepeat, world * 0.1, h.footX.xz * 0.1, h.footY.xz * 0.1).r;",
                      "const float away = min(length(h.position - g.cameraPos.xyz) * 0.01, 1.0);",
                      "const float soaked = saturate(g.rain.y + away * (0.75 * cloud - g.rain.y));",
                      "float puddle = saturate(5.0 * (1.2 * moisture - cloud));",
                      "puddle = puddle * puddle * (3.0 - 2.0 * puddle);",
                      "const float wet = saturate(saturate(puddle * (up * up * up * up * up) + moisture) - 0.33 * g.rain.z);",
                      "return saturate(1.0 - (smoothness + wet * (soaked - smoothness)));"]:
            self.assertIn(token, body)
        for screen in ["g.size", "inputs0", "inputs1", "ResourceDescriptorHeap[g.surface", "DispatchThreadID"]:
            self.assertNotIn(screen, body)
        # Last in a hit's material, after the decals (the game's pass comes after its G-buffer), and not on what HDRP shades
        # forward with a shine of its own nor on an unlit surface.
        material = common.split("SurfaceMaterial hit_material(Hit h) {", 1)[1].split("\n}", 1)[0]
        self.assertLess(material.index("apply_decals(h, s, base, finish.x);"), material.index("s.roughness = rain_roughness(h, s.normal, s.roughness);"))
        self.assertIn("(m.flags & (MATERIAL_GLASS | MATERIAL_EYE | MATERIAL_CAMERA_ONLY | MATERIAL_STRANDS | MATERIAL_HAIR_LAYER)) == 0)", material)
        # The game's answer: one run of one scene, the game, then no denoiser with the switch off and on; no neural step.
        import json
        recipe = json.loads(self.text("tests/ingame/scenarios.json"))["recipes"]["rain"]
        values = [s["value"] for step in recipe for s in step["set"] if s.get("key") == "Rain wetness"]
        self.assertEqual(values, ["0", "1", "0"])
        self.assertEqual([s["value"] for step in recipe for s in step["set"] if s.get("key") == "Denoising"], ["None", "Ray Reconstruction"])
        self.assertEqual([step["frames"] for step in recipe if step["traced"]], [16, 16])
        for step in recipe:
            self.assertNotIn("neural", step)

    def test_path_tracing_mega_geometry_on_trial_beside_the_plain_path(self):
        # PT-109, ON TRIAL: NVIDIA RTX Mega Geometry, through NVAPI. A path BESIDE the plain
        # acceleration structures: compiled in only by CMake's TGPT_MEGA_GEOMETRY (OFF: a build
        # without it holds no line of it and links no NVAPI), asked for by a hidden setting that is 0,
        # and taken only where the driver itself reports the feature (never a table of cards); the
        # plain path serves everywhere else and whenever a call fails. The picture does not change:
        # the same instances.
        cmake = self.text("CMakeLists.txt")
        scene = self.text(PT + "src/scene.cpp")
        scene_h = self.text(PT + "src/scene.hpp")
        tracer = self.text(PT + "src/tracer.cpp")
        mega = self.text(PT + "src/mega_geometry.cpp")
        mega_h = self.text(PT + "src/mega_geometry.hpp")
        header = self.text(PT + "include/tgpt_api.h")
        settings = self.text("src/TGPathTracing/Settings.cs")
        self.assertIn('option(TGPT_MEGA_GEOMETRY "TG Path Tracing: compile the NVIDIA RTX Mega Geometry path (through NVAPI; on trial) in" OFF)', cmake)
        block = cmake.split("  if(TGPT_MEGA_GEOMETRY)\n    # NVAPI's static library", 1)[1].split("  endif()", 1)[0]
        for token in ["target_sources(TGPathTracing.Native PRIVATE native/pathtracing/src/mega_geometry.cpp)",
                      'target_link_libraries(TGPathTracing.Native PRIVATE "${TG_NVAPI}/amd64/nvapi64.lib" meshoptimizer)',
                      "target_compile_definitions(TGPathTracing.Native PRIVATE TGPT_MEGA_GEOMETRY=1)"]:
            self.assertIn(token, block)
        # NVAPI is a download beside the SDKs, never committed; its notice goes with a build that links it.
        self.assertIn("/NVAPI-*/", self.text(".gitignore"))
        self.assertIn('install(FILES "${TG_NVAPI}/License.txt" DESTINATION TGPathTracing/Notices RENAME LICENSE-NVAPI.txt COMPONENT TGNative)', cmake)
        # PT-109's second piece (docs/agents/megageo-4.md): NVIDIA's clusterizer, meshoptimizer (MIT), fetched by CMake at a
        # release and only with the switch, never copied in; its notice beside the DLL and in LICENSES/ and docs/SOURCES.md.
        switched = cmake.split("  if(TGPT_MEGA_GEOMETRY)\n    set(NVAPI_ROOT", 1)[1].split("\n  endif()", 1)[0]
        self.assertIn("FetchContent_Declare(meshoptimizer GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git GIT_TAG v1.3 GIT_SHALLOW TRUE)", switched)
        self.assertIn('install(FILES "${meshoptimizer_SOURCE_DIR}/LICENSE.md" DESTINATION TGPathTracing/Notices RENAME LICENSE-meshoptimizer.md COMPONENT TGNative)', cmake)
        self.assertIn("meshoptimizer", self.text("LICENSES/THIRD_PARTY.md"))
        self.assertIn("meshoptimizer v1.3", self.text("docs/SOURCES.md"))
        self.assertFalse(any(path.name.startswith("meshopt") for path in (ROOT / "native").rglob("*")), "no meshoptimizer source copied in")
        # Nothing of it outside the switch: nvapi.h is named by one file, and the tracer's other sources reach that file's
        # header and its names only between #if TGPT_MEGA_GEOMETRY and the #else or #endif that ends it.
        for name in ["scene.cpp", "scene.hpp", "scene_water.cpp", "tracer.cpp", "extension.cpp"]:
            text = self.text(PT + "src/" + name)
            self.assertNotIn("nvapi", text.lower(), name)
            inside = 0
            for line in text.splitlines():
                stripped = line.strip()
                if stripped.startswith("#if TGPT_MEGA_GEOMETRY"):
                    inside += 1
                elif inside and (stripped.startswith("#endif") or stripped.startswith("#else")):
                    inside -= 1
                elif not inside and not stripped.startswith("//"):
                    code = line.split("//", 1)[0]
                    for token in ["megaTop_", "megaCaps", "megaTrial", "megaStill", "megaFresh_", "mega_fresh(", "MegaTopLevel", "mega_probe(", "mega_geometry.hpp", "partitioned=", "partitioned ?"]:
                        self.assertNotIn(token, code, name + ": " + stripped)
            self.assertEqual(inside, 0, name)
        self.assertIn("#include <nvapi.h>", mega)
        # Asked of the driver, on NVIDIA's cards alone, once the shared device is there; what it answers is logged.
        self.assertIn("if(vendor!=0x10DE) {", mega)
        self.assertIn("NvAPI_D3D12_GetRaytracingCaps(device, NVAPI_D3D12_RAYTRACING_CAPS_TYPE_PARTITIONED_TLAS, &topCaps, sizeof(topCaps));", mega)
        self.assertIn("NvAPI_D3D12_GetRaytracingCaps(device, NVAPI_D3D12_RAYTRACING_CAPS_TYPE_CLUSTER_OPERATIONS, &clusterCaps, sizeof(clusterCaps));", mega)
        self.assertIn("st.scene.megaCaps=mega_probe(device, vendor, megaLines);", tracer)
        # The switch: hidden, 0, in the frame's flags (no option bit: all 32 are taken), and only
        # where the driver reports it.
        self.assertIn('MegaGeometryTrial = file.Bind(Advanced, "Mega Geometry trial", 7, Hidden(', settings)
        self.assertNotIn('"Mega Geometry trial", 0, Hidden(', settings)
        self.assertIn("#define TGPT_FRAME_MEGA_SHIFT 2u", header)
        self.assertIn("#define TGPT_FRAME_MEGA_MASK 15u", header)
        self.assertIn("frame.Flags |= (uint)Mathf.Clamp(settings.MegaGeometryTrial.Value, 0, (int)Tgpt.FrameMegaMask) << Tgpt.FrameMegaShift;", self.text("src/TGPathTracing/HdrpPasses.cs"))
        self.assertIn("internal const int FrameMegaShift = 2;", self.text("src/TGPathTracing/NativeTracer.cs"))
        self.assertIn("const uint32_t trial=(f.flags>>TGPT_FRAME_MEGA_SHIFT)&TGPT_FRAME_MEGA_MASK;", tracer)
        # (1, 2, 3: its cells of 64, 128 or 256 m, for the game to say which suits its world; megageo-5: and under 7 and 8, NVIDIA's
        # layout of every plant's own bottom level of clusters, at 128 m.)
        self.assertIn("const bool megaTopLevel=((megaTrial>=1 && megaTrial<=3) || megaTrial==7 || megaTrial==8) && megaCaps.partitionedTop && !megaTop_.failed();", scene)
        self.assertIn("const uint32_t grid=megaTrial==1 ? 32u : megaTrial==3 ? 8u : 16u;", scene)
        # megageo-5: the clusters carry micromaps as NVAPI documents them: NVAPI's own arrays from the same bake, FLAG_ALLOW_OMM on
        # every cluster call that builds or reads them, the pipelines made with its micromap support beside the clusters'; and a
        # cut keeps each triangle's own corners in their own order (a micromap hangs on them).
        for token in ["NvAPI_D3D12_GetRaytracingCaps(device, NVAPI_D3D12_RAYTRACING_CAPS_TYPE_OPACITY_MICROMAP, &ommCaps, sizeof(ommCaps));",
                      "const NvAPI_Status built=NvAPI_D3D12_BuildRaytracingOpacityMicromapArray(list, &build);",
                      "NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_CLUSTER_SUPPORT|(caps.omm ? NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_OMM_SUPPORT : 0u);",
                      "a.opacityMicromapIndexFormat=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INDEX_FORMAT_32BIT;",
                      "for(int v=0;v<3;++v) out.indices[put+v]=idx[number*3+v];"]:
            self.assertIn(token, mega)
        self.assertEqual(mega.count("(omm ? NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_ALLOW_OMM : NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NONE)"), 2)
        self.assertIn("constexpr uint32_t kMegaClusterTriangles=64, kMegaClusterVertices=64;", mega_h)
        self.assertIn("constexpr float kTopSpan=2048.f;", mega)
        self.assertNotIn("constexpr bool megaTopLevel=false;", scene) # (PT-110's M5: outside the build that has it nothing reads megaTopLevel any more)
        self.assertIn("Mega Geometry trial: this build has it compiled out (configure with -DTGPT_MEGA_GEOMETRY=ON to try it): nothing changes", tracer)
        # The plain top level stays whole, and serves whenever the partitioned one recorded nothing (its address 0).
        self.assertIn("    if(!partitioned)\n#endif\n    {\n    // The top level: rebuilt each frame (it is small next to the tracing).", scene)
        self.assertIn("in.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;\n    in.NumDescs=UINT(descs.size());", scene)
        self.assertIn("    tlas=partitioned ? partitioned : tlasBuffer_.va(); hasTlas=true;\n#else\n    tlas=tlasBuffer_.va(); hasTlas=true;\n#endif", scene)
        # The same instances: a slot is written with the frame's own instance, field for field, and what a partitioned top
        # level cannot say (an instance's "no micromaps" flag) sends the frame to the plain build.
        for token in ["std::memcpy(w.transform, d.Transform, sizeof(w.transform));",
                      "w.instanceID=d.InstanceID; w.instanceMask=d.InstanceMask; w.instanceContributionToHitGroupIndex=d.InstanceContributionToHitGroupIndex;",
                      "w.instanceFlags=d.Flags; w.instanceIndex=uint32_t(i); w.partitionIndex=partition; w.accelerationStructure=d.AccelerationStructure;",
                      "for(size_t i=0;i<count;++i) if(descs[i].Flags&~kTopFlags) {",
                      "in.maxInstancePerPartitionCount=capacity; in.maxInstanceInGlobalPartitionCount=capacity;",
                      'lines.push_back("Mega Geometry: the partitioned top level failed ("+what+"); the plain top level serves from now on");']:
            self.assertIn(token, mega)
        # Under it an instance without its mesh's micromaps takes the mesh's plain bottom level (the same triangles) in
        # place of the micromap one with DISABLE_OMMS: the rule under the plain top level too since PT-110's M5 (2026-10-10).
        self.assertEqual(scene.count("const bool linked=m->omm==Mesh::Omm::Linked && omm;"), 2) # (PT-110's M5: under either top level now)
        self.assertNotIn("waterLinked", scene) # (the same: HDRP's water takes its mesh's plain bottom level, always)
        # A slot is also written again when a still mesh's bottom level was built anew at the address the slot names (a new
        # mesh's, a compacted copy, one with micromaps: a new structure may stand where a retired one stood).
        self.assertEqual(scene.count("mega_fresh("), 4) # (the fourth since PT-110's M5: a structure with micromaps, compacted)
        self.assertIn("void mega_fresh(D3D12_GPU_VIRTUAL_ADDRESS va) { if(va && megaTop_.bytes()) megaFresh_.push_back(va); }", scene_h)
        self.assertIn("const bool rebuilt=!fresh.empty() && std::binary_search(fresh.begin(), fresh.end(), d.AccelerationStructure);", mega)
        self.assertIn("if(partitioned || !megaTop_.bytes()) megaFresh_.clear();", scene)
        # What is posed, refitted or moved each frame is told to the driver each frame: its bottom level's bounds changed.
        self.assertIn("} else if(moves) updates.push_back(TopUpdate{uint32_t(i), d.InstanceContributionToHitGroupIndex, d.AccelerationStructure});", mega)
        self.assertIn("const size_t megaStill=descs.size();", scene)
        # It goes with the scene, and is counted with the tracer's memory while it serves.
        self.assertIn("megaTop_.release(release, retireValue); // PT-109: the partitioned top level goes with the scene", scene)
        self.assertIn("d.acceleration+=megaTop_.bytes(); d.scratch+=megaTop_.scratch_bytes();", scene)
        self.assertIn("MegaTopLevel megaTop_;", scene_h)
        self.assertIn("class MegaTopLevel {", mega_h)
        # What the structures cost is in the log every 10 s, for every build ("PT structures"): a count and a timestamp.
        self.assertIn('log("PT structures "+std::to_string((now-st.lastSummary)/1000)+"s: "+text+tail);', tracer)
        self.assertIn("void structure_stamp(uint32_t kind) { if(structureStamp) structureStamp(kind); }", scene_h)
        # Its own check, alone (never in the whole test), and the game's answer: plain and partitioned, frozen and running.
        test = self.text("tests/pathtracing_gpu_test.cpp")
        self.assertIn('if(GetEnvironmentVariableA("TGPT_TEST_MEGA_ONLY", nullptr, 0)>0)\n#include "pathtracing_mega_checks.inl"', test)
        self.assertIn("f.flags|=(megaTrial&TGPT_FRAME_MEGA_MASK)<<TGPT_FRAME_MEGA_SHIFT;", test)
        import json
        recipes = json.loads(self.text("tests/ingame/scenarios.json"))["recipes"]
        values = [s["value"] for step in recipes["mega"] for s in step["set"] if s.get("key") == "Mega Geometry trial"]
        self.assertEqual(values, ["0", "1", "1", "2", "3", "0", "0"])
        # megageo-5: the walk, the trial at 0 and 7 in turn, each the same straight route, its frames a second at a time.
        walk = [step for step in recipes["mega-walk"] if step["traced"]]
        self.assertEqual([s["value"] for step in walk for s in step["set"] if s.get("key") == "Mega Geometry trial"], ["0", "7", "0", "7"])
        self.assertEqual({(step["walkForward"], step["walkSpeed"], step["perfSeconds"], step["perSecond"], step["live"]) for step in walk}, {(150, 5, 34, True, True)})
        self.assertEqual([step["frames"] for step in recipes["mega"] if step["traced"]], [16, 16, 4, 4, 4, 4])
        self.assertEqual([bool(step.get("live")) for step in recipes["structures"] if step["traced"]], [False, True])
        for step in recipes["mega"] + recipes["structures"] + recipes["mega-walk"] + recipes["mega-plants"]:
            self.assertNotIn("neural", step)

    def test_path_tracing_record_keeps_only_what_it_can_prove(self):
        # PT-99 (docs/agents/reccpu.md): the CPU's work to record a frame's scene made cheaper without changing a byte of
        # what it records. What is kept between frames is kept only while everything it was made from is what it was.
        scene = self.text(PT + "src/scene.cpp")
        hpp = self.text(PT + "src/scene.hpp")
        grid = self.text(PT + "src/light_grid.hpp")
        cmake = self.text("CMakeLists.txt")
        test = self.text("tests/light_grid_test.cpp")
        # The light grid's cells: one walk a light, held against the loop it took the place of, byte by byte, on the CPU.
        self.assertIn("inline Counts build_cells(const Shape& shape, const double corner[3], const TGPT_Light* lights, size_t lightCount, const TGPT_Light* clusters, size_t clusterCount,", grid)
        self.assertIn("inline Counts build_cells_reference(const Shape& shape, const double corner[3], const TGPT_Light* lights, size_t lightCount, const TGPT_Light* clusters, size_t clusterCount,", grid)
        self.assertIn("d2+=ty; d2+=tz;", grid)  # the sum in the loop's own order: x, then y, then z
        self.assertIn("const bool ok=same(a.cells, b.cells) && same(a.indices, b.indices) && same(a.weights, b.weights) &&", test)
        self.assertIn("    add_executable(tgpt_light_grid_test tests/light_grid_test.cpp)\n    target_include_directories(tgpt_light_grid_test PRIVATE ${PT_INCLUDE})\n"
                      "    add_test(NAME tgpt_light_grid COMMAND tgpt_light_grid_test)\n", cmake)
        # The grid is made anew when one of the numbers it was made for differs: the numbers themselves are compared, in the
        # order they were hashed in until 2026-10-12. Nothing was taken out of them.
        self.assertIn("if(!gridBuilt_ || key!=gridKey_) {", scene)
        self.assertIn("key.push_back(l.type); key.push_back(uint64_t(std::llround(double(l.range)*16.0))); key.push_back(l.flags&TGPT_LIGHT_NO_RANGE);", scene)
        self.assertIn("key.push_back(uint64_t(std::llround(double(l.range)*16.0))); key.push_back(uint64_t(std::llround(double(l.color[0])*16.0)));", scene)
        # The plants' list is kept only while the scene's list, the materials, the meshes and the winds' materials are the
        # ones it was made from, never while the Mega Geometry trial splits cells, and never after a check found a difference.
        self.assertIn("const bool kept=plan.valid && !plan.off && !split && sameList && sameMaterials && sameMeshes && sameWinds;", scene)
        self.assertIn("const bool sameList=plan.listEpoch==listEpoch_, sameMaterials=plan.materialsEpoch==materialsEpoch_, sameMeshes=plan.meshEpoch==meshEpoch_,", scene)
        # (The list's epoch: counted on in set_lists unless the new list is the old one byte for byte, and in clear.)
        self.assertIn("if(!same) ++listEpoch_;\n        sceneList_=list; instances_=list->instances; materialIndices_=list->materialIndices; indicesDirty_=true;", scene)
        self.assertEqual(scene.count("++listEpoch_;"), 2)
        self.assertIn("instances_.clear(); materialIndices_.clear(); sceneList_.reset(); gridBuilt_=false; gridKey_.clear(); gridKeyLights_=0;\n    ++listEpoch_;", scene)
        self.assertEqual(len(re.findall(r"(?<![A-Za-z_])instances_\.(clear|assign|resize|push_back|swap|erase|insert)\(", scene)), 1)
        self.assertEqual(len(re.findall(r"(?<![A-Za-z_])materialIndices_\.(clear|assign|resize|push_back|swap|erase|insert)\(", scene)), 1)
        # Every place that writes what the list is made from counts its epoch on. A NEW place that writes materials_,
        # instances_, materialIndices_, a mesh's being ready, the mesh table or the mesh and geometry records must do so too:
        # these counts are here so that such a place is not added without it.
        self.assertEqual(len(re.findall(r"(?<![A-Za-z])materials_\s*=[^=]", scene)), 1)
        self.assertIn("materials_=e.materials; e.materialsDirty=false; materialsDirty_=true;\n            ++materialsEpoch_;", scene)
        self.assertEqual(len(re.findall(r"(?<![A-Za-z_])instances_\s*=[^=]", scene)), 1)
        self.assertEqual(len(re.findall(r"(?<![A-Za-z_])materialIndices_\s*=[^=]", scene)), 1)
        self.assertIn("sceneList_=list; instances_=list->instances; materialIndices_=list->materialIndices; indicesDirty_=true;", scene)
        self.assertEqual(scene.count("++meshEpoch_;"), 4)
        self.assertIn("if(!added.empty()) ++meshEpoch_;", scene)                         # intake: a handle's mesh is another object
        self.assertIn("if(!converted.empty()) {\n        ++meshEpoch_;", scene)             # record: meshes become ready, their records are written
        self.assertIn("meshes_[handle-1].reset(); meshRecordsDirty_=true; emittersDirty_=true;\n        ++meshEpoch_;", scene)  # retire_meshes
        self.assertIn("meshes_.clear(); pending_.clear(); retiredMeshes_.clear();\n    ++meshEpoch_; plantPlan_=PlantPlan{};", scene)  # clear
        self.assertEqual(scene.count("m->state=Mesh::State::Ready;"), 1)
        self.assertEqual(scene.count("geometryRecords_.push_back("), 1)
        self.assertEqual(len(re.findall(r"meshRecords_\[[^\]]*\]\s*=[^=]", scene)), 2)   # a new mesh's record, a retired mesh's
        self.assertEqual(len(re.findall(r"meshes_\[[^\]]*\]\s*=[^=]", scene)), 1)        # intake
        # The way the list was made until then stays in the code, is compared with the kept one (every frame with the
        # environment's TGPT_TEST_RECORD_CHECK, every 512th kept frame without), and takes over at the first difference.
        self.assertIn("void GpuScene::plan_plant_list_reference(PlantPlan& out, bool split) {", scene)
        self.assertIn("if(recordCheck_ || (kept && (++plan.sinceCheck&511u)==0)) {", scene)
        self.assertIn("plan.off=true; plan.cells=std::move(was.cells); plan.members=std::move(was.members); plan.order=std::move(was.order);", scene)
        self.assertIn("if(plan.off) plan_plant_list_reference(plan, split);", scene)
        # A plant's turn at its mesh's micromaps stays every frame's, in the order of the scene's list.
        part = scene.split("for(const uint32_t at:plan.order) {", 1)[1].split("record_cpu.section(", 1)[0]
        self.assertIn("member.link=0; member.pending=false;", part)
        self.assertIn("bool mapped=omm_instance(*m, mats, retireValue);", part)
        # A key is the same FNV number (a group's members stand in their keys' order), kept an instance.
        self.assertEqual(scene.count("mix(&inst.mesh, 4); mix(inst.transform, sizeof(inst.transform)); mix(&inst.flags, 4); mix(&inst.renderingLayerMask, 4);"), 2)
        self.assertIn("if(plan.keyGeometries[source]!=own.geometryCount) {", scene)
        self.assertIn("uint64_t materialsEpoch_=1, meshEpoch_=1;", hpp)
        # The grid's build runs beside the record, on another thread, and the record WAITS for it before the grid's lists go
        # up: never a frame late. It starts only after the emitters (the clusters it lists are theirs), it is the one use of
        # another thread in the record, it calls none of the record's clocks, and the record sets the corner once it is done.
        self.assertEqual(scene.count("std::async("), 1)
        begun = scene.index("gridJob=std::async(std::launch::async, [this] { build_light_grid(gridJobCorner_); });")
        waited = scene.index("gridJob.get(); // (what the build threw, if anything, is thrown here)")
        emitters = scene.index("build_emitters(device, list, heap, ring, release, retireValue, origin, sources, barriers);")
        uploads = scene.index("upload_light_lists(device, list, heap, ring, release, retireValue, barriers); // (gridIndexBuffer_")
        self.assertTrue(emitters < begun < waited < uploads)
        between = scene[begun:waited]
        for theirs in ("clusterLights_", "clusterBase", "gridCells_", "gridIndices_", "gridWeights_", "gridScratch_", "farCells_", "farEntries_", "farLights_",
                       "farCorner_", "farUpload", "gridCorner_", "lights_=", "gridStage", "gridStaged_"):
            self.assertNotIn(theirs, between, theirs)
        build = scene.split("void GpuScene::build_light_grid(const double corner[3]) {", 1)[1].split("\nvoid GpuScene::upload_light_lists(", 1)[0]
        self.assertNotIn("record_cpu.", build)
        self.assertNotIn("memcpy(gridCorner_", build)
        for d3d in ("device->", "list->", "heap.", "ring.", "release.add("):
            self.assertNotIn(d3d, build, d3d)
        self.assertIn("std::memcpy(gridCorner_, gridJobCorner_, sizeof(gridCorner_));", scene)
        # A plant group keeps the description its refits name (made by plant_geometry at its first pose); no group is copied.
        self.assertIn("PlantGeometry geometry; bool geometryMade=false;", hpp)
        self.assertEqual(len(re.findall(r"(?<![A-Za-z_])PlantGroup [a-z]+=\*", scene)), 0)
        # The water's units are found through an index of the map's own units, written wherever the map gains or loses one.
        water = self.text(PT + "src/scene_water.cpp")
        self.assertIn("else { held=&waterUnits_[unitKey]; waterUnitIndex_.emplace(unitKey, held); }", water)
        self.assertEqual(water.count("waterUnits_.erase("), 2)
        self.assertIn("waterUnits_.erase(unitKey); waterUnitIndex_.erase(unitKey);", water)
        self.assertIn("waterUnitIndex_.erase(it->first); it=waterUnits_.erase(it);", water)
        self.assertEqual(water.count("waterUnits_.clear("), 1)
        self.assertIn("waterUnits_.clear(); waterUnitIndex_.clear();", water)
        self.assertEqual(water.count("waterUnits_["), 2)  # (the one place a unit is put in, and its comment)
        self.assertNotIn("waterUnits_", scene.replace("stats_.waterUnitsKept=uint32_t(waterUnits_.size());", ""))

    def test_path_tracing_lamp_tree_is_the_rule(self):
        # PT-99 (docs/agents/perf-1.md "perf2", docs/agents/rule-1.md): a ray off water or see-through glass asked EVERY lamp
        # of the scene whether it runs into its ball (the far list holds every lamp that falls off with distance alone), at
        # every turn of every water pixel's path. Since 2026-10-10 the same lamps are found by a search tree made once a
        # frame, in every build and with no switch (the game, run 20261010-092027: the beach's frame 192 -> 161 ms, the
        # same picture). Of the two trials tried beside it the feeders' key is the rule too (the tavern's reflections
        # 11.4 -> 9.9 ms), and the water's short rays came out (no gain that could be told from the noise).
        cmake = self.text("CMakeLists.txt")
        common = self.text(PT + "shaders/pt_common.hlsli")
        path = self.text(PT + "shaders/pt_water_path.hlsli")
        gi = self.text(PT + "shaders/pt_gi.hlsl")
        specular = self.text(PT + "shaders/pt_specular.hlsl")
        scene = self.text(PT + "src/scene.cpp")
        tree = self.text(PT + "src/lamp_tree.hpp")
        tracer = self.text(PT + "src/tracer.cpp")
        settings = self.text("src/TGPathTracing/Settings.cs")
        # No switch is left: not in CMake, not in a pass, not in the tracer, not in the plugin. (A trial that is switched
        # off still costs frame time in the big passes: AGENTS.md.)
        sources = {"CMakeLists.txt": cmake, "pt_common.hlsli": common, "pt_water_path.hlsli": path, "pt_gi.hlsl": gi, "pt_specular.hlsl": specular,
                   "scene.cpp": scene, "scene.hpp": self.text(PT + "src/scene.hpp"), "lamp_tree.hpp": tree, "tracer.cpp": tracer, "Settings.cs": settings}
        for name in ["pt_walk.hlsl", "pt_dielectric.hlsli", "pt_water_sun.hlsli", "pt_materials.hlsli"]:
            sources[name] = self.text(PT + "shaders/" + name)
        for name, text in sources.items():
            for gone in ("LAMP_TREE", "REGROUP_FEED", "WATER_FLIGHT", "PT_REGROUP_KEYS", "PT_PATH_DEFINES", "PT_PASS_DEFINES", "reflected_lamps_tree", "water_flight_reach"):
                self.assertNotIn(gone, text, name)
        # Every pass is compiled with every pass's defines again; the reordering passes' carry the water's trial alone.
        self.assertEqual(cmake.count("-T cs_6_6 -E main ${PT_SPEED_DEFINES} -HV 2021"), 1)
        self.assertEqual(cmake.count("-T cs_6_9 -E main -D PT_DXR12 ${PT_SPEED_DEFINES} -HV 2021"), 1)
        self.assertEqual(cmake.count("-T lib_6_9 -D PT_DXR12 -D PT_SER ${PT_REGROUP_DEFINES} ${PT_SPEED_DEFINES} -HV 2021"), 1)
        self.assertIn('option(TGPT_REGROUP_WATER "TG Path Tracing: on trial, the water\'s path regroups its threads inside the reordering passes" OFF)', cmake)
        self.assertIn('message(FATAL_ERROR "TG Path Tracing: TGPT_REGROUP_WATER and TGPT_SPEED_MEASURE give the Speed trial\'s numbers 2 and 4 two meanings: build them apart")', cmake)
        # The loop over the point's two light lists is gone: the walk through the tree is the function, and nothing of the
        # hidden "Speed trial" is asked on the way.
        self.assertEqual(common.count("float3 reflected_lamps("), 1)
        walk = common.split("float3 reflected_lamps(float3 position, float3 origin, float3 dir, float tmax) {", 1)[1].split("\n}", 1)[0]
        self.assertIn("if (!light_lists(position, lists)) return 0.0;", walk)
        for gone in ("lists.farCount", "lists.farFirst", "entries[", "speed_trial()", "load_light("):
            self.assertNotIn(gone, walk)
        # The walk asks a leaf's lamp the loop's own question, and counts it where the loop counted it.
        for line in ("if (!reflected_as_sphere(l)) continue;",
                     "const float ball = lamp_trial_radius(l, LAMP_TRIAL_REFLECTED);",
                     "const float b = dot(oc, dir), disc = b * b - (dot(oc, oc) - ball * ball);",
                     "if (disc <= 0.0) continue;",
                     "const float t = -b - sqrt(disc) > 0.0 ? -b - sqrt(disc) : -b + sqrt(disc);",
                     "if (t <= 0.0 || t >= tmax) continue;",
                     "const float3 e = light_incident(l, position, toLight, dist);",
                     "e / (light_falloff(l, dist * dist) * PI * ball * ball);"):
            self.assertIn(line, walk)
        # The lamps met are added in the loop's order (the cell's by their index, then the others by theirs), up to four
        # at once: the same bits as the loop's sum. The CPU test holds the walk's sum against the loop's, bit for bit.
        self.assertIn("const bool listed = inGrid && cell_lists(lists.first, lists.count, index);", walk)
        self.assertIn("uint key = (listed ? 0u : 0x80000000u) | index;", walk)
        self.assertIn("if (key < keys[z]) { const float3 heldTerm = held[z]; const uint heldKey = keys[z]; held[z] = term; keys[z] = key; term = heldTerm; key = heldKey; }", walk)
        self.assertIn("{ for (uint z = 0; z < LAMPS_HELD; ++z) if (keys[z] != INVALID) sum += held[z]; }", walk)
        self.assertIn("static const uint LAMPS_HELD = 4;", common)
        self.assertIn("if ((l.flags & LIGHT_NO_RANGE) == 0u && inGrid && !listed) continue;", walk)
        # What lets it pass a ball by: the share that grows with the distance, from the direction's own length (the
        # reckoning: lamp_tree.hpp), and a leaf's ball of 2.1 times the lamp's largest. The CPU test walks with the same.
        self.assertIn("const float slack = 2.3 * sqrt(2.0e-6 + off) + off, perLength = 1.0 / length2;", walk)
        self.assertIn("const float reach = node.range + slack * (length(w) + node.range);", walk)
        self.assertIn("if (dot(q, q) > reach * reach) { i = max(node.type, i + 1u); continue; }", walk)
        self.assertIn("constexpr double kLeafBalls=2.1;", tree)
        self.assertIn("return std::max(double(l.radius)*5.0, double(l.flags>>TGPT_LIGHT_HDRP_RADIUS_SHIFT)*0.001);", tree)
        # The CPU test keeps the loop, as what the walk is held against, and is built and run in every build: no option
        # stands before it.
        test = self.text("tests/lamp_tree_test.cpp")
        self.assertIn("#define LAMP_TREE_TEST_SLACK 2.3f", test)
        self.assertIn("const float slack=LAMP_TREE_TEST_SLACK*std::sqrt(2.0e-6f+off)+off;", test)
        self.assertIn("constexpr uint32_t kHeld=4;", test)
        self.assertIn("expect(std::memcmp(&a, &b, sizeof a)==0,", test)
        self.assertIn("for(size_t i=0;i<s.cell.size()+s.far.size();++i) {", test)
        self.assertIn("if(far && std::binary_search(s.cell.begin(), s.cell.end(), index)) continue;", test)
        self.assertIn("    add_executable(tgpt_lamp_tree_test tests/lamp_tree_test.cpp)\n    target_include_directories(tgpt_lamp_tree_test PRIVATE ${PT_INCLUDE})\n"
                      "    add_test(NAME tgpt_lamp_tree COMMAND tgpt_lamp_tree_test)\n", cmake)
        # The tree is always laid, after the frame's lights, whose count the passes keep (the root is record number
        # lightCount): no #if stands between the lights' count and their upload.
        self.assertIn('#include "lamp_tree.hpp"', scene)
        part = scene.split("lightCount=uint32_t(lights.size());", 1)[1].split("upload_structured(", 1)[0]
        self.assertIn("const std::vector<TGPT_Light> tree=lamp_tree::build(lights.data(), lights.size());", part)
        self.assertIn("lampTreeNodes=tree[0].type;", part)
        self.assertIn("lights.insert(lights.end(), tree.begin(), tree.end());", part)
        self.assertNotIn("#if", part)
        self.assertIn("const uint root = g.settings2.z;", walk)
        self.assertIn("k.settings2[2]=st.scene.lightCount;", tracer)
        self.assertIn("\" of the far lists; the lamps' search tree \"+", tracer)
        # The feeders' key: the two reordering passes regroup their threads by the material their ray met and by whether
        # the pixel feeds the radiance cache, always. No regrouping is added: one a pass, and one line in pt_common.
        self.assertIn("uint regroup_key(uint material, bool feeds) { return material == 0xFFFFu ? material : ((material & 0x7FFFu) | (feeds ? 0x8000u : 0u)); }", common)
        # The bounced light's pass regroups with NO hint of our own since 2026-10-10 (the study's runs 20261010-113425 and
        # -115539, docs/agents/ser-study-run.md: the 16-bit material key cost that pass 20 ms and more in a tavern, a town
        # and at a beach, the same picture): one call, inside PT_SER, never the material's key again. The reflections' pass
        # keeps its key (there the regrouping pays in every scene measured). Reordering is never switched off (AGENTS.md).
        # (Its one call stands after the pixel's first ray, outside the rays' loop, since the same day:
        # test_path_tracing_ser_split_is_the_rule.)
        self.assertNotIn("PT_REORDER(regroup_key(found ? hit.material : 0xFFFFu, feed));", gi)
        self.assertEqual(gi.count("dx::MaybeReorderThread("), 1)
        self.assertIn("#ifdef PT_SER\n", gi.replace("\r\n", "\n"))
        self.assertIn("    dx::MaybeReorderThread(regroup_key(material, cache_feeds(p, 0u)) & 0xFFFFu, 16);\n    p = DispatchRaysIndex().xy;\n", gi.replace("\r\n", "\n"))
        self.assertIn("material = keyMaterials[keyInstances[met.instance].materialBase + met.geometry];", gi)  # (round 20: the key, the rule)
        self.assertIn("dx::MaybeReorderThread(regroup_key(keyMaterial, cache_feeds(p, 2u)) & 0xFFFFu, select((g.size.w & 4u) != 0u, 0u, 16u));", specular)

        self.assertEqual(specular.count("MaybeReorderThread("), 1)
        self.assertEqual(common.count("dx::MaybeReorderThread("), 1)
        # The water's short rays came out: a turn's ray asks for what is nearest however far, in one line; and the early
        # draw of the turn's flight is the water's regrouping trial's alone (the ray draws no random number).
        loop = path.split("float3 water_path(", 1)[1]
        self.assertEqual(loop.count("trace_closest(origin, direction,"), 1)
        self.assertIn("#endif\n        const bool met = trace_closest(origin, direction, water_epsilon(origin), 1e30, hit);\n", loop)
        self.assertIn("#ifdef PT_REGROUPS\n        // ON TRIAL (water_regroup, above): the turn's free flight is drawn here", loop)
        self.assertNotIn("rng", common.split("bool trace_closest(float3 origin, float3 dir, float tmin, float tmax, out Hit hit) {", 1)[1].split("\n}", 1)[0])
        # What the hidden "Speed trial" still says, and the tracer's log: 128 to 32768 do nothing in a build for players
        # (16 and 32 hold the reflections' sort since R19, the same picture; 64 turns PT-131's rule, the cell by the ray's
        # width, off for one comparison).
        self.assertIn('The numbers from 512 to 32768 are for a measuring " +\n                "build of the path tracer and do nothing in this one. Leave it at 0."', settings.replace("\r\n", "\n"))
        self.assertIn("((trial&0xFFC0u) ? \"the numbers \"+std::to_string(trial&0xFFC0u)+\" do nothing in this build", tracer)
        self.assertNotIn("search tree is asked for", tracer)
        # The recipes: the three trials' own run is gone with them, and the water's regrouping keeps its steps.
        import json
        recipes = json.loads(self.text("tests/ingame/scenarios.json"))["recipes"]
        self.assertNotIn("speed2", recipes)
        values = [s["value"] for step in recipes["regroup"] for s in step["set"] if s.get("key") == "Speed trial"]
        self.assertEqual(values, ["0", "2", "4", "6", "0", "0"])
        for step in recipes["regroup"]:
            self.assertNotIn("neural", step)

    def test_path_tracing_terrain_frame_is_the_rule(self):
        # PT-104 (docs/agents/terrain-1.md, docs/agents/rule-1.md): the game's ground meshes bring no texture coordinates
        # and most of them no tangents; the game's shader turns its layers' bumps by the tangent Unity hands it for such a
        # mesh, the object's x axis. The tracer made a normal map's frame of the mesh's texture coordinates, had none, and
        # answered the flat normal: the ground had no relief. Since 2026-10-10 the terrain's program makes its frame of
        # the object's x axis with the sign +1, always (the game, run 20261010-092027: another normal than the game's on
        # 63 % of the terrain's pixels flat, on 4 % with +1, on 54 % with -1). No switch is left.
        materials = self.text(PT + "shaders/pt_materials.hlsli")
        programs = self.text("src/TGPathTracing/MaterialPrograms.cs")
        capture = self.text("src/TGPathTracing/SceneCapture.cs")
        scenarios = self.text("tests/ingame/scenarios.json")
        body = materials.split("MaterialPoint program_terrain(Hit h, MaterialRecord m, Program p, TangentFrame f) {", 1)[1].split("\n}", 1)[0]
        for line in ("    float3 c0, c1, c2, origin; hit_axes(h, c0, c1, c2, origin);\n",
                     "    if (along > 1e-12) {\n",
                     "        own.n = h.rounded; own.t = c0 / along; own.ok = true;\n",
                     "        own.b = cross(own.n, own.t) * (dot(c0, cross(c1, c2)) < 0.0 ? -1.0 : 1.0);\n",
                     "    o.normal = frame_normal(own, n * float3(1.0, -1.0, 1.0), h);\n"):
            self.assertIn(line, body)
        self.assertNotIn("v17.y", body)
        for name, text in (("Settings.cs", self.text("src/TGPathTracing/Settings.cs")), ("MaterialPrograms.cs", programs), ("SceneCapture.cs", capture),
                           ("scenarios.json", scenarios), ("pt_materials.hlsli", materials)):
            for gone in ("Terrain frame", "TerrainFrame", "terrainFrame"):
                self.assertNotIn(gone, text, name)
        # The record's value 17 holds the splat maps' clamping alone again.
        self.assertIn("values.Add(new Vector4(splat != null && splat.wrapMode == TextureWrapMode.Clamp ? 1f : 0f, 0f, 0f, 0f));", programs)
        # The log still names the mesh a terrain material is drawn with (the ground meshes that bring tangents of their
        # own are turned with +1 too: the sign by the mesh is not built, and the line says so).
        self.assertIn("if (mm.Program == MaterialPrograms.Terrain) MaterialPrograms.TerrainDrawn(handle, material);", capture)
        self.assertIn("the sign by the mesh is not built", programs)
        # Every other program, and HDRP Lit's way, still answer the flat normal on a mesh without texture coordinates
        # (frame_normal: not changed here). The log now says how many instances of which materials, once a scan finds one
        # it has not named.
        self.assertIn("    if (!f.ok) return h.normal;\n", materials)
        self.assertIn("bool noCoordinates = MaterialPrograms.NoCoordinates(handle), flat = false;", capture)
        self.assertIn("else if (noCoordinates) flat |= MaterialPrograms.FlatDrawn(handle, material, mm.Program, mm.NormalTexture);", capture)
        self.assertIn("if (flat) MaterialPrograms.FlatInstance();", capture)
        self.assertIn("MaterialPrograms.ScanBegins();", capture)
        self.assertIn("MaterialPrograms.ScanEnds();", capture)
        self.assertIn("if (program == Terrain || !(normalTexture >= 0 || program == Triplanar || program == Skin)) return false;", programs)
        self.assertIn('" materials have a normal map and a mesh without texture coordinates: drawn flat "', programs)
        # The recipe that asked the game for the sign shows the rule now: the picture, its normals, the yardstick, the rain.
        import json
        recipe = json.loads(scenarios)["recipes"]["terrain-frame"]
        self.assertEqual([step["name"] for step in recipe], ["1-game", "2-none", "3-normals", "4-yardstick", "5-none-wet", "6-off"])
        for step in recipe:
            self.assertNotIn("neural", step)

    def test_cutouts_are_counted_and_their_cost_can_be_bound(self):
        # PT-110 (docs/agents/omm-1.md; the review: docs/agents/compat-1.md section 3). Opacity micromaps let the graphics
        # card settle most of a cut-out triangle without the shader's alpha test; nobody knew how much of the traced
        # world's cut-outs they settle, nor what the rest costs. M0, the measure: a count in the log every 10 s, and a
        # hidden setting that bounds each kind's cost for one step of one run.
        header = self.text(PT + "include/tgpt_api.h")
        scene = self.text(PT + "src/scene.cpp").replace("\r\n", "\n")
        scene_h = self.text(PT + "src/scene.hpp").replace("\r\n", "\n")
        tracer = self.text(PT + "src/tracer.cpp").replace("\r\n", "\n")
        settings = self.text("src/TGPathTracing/Settings.cs")
        # The switch: hidden, 0, in the frame's flags beside the Mega Geometry trial's bits (all 32 option bits are taken).
        self.assertIn('MicromapsTrial = file.Bind(Advanced, "Micromaps trial", 0, Hidden(', settings)
        self.assertIn("#define TGPT_FRAME_OMM_SHIFT 6u", header)
        self.assertIn("#define TGPT_FRAME_OMM_MASK 255u", header)
        for name, value in (("PLANTS_SOLID", 1), ("CHARACTERS_SOLID", 2), ("STILL_SOLID", 4), ("NO_MAPS", 8), ("PLAIN_TESTED", 16),
                            ("NO_PLANT_MAPS", 32), ("PLANT_REBUILD", 64)):
            self.assertIn("#define TGPT_OMM_TRIAL_%s %du" % (name, value), header)
        self.assertIn("frame.Flags |= (uint)Mathf.Clamp(settings.MicromapsTrial.Value, 0, (int)Tgpt.FrameOmmMask) << Tgpt.FrameOmmShift;",
                      self.text("src/TGPathTracing/HdrpPasses.cs"))
        native = self.text("src/TGPathTracing/NativeTracer.cs")
        self.assertIn("internal const int FrameOmmShift = 6;", native)
        self.assertIn("internal const uint FrameOmmMask = 255;", native)
        self.assertIn("const uint32_t trial=(f.flags>>TGPT_FRAME_OMM_SHIFT)&TGPT_FRAME_OMM_MASK;", tracer)
        self.assertIn("st.scene.ommTrial=trial|leaves;", tracer)  # (round 20, on trial: the leaves' micromap level from the Speed trial)
        # Its bits do not overlap the flags that are there: reset, guides, the Mega Geometry trial's four.
        self.assertEqual((255 << 6) & (1 | 2 | (15 << 2)), 0)
        # Never a default, and never said to be anything but a measurement: the setting's text, the log when it changes
        # and every count line while it is set.
        import re
        text = settings.split('MicromapsTrial = file.Bind(Advanced, "Micromaps trial", 0, Hidden(', 1)[1].split("));", 1)[0]
        text = "".join(re.findall(r'"([^"]*)"', text)) # the sentence, its pieces joined
        self.assertIn("For measuring.", text)
        self.assertIn("make the picture wrong on purpose", text)
        self.assertIn("Leave it at 0.", text)
        self.assertIn("A MEASURING STEP, the picture or its speed changed on purpose:", tracer)
        self.assertIn("A MEASURING STEP: ", scene)
        # "Solid" is an instance's flag, which the camera's own rays outrank by their ray's flag: the first hit stays right.
        self.assertIn("const bool plantsSolid=(ommTrial&TGPT_OMM_TRIAL_PLANTS_SOLID)!=0, charactersSolid=(ommTrial&TGPT_OMM_TRIAL_CHARACTERS_SOLID)!=0,", scene)
        self.assertIn("(plantsSolid ? D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE : D3D12_RAYTRACING_INSTANCE_FLAG_NONE);", scene)
        self.assertIn("PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_NON_OPAQUE) q;", self.text(PT + "shaders/pt_primary.hlsli"))
        # "No micromaps" is the frame's own switch, as the offline test's option sets it.
        self.assertIn("if(trial&TGPT_OMM_TRIAL_NO_MAPS) st.scene.ommAllowed=false;", tracer)
        # The count: by kind, from the instances the frame traces; a mesh's pieces by state from its bake's own output,
        # read back once and counted on the CPU (no pass changed, nothing counted in a shader).
        self.assertIn("enum CutoutKind : uint32_t { kCutStill, kCutPlants, kCutCharacters, kCutCloth, kCutKinds };", scene_h)
        self.assertIn("for(uint32_t k=0;k<kCutKinds;++k) stats_.cutouts[k]=cut[k];", scene)
        self.assertIn("count_omm(release, retireValue);", scene)
        self.assertIn("if(index==-1) { states[0]+=triPieces; ++settled; continue; }", scene)
        self.assertIn("if(index==-2) { states[1]+=triPieces; ++settled; continue; }", scene)
        # (Round 20, on trial: a leaf's finer maps; every mesh's pieces kept in level 4's units, shares of a triangle.)
        self.assertIn("for(int i=0;i<3;++i) m->ommPieces[i]=states[i]*kOmmPieces/triPieces;", scene)
        # Round 20 (PT-110's M7): leaves' and grass's micromaps at level 6, the rest at 4 (run 20261011-055446: the forest
        # 82.8 -> 75.5 ms, the frozen pictures the same); a mesh baked at another level is baked again.
        self.assertIn("constexpr uint32_t kOmmLevel=4, kOmmBytes=64, kOmmLevelLeaves=6, kOmmLevelMost=6;", scene_h)
        self.assertIn("for(size_t g=0;g<count;++g) if(materials[g]<materials_.size() && is_foliage(materials_[materials[g]].flags)) {", scene)
        # (2026-10-11: a mesh whose bake would pass one frame's pieces goes a level coarser: the scene stage hung once.)
        self.assertIn("while(level>kOmmLevel && (alphaTriangles<<(2u*(level-kOmmLevel)))>kOmmTrianglesPerFrame) --level;", scene)
        self.assertIn("return mesh.ommLevel!=omm_level(mesh.ommMaterials.data(), mesh.ommMaterials.size(), mesh.alphaTriangles);", scene)
        self.assertIn("static const uint OMM_LEVEL_MOST = 6u;", self.text(PT + "shaders/pt_omm_bake.hlsl"))
        self.assertIn("OMM_SPECIAL_FULLY_TRANSPARENT = -1, OMM_SPECIAL_FULLY_OPAQUE = -2", self.text(PT + "shaders/pt_omm_bake.hlsl"))
        self.assertIn("static const uint OMM_TRANSPARENT = 0, OMM_OPAQUE = 1, OMM_UNKNOWN_OPAQUE = 3;", self.text(PT + "shaders/pt_omm_bake.hlsl"))
        self.assertIn("constexpr uint64_t kOmmPieces=256;", scene_h)
        self.assertIn("    log(st.scene.cutout_text());", tracer)
        self.assertIn('return "PT cut-outs: "+number(triangles)+" cut-out triangles in "', scene)
        # A mesh that goes, or whose micromaps do, takes its count along: no read-back is counted into another mesh.
        self.assertEqual(scene.count("forget_omm_readback("), 2)
        # The count tells a leaf's unknown pieces from the rest: the bake leaves every covered piece of a leaf, a blade
        # or thin cloth unknown on purpose (the review's M2), which says nothing of how well the level fits (its M7).
        self.assertIn("if ((m.flags & MATERIAL_THIN_CLOTH) != 0 && state == OMM_OPAQUE) state = OMM_UNKNOWN_OPAQUE;", self.text(PT + "shaders/pt_omm_bake.hlsl"))
        self.assertIn("if((materials_[index].flags&TGPT_MATERIAL_ALPHA_TEST) && (materials_[index].flags&(TGPT_MATERIAL_TRANSLUCENT|TGPT_MATERIAL_THIN_CLOTH))) mesh.ommLeaf=true;", scene)
        self.assertIn("if(mesh.ommLeaf) c.leafUnknown+=mesh.ommPieces[2];", scene)
        # M3: a character's structure, and a plant group's, are their own, so each submesh carries in it what its material
        # is to a ray. A submesh that is no cut-out is solid there, and only the cut-outs (hair, leaves) reach the
        # shader's alpha test: until 2026-10-10 one alpha-tested material made every triangle of the character or the
        # plant a candidate (109,800 such triangles in a tavern's frame). The same hits, because the alpha test's first
        # line answers "solid" for every material that is not alpha-tested, and the walks that look for glass, thin
        # cloth and leaves among the candidates take none of these either.
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("    if ((m.flags & MATERIAL_ALPHA_TEST) == 0) return true;\n", common.replace("\r\n", "\n"))
        self.assertIn("if ((cm.flags & MATERIAL_THIN_CLOTH) != 0 && (cm.flags & MATERIAL_ALPHA_TEST) != 0 &&", common)
        self.assertIn("const uint leaf = MATERIAL_ALPHA_TEST | MATERIAL_TRANSLUCENT;", common)
        self.assertIn("if(!plainTested) for(uint32_t g=0;g<count;++g) marks[g]=(materials_[mats[g]].flags&TGPT_MATERIAL_ALPHA_TEST) ? 0 : 1;", scene)
        self.assertIn("if(mesh.skinned && !positions && i<mesh.skinOpaque.size() && mesh.skinOpaque[i]) g.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;", scene)
        self.assertIn("marked ? D3D12_RAYTRACING_INSTANCE_FLAG_NONE : D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE);", scene)
        self.assertIn("const bool tested=alpha && (cutout || wholeTested);", scene)
        self.assertIn("g.opaque.push_back(tested ? 0 : 1);", scene)
        # M3 since 2026-10-11 (docs/agents/omm-chars.md): one glass submesh no longer takes the marks from a whole
        # character (the tavern's 109,800 solid triangles were tested for that). A solved caustic's segments ask for
        # candidates themselves (caustic_clear), so they still pass their own glass instance whole, its solid parts
        # too. A plant group with glass keeps the old way (wholeTested, the next line but one).
        self.assertIn("if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && q.CandidateInstanceID() != glassInstance &&", common)
        self.assertIn("q.TraceRayInline(g_scene, RAY_FLAG_FORCE_NON_OPAQUE, MASK_ALL, ray);", common)
        self.assertNotIn("glass=(materials_[mats[g]].flags&TGPT_MATERIAL_GLASS)!=0;", scene)
        self.assertIn("for(uint32_t gi=0;gi<count && !wholeTested;++gi) wholeTested=(materials_[materialIndices_[inst.materialBase+gi]].flags&TGPT_MATERIAL_GLASS)!=0;", scene)
        # A refit must name the marks of its build: a change of them builds a character's structure anew, and a plant
        # group whose material's alpha test is switched is another group (its signature holds the flag).
        self.assertIn("m->skinOpaque=std::move(marks); m->sinceBuild=kInvalid;", scene)
        self.assertIn("const uint32_t cut=materials_[materialIndices_[inst.materialBase+gi]].flags&(TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_GLASS);", scene)
        self.assertIn("const auto rebuilt=[](const Mesh& m) { return m.sinceBuild>=kCharacterRebuild-1; };", scene)
        # M3's own offline check, alone (never in the whole test), on either runtime: a character with plain sides and an
        # invisible lid, two trees in the wind's pose refitted every frame; the same frames with the marks, without
        # them (the trial's 16) and with them again are the same pictures, a run with the cut-outs solid is not, and
        # the log counts the triangles spared.
        test = self.text("tests/pathtracing_gpu_test.cpp").replace("\r\n", "\n")
        self.assertIn('if(GetEnvironmentVariableA("TGPT_TEST_OMM_ONLY", nullptr, 0)>0)\n#include "pathtracing_omm_checks.inl"', test)
        self.assertIn("f.flags|=(ommTrial&TGPT_FRAME_OMM_MASK)<<TGPT_FRAME_OMM_SHIFT;", test)
        own = self.text("tests/pathtracing_omm_checks.inl")
        self.assertIn("const auto whole=sequence(TGPT_OMM_TRIAL_PLAIN_TESTED);", own)
        self.assertIn("if(offWhole[i].pixels>offAgain[i].pixels+slack) same=false;", own)
        self.assertIn("const auto solid=sequence(TGPT_OMM_TRIAL_PLANTS_SOLID|TGPT_OMM_TRIAL_CHARACTERS_SOLID);", own)
        self.assertIn("expect(controlSaw>500, ", own)
        self.assertIn('holds("(42 more are spared it")', own)
        self.assertIn('holds("(0 more are spared it")', own)
        # M4, micromaps for the characters themselves (the hidden "Character micromaps (trial)", frame flag bit 20), is
        # OUT since 2026-10-11 (docs/agents/ommout.md; PT-110): the game answered twice, alone on the card and in front,
        # the tavern 36.20 / 36.25 / 36.20 ms of tracer time (off, on, off) and the town 67.81 / 68.17 / 67.40. Its
        # conservative bounds left hair unknown, so hair's alpha test was never spared. A character's structure is its
        # plain one, refitted, with M3's marks; a skinned mesh is never queued for a bake; the bake has no rule for one.
        self.assertNotIn("CHARACTER_MICROMAPS", header) # (the flags' bit 20 is free)
        for name in ("Settings.cs", "HdrpPasses.cs", "NativeTracer.cs"):
            self.assertNotIn("CharacterMicromaps", self.text("src/TGPathTracing/" + name), name)
        self.assertNotIn("Character micromaps", settings + scene + tracer)
        for token in ("skinMap", "characterMicromaps", "character_inputs", "strandTiles_", "tileVersions"):
            self.assertNotIn(token, scene + scene_h + tracer, token)
        self.assertIn("if(alpha) count_cutouts(cut[kCutCharacters], *m, uint32_t(s.mesh), combinedIndices_.data()+characterBase[i], false, marked);", scene)
        self.assertIn("desc.DestAccelerationStructureData=m->blasVa; desc.Inputs=in;", scene)
        bake = self.text(PT + "shaders/pt_omm_bake.hlsl")
        self.assertIn("uint omm_level() { return pass.b.z >> 28; }", bake)
        for token in ("OMM_BOUNDS_HAIR", "hair_camera_state", "MATERIAL_HAIR_LAYER", "0x80000000u"):
            self.assertNotIn(token, bake, token)
        self.assertNotIn("micromaps-characters", self.text("tests/ingame/scenarios.json"))
        # M5: no micromap structure is built with ALLOW_DISABLE_OMMS (DirectX Raytracing: "may result in some reductions in
        # traversal performance"), and no instance carries the "no micromaps" flag. An instance that goes without its
        # mesh's micromaps (other materials, the frame's switch, HDRP's water) takes the mesh's plain bottom level: the
        # same triangles, tested the same way, as a partitioned top level already had to have it (PT-109).
        self.assertNotIn("FLAG_ALLOW_DISABLE_OMMS", scene)
        self.assertNotIn("FLAG_DISABLE_OMMS", scene)
        self.assertEqual(scene.count("const bool linked=m->omm==Mesh::Omm::Linked && omm;"), 2)
        self.assertEqual(scene.count("d.AccelerationStructure=linked ? m->ommBlasVa : m->blasVa;"), 2)
        self.assertIn("d.Flags=D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE|D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;\n            d.AccelerationStructure=m->blasVa;", scene)
        # And the micromap structure is compacted like the plain ones (it stayed at its build's largest size): built for
        # it, its compacted size read back, copied compacted some frames on. Only the very build the size was read
        # from: maps made anew meanwhile (a texture the game's streaming replaced) are another structure, which a new
        # allocation may put at the same address.
        self.assertIn("k.blasInputs.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE|D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;", scene)
        self.assertIn("try { build(k.blasInputs, k.blasInfo, k.blasRange, sized ? &post : nullptr); }", scene)
        self.assertIn("const uint32_t serial=++k.mesh->ommSerial;\n            if(sized) compact.entries.push_back(CompactEntry{k.mesh->handle, k.blasRange.va(), true, k.blasRange.size, serial});", scene)
        self.assertIn("if(!m || m->omm!=Mesh::Omm::Linked || m->ommBlasVa!=entry.source || m->ommSerial!=entry.serial || !bytes || bytes>=entry.bytes) continue;", scene)
        self.assertIn("list->CopyRaytracingAccelerationStructure(compact.va(),m->ommBlasVa,D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);", scene)
        self.assertIn("struct CompactEntry { int32_t handle; uint64_t source; bool omm=false; uint64_t bytes=0; uint32_t serial=0; };", scene_h)
        # M1, THE RULE since round 20 (the town 65.2 -> 64.6 ms of the tracer, the forest 121 -> 115 frozen; the trial's 32
        # now leaves them out, for measuring): the plants in the wind's pose trace with their meshes' micromaps. A micromap hangs on a triangle's texture coordinates, which the wind does not move, and
        # a group holds each member's triangles in the member's own order: so a group's cut-out submesh names its
        # member mesh's micromap array and that mesh's own index buffer, at its build and, unchanged, at every refit.
        sway = self.text(PT + "shaders/pt_sway.hlsl")
        self.assertIn("const uint v = indicesIn.Load((r.indexStartIn + (thread - r.indexStartOut)) * 4u);", sway) # a member's triangles in its own order
        self.assertIn("const bool plantMaps=(ommTrial&TGPT_OMM_TRIAL_NO_PLANT_MAPS)==0 && ommEnabled && ommAllowed; // (M1, the rule since round 20)", scene)
        self.assertIn("bool mapped=omm_instance(*m, mats, retireValue);", scene)
        self.assertIn("if(maps && alpha && cutout && m->ommFirst[gi]!=kInvalid) { link.array=m->ommArrayVa; link.indices=m->ommIndices.va()+uint64_t(m->ommFirst[gi])*4; }", scene)
        self.assertIn("l.OpacityMicromapArray=g.links[i].array;", scene)
        self.assertIn("d.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES; d.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;", scene)
        # One description for the build and for every refit (a refit must name what its build named).
        # (Since 2026-10-12, PT-99: the refits name the description the group keeps from its first pose, made by the same
        # plant_geometry from the same group; the plan makes one for the build's sizes.)
        self.assertEqual(scene.count("PlantGeometry geometry; plant_geometry(g, geometry);"), 1)
        self.assertIn("if(!g.geometryMade) { plant_geometry(g, g.geometry); g.geometryMade=true; }\n        const auto& geos=g.geometry.geos;", scene)
        # The mesh keeps the bake's index buffer with its micromaps where the wind moves it (the builds alone read it
        # until now), and lets it go with them once the GPU is past that frame.
        self.assertIn("k.mesh->ommIndices=k.indices; k.mesh->ommFirst=k.first;", scene)
        self.assertIn("if(mesh.ommIndices) ommIndicesRetired_.emplace_back(retireValue, std::move(mesh.ommIndices));", scene)
        # A group whose member's micromaps went, or were made anew, is never traced again: their array goes when the
        # GPU is past the frame that let them go, and the group is made anew like one whose members changed. One that
        # only lacks micromaps come since serves as it is until every member's are in and the frame has room.
        self.assertIn("lost|=g.memberMaps[i]!=0 && g.memberMaps[i]!=list[i].link;", scene)
        self.assertIn("if(same || (!lost && (pending || !room))) {", scene)
        self.assertIn("const bool maps=member.link!=0 && m->omm==Mesh::Omm::Linked && m->ommSerial==member.link && m->ommArrayVa && m->ommIndices && m->ommFirst.size()==own.geometryCount;", scene)
        # With the trial at 0 no plant asks for micromaps and no group links any: every group is as before.
        self.assertIn("if(plantMaps && (inst.flags&TGPT_INSTANCE_ALPHA_TEST)) {", scene)
        self.assertIn("if(i>=g.links.size() || !g.links[i].array) {", scene)
        # Its own recipe, apart from the measuring one (a fault of the driver's in it costs no measurement): the frozen
        # pair is the picture with and without (16 frames each, a group built with micromaps), the running pair the
        # refit and what it brings.
        import json
        plants = json.loads(self.text("tests/ingame/scenarios.json"))["recipes"]["micromaps-plants"]
        self.assertEqual([s["value"] for step in plants for s in step["set"] if s.get("key") == "Micromaps trial"], ["0", "32", "0", "32", "0", "0"])
        self.assertEqual([bool(step.get("live")) for step in plants if step["traced"]], [False, False, True, True, True])
        self.assertEqual([step["frames"] for step in plants if step["traced"]][:2], [16, 16])
        for step in plants:
            self.assertNotIn("neural", step)
            if any(s.get("key") == "Micromaps trial" and s["value"] != "0" for s in step["set"]):
                self.assertIn("MEASURING", step["name"])
        # The recipe: the world running (plants are refitted only then), no denoiser, one number a step, and the same
        # picture as it ships before and after the measuring steps. Its frozen pair (steps 2 and 3, 16 frames each) is
        # M3 on and off: the same picture.
        recipe = json.loads(self.text("tests/ingame/scenarios.json"))["recipes"]["micromaps"]
        values = [s["value"] for step in recipe for s in step["set"] if s.get("key") == "Micromaps trial"]
        self.assertEqual(values, ["0", "16", "0", "1", "2", "4", "7", "8", "16", "0", "0"])
        self.assertEqual([bool(step.get("live")) for step in recipe if step["traced"]], [False, False] + [True] * 8)
        self.assertEqual([step["frames"] for step in recipe if step["traced"]][:2], [16, 16])
        for step in recipe:
            self.assertNotIn("neural", step)
            for s in step["set"]:
                self.assertNotEqual((s.get("key"), s.get("value")), ("Denoising", "NRD"))
            if any(s.get("key") == "Micromaps trial" and s["value"] not in ("0",) for s in step["set"]):
                self.assertIn("MEASURING", step["name"])

    def test_path_tracing_hdrp_draws_nothing_opaque_while_tracing(self):
        # PT-112 (A), the rule since 2026-10-11: HDRP's opaque draw lists are left empty and its
        # decals left out of the gameplay camera's frame while the traced light is on screen; the
        # tracer writes the frame's depth and motion into HDRP's buffers. Never the frame setting
        # OpaqueObjects (it woke the game's ECS uploads: 800 ms frames, PROJECT-CONTEXT DEAD ENDS).
        passes = self.text("src/TGPathTracing/GamePasses.cs")
        self.assertIn("__result.layerMask = 0; __result.batchLayerMask = 0;", passes)
        self.assertIn("aggregatedFrameSettings.SetEnabled(FrameSettingsField.Decals, false);", passes)

        self.assertIn("foreach (var field in LeftOut) aggregatedFrameSettings.SetEnabled(field, false);", passes)
        for field in ("SSAO", "ContactShadows", "SSR", "SSGI", "ScreenSpaceShadows", "SubsurfaceScattering", "ShadowMaps", "Volumetrics"):
            self.assertRegex(passes, r"FrameSettingsField\." + field + r"\b[^;]*\};")
        self.assertIn("private static bool BeforeDeferredLighting(HDCamera hdCamera) => !OwnDepth || hdCamera == null || !Gameplay(hdCamera.camera);", passes)
        self.assertNotIn("FrameSettingsField.OpaqueObjects", passes)
        self.assertIn("settings == null || settings.HdrpDraws.Value || !settings.OwnCameraRays.Value || settings.CurrentView == DebugView.Yardstick;", passes)
        self.assertIn("if (GamePasses.OwnDepth) frame.Flags |= Tgpt.FrameOwnDepth;", self.text("src/TGPathTracing/HdrpPasses.cs"))
        self.assertIn('HdrpDraws = file.Bind(Advanced, "HDRP draws under the traced picture", false, Hidden(', self.text("src/TGPathTracing/Settings.cs"))
        # The traced decals follow the game's own decal settings, not HDRP's of the frame (round 19: they vanished with HDRP's).
        decals = self.text("src/TGPathTracing/DecalCapture.cs")
        self.assertIn("bool enabled = GamePasses.GameDecals;", decals)
        self.assertNotIn("hd.frameSettings.IsEnabled(FrameSettingsField.Decals)", decals)
        # What the wind sways has its own motion for the upscaler (HDRP's motion vectors are the tracer's now): a group of
        # plants by each vertex's move since the frame before, a cloth by its previous pose. Until 2026-10-11 a group's
        # record left `previous` at 0 (aggregate initialisation), and the camera rays read heap slot 0 as its last positions.
        scene = self.text(PT + "src/scene.cpp")
        self.assertIn("g.previousSrv!=kInvalid ? (g.previousSrv|kMeshPreviousDelta) : kInvalid});", scene)
        self.assertIn("own.normals, copy->previousSrv, own.uvs2, own.tangents, own.decalColors});", scene)
        self.assertIn("step(6, false); // no move where the pose is kept", scene)
        primary = self.text(PT + "shaders/pt_primary.hlsli")
        self.assertIn("#define MESH_PREVIOUS_DELTA 0x40000000u", primary)
        self.assertIn("constexpr uint32_t kMeshPreviousDelta=0x40000000u;", self.text(PT + "src/scene.hpp"))
        sway = self.text(PT + "shaders/pt_sway.hlsl")
        self.assertIn("const float3 was = (grp.flags & 1u) != 0u ? asfloat(data.Load3(grp.positionsAt + thread * 12u)) : moved;", sway)
        self.assertIn("pose.Store3(r.previousAt + v * 12u, pose.Load3(v * 12u));", sway)
        # The recipe compares HDRP drawing (the test switch) with the rule, twice; round 19's trial switch is gone.
        recipes = json.loads(self.text("tests/ingame/scenarios.json"))["recipes"]
        self.assertNotIn("game-passes", recipes)
        values = [s["value"] for step in recipes["no-hdrp"] for s in step["set"] if s.get("key") == "HDRP draws under the traced picture"]
        self.assertEqual(values, ["true", "false", "true", "false"])
        self.assertNotIn("Game passes while tracing", self.text("tests/ingame/scenarios.json"))

    def test_path_tracing_foliage_opaque_but_to_light_rays(self):
        # Round 20 (PT-110's M2, its picture kept): a leaf's and a blade's covered micromap pieces are opaque to every ray but
        # a light's (no shader's test where a bounce or a reflection crosses a leaf); a light's ray, which passes a leaf by
        # TRANSMISSION's chance where it covers, meets the instances holding leaves and grass in a pass of its own, every
        # triangle forced to the test. Thin cloth keeps its covered pieces unknown (a bounce ray passes its weave too).
        bake = self.text(PT + "shaders/pt_omm_bake.hlsl")
        self.assertIn("if ((m.flags & MATERIAL_THIN_CLOTH) != 0 && state == OMM_OPAQUE) state = OMM_UNKNOWN_OPAQUE;", bake)
        common = self.text(PT + "shaders/pt_common.hlsli")
        self.assertIn("#define MASK_FOLIAGE 0x40u", common)
        self.assertIn("f.TraceRayInline(g_scene, RAY_FLAG_FORCE_NON_OPAQUE, mask, ray);", common)
        self.assertIn("const uint foliage = g_lightRay ? (mask & (MASK_FOLIAGE | MASK_SWAYS)) : 0u;", common)
        self.assertIn("return foliage != 0u && foliage_blocks(origin, dir, tmin, tmax, foliage);", common)
        self.assertIn("return any_blocks(origin, dir, tmin, tmax, light_mask(MASK_ALL));", common)
        self.assertIn("return any_blocks(origin, dir, max(nearEnd, tmin), tmax, light_mask(MASK_ALL));", common)
        self.assertEqual(common.count("g_lightRay ? RAY_FLAG_FORCE_NON_OPAQUE : RAY_FLAG_NONE"), 2)
        scene_h = self.text(PT + "src/scene.hpp")
        self.assertIn("constexpr uint32_t kMaskFoliage=0x40, kMaskStillPlain=0x21;", scene_h)
        self.assertIn("kMaskStill=0x61", scene_h)
        scene = self.text(PT + "src/scene.cpp")
        self.assertIn("return (flags&TGPT_MATERIAL_TRANSLUCENT)!=0 && (flags&TGPT_MATERIAL_ALPHA_TEST)!=0 && (flags&TGPT_MATERIAL_THIN_CLOTH)==0;", scene)
        self.assertIn("d.InstanceMask=g.foliage ? kMaskFoliage : kMaskStillPlain;", scene)
        self.assertIn("d.InstanceMask=kMaskStillPlain|(strands ? kMaskStrands : 0u);", scene)
        self.assertEqual(scene.count("const uint32_t still=foliage ? kMaskFoliage : kMaskStillPlain;"), 2)
        # Every still instance takes one of the two: no instance keeps kMaskStill (0x61) itself.
        self.assertNotIn("InstanceMask=kMaskStill;", scene)
        self.assertNotIn("(kMaskStill|kMaskSeeThrough)", scene)

    def test_path_tracing_fog_is_traced_in_cells(self):
        # PT-118, the rule since 2026-10-12 (docs/agents/fogrule2.md): the camera's fog is held in cells of a grid that
        # follows the camera, as RTX Remix builds its fog, over the WHOLE way (48 slices to 20 m, 48 more to the far
        # distance). The per-pixel route it replaced, the hidden switch between the two, the grid's trials and its
        # diagnosis read-back are gone, with their flag bits.
        shaders = ROOT / PT / "shaders"
        for gone in ("pt_fog.hlsl", "pt_fog_filter.hlsl", "pt_fog_reservoir.hlsli"):
            self.assertFalse((shaders / gone).exists(), gone)
        cmake = self.text("CMakeLists.txt")
        self.assertIn("fogFroxel:pt_fog_froxel fogIntegrate:pt_fog_integrate fogTail:pt_fog_tail", cmake)
        for gone in ("fog:pt_fog", "pt_fog_filter", "pt_fog_reservoir"):
            self.assertNotIn(gone, cmake)
        api = self.text(PT + "include/tgpt_api.h")
        for gone in ("TGPT_FRAME_FOG_FROXELS", "TGPT_FRAME_FOG_FROXEL_TRIAL", "TGPT_FRAME_FOG_FROXEL_DIAG"):
            self.assertNotIn(gone, api)
        tracer = self.text(PT + "src/tracer.cpp")
        for gone in ("fogTrace", "fogFilter", "fogReservoirs", "fogFiltered", "fogProbe", "fogDiag", "wantsFroxels", "tracedFog["):
            self.assertNotIn(gone, tracer)
        self.assertIn('"denoising", "fog cells", "fog walk", "fog lamps", "composite"};', tracer)
        self.assertIn("if(st.tracedFog.resource && record_fog_froxels(list, b, f, reset, k, st.tracedFog.uav, mark)) fogUav=st.tracedFog.uav;", tracer)
        for name in ("Settings.cs", "HdrpPasses.cs", "NativeTracer.cs"):
            plugin = self.text("src/TGPathTracing/" + name)
            for gone in ("FogFroxel", "file.Bind(Advanced, \"Traced fog: froxel cache", "FrameFogFroxels"):
                self.assertNotIn(gone, plugin, name)
        # Nothing marked temporary is left in the fog's code.
        for name in ("shaders/pt_fog_froxel.hlsl", "shaders/pt_fog_integrate.hlsl", "shaders/pt_fog_tail.hlsl", "shaders/pt_froxel.hlsli",
                     "src/fog_froxel.inl", "src/scene.hpp"):
            self.assertNotIn("TEMP DIAG", self.text(PT + name), name)
        self.assertNotIn("TEMP DIAG", self.text("tests/pathtracing_fog_froxel_checks.inl"))
        # The cells: the slices' count is one number on both sides, and the cells reach past Remix's 20 m.
        froxel = self.text(PT + "shaders/pt_froxel.hlsli")
        self.assertIn("static const uint FOG_NEAR_SLICES = 48u;", froxel)
        self.assertIn("static const uint FOG_FAR_SLICES = 48u;", froxel)
        self.assertIn("static const uint FOG_SLICES = FOG_NEAR_SLICES + FOG_FAR_SLICES;", froxel)
        self.assertIn("constexpr uint32_t kFogSlices=96;", self.text(PT + "src/fog_froxel.inl"))
        self.assertIn("return FOG_REACH * pow(fog_far() / FOG_REACH, (slice - float(FOG_NEAR_SLICES)) / float(FOG_FAR_SLICES));", froxel)
        # The walk goes to the way's own end in its last slice: the cells' far side is no end of the medium.
        walk = self.text(PT + "shaders/pt_fog_integrate.hlsl")
        self.assertIn("return slice + 1u == FOG_SLICES ? end : min(end, fog_slice_depth(float(slice + 1u)) / viewRate);", walk)
        self.assertIn("groupshared float4 fogTile[3 * 3 * FOG_SLICES];", walk)
        # No limit that changes the picture: no clamp on a cell's sample, a lamp's light split in two shares that sum to
        # it, the pixel's share over the whole way with no fixed count of stretches.
        cells = self.text(PT + "shaders/pt_fog_froxel.hlsl")
        self.assertIn("output[cell] = float4(lerp(history.rgb, to_storage(max(light, 0.0)), 1.0 / age), age);", cells)
        self.assertIn("float fog_smooth_share(float d2, float r) { return d2 / (d2 + r * r); }", froxel)
        self.assertIn("const float share = 1.0 - fog_smooth_share(dist * dist, fog_smooth_radius(t * viewRate));", froxel)
        self.assertNotIn("stretch < 64u", froxel)
        self.assertIn("const float w = target * invPdf / (float(candidates) * asked);", froxel)
        # PT-133: the cells' pass keeps the shape that does not trip the Shader Model 6.9 fault without a sun.
        self.assertIn("KEEP THIS SHAPE", cells)
        self.assertIn("float3 sunSky = 0.0;", cells)
        self.assertIn("    light += sunSky;\n    light += froxel_lamps(x, d, radius, rng);", cells.replace("\r\n", "\n"))
        # Only points the camera's rays read take a sample; a cell never seen counts for nothing in the walk and in
        # the history's fetch.
        self.assertIn("if (!read) { output[cell] = history; return; }", cells)
        self.assertIn("float4 fog_cell_counted(float4 c) { return c.a > 0.0 ? float4(c.rgb, 1.0) : float4(0.0, 0.0, 0.0, 0.0); }", walk)
        self.assertIn("if (c.a > 0.0 && all(isfinite(c))) { sum += c * w; weights += w; }", cells)
        medium = self.text(PT + "shaders/pt_medium.hlsli")
        self.assertNotIn("sampleT", medium)
        # The check: held against what is known exactly where that exists, and pinned elsewhere; each case from an
        # empty mist cache.
        check = self.text("tests/pathtracing_fog_froxel_checks.inl")
        self.assertIn('if(judged) expect(right,haveKnown ? "the fog is what is known exactly (each region\'s mean within 3 %)" : "the fog is what its pin holds it to (each region\'s mean within 3 %)");', check)
        # (The mist's fixed point is printed, not judged: its cache is fed inside the camera's view alone.)
        self.assertIn("const bool pinned=fogCase.pin[4]>0.0, judged=!((fogCase.place==2 || night) && fogCase.cache);", check)
        # PT-138 (docs/agents/fog-3.md): the open cases' ground is black by its base colour and still reflects 4 %:
        # its highlight of the sun and of the lamp, dimmed by the fog, is part of the exact value (left out, the
        # check's first run read the ground x1.24 to x3.43).
        self.assertIn("auto groundShine=[&](V3 n, V3 v, V3 l) {", check)
        self.assertIn("const double behind=sky ? double(skyColor[c])/double(exposure) : sunE*sunShine+(fogCase.place==5 ? double(lamps[0].color[c])*lampShine : 0.0);", check)
        self.assertIn('const bool absorber=asked("TGPT_TEST_FOG_ABSORBER");', check)
        # PT-37 (the same report): the night's fog, a fog that thins with height, known exactly by
        # quadrature, and HDRP's own fog by hand beside it.
        self.assertIn("total+=(a*(sunE*sunShare*w.sunPart+s*w.skyPart)+behind)*double(exposure);", check)
        self.assertIn("totalHdrp+=(a*(moonDimmer*sunE*sunShare*w.sunPart+s*w.taken)+behind)*double(exposure);", check)
        self.assertNotIn("FOG_SKY_UNDIMMED", self.text(PT + "include/tgpt_api.h"))
        self.assertIn("cut=true; run(TGPT_VIEW_NONE,view,view,frameBase,baseOptions|TGPT_OPTION_NO_RADIANCE_CACHE); context->Flush();", check)
        pins = self.text("tests/pathtracing_fog_froxel_pins.inl")
        for case in ('"open: a uniform fog, sun and sky, the light scattered once"', '"open: no sun, no sky, a lamp 60 m ahead"',
                     '"open, no ground: the mist\'s cache"', '"no sun in the frame, a lamp 2 m ahead"',
                     '"outdoors, the vista\'s fog, the mist\'s cache"',
                     '"outdoors, the night\'s fog: its moon alone, the light scattered once"',
                     '"outdoors, the night\'s fog: its sky alone, the light scattered once"'):
            self.assertIn(case, pins)
        self.assertNotIn("froxel", self.text("tests/ingame/scenarios.json"))

if __name__ == "__main__":
    unittest.main(verbosity=2)
