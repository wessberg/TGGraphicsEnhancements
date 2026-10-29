"""Print the lines of a run's output that matter (tolerant of PowerShell's encodings).

    python filters-show.py tag[,tag...] [regex] [width]
"""
import re
import sys
from pathlib import Path

sys.stdout.reconfigure(errors="replace")
out = Path.home() / "AppData/Local/Temp/tgb/filters-out"
pat = re.compile(sys.argv[2] if len(sys.argv) > 2 else r"NRD mean|NRD study|mirror filters|FAIL|EXIT|passes made|totals|last error|RUN ")
width = int(sys.argv[3]) if len(sys.argv) > 3 else 460
for tag in sys.argv[1].split(","):
    f = out / (tag + ".txt")
    if not f.exists():
        print(f"({tag}: no output yet)")
        continue
    data = f.read_bytes()
    if data[:2] in (b"\xff\xfe", b"\xfe\xff"):
        text = data.decode("utf-16", "replace")
    else:
        text = data.replace(b"\x00", b"").decode("utf-8-sig", "replace")
    for line in text.splitlines():
        if pat.search(line):
            line = line.strip().replace("NRD mean, ", "").replace("without reflections, ", "")
            line = re.sub(r" \(the other mean[^)]*\)", "", line)
            print(line[:width])
