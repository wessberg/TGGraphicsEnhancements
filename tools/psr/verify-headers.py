"""Do the shader headers in the worker's build tree hold what the copy's sources compile to NOW?

    python tools/psr/verify-headers.py pt_cache pt_meter pt_sky ...      (header names without .h; one compile at a time)

Each named pass is compiled again with the very command CMake wrote for it (CMakeFiles/TGPathTracingShaders.dir/
build.make), into %TEMP%/tgb/psr-build/keep/verify/, and the result is compared byte for byte with the header in the
tree. Why: psr-2 changed code that only five passes contain (pt_specular, pt_prepare, pt_compose, pt_nrd_prep,
pt_nrd_confidence_filter); every other pass includes the changed headers but none of the changed code, so its header as
the first worker compiled it must be what the final sources give. This shows it for the passes named, instead of
compiling all 57 again behind the coordinator's game runs (one compile at a time while the game is open: hours).
"""
from pathlib import Path
import re
import subprocess
import sys
import time

build = Path("C:/Users/Frede/AppData/Local/Temp/tgb/psr-build")
make = (build / "CMakeFiles/TGPathTracingShaders.dir/build.make").read_text(encoding="utf-8", errors="replace")
out = build / "keep/verify"
out.mkdir(parents=True, exist_ok=True)
commands = {}
for line in make.splitlines():
    match = re.search(r"-Fh (\S+/generated/pathtracing/(\w+)\.h) ", line)
    if match and "dxc.exe" in line:
        commands[match.group(2)] = (line.strip(), match.group(1))
same = True
first = Path("C:/Users/Frede/AppData/Local/Temp/tgb/BUILD-FIRST")  # the coordinator's build of the main tree (Build-PtShaders -First)
for name in sys.argv[1:]:
    command, header = commands[name]
    target = out / (name + ".h")
    # One compile at a time, and none started while the coordinator's own build goes first or another compile runs.
    while (first.exists() and time.time() - first.stat().st_mtime < 60) or \
            "dxc.exe" in subprocess.run(["tasklist", "/FI", "IMAGENAME eq dxc.exe"], capture_output=True, text=True).stdout:
        time.sleep(10)
    started = time.time()
    code = subprocess.run(command.replace(header, target.as_posix()), cwd=build, shell=True, capture_output=True, text=True)
    if code.returncode != 0:
        print(name, "did not compile:", (code.stdout + code.stderr)[-600:])
        same = False
        continue
    a, b = Path(header).read_bytes(), target.read_bytes()
    equal = a == b
    same = same and equal
    print(f"{name}: {'the same bytes' if equal else 'DIFFERENT'} ({len(b)} bytes, compiled in {time.time() - started:.0f} s; "
          f"the tree's header is of {time.strftime('%H:%M:%S', time.localtime(Path(header).stat().st_mtime))})", flush=True)
sys.exit(0 if same else 1)
