from pathlib import Path
import shutil
def edit(path,fn):
    p=Path(path); raw=p.read_bytes(); s=raw.decode().replace('\r\n','\n'); new=fn(s)
    assert new!=s,path
    b=Path('.testruns/nr/before-shared-ngx')/path; b.parent.mkdir(parents=True,exist_ok=True)
    if not b.exists(): shutil.copyfile(p,b)
    assert p.read_bytes()==raw,path
    p.write_bytes(new.replace('\n','\r\n').encode())
def rep(s,a,b):
    assert s.count(a)==1,a[:100]
    return s.replace(a,b)
def native(s):
    s=s.replace('#include <tgb/ngx_session.hpp>','#include <tgb/ngx_session.hpp>\n#include <tgb/dlss_inputs.hpp>')
    s=s.replace('uint mode; uint padding; };','uint mode; uint padding; uint2 origin; uint2 unused; };')
    s=s.replace('source.Load(int3(p.x,extent.y-1-p.y,0))','source.Load(int3(origin.x+p.x,origin.y+extent.y-1-p.y,0))')
    s=s.replace('float4 Output(float4 p:SV_Position):SV_Target {','float4 Output(float4 p:SV_Position):SV_Target {\n p.xy-=origin;')
    s=s.replace('Before SR, use an invertible','At either placement, use an invertible')
    s=s.replace('cb.ByteWidth=16;','cb.ByteWidth=32;')
    s=rep(s,'TGNR_Frame guideFrame{},previous{};','''TGNR_Frame guideFrame{},previous{};
    DlssRect colorRegion{},motionRegion{};
    float observedScaleX=0,observedScaleY=0;''')
    s=rep(s,'ID3D11ComputeShader* shader,uint32_t mode=0) {','ID3D11ComputeShader* shader,uint32_t mode=0,uint32_t x=0,uint32_t y=0) {')
    s=s.replace('if(desc.Width<to.width || desc.Height<to.height)',
        'if(x>desc.Width || y>desc.Height || to.width>desc.Width-x || to.height>desc.Height-y)')
    s=s.replace('const uint32_t cb[]={to.width,to.height,mode,0};','const uint32_t cb[]={to.width,to.height,mode,0,x,y,0,0};')
    s=rep(s,'void guides(const Packet& p) {','void guides(const Packet& p,const DlssInputs* observed=nullptr) {')
    s=rep(s,'''        shared(motion,f.renderWidth,f.renderHeight,DXGI_FORMAT_R16G16_FLOAT);
        ContextScope scope(host->game_context(),contextState.Get());
        copy(p.depth.Get(),depth,ReadAs::Depth,copyDepth.Get());
        copy(p.motion.Get(),motion,ReadAs::Motion,copyMotion.Get());''','''        const DlssRect depthRegion=observed ? observed->depthRect : DlssRect{0,0,f.renderWidth,f.renderHeight};
        motionRegion=observed ? observed->motionRect : depthRegion;
        colorRegion=observed ? ((f.flags&TGNR_BEFORE_UPSCALE) ? observed->colorRect : observed->outputRect) : DlssRect{0,0,f.width,f.height};
        observedScaleX=observed ? observed->motionScaleX*float(f.width)/motionRegion.width : -float(f.width);
        observedScaleY=observed ? -observed->motionScaleY*float(f.height)/motionRegion.height : float(f.height);
        shared(motion,motionRegion.width,motionRegion.height,DXGI_FORMAT_R16G16_FLOAT);
        ContextScope scope(host->game_context(),contextState.Get());
        copy(p.depth.Get(),depth,ReadAs::Depth,copyDepth.Get(),0,depthRegion.x,depthRegion.y);
        copy(p.motion.Get(),motion,ReadAs::Motion,copyMotion.Get(),0,motionRegion.x,motionRegion.y);''')
    s=s.replace('((f.flags&TGNR_HDR_INPUT)?2u:0u));','((f.flags&TGNR_HDR_INPUT)?2u:0u),colorRegion.x,colorRegion.y);')
    s=rep(s,'params->Set("DLSSNR.MVecScaleX",-float(f.width)); params->Set("DLSSNR.MVecScaleY",float(f.height));',
        'params->Set("DLSSNR.MVecScaleX",observedScaleX); params->Set("DLSSNR.MVecScaleY",observedScaleY);')
    s=s.replace('region("MVec",f.renderWidth,f.renderHeight);','region("MVec",motionRegion.width,motionRegion.height);')
    s=s.replace('D3D11_VIEWPORT viewport{0,0,float(f.width),float(f.height),0,1};',
        'D3D11_VIEWPORT viewport{float(colorRegion.x),float(colorRegion.y),float(f.width),float(f.height),0,1};')
    s=s.replace('uint32_t cb[]={f.width,f.height,(f.flags&TGNR_HDR_INPUT)?2u:0u,0};',
        'uint32_t cb[]={f.width,f.height,(f.flags&TGNR_HDR_INPUT)?2u:0u,0,colorRegion.x,colorRegion.y,0,0};')
    marker='void __stdcall render(int event,void* data) {'
    observer='''// The graph only arms/bounds the gameplay camera. The observed DLSS call selects all GPU inputs.
struct Observation {
    Packet packet;
    ComPtr<ID3D11Resource> expectedColor;
    const void* feature=nullptr;
    bool armed=false,seen=false,succeeded=false;
    uint32_t calls=0;
} observation;
void clear_observation() { observation=Observation{}; }
void __cdecl observe_dlss(DlssStage stage,const DlssInputs* in) noexcept {
    if(!in || (stage!=DlssStage::Before && stage!=DlssStage::After)) return;
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!observation.armed || status.state!=TGNR_READY) return;
    try {
        auto& o=observation; auto& p=o.packet; auto& f=p.frame;
        if(stage==DlssStage::After) {
            if(o.seen && o.feature==in->feature) o.succeeded=in->success;
            return;
        }
        // Color identity associates the invocation with the graph's gameplay camera without
        // guessing by resolution or observing another camera's DLSS evaluation.
        if(!same_object(o.expectedColor.Get(),in->color)) return;
        if(++o.calls!=1) throw DxError(E_FAIL,"Multiple DLSS evaluations for the armed Neural Rendering camera.");
        if(!in->valid || in->outputRect.width!=f.width && !(f.flags&TGNR_BEFORE_UPSCALE) ||
           in->outputRect.height!=f.height && !(f.flags&TGNR_BEFORE_UPSCALE) ||
           in->context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)
            throw DxError(E_INVALIDARG,"The observed DLSS input contract does not match the gameplay camera.");
        // The validated NR profile expects non-jittered 2D motion, as this game's DLSS supplies.
        if(in->flags&NVSDK_NGX_DLSS_Feature_Flags_MVJittered)
            throw DxError(E_INVALIDARG,"The selected DLSS invocation uses jittered motion; no validated NR profile is available for it.");
        const bool before=(f.flags&TGNR_BEFORE_UPSCALE)!=0;
        if(before && in->source==DlssSource::RayReconstruction)
            throw DxError(E_INVALIDARG,"Neural Rendering must follow Ray Reconstruction's denoising.");
        const auto region=before ? in->colorRect : in->outputRect;
        f.width=region.width; f.height=region.height;
        f.renderWidth=in->depthRect.width; f.renderHeight=in->depthRect.height;
        f.flags=(f.flags&~TGNR_REVERSED_DEPTH) | ((in->flags&NVSDK_NGX_DLSS_Feature_Flags_DepthInverted)?TGNR_REVERSED_DEPTH:0u);
        if(in->reset) f.flags|=TGNR_RESET;
        f.jitterX=in->jitterX; f.jitterY=in->jitterY;
        p.depth=in->depth; p.motion=in->motion; p.color=before ? in->color : in->output;
        f.depth=reinterpret_cast<uint64_t>(p.depth.Get()); f.motion=reinterpret_cast<uint64_t>(p.motion.Get());
        f.color=reinterpret_cast<uint64_t>(p.color.Get());
        runtime().guides(p,in); o.feature=in->feature; o.seen=true;
        if(before) runtime().evaluate(p);
        if(status.evaluated<2 || f.frame%300==0) log(std::string("NR inputs from actual ")+
            (in->source==DlssSource::SuperResolution ? "DLSS SR" : "DLSS RR")+", frame="+std::to_string(f.frame)+
            ", motion="+std::to_string(in->motionRect.width)+"x"+std::to_string(in->motionRect.height)+
            ", scale="+std::to_string(in->motionScaleX)+","+std::to_string(in->motionScaleY));
    } catch(const std::exception& e) { ++status.failed; unavailable(TGNR_FAILED,e.what()); }
      catch(...) { ++status.failed; unavailable(TGNR_FAILED,"Could not observe DLSS inputs for Neural Rendering."); }
}
'''
    s=rep(s,marker,observer+marker)
    s=s.replace('try { runtime().disable(); }','try { clear_observation(); runtime().disable(); }')
    s=rep(s,'if(event==1) runtime().guides(*p); else if(event==2) runtime().evaluate(*p);','''if(event==1) runtime().guides(*p); else if(event==2) runtime().evaluate(*p); // standalone GPU probe
            else if(event==4) {
                clear_observation(); observation.packet=*p; observation.expectedColor=p->color; observation.armed=true;
            } else if(event==5) {
                auto& o=observation;
                if(!o.armed || !o.seen || !o.succeeded || o.packet.frame.frame!=p->frame.frame || o.packet.frame.camera!=p->frame.camera)
                    throw DxError(E_FAIL,"No successful DLSS invocation supplied this Neural Rendering frame.");
                if(!(o.packet.frame.flags&TGNR_BEFORE_UPSCALE)) {
                    if(!same_object(o.packet.color.Get(),p->color.Get())) throw DxError(E_FAIL,"DLSS output does not match the Neural Rendering graph output.");
                    runtime().evaluate(o.packet);
                }
                clear_observation();
            }''')
    s=s.replace('if(!f || (stage!=1 && stage!=2) || !tgnr::valid(*f,stage==1))',
        'if(!f || (stage!=1 && stage!=2 && stage!=4 && stage!=5) || !tgnr::valid(*f,stage==1))')
    s=rep(s,'if(status.state!=TGNR_WAITING) return;','if(status.state!=TGNR_WAITING) return;') if False else s
    marker='bool bootstrap() noexcept override {'
    s=rep(s,marker,marker+'\n        if(!subscribe_dlss(&observe_dlss)) { unavailable(TGNR_FAILED,"TG Neural Rendering requires the shared DLSS observer in bridge 1.2.3 or newer."); return false; }')
    s=s.replace('stage=AfterPostProcess before UI and FG; SDR sRGB, upright, real render-resolution guides.',
        'stage=before lens effects; actual DLSS invocation inputs, upright NR colour.')
    return s
edit('native/neural/src/extension.cpp',native)
edit('native/neural/include/tgnr_api.h',lambda s:s.replace('0x00010100u','0x00010200u').replace('TGNR_HDR_INPUT=8','TGNR_HDR_INPUT=8, TGNR_BEFORE_UPSCALE=16').replace('// Guides are captured at BeforePostProcess; color at that stage (HDR_INPUT) or AfterPostProcess. Both carry',
    '// Production events arm/finish at the upscaler boundary; the observed SR/RR call supplies guides. Both carry'))
edit('native/neural/include/tgnr_policy.hpp',lambda s:s.replace('~15u','~31u').replace('(current.flags&TGNR_HDR_INPUT)!=(previous.flags&TGNR_HDR_INPUT)',
    '(current.flags&(TGNR_HDR_INPUT|TGNR_BEFORE_UPSCALE))!=(previous.flags&(TGNR_HDR_INPUT|TGNR_BEFORE_UPSCALE))'))
edit('src/TGNeuralRendering/NativeNeural.cs',lambda s:s.replace('0x00010100','0x00010200'))
def placement(s):
    s=s.replace('private static NeuralScenePass pass;','private static NeuralScenePass pass;\n        private static NeuralInputPass inputPass;')
    s=s.replace('Il2CppTypes.Register<NeuralScenePass>();','Il2CppTypes.Register<NeuralScenePass>();\n            Il2CppTypes.Register<NeuralInputPass>();')
    s=s.replace('harmony = new Harmony(Plugin.Id + ".placement");',
        'inputPass = new NeuralInputPass { name = "TGNR DLSS inputs", targetColorBuffer = CustomPass.TargetBuffer.None, targetDepthBuffer = CustomPass.TargetBuffer.None };\n            harmony = new Harmony(Plugin.Id + ".placement");')
    s=s.replace('if (Session.BeforeUpscaling) Record(__instance, renderGraph, hdCamera, source, depthBuffer, motionVectors);',
        'Record(__instance, renderGraph, hdCamera, source, depthBuffer, motionVectors, true);')
    s=s.replace('if (!Session.BeforeUpscaling) Record(__instance, renderGraph, hdCamera, __result, depthBuffer, motionVectors);',
        'Record(__instance, renderGraph, hdCamera, __result, depthBuffer, motionVectors, false);')
    s=s.replace('TextureHandle color, TextureHandle depth, TextureHandle motion)\n',
        'TextureHandle color, TextureHandle depth, TextureHandle motion, bool begin)\n')
    s=s.replace('if (recordedFrame == Time.frameCount)', 'if (!begin && recordedFrame == Time.frameCount)')
    s=s.replace('if (recordedFrame >= 0 && executedFrame != recordedFrame)', 'if (!begin && recordededPreviousMissing())')
    s=s.replace('CustomPassRecorder.Record(pass, volume, pipeline, graph, camera, new CustomPass.RenderTargets {',
        'CustomPassRecorder.Record(begin ? (CustomPass)inputPass : pass, volume, pipeline, graph, camera, new CustomPass.RenderTargets {')
    s=s.replace('recordedFrame = Time.frameCount;', 'if (!begin) recordedFrame = Time.frameCount;')
    s=s.replace('internal static void Execute(CustomPassContext context)',
        'private static bool recordededPreviousMissing() => recordedFrame >= 0 && executedFrame != recordedFrame;\n        internal static void Arm(CustomPassContext context) => SceneCapture.Render(context, true);\n        internal static void Execute(CustomPassContext context)')
    s=s.replace('SceneCapture.Render(context);','SceneCapture.Render(context, false);')
    s=s.replace('volume = null; pass = null;', 'volume = null; pass = null; inputPass = null;')
    # Two different pass instances: ExecuteInternal stores its targets on the pass instance.
    a=s.index('    internal sealed class NeuralScenePass')
    cls=s[a:s.rfind('\n}')].replace('NeuralScenePass','NeuralInputPass').replace('UpscalePlacement.Execute(ctx)','UpscalePlacement.Arm(ctx)')
    s=s[:a]+cls+'\n'+s[a:]
    return s.replace('recordededPreviousMissing','PreviousExecutionMissing')
edit('src/TGNeuralRendering/UpscalePlacement.cs',placement)
def capture(s):
    s=s.replace('Render(CustomPassContext ctx)','Render(CustomPassContext ctx, bool begin)')
    s=s.replace('try { CaptureGuides(ctx); Color(ctx); }','try { if (begin) ArmDlss(ctx); else Color(ctx); }')
    s=s.replace('private static void CaptureGuides(', 'private static void ArmDlss(')
    s=s.replace('Depth = unchecked((ulong)NativeTextures.Pointer(ctx.cameraDepthBuffer).ToInt64()), Motion = unchecked((ulong)NativeTextures.Pointer(ctx.cameraMotionVectorsBuffer).ToInt64()),',
        '// Only the source identifies this camera. Actual depth, motion, jitter and scales come from DLSS.\n                Color = unchecked((ulong)NativeTextures.Pointer(ctx.cameraColorBuffer).ToInt64()),')
    s=s.replace('Flags = (SystemInfo.usesReversedZBuffer ? 1u : 0u) | (cut ? 2u : 0u),',
        'Flags = (SystemInfo.usesReversedZBuffer ? 1u : 0u) | (cut ? 2u : 0u) | 8u | (Session.BeforeUpscaling ? 16u : 0u),')
    s=s.replace('Submit(ref f, 1)','Submit(ref f, 4)').replace('RenderEvent, 1, ticket','RenderEvent, 4, ticket')
    s=s.replace('Submit(ref f, 2)','Submit(ref f, 5)').replace('RenderEvent, 2, ticket','RenderEvent, 5, ticket')
    return s
edit('src/TGNeuralRendering/SceneCapture.cs',capture)
def up(s):
    s='#include <tgb/dlss_inputs.hpp>\n#include <nvsdk_ngx_defs.h>\n'+s
    marker='    if(eventId==1) {\n        guarded([&]{ outcome=process_inputs(frame); });'
    replacement='''    tgb::DlssInputs observed;
    const bool observeRr=frame.backend==TGUP_BACKEND_RAY_RECONSTRUCTION && frame.output!=0;
    if(observeRr) {
        observed.source=tgb::DlssSource::RayReconstruction; observed.feature=&frame; observed.context=host().game_context();
        observed.color=reinterpret_cast<ID3D11Resource*>(frame.color); observed.depth=reinterpret_cast<ID3D11Resource*>(frame.depth);
        observed.motion=reinterpret_cast<ID3D11Resource*>(frame.motion); observed.output=reinterpret_cast<ID3D11Resource*>(frame.output);
        observed.colorRect=observed.depthRect=observed.motionRect={0,0,frame.renderWidth,frame.renderHeight};
        observed.outputRect={0,0,frame.outputWidth,frame.outputHeight};
        observed.flags=NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
            ((frame.flags&TGUP_FLAG_DEPTH_INVERTED)?NVSDK_NGX_DLSS_Feature_Flags_DepthInverted:0);
        observed.reset=(frame.flags&TGUP_FLAG_RESET)!=0;
        observed.motionScaleX=frame.motionScaleX; observed.motionScaleY=frame.motionScaleY;
        observed.jitterX=-frame.jitterX; observed.jitterY=-frame.jitterY;
        observed.valid=tgb::valid_dlss_inputs(observed);
    }
    if(eventId==1) {
        if(observeRr) tgb::publish_dlss(tgb::DlssStage::Before,observed);
        guarded([&]{ outcome=process_inputs(frame); });'''
    s=rep(s,marker,replacement)
    s=s.replace('if(single) guarded([&]{ write_output(frame, outcome); });',
        'if(single) guarded([&]{ write_output(frame, outcome); });\n        if(observeRr) { observed.success=outcome.upscaled; tgb::publish_dlss(tgb::DlssStage::After,observed); }')
    return s
edit('native/upscaling/src/upscaler.cpp',up)
print('NR now arms the gameplay camera, takes actual shared DLSS inputs, then completes before lens effects; RR publishes its selected packet.')
