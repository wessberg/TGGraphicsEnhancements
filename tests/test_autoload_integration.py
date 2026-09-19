"""Source/build-graph checks only; not C#/PowerShell compilation or runtime tests."""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET
ROOT = Path(__file__).resolve().parents[1]

def text(name):
    return (ROOT/name).read_text(encoding='utf-8')

class AutoloadIntegration(unittest.TestCase):
    def test_build_only_reference(self):
        tree = ET.parse(ROOT/'src/TGFrameGeneration/TGFrameGeneration.csproj')
        refs = tree.findall('.//ProjectReference')
        self.assertEqual(len(refs), 1)
        self.assertEqual(refs[0].get('ReferenceOutputAssembly'), 'false')
        self.assertEqual(refs[0].get('Private'), 'false')
        child = (ROOT/'src/TGFrameGeneration'/refs[0].get('Include').replace('\\','/')).resolve()
        self.assertEqual(child, ROOT/'src/TGFG.Autoload/TGFG.Autoload.csproj')
        self.assertTrue(child.is_file())

    def test_matching_versions_and_paths(self):
        trees = [ET.parse(ROOT/p) for p in (
            'src/TGFrameGeneration/TGFrameGeneration.csproj','src/TGFG.Autoload/TGFG.Autoload.csproj')]
        for t in trees:
            self.assertEqual(t.find('.//Version').text, '0.5.0')
            self.assertEqual(set(t.find('.//TargetFrameworks').text.split(';')), {'net472','net6.0'})
            self.assertEqual(t.find('Import').get('Project'), '..\\..\\Build.Local.props')
        for prop in ['GameDirMono', 'GameDirIl2Cpp']:
            self.assertEqual(trees[0].find('.//'+prop).text, trees[1].find('.//'+prop).text)
        self.assertIn('Version = "0.5.0"', text('src/TGFrameGeneration/Plugin.cs'))
        self.assertIn('TGFG.Autoload 0.5.0', text('src/TGFG.Autoload/Bootstrap.cs'))

    def test_preloader_does_not_reference_gameplay_or_unity(self):
        tree = ET.parse(ROOT/'src/TGFG.Autoload/TGFG.Autoload.csproj')
        names = {r.get('Include') for r in tree.findall('.//Reference')}
        self.assertEqual(names, {'BepInEx','Mono.Cecil','BepInEx.Core','BepInEx.Preloader.Core'})
        self.assertFalse(tree.findall('.//ProjectReference'))
        for ref in tree.findall('.//Reference'):
            self.assertEqual(ref.find('Private').text, 'false')

    def test_two_runtime_lifecycles(self):
        source=text('src/TGFG.Autoload/Patcher.cs')
        for token in ['BEPINEX_MONO','BEPINEX_IL2CPP','public static void Initialize()',
                      'IEnumerable<string> TargetDLLs','void Patch(AssemblyDefinition assembly) { }',
                      'public override void Initialize()', '[PatcherPluginInfo(']:
            self.assertIn(token,source)

    def test_synchronous_same_abi(self):
        source=text('src/TGFG.Autoload/Bootstrap.cs')
        self.assertIn('ExpectedAbi = 0x00050000',source)
        self.assertIn('Bind<NativeBootstrap>("TGFG_Bootstrap")(IntPtr.Zero)',source)
        self.assertNotIn('new Thread(',source)
        self.assertNotIn('Task.Run(',source)
        self.assertNotIn('CreateRemoteThread(',source)
        self.assertIn('disable-autoload.txt',source)
        self.assertIn('NOT that a game swapchain was intercepted or FG works',source)

    def test_build_deploy_from_same_repo(self):
        source=text('Build.ps1')
        self.assertIn('src\\TGFrameGeneration\\TGFrameGeneration.csproj',source)
        self.assertIn('src\\TGFG.Autoload\\bin\\$Configuration\\$framework\\TGFG.Autoload.dll',source)
        self.assertIn('"-p:TargetFrameworks=$framework"',source)
        self.assertIn("'-p:DeployToGame=false'",source)
        self.assertIn("'Package.ps1'",source)
        self.assertNotIn('$ProjectRoot',source)
        self.assertIn('TGFG.GameDir.txt',source)
        self.assertIn('autoload_sha256',source)

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
        self.assertIn("$nativeManifest.version -ne '0.5.0'",source)
        self.assertIn('Compress-Archive',source)

    def test_direct_msbuild_deploy_uses_shared_flow(self):
        tree=ET.parse(ROOT/'src/TGFrameGeneration/TGFrameGeneration.csproj')
        target=tree.find("Target[@Name='CopyToGame']")
        self.assertIsNotNone(target)
        self.assertEqual(target.get('DependsOnTargets'),'WriteTGFGContext')
        self.assertIn('-StageOnly -Deploy',target.find('Exec').get('Command'))
        self.assertIsNone(target.find('Copy'))

    def test_manifest_whitelist_and_backups(self):
        source=text('tools/Package.Common.ps1')
        for token in ['Get-TgfgOwnedPath','Get-TgfgAllowedPaths','Test-TgfgPayload',
                      'Install-manifest.json','native_abi','Get-FileHash',
                      'Assert-TgfgNvidiaSignature','Backups/', 'Could not roll back',
                      'Duplicate payload entry:']:
            self.assertIn(token,source)
        self.assertNotIn('Remove-Item -Recurse',source)
        self.assertNotIn("Relative='dxgi.dll'",source)

    def test_diagnostics_without_late_loading(self):
        bridge=text('src/TGFrameGeneration/NativeBridge.cs')
        diagnostic=text('src/TGFrameGeneration/Diagnostics.cs')
        self.assertNotIn('Use TGFG.Launcher',bridge)
        self.assertNotIn('LoadLibrary',bridge)
        self.assertIn('Autoload.log',bridge)
        self.assertIn('AppendAutoloadDiagnostic(sb)',diagnostic)
        self.assertIn('stream.Length - 32768',diagnostic)
        self.assertIn('may include earlier sessions',diagnostic)

    def test_solution_has_both_projects(self):
        solution=text('TGFrameGeneration.sln')
        for path in re.findall(r'"(src\\[^\"]+\.csproj)"',solution):
            self.assertTrue((ROOT/path.replace('\\','/')).is_file())
        self.assertIn('TGFG.Autoload.csproj',solution)
        self.assertIn('TGFrameGeneration.csproj',solution)

    def test_native_version_kept_separate(self):
        self.assertIn('VERSION 0.5.0',text('CMakeLists.txt'))
        self.assertIn("version='0.5.0'",text('Build-Native.ps1'))
        self.assertIn("version='0.5.0'",text('Package.ps1'))
        self.assertIn("native_version='0.5.0'",text('Package.ps1'))
        self.assertIn('0x00050000',text('src/TGFrameGeneration/NativeAbi.cs'))

if __name__ == '__main__':
    unittest.main(verbosity=2)
