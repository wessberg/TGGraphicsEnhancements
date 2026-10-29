from pathlib import Path
import shutil
root=Path(__file__).resolve().parents[2]
def edit(name, old, new):
    p=root/name
    backup=root/'.testruns/nr/base'/name
    backup.parent.mkdir(parents=True,exist_ok=True)
    if not backup.exists(): shutil.copyfile(p,backup)
    raw=p.read_bytes(); crlf=b'\r\n' in raw
    text=raw.decode('utf-8').replace('\r\n','\n')
    assert text.count(old)==1, (name,text.count(old),old[:90])
    text=text.replace(old,new)
    p.write_bytes(text.replace('\n','\r\n').encode('utf-8') if crlf else text.encode('utf-8'))
p=root/'src/TGFrameGeneration/HudlessUi.cs'
text=p.read_text()
a=text.index('    internal sealed class HudlessPass')
b=text.index('    internal static class HudlessUi')
edit(str(p.relative_to(root)),text[a:b],'')
text=p.read_text(); a=text.index('#if BEPINEX_IL2CPP',text.index('internal static void Initialize')); b=text.index('                failed = false;',a)
edit(str(p.relative_to(root)),text[a:b], '                ScenePostProcess.Register(ScenePostStage.FrameGenerationCapture, Execute);\n                installed = true;\n')
edit(str(p.relative_to(root)),'private static GameObject host;','private static bool installed;')
edit(str(p.relative_to(root)),'if (host != null) return;','if (installed) return;')
edit(str(p.relative_to(root)),'!failed && host != null','!failed && installed')
edit(str(p.relative_to(root)),'host?.SetActive(want);','ScenePostProcess.SetActive(ScenePostStage.FrameGenerationCapture, want);')
edit(str(p.relative_to(root)),'host?.SetActive(false);','ScenePostProcess.SetActive(ScenePostStage.FrameGenerationCapture, false);')
edit(str(p.relative_to(root)),'if (host != null) UnityEngine.Object.Destroy(host);\n            host = null;','ScenePostProcess.Unregister(ScenePostStage.FrameGenerationCapture);\n            installed = false;')
edit('native/framegen/include/sl_runtime.hpp','std::array<const wchar_t*,2> pluginPaths{};','std::array<const wchar_t*,3> pluginPaths{};\n    std::wstring neuralRenderingDir;')
edit('native/framegen/src/sl_runtime.cpp','#include "sl_runtime.hpp"','#include "sl_runtime.hpp"\n#include <tgb/ngx_session.hpp>')
edit('native/framegen/src/sl_runtime.cpp','    sl::Preferences pref{};', '''    s.neuralRenderingDir=tgb::neural_runtime_folder(host());
    if(!s.neuralRenderingDir.empty()) s.pluginPaths[pluginPathCount++]=s.neuralRenderingDir.c_str();
    sl::Preferences pref{};''')
edit('native/upscaling/src/rr_upscaler.cpp','#include "runtime.hpp"','#include "runtime.hpp"\n#include <tgb/ngx_session.hpp>')
edit('native/upscaling/src/rr_upscaler.cpp','    const wchar_t* paths[]={folder.c_str()};','    const auto nrFolder=tgb::neural_runtime_folder(host());\n    const wchar_t* paths[]={folder.c_str(), nrFolder.c_str()};')
edit('native/upscaling/src/rr_upscaler.cpp','info.PathListInfo.Length=1;','info.PathListInfo.Length=nrFolder.empty() ? 1u : 2u;')
edit('native/upscaling/src/rr_upscaler.cpp','    if(ngx.initialized) { NVSDK_NGX_D3D12_Shutdown1(ngx.device.Get()); ngx.initialized=false; }','    // The bridge device and its NGX session live for the process; another mod may own features.\n    if(ngx.initialized) { ngx.reason="The shared NGX device changed; restart the game."; return false; }')
edit('src/TGGraphicsBridge/Notices.cs','Upscaling = 2 }','Upscaling = 2, NeuralRendering = 4 }')
edit('src/TGGraphicsBridge/Notices.cs','            if (source == "TG Better Upscaling") return AlertRow.Upscaling;','            if (source == "TG Better Upscaling") return AlertRow.Upscaling;\n            if (source == "TG Neural Rendering") return AlertRow.NeuralRendering;')
edit('src/TGGraphicsBridge/RowAlerts.cs','                if (Has(AlertRow.FrameGeneration))','                if (Has(AlertRow.NeuralRendering))\n                {\n                    lock (registered) if (registered.TryGetValue(AlertRow.NeuralRendering, out var nr)) alerted.Add(VideoTab.Identity(nr));\n                }\n                if (Has(AlertRow.FrameGeneration))')
print('Shared edits applied; originals in .testruns/nr/base.')
