"""Source/build-graph checks only; not C#/PowerShell compilation or runtime tests."""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET
ROOT = Path(__file__).resolve().parents[1]
MODS = ['TGGraphicsBridge', 'TGFrameGeneration', 'TGBetterUpscaling']

def text(name):
    return (ROOT/name).read_text(encoding='utf-8')

class AutoloadIntegration(unittest.TestCase):
    def test_build_graph(self):
        # Each mod is compiled against the bridge's library and builds its own copy of the bridge's
        # preloader, which it never references as an assembly. The library references nothing.
        self.assertFalse(ET.parse(ROOT/'src/TGGraphicsBridge/TGGraphicsBridge.csproj').findall('.//ProjectReference'))
        for mod in ['TGFrameGeneration', 'TGBetterUpscaling']:
            refs = {Path(r.get('Include').replace('\\','/')).name: r for r in ET.parse(ROOT/f'src/{mod}/{mod}.csproj').findall('.//ProjectReference')}
            self.assertEqual(set(refs), {'TGGraphicsBridge.csproj', f'{mod}.Autoload.csproj'}, mod)
            bridge, autoload = refs['TGGraphicsBridge.csproj'], refs[f'{mod}.Autoload.csproj']
            self.assertIsNone(bridge.get('ReferenceOutputAssembly'), mod)   # compiled against the bridge
            self.assertEqual(bridge.get('Private'), 'false', mod)           # staged by Build.ps1, not the build
            self.assertEqual(autoload.get('ReferenceOutputAssembly'), 'false', mod)
            self.assertEqual(autoload.get('Private'), 'false', mod)
            for r in refs.values():
                self.assertTrue((ROOT/f'src/{mod}'/r.get('Include').replace('\\','/')).resolve().is_file())
            # Its own assembly name: BepInEx loads every copy side by side.
            wrapper = ET.parse(ROOT/f'src/{mod}.Autoload/{mod}.Autoload.csproj')
            self.assertEqual(wrapper.find('.//AssemblyName').text, f'{mod}.Autoload')
            self.assertEqual(wrapper.find('Import').get('Project'), '..\\TGGraphicsBridge.Autoload\\Autoload.props')
        self.assertFalse((ROOT/'src/TGFG.Autoload/TGFG.Autoload.csproj').exists())
        self.assertFalse((ROOT/'src/TGGraphicsBridge.Autoload/TGGraphicsBridge.Autoload.csproj').exists())

    def test_matching_versions_and_paths(self):
        autoload = ET.parse(ROOT/'src/TGGraphicsBridge.Autoload/Autoload.props')
        props = ET.parse(ROOT/'src/Game.References.props')
        self.assertEqual(autoload.find('.//Version').text, '1.2.3')
        self.assertEqual(set(autoload.find('.//TargetFrameworks').text.split(';')), {'net472','net6.0'})
        self.assertEqual(autoload.find('Import').get('Project'), '$(MSBuildThisFileDirectory)..\\..\\Build.Local.props')
        for prop in ['GameDirMono', 'GameDirIl2Cpp']:
            self.assertEqual(autoload.find('.//'+prop).text, props.find('.//'+prop).text)
        for mod in MODS:
            project = ET.parse(ROOT/f'src/{mod}/{mod}.csproj')
            imports = [i.get('Project') for i in project.findall('Import')]
            self.assertEqual(imports, ['..\\..\\Build.Local.props', '..\\Game.References.props'], mod)
        for path in ['src/TGGraphicsBridge/Plugin.cs', 'src/TGFrameGeneration/Plugin.cs', 'src/TGBetterUpscaling/Plugin.cs']:
            self.assertIn('Version = "1.2.3"', text(path))
        # BepInEx picks a plugin copy by its declared version, the runtime binds references by the
        # assembly version: the two must never differ, or BepInEx 6 may load a copy a mod cannot bind to.
        for mod in MODS:
            declared = re.search(r'(?:public|internal) const string Version = "([^"]+)";', text(f'src/{mod}/Plugin.cs')).group(1)
            assembly = ET.parse(ROOT/f'src/{mod}/{mod}.csproj').find('.//Version').text
            self.assertEqual(declared, assembly, mod)
            self.assertIsNone(ET.parse(ROOT/f'src/{mod}/{mod}.csproj').find('.//AssemblyVersion'), mod)
        bridge = re.search(r'internal const string BridgeVersion = "([^"]+)";', text('src/TGGraphicsBridge.Autoload/Bootstrap.cs')).group(1)
        self.assertEqual(bridge, ET.parse(ROOT/'src/TGGraphicsBridge/TGGraphicsBridge.csproj').find('.//Version').text)
        self.assertEqual(bridge, autoload.find('.//Version').text)
        self.assertIn('internal const string BridgeVersion = "1.2.3";', text('src/TGGraphicsBridge.Autoload/Bootstrap.cs'))
        self.assertIn('[PatcherPluginInfo(Identity.PatcherGuid, Identity.Title + " graphics bridge autoload", Bootstrap.BridgeVersion)]', text('src/TGGraphicsBridge.Autoload/Patcher.cs'))
        # Every copy of the bridge a mod bundles says the same version, which is the bridge's.
        import json
        guids = set()
        for mod, title in [('TGFrameGeneration', 'TG Frame Generation'), ('TGBetterUpscaling', 'TG Better Upscaling')]:
            manifest = json.loads(text(f'src/{mod}/graphics-bridge-extension.json'))
            self.assertEqual(manifest['bridge'], 'Native/TGGraphicsBridge.Native.dll', mod)
            self.assertEqual(manifest['bridgeVersion'], '1.2.3', mod)
            identity = text(f'src/{mod}.Autoload/Identity.cs')
            self.assertIn(f'internal const string Folder = "{mod}";', identity)
            self.assertIn(f'internal const string Title = "{title}";', identity)
            guids.add(re.search(r'PatcherGuid = "([^"]+)"', identity).group(1))
        self.assertEqual(guids, {'com.wessberg.tgframegeneration.autoload', 'com.wessberg.tgbetterupscaling.autoload'})

    def test_preloader_does_not_reference_gameplay_or_unity(self):
        tree = ET.parse(ROOT/'src/TGGraphicsBridge.Autoload/Autoload.props')
        names = {r.get('Include') for r in tree.findall('.//Reference')}
        self.assertEqual(names, {'BepInEx','Mono.Cecil','BepInEx.Core','BepInEx.Preloader.Core'})
        self.assertFalse(tree.findall('.//ProjectReference'))
        for ref in tree.findall('.//Reference'):
            self.assertEqual(ref.find('Private').text, 'false')
        for mod in ['TGFrameGeneration', 'TGBetterUpscaling']:
            wrapper = ET.parse(ROOT/f'src/{mod}.Autoload/{mod}.Autoload.csproj')
            self.assertFalse(wrapper.findall('.//Reference') or wrapper.findall('.//ProjectReference'), mod)

    def test_two_runtime_lifecycles(self):
        source=text('src/TGGraphicsBridge.Autoload/Patcher.cs')
        for token in ['BEPINEX_MONO','BEPINEX_IL2CPP','public static void Initialize()',
                      'IEnumerable<string> TargetDLLs','void Patch(AssemblyDefinition assembly) { }',
                      'public override void Initialize()', '[PatcherPluginInfo(',
                      'public static void Finish() { Bootstrap.Finish(); }', 'public override void Finalizer() { Bootstrap.Finish(); }']:
            self.assertIn(token,source)

    def test_synchronous_same_abi(self):
        source=text('src/TGGraphicsBridge.Autoload/Bootstrap.cs')
        self.assertIn('ExpectedAbi = 0x00010100',source)
        self.assertIn('Bind<NativeBootstrap>("TGGraphicsBridge_Bootstrap")(IntPtr.Zero)',source)
        self.assertNotIn('new Thread(',source)
        self.assertNotIn('Task.Run(',source)
        self.assertNotIn('CreateRemoteThread(',source)
        self.assertIn('disable-autoload.txt',source)
        self.assertIn("NOT that the game's swapchain was seen",source)
        # Nothing to do without a mod built on the bridge: no native DLL is loaded.
        self.assertLess(source.index('if (discovered.Count == 0)'), source.index('LoadLibraryExW(nativePath'))
        # One bridge per process: the copy bundled with the newest bridge starts it, the others wait,
        # and an older copy starts its own in Finish only if no newer one did.
        run = source.split('internal static void Run(string runtime)', 1)[1].split('internal static void Finish()', 1)[0]
        self.assertLess(run.index('if (StartedBy != null)'), run.index('Discover('))
        self.assertLess(run.index('if (newest.BridgeVersion > own.BridgeVersion)'), run.index('Start(own);'))
        self.assertIn('deferred = true;', run)
        finish = source.split('internal static void Finish()', 1)[1].split('private static Extension Own()', 1)[0]
        self.assertIn('if (!deferred || StartedBy != null) return;', finish)
        start = source.split('private static void Start(Extension own)', 1)[1]
        self.assertLess(start.index('LoadLibraryExW(nativePath'), start.index('StartedBy = '))
        self.assertLess(start.index('StartedBy = '), start.index('Bind<NativeBootstrap>'))
        self.assertIn('AppDomain.CurrentDomain.SetData(StartedKey, value)', source)
        # Every file is read before this copy claims the process; an unreadable extension is left out.
        for token in ['RequireAmd64Pe(nativePath);', 'string bridgeHash = Sha256(nativePath);', 'e.Hash = Sha256(e.Native);', 'usable.Add(e);']:
            self.assertLess(start.index(token), start.index('LoadLibraryExW(nativePath'), token)
        self.assertIn('if (usable.Count == 0)', start)
        self.assertIn('foreach (var e in usable)', start)
        self.assertNotIn('foreach (var e in discovered)', start.split('StartedBy = ', 1)[1])
        # Frame Generation 1.0.x (its own preloader, no 1.1 manifest) runs a bridge of its own: none starts.
        legacy = run.split('"TGFG.Autoload.dll"', 1)[1].split('Discover(', 1)[0]
        self.assertIn('if (!File.Exists(Path.Combine(Paths.BepInExRootPath, "TGFrameGeneration", ManifestName)))', legacy)
        self.assertIn('return;', legacy.split('ManifestName)))', 1)[1].split('}', 1)[0])
        self.assertLess(run.index('Identity.Folder, "disable-autoload.txt"'), run.index('Discover('))
        # A folder a mod left behind when removed by hand is not an installed mod.
        discover = source.split('private static List<Extension> Discover(string bepinex)', 1)[1]
        self.assertIn('Path.Combine(Paths.PatcherPluginPath, mod + ".Autoload.dll")', discover)
        self.assertIn('Path.Combine(Paths.PluginPath, mod, mod + ".dll")', discover)
        # Copies of one bridge version that differ are reported, and the running copy is named.
        self.assertLess(run.index('WarnAboutDifferentBuilds(own);'), run.index('Start(own);'))

    def test_build_deploy_from_same_repo(self):
        source=text('Build.ps1')
        for project in ['src\\TGFrameGeneration\\TGFrameGeneration.csproj', 'src\\TGBetterUpscaling\\TGBetterUpscaling.csproj']:
            self.assertIn(project, source)
        for mod in ['TGFrameGeneration', 'TGBetterUpscaling']:
            self.assertIn(f'src\\{mod}.Autoload\\bin\\{{0}}\\{{1}}\\{mod}.Autoload.dll', source)
        self.assertIn('"-p:TargetFrameworks=$framework"',source)
        self.assertIn("'-p:DeployToGame=false'",source)
        self.assertIn("'Package.ps1'",source)
        self.assertNotIn('$ProjectRoot',source)
        self.assertIn('TG.GameDir.txt',source)
        self.assertIn('managed_sha256',source)
        self.assertIn('TG.GameDir.txt', text('src/Game.References.props'))

    def test_no_launcher_in_default_packaging(self):
        for path in ['Build.ps1','Package.ps1','tools/Package.Common.ps1','Test-Setup.ps1']:
            source=text(path)
            self.assertNotIn('TGFG.Launcher.exe',source,path)
            self.assertNotIn('Start-Process',source,path)
            self.assertNotIn('CreateProcess',source,path)
            self.assertNotIn('& cmake',source,path)
            self.assertNotIn('launcher_sha256',source,path)
        source=text('Package.ps1')
        self.assertIn("'artifacts\\Native'",source)
        self.assertIn("$nativeManifest.version -ne $version",source)
        # Forward-slash zip entries (not Compress-Archive) and the Nexus download copy.
        # The archive root carries the package manifest and the Vortex mod type, nothing else.
        self.assertIn("New-TgZip $stage @('BepInEx',$manifestName,(Get-TgVortexOverrideName)) ($stage + '.zip')",source)
        self.assertIn('Write-TgVortexOverrides $stage',source)
        self.assertNotIn('PLAYER-INSTALL',source)
        self.assertNotIn('Compress-Archive',source)
        self.assertIn("'artifacts/Nexus/' + (Get-TgNexusName $name $target)",source)

    def test_direct_msbuild_deploy_uses_shared_flow(self):
        props=ET.parse(ROOT/'src/Game.References.props')
        target=props.find("Target[@Name='CopyToGame']")
        self.assertIsNotNone(target)
        self.assertEqual(target.get('DependsOnTargets'),'WriteTGContext')
        self.assertIn('-StageOnly -Deploy',target.find('Exec').get('Command'))
        self.assertIsNone(target.find('Copy'))

    def test_manifest_whitelist_and_backups(self):
        source=text('tools/Package.Common.ps1')
        for token in ['Get-TgOwnedPath','Get-TgAllowedPaths','Test-TgPayload',
                      'Install-manifest.json','Get-FileHash',
                      'Assert-TgSignature','Backups/', 'Could not roll back',
                      'Duplicate payload entry:']:
            self.assertIn(token,source)
        self.assertNotIn('Remove-Item -Recurse',source)
        self.assertNotIn("Relative='dxgi.dll'",source)

    def test_diagnostics_without_late_loading(self):
        for path in ['src/TGFrameGeneration/NativeBridge.cs', 'src/TGBetterUpscaling/NativeUpscaler.cs', 'src/TGGraphicsBridge/GraphicsBridge.cs']:
            source=text(path)
            self.assertNotIn('Use TGFG.Launcher',source)
            self.assertNotIn('LoadLibrary',source)
            self.assertIn('Autoload.log',source)
        bridge=text('src/TGGraphicsBridge/GraphicsBridge.cs')
        self.assertIn('stream.Length - 32768',bridge)
        self.assertIn('may include earlier sessions',bridge)
        for path in ['src/TGFrameGeneration/Diagnostics.cs', 'src/TGBetterUpscaling/Diagnostics.cs']:
            self.assertIn('GraphicsBridge.AppendDiagnostics(sb);',text(path))

    def test_solution_has_all_projects(self):
        solution=text('TGFrameGeneration.sln')
        paths=re.findall(r'"(src\\[^\"]+\.csproj)"',solution)
        for path in paths:
            self.assertTrue((ROOT/path.replace('\\','/')).is_file(), path)
        self.assertEqual(sorted(Path(p.replace('\\','/')).name for p in paths),
                         ['TGBetterUpscaling.Autoload.csproj','TGBetterUpscaling.csproj','TGFrameGeneration.Autoload.csproj','TGFrameGeneration.csproj','TGGraphicsBridge.csproj','TGNeuralRendering.Autoload.csproj','TGNeuralRendering.csproj'])

    def test_native_version_kept_together(self):
        self.assertIn('VERSION 1.2.3',text('CMakeLists.txt'))
        self.assertIn("$script:TgVersion = '1.2.3'",text('tools/Package.Common.ps1'))
        self.assertIn("$script:TgAbi = '0x00010100'",text('tools/Package.Common.ps1'))
        self.assertIn("version = (Get-TgVersion)",text('tools/Write-NativeManifest.ps1'))
        self.assertIn('0x00010100',text('src/TGFrameGeneration/NativeAbi.cs'))

if __name__ == '__main__':
    unittest.main(verbosity=2)
