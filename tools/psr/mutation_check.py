"""The worker psr-2's mutant for TGPT_TEST_PSR_ONLY (tests/pathtracing_psr_checks.inl): the normal NRD is told of a
replaced surface is NOT mirrored in the mirror's plane. The check's panel leans back so that this shows.

    python tools/psr/mutation_check.py build     the mutant DLL into %TEMP%/tgb/psr-build/variants/M (about 12 minutes),
                                                 the source and the build tree back as they were
    then: tools/psr/run-check.ps1 -Runs M        must end with FAIL lines about the normal, and exit code 1

Only the reflections' pass in its Shader Execution Reordering build is compiled again (tools/psr/mutant.cmd): the one
this GPU runs. The source file gets its bytes AND its time back before NMake looks, or NMake would compile every pass
that depends on it. The good header is kept and put back with a new time, so the tree's DLL is the good one again.
(The first worker's mutant, a wrong managed flag bit, went with the flag.)
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import time

root = Path(__file__).resolve().parents[2]
build = Path("C:/Users/Frede/AppData/Local/Temp/tgb/psr-build")
source = root / "native/pathtracing/shaders/pt_psr_capture.hlsli"
header = build / "generated/pathtracing/pt_specular_ser.h"
good = build / "keep/pt_specular_ser.good.h"
old = b"records[slot].surface = uint4(pack_normal(reflect(normal, n)),"
new = b"records[slot].surface = uint4(pack_normal(normal),"


def run(command, log):
    with open(build / log, "wb") as out:
        code = subprocess.run(["cmd", "/c", str(root / command)], cwd=root, stdout=out, stderr=subprocess.STDOUT).returncode
    print(command, "->", code, flush=True)
    return code


if len(sys.argv) != 2 or sys.argv[1] != "build":
    sys.exit(__doc__)
original = source.read_bytes()
stat = source.stat()
assert original.count(old) == 1
good.parent.mkdir(parents=True, exist_ok=True)
shutil.copy2(header, good)
variant = build / "variants/M"
variant.mkdir(parents=True, exist_ok=True)
try:
    source.write_bytes(original.replace(old, new))
    assert run("tools/psr/mutant.cmd", "mutant-shader.log") == 0, "the mutant did not compile"
finally:
    source.write_bytes(original)
    os.utime(source, ns=(stat.st_atime_ns, stat.st_mtime_ns))
assert source.read_bytes() == original and source.stat().st_mtime_ns == stat.st_mtime_ns
try:
    assert run("tools/psr/build.cmd", "mutant-link.log") == 0, "the mutant did not link"
    shutil.copy2(build / "TGPathTracing.Native.dll", variant / "TGPathTracing.Native.dll")
finally:
    shutil.copyfile(good, header)
    now = time.time()
    os.utime(header, (now, now))
assert run("tools/psr/build.cmd", "mutant-restore-link.log") == 0, "the good pass did not link again"
print("mutant DLL:", variant / "TGPathTracing.Native.dll")
