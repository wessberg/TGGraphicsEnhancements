from pathlib import Path

def edit(name, pairs):
    p=Path(name); s=p.read_text(encoding='utf-8-sig')
    for a,b in pairs:
        assert s.count(a)==1,(name,a[:100],s.count(a))
        s=s.replace(a,b)
    p.write_bytes(s.replace('\r\n','\n').replace('\n','\r\n').encode('utf-8'))

edit('native/neural/src/extension.cpp',[
('#include <cstring>','#include <cstring>\n#include <limits>'),
('ComPtr<ID3D11Resource> color,depth,motion; };','ComPtr<ID3D11Resource> color,depth,motion,exposure; };'),
('struct Runtime {','#include "diagnostics.hpp"\n\nstruct Runtime {\n    Diagnostics diagnostics;'),
('auto context=host->game_context(); ContextScope scope(context,contextState.Get());\n        copy(p.color',
 'auto context=host->game_context(); ContextScope scope(context,contextState.Get());\n        diagnostics.samples.clear();\n        diagnostics.sample(p.color.Get(),colorRegion,true); // scene_in, linear pre-exposed HDR\n        copy(p.color'),
('const auto ready=produced.next(); check(context->Signal',
 'diagnostics.sample(color.d11.Get(),{0,0,f.width,f.height},false); // model_in, encoded SDR\n        const auto ready=produced.next(); check(context->Signal'),
('// Commit only a completed successful output.',
 'diagnostics.sample(result.d11.Get(),{0,0,f.width,f.height},false); // model_out, encoded SDR\n        // Commit only a completed successful output.'),
('previous=f; havePrevious=true; ++status.evaluated;',
 'diagnostics.sample(p.color.Get(),colorRegion,true); // scene_out, restored linear HDR\n        diagnostics.sample(motion.d11.Get(),{0,0,motion.width,motion.height},false,1,observedScaleX,observedScaleY);\n        diagnostics.finish(f,reset,observedScaleX,observedScaleY,p.exposure.Get());\n        previous=f; havePrevious=true; ++status.evaluated;'),
('p->color.Reset(); p->depth.Reset(); p->motion.Reset(); p->busy=false;',
 'p->color.Reset(); p->depth.Reset(); p->motion.Reset(); p->exposure.Reset(); p->busy=false;'),
('p.frame=*f;','p.frame=*f; p.exposure.Reset();')])
p=Path('native/neural/src/extension.cpp')
with p.open('a',encoding='utf-8',newline='\r\n') as f:
    f.write('''
// Additive, harness-only exports: no frame/status ABI change and no normal-play readback.
TGNR_EXPORT int32_t __cdecl TGNR_SetDiagnostics(const wchar_t* folder) {
    std::lock_guard<std::recursive_mutex> guard(tgnr::mutex);
    try { tgnr::runtime().diagnostics.configure(folder); return 0; }
    catch(const std::exception& e) { tgnr::log(e.what()); return -1; }
}
TGNR_EXPORT int32_t __cdecl TGNR_QueueDiagnosticExposure(void* ticket,ID3D11Resource* exposure) {
    std::lock_guard<std::recursive_mutex> guard(tgnr::mutex);
    for(auto& p:tgnr::packets) if(&p==ticket && p.busy) { p.exposure=exposure; return 0; }
    return -1;
}
''')
edit('src/TGNeuralRendering/NativeNeural.cs',[
('private Read read; private Reason reason; private Queue queue;', '''[UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int DiagnosticOutput([MarshalAs(UnmanagedType.LPWStr)] string folder);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int DiagnosticExposure(IntPtr ticket, IntPtr texture);
        private DiagnosticOutput diagnosticOutput; private DiagnosticExposure diagnosticExposure;
        internal bool DiagnosticsEnabled { get; private set; }
        private Read read; private Reason reason; private Queue queue;'''),
('RenderEvent = Bind<GetEvent>(module, "TGNR_GetRenderEvent")(); read = Bind<Read>(module, "TGNR_ReadStatus");', '''RenderEvent = Bind<GetEvent>(module, "TGNR_GetRenderEvent")(); read = Bind<Read>(module, "TGNR_ReadStatus");
                // Older compatible native builds keep working; diagnostics explicitly require these additions.
                if (GetProcAddress(module, "TGNR_SetDiagnostics") != IntPtr.Zero)
                {
                    diagnosticOutput = Bind<DiagnosticOutput>(module, "TGNR_SetDiagnostics");
                    diagnosticExposure = Bind<DiagnosticExposure>(module, "TGNR_QueueDiagnosticExposure");
                }'''),
('internal IntPtr Submit(ref NeuralFrame frame, uint stage)', '''internal void ConfigureDiagnostics(string folder)
        {
            if (diagnosticOutput == null)
            {
                if (!string.IsNullOrEmpty(folder)) throw new InvalidOperationException("This native NR build lacks stage diagnostics.");
                return;
            }
            DiagnosticsEnabled = false;
            if (diagnosticOutput(folder) != 0) throw new InvalidOperationException("Could not configure NR stage diagnostics.");
            DiagnosticsEnabled = !string.IsNullOrEmpty(folder);
        }
        internal void AttachDiagnosticExposure(IntPtr ticket, IntPtr texture)
        {
            if (diagnosticExposure(ticket, texture) != 0) throw new InvalidOperationException("NR diagnostic frame ticket was not retained.");
        }
        internal IntPtr Submit(ref NeuralFrame frame, uint stage)''')])
edit('src/TGNeuralRendering/SceneCapture.cs',[
('private static readonly FieldInfo historyReset =', '''private static readonly Func<HDRenderPipeline, HDCamera, RTHandle> ExposureTexture =
            HarmonyLib.AccessTools.Method(typeof(HDRenderPipeline), "GetExposureTexture", new[] { typeof(HDCamera) }) is MethodInfo exposureTexture
                ? (Func<HDRenderPipeline, HDCamera, RTHandle>)Delegate.CreateDelegate(typeof(Func<HDRenderPipeline, HDCamera, RTHandle>), exposureTexture) : null;
        private static readonly FieldInfo historyReset ='''),
('ctx.cmd.IssuePluginEventAndData(Session.Native.RenderEvent, 4, ticket);', '''if (Session.Native.DiagnosticsEnabled)
            {
                var pipeline = RenderPipelineManager.currentPipeline as HDRenderPipeline;
#if BEPINEX_IL2CPP
                var exposure = pipeline == null ? null : pipeline.GetExposureTexture(hd);
#else
                var exposure = pipeline == null ? null : ExposureTexture?.Invoke(pipeline, hd);
#endif
                Session.Native.AttachDiagnosticExposure(ticket, NativeTextures.Pointer(exposure));
            }
            ctx.cmd.IssuePluginEventAndData(Session.Native.RenderEvent, 4, ticket);''')])
edit('src/TGPathTracing.Harness/Request.cs',[
('public bool? neural = null;', 'public bool? neural = null;\n        public bool neuralStages = false; // Synchronous NR input/output/exposure capture; invalidates performance timings.')])
edit('src/TGPathTracing.Harness/Neural.cs',[
('internal static void Check(Step step, long before, StepResult result)', '''internal static void StageDiagnostics(string folder)
        {
            if (!Chainloader.PluginInfos.TryGetValue(PluginId, out var plugin) || plugin.Instance == null)
            {
                if (folder != null) throw new InvalidOperationException("NR stage diagnostics require TG Neural Rendering.");
                return;
            }
            var session = plugin.Instance.GetType().Assembly.GetType("TGNeuralRendering.Session");
            var native = session?.GetField("Native", Any)?.GetValue(null);
            var method = native?.GetType().GetMethod("ConfigureDiagnostics", Any);
            if (method == null)
            {
                if (folder != null) throw new InvalidOperationException("This NR build lacks stage diagnostics.");
                return;
            }
            method.Invoke(native, new object[] { folder });
        }
        internal static void Check(Step step, long before, StepResult result)''')])
edit('src/TGPathTracing.Harness/Runner.cs',[
('Try("Stopping the camera\'s slide", CameraSlide.Stop);\n            NeuralMotion.SuppressBlur',
 'Try("Stopping the camera\'s slide", CameraSlide.Stop);\n            Try("Stopping NR stage diagnostics", () => Neural.StageDiagnostics(null));\n            NeuralMotion.SuppressBlur'),
('if (step.frames > 0) yield return Burst(dir, step, r);\n            Neural.Check', '''if (step.neuralStages && !Try("Starting NR stage diagnostics", () => Neural.StageDiagnostics(Path.Combine(dir, "nr-stages"))))
            { r.error += " NR stage diagnostics could not start."; yield break; }
            try { if (step.frames > 0) yield return Burst(dir, step, r); }
            finally { if (step.neuralStages) Try("Stopping NR stage diagnostics", () => Neural.StageDiagnostics(null)); }
            Neural.Check''')])
edit('.testruns/nr/deploy-tuning.ps1',[
('Assert-TgGameClosed\nif (Get-Process', "Assert-TgGameClosed\nif (Test-Path -LiteralPath (Join-Path $repo 'artifacts/harness/RUNNING')) { throw 'The shared game harness is busy.' }\nif (Get-Process"),
("version='1.2.2'", "version='1.2.3'")])
print('All diagnostic integration edits landed.')
