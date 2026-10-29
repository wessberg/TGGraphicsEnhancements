from pathlib import Path
p = Path('src/TGBetterUpscaling/StpHost.cs')
s = p.read_text(encoding='utf-8')
s = s.replace('using TGGraphicsBridge;\n', 'using TGGraphicsBridge;\nusing TGGraphicsBridge.Internal;\n', 1)
for line in [
    '        private static readonly FieldInfo CustomColor = AccessTools.Field(typeof(HDRenderPipeline), "m_CustomPassColorBuffer");\n',
    '        private static readonly FieldInfo CustomDepth = AccessTools.Field(typeof(HDRenderPipeline), "m_CustomPassDepthBuffer");\n',
    '        private static readonly MethodInfo ExecuteInternal = AccessTools.Method(typeof(CustomPass), "ExecuteInternal");\n']:
    assert s.count(line) == 1
    s = s.replace(line, '')
start = s.index('        private static void Record(UpscalePass')
end = s.index('        private static void SetScaler()', start)
s = s[:start] + s[end:]
s = s.replace('CameraField != null && ExecuteInternal != null && OutputHandle != null && CustomColor != null && CustomDepth != null',
              'CameraField != null && OutputHandle != null && CustomPassRecorder.Ready')
start = s.index('        // Not the interop\'s CustomPass.ExecuteInternal:')
end = s.index('        private static void SetScaler()', start)
s = s[:start] + s[end:]
s = s.replace('private static bool Ready => ExecuteInternalMethod != IntPtr.Zero;', 'private static bool Ready => CustomPassRecorder.Ready;')
s = s.replace('Record(pass, __instance, renderGraph, hdCamera, targets);', 'CustomPassRecorder.Record(pass, volume, __instance, renderGraph, hdCamera, targets);')
assert 'ExecuteInternalMethod' not in s and 'void Record(' not in s
p.write_bytes(s.replace('\n', '\r\n').encode('utf-8'))
