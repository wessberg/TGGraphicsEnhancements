from pathlib import Path
exec(Path('.testruns/nr/add-stage-diagnostics.py').read_text().split("edit('native/neural/src/extension.cpp'")[0])
edit('native/neural/src/diagnostics.hpp',[
('width=64,height=36,stages=5;', 'width=64,height=36,stages=6;'),
('bool active() const { return !folder.empty(); }', '''bool replayRequested=false,replayCaptured=false,replayReset=false;
    TGNR_Frame replayFrame{};
    bool active() const { return !folder.empty(); }
    void set_replay(uint32_t flags) {
        if(flags>3 || flags==2 || (flags && !active())) throw std::runtime_error("Replay requires active NR stage diagnostics.");
        replayRequested=(flags&1)!=0; replayReset=(flags&2)!=0; replayCaptured=false; replayFrame={};
    }
    bool replaying(const TGNR_Frame& f) {
        if(!active() || !replayCaptured) return false;
        const auto& s=replayFrame;
        if(s.camera==f.camera && s.width==f.width && s.height==f.height && s.renderWidth==f.renderWidth &&
           s.renderHeight==f.renderHeight && (s.flags&~TGNR_RESET)==(f.flags&~TGNR_RESET)) return true;
        fail(std::runtime_error("Replay camera, size or colour/depth convention changed."));
        return false;
    }'''),
('folder.clear(); metadata.close(); metadata.clear(); samples.clear();', 'set_replay(0);\n        folder.clear(); metadata.close(); metadata.clear(); samples.clear();'),
('jitterX,jitterY,exposurePresent\\n', 'jitterX,jitterY,exposurePresent,replaySourceFrame,replayReset\\n'),
('folder.clear(); metadata.close();\n    }', 'replayRequested=replayCaptured=replayReset=false; replayFrame={};\n        folder.clear(); metadata.close();\n    }'),
('const auto readAs=mode==2 && (desc.Format==DXGI_FORMAT_R32_FLOAT || desc.Format==DXGI_FORMAT_R32_TYPELESS)',
 'const auto readAs=(mode==2 || mode==3) && (desc.Format==DXGI_FORMAT_R32_FLOAT || desc.Format==DXGI_FORMAT_R32_TYPELESS)'),
('const uint32_t header[]={0x3144524e,1,', 'const uint32_t header[]={0x3144524e,2,'),
("<<(exposure?1:0)<<'\\n';", "<<(exposure?1:0)<<','<<(replayCaptured?replayFrame.frame:0)<<','<<replayReset<<'\\n';")])
edit('native/neural/src/extension.cpp',[
('void disable() {\n        if(consumed.ready())', 'void disable() {\n        diagnostics.set_replay(0);\n        if(consumed.ready())'),
('copy(p.depth.Get(),depth,ReadAs::Depth,copyDepth.Get(),0,depthRegion.x,depthRegion.y);\n        copy(p.motion.Get(),motion,ReadAs::Motion,copyMotion.Get(),observed && observed->source==DlssSource::SuperResolution ? 1u:0u,motionRegion.x,motionRegion.y);', '''if(!diagnostics.replaying(f)) {
            copy(p.depth.Get(),depth,ReadAs::Depth,copyDepth.Get(),0,depthRegion.x,depthRegion.y);
            copy(p.motion.Get(),motion,ReadAs::Motion,copyMotion.Get(),observed && observed->source==DlssSource::SuperResolution ? 1u:0u,motionRegion.x,motionRegion.y);
        }'''),
('copy(p.color.Get(),color,ReadAs::Color,copyColor.Get(),((f.flags&TGNR_SRGB_VIEW)?1u:0u)|((f.flags&TGNR_HDR_INPUT)?2u:0u),colorRegion.x,colorRegion.y);', '''const bool replayStarting=diagnostics.active() && diagnostics.replayRequested && !diagnostics.replayCaptured;
        if(!diagnostics.replaying(f))
            copy(p.color.Get(),color,ReadAs::Color,copyColor.Get(),((f.flags&TGNR_SRGB_VIEW)?1u:0u)|((f.flags&TGNR_HDR_INPUT)?2u:0u),colorRegion.x,colorRegion.y);
        if(replayStarting) {
            const float zero[4]={}; context->ClearUnorderedAccessViewFloat(motion.uav.Get(),zero);
            diagnostics.replayFrame=f; diagnostics.replayCaptured=true;
            log("NR identical-input replay starts at frame="+std::to_string(f.frame)+", reset="+std::to_string(diagnostics.replayReset));
        }'''),
('bool reset=reset_history(f,previous,havePrevious);', 'bool reset=reset_history(f,previous,havePrevious) || (replayStarting && diagnostics.replayReset);'),
('diagnostics.finish(f,reset,observedScaleX,observedScaleY,p.exposure.Get());', '''diagnostics.sample(depth.d11.Get(),{0,0,depth.width,depth.height},false,3);
        diagnostics.finish(f,reset,observedScaleX,observedScaleY,p.exposure.Get());''')])
with Path('native/neural/src/extension.cpp').open('a',encoding='utf-8',newline='\r\n') as f:
    f.write('''
TGNR_EXPORT int32_t __cdecl TGNR_SetDiagnosticReplay(uint32_t flags) {
    std::lock_guard<std::recursive_mutex> guard(tgnr::mutex);
    try { tgnr::runtime().diagnostics.set_replay(flags); return 0; }
    catch(const std::exception& e) { tgnr::log(e.what()); return -1; }
}
''')
edit('src/TGNeuralRendering/NativeNeural.cs',[
('private DiagnosticOutput diagnosticOutput;', '[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int DiagnosticReplay(uint flags);\n        private DiagnosticReplay diagnosticReplay;\n        private DiagnosticOutput diagnosticOutput;'),
('diagnosticExposure = Bind<DiagnosticExposure>(module, "TGNR_QueueDiagnosticExposure");', '''diagnosticExposure = Bind<DiagnosticExposure>(module, "TGNR_QueueDiagnosticExposure");
                    if (GetProcAddress(module, "TGNR_SetDiagnosticReplay") != IntPtr.Zero)
                        diagnosticReplay = Bind<DiagnosticReplay>(module, "TGNR_SetDiagnosticReplay");'''),
('internal void AttachDiagnosticExposure(IntPtr ticket, IntPtr texture)', '''internal void ConfigureReplay(uint flags)
        {
            if (diagnosticReplay == null || diagnosticReplay(flags) != 0)
                throw new InvalidOperationException("Could not configure identical-input NR replay.");
        }
        internal void AttachDiagnosticExposure(IntPtr ticket, IntPtr texture)''')])
edit('src/TGPathTracing.Harness/Request.cs',[
('public bool neuralStages = false;', 'public bool neuralReplay = false, neuralReplayReset = false; // Retain NR inputs after motion stops; optional one-time history reset.\n        public bool neuralStages = false;')])
edit('src/TGPathTracing.Harness/Neural.cs',[
('internal static void Check(Step step, long before, StepResult result)', '''internal static void Replay(bool reset)
        {
            if (!Chainloader.PluginInfos.TryGetValue(PluginId, out var plugin) || plugin.Instance == null)
                throw new InvalidOperationException("NR replay requires TG Neural Rendering.");
            var session = plugin.Instance.GetType().Assembly.GetType("TGNeuralRendering.Session");
            var native = session?.GetField("Native", Any)?.GetValue(null);
            var method = native?.GetType().GetMethod("ConfigureReplay", Any);
            if (method == null) throw new InvalidOperationException("This NR build lacks input replay.");
            method.Invoke(native, new object[] { reset ? 3u : 1u });
        }
        internal static void Check(Step step, long before, StepResult result)''')])
edit('src/TGPathTracing.Harness/Runner.cs',[
('if (step.neuralStages && !Try("Starting NR stage diagnostics",', '''if (step.neuralReplay && (!step.neuralStages || step.neural != true || step.motionFrames < 1))
            { r.error += " NR replay requires stage diagnostics, NR On and a moving/held sequence."; yield break; }
            if (step.neuralStages && !Try("Starting NR stage diagnostics",'''),
('frames.Add(info);\n                }', '''frames.Add(info);
                    // End of frame: the NEXT frame is the first stationary input retained for NR.
                    if (step.neuralReplay && i == step.motionFrames &&
                        !Try("Starting identical-input NR replay", () => Neural.Replay(step.neuralReplayReset)))
                    { error = "NR replay could not start"; break; }
                }''')])
print('Replay integration landed.')
