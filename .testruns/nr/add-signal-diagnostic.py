from pathlib import Path

def edit(name, changes):
    p = Path(name)
    text = p.read_text(encoding='utf-8')
    for old, new in changes:
        assert text.count(old) == 1, (name, old[:100], text.count(old))
        text = text.replace(old, new)
    p.write_bytes(text.replace('\n', '\r\n').encode('utf-8'))

edit('native/neural/src/diagnostics.hpp', [
    ('TGNR_Frame replayFrame{};', 'TGNR_Frame replayFrame{};\n    float inputScale=1; // Harness-only reversible HDR conditioning; normal play always uses 1.\n    void set_input_scale(float value) {\n        if(!active() || replayCaptured || !std::isfinite(value) || value<0.125f || value>8)\n            throw std::runtime_error("NR input scale requires active diagnostics, no retained replay and a value in [0.125,8].");\n        inputScale=value;\n    }'),
    ('        set_replay(0);\n        folder.clear();', '        set_replay(0); inputScale=1;\n        folder.clear();'),
    ('replaySourceFrame,replayReset\\n', 'replaySourceFrame,replayReset,inputScale\\n'),
    ('replayRequested=replayCaptured=replayReset=false; replayFrame={};', 'replayRequested=replayCaptured=replayReset=false; replayFrame={}; inputScale=1;'),
    ('float sx,float sy,ID3D11Resource* exposure) noexcept', 'float sx,float sy,ID3D11Resource* exposure,float usedInputScale) noexcept'),
    ("<<replayReset<<'\\n';", "<<replayReset<<','<<usedInputScale<<'\\n';")])

edit('native/neural/src/extension.cpp', [
    ('uint2 origin; uint2 unused;', 'uint2 origin; float inputScale; uint unused;'),
    ('compressHdr(max(v.rgb,0))', 'compressHdr(max(v.rgb,0)*inputScale)'),
    ('rgb=expandHdr(rgb);', 'rgb=expandHdr(rgb)/inputScale;'),
    ('double gpuMilliseconds=0;', 'double gpuMilliseconds=0;\n    float previousInputScale=1;'),
    ('uint32_t mode=0,uint32_t x=0,uint32_t y=0) {', 'uint32_t mode=0,uint32_t x=0,uint32_t y=0,float inputScale=1) {'),
    ('const uint32_t cb[]={to.width,to.height,mode,0,x,y,0,0};', 'struct CopyParams { uint32_t w,h,mode,pad,x,y; float scale; uint32_t unused; };\n        const CopyParams cb{to.width,to.height,mode,0,x,y,inputScale,0};'),
    ('nullptr,cb,0,0);\n        ID3D11Buffer* buffer', 'nullptr,&cb,0,0);\n        ID3D11Buffer* buffer'),
    ('diagnostics.samples.clear();', 'const float inputScale=diagnostics.active() ? diagnostics.inputScale:1;\n        diagnostics.samples.clear();'),
    ('colorRegion.x,colorRegion.y);\n        if(replayStarting)', 'colorRegion.x,colorRegion.y,inputScale);\n        if(replayStarting)'),
    ('reset_history(f,previous,havePrevious) || (replayStarting', 'reset_history(f,previous,havePrevious) || inputScale!=previousInputScale || (replayStarting'),
    ('uint32_t cb[]={f.width,f.height,(f.flags&TGNR_HDR_INPUT)?2u:0u,0,colorRegion.x,colorRegion.y,0,0}; context->UpdateSubresource(constants.Get(),0,nullptr,cb,0,0);', 'struct OutputParams { uint32_t w,h,mode,pad,x,y; float scale; uint32_t unused; };\n            const OutputParams cb{f.width,f.height,(f.flags&TGNR_HDR_INPUT)?2u:0u,0,colorRegion.x,colorRegion.y,inputScale,0};\n            context->UpdateSubresource(constants.Get(),0,nullptr,&cb,0,0);'),
    ('diagnostics.finish(f,reset,observedScaleX,observedScaleY,p.exposure.Get());', 'diagnostics.finish(f,reset,observedScaleX,observedScaleY,p.exposure.Get(),inputScale);\n        previousInputScale=inputScale;'),
    ('TGNR_EXPORT int32_t __cdecl TGNR_SetDiagnosticReplay(uint32_t flags) {', 'TGNR_EXPORT int32_t __cdecl TGNR_SetDiagnosticInputScale(float scale) {\n    std::lock_guard<std::recursive_mutex> guard(tgnr::mutex);\n    try { tgnr::runtime().diagnostics.set_input_scale(scale); return 0; }\n    catch(const std::exception& e) { tgnr::log(e.what()); return -1; }\n}\n\nTGNR_EXPORT int32_t __cdecl TGNR_SetDiagnosticReplay(uint32_t flags) {')])

edit('src/TGNeuralRendering/NativeNeural.cs', [
    ('private DiagnosticReplay diagnosticReplay;', '[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int DiagnosticInputScale(float scale);\n        private DiagnosticInputScale diagnosticInputScale;\n        private DiagnosticReplay diagnosticReplay;'),
    ('if (GetProcAddress(module, "TGNR_SetDiagnosticReplay")', 'if (GetProcAddress(module, "TGNR_SetDiagnosticInputScale") != IntPtr.Zero)\n                        diagnosticInputScale = Bind<DiagnosticInputScale>(module, "TGNR_SetDiagnosticInputScale");\n                    if (GetProcAddress(module, "TGNR_SetDiagnosticReplay")'),
    ('internal void ConfigureReplay(uint flags)', 'internal void ConfigureInputScale(float scale)\n        {\n            if (diagnosticInputScale == null || diagnosticInputScale(scale) != 0)\n                throw new InvalidOperationException("Could not configure NR input conditioning.");\n        }\n        internal void ConfigureReplay(uint flags)')])

edit('src/TGPathTracing.Harness/Neural.cs', [
    ('internal static void StageDiagnostics(string folder)', 'internal static void StageDiagnostics(string folder, float inputScale = 1)'),
    ('method.Invoke(native, new object[] { folder });', 'method.Invoke(native, new object[] { folder });\n            if (folder != null && inputScale != 1)\n            {\n                try\n                {\n                    var scale = native.GetType().GetMethod("ConfigureInputScale", Any);\n                    if (scale == null) throw new InvalidOperationException("This NR build lacks input conditioning diagnostics.");\n                    scale.Invoke(native, new object[] { inputScale });\n                }\n                catch { method.Invoke(native, new object[] { null }); throw; }\n            }')])
edit('src/TGPathTracing.Harness/Request.cs', [
    ('public bool neuralStages = false;', 'public float neuralInputScale = 1; // Diagnostic HDR conditioning, inverted on output; never a player setting.\n        public bool neuralStages = false;')])
edit('src/TGPathTracing.Harness/Runner.cs', [
    ('if (step.neuralReplay && (!step.neuralStages', 'if (step.neuralInputScale != 1 && (!step.neuralStages || step.neural != true))\n            { r.error += " NR input conditioning requires stage diagnostics and NR On."; yield break; }\n            if (step.neuralReplay && (!step.neuralStages'),
    ('Neural.StageDiagnostics(Path.Combine(dir, "nr-stages"))', 'Neural.StageDiagnostics(Path.Combine(dir, "nr-stages"), step.neuralInputScale)')])
