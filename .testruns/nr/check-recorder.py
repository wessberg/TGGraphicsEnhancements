import importlib.util
from pathlib import Path
spec=importlib.util.spec_from_file_location('checks', Path('tests/source_checks.py'))
module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
test=module.SourceConsistency('test_upscaling_pass_is_recorded_with_its_targets_data')
test.test_upscaling_pass_is_recorded_with_its_targets_data()
read=test.text
test.text=lambda path: read(path).replace('IL2CPP.il2cpp_object_unbox(IL2CPP.Il2CppObjectBaseToPtrNotNull(targets))', 'IL2CPP.Il2CppObjectBaseToPtrNotNull(targets)') if path=='src/Shared/CustomPassRecorder.cs' else read(path)
try:
    test.test_upscaling_pass_is_recorded_with_its_targets_data()
except AssertionError:
    print('PASS: recorder baseline passes; boxed-target mutation fails')
else:
    raise AssertionError('Recorder mutation was not detected')
