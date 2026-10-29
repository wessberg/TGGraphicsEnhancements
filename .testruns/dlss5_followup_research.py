"""Read public source and release metadata. Never download or inspect binaries."""
import concurrent.futures
import json
import re
import urllib.request

BASES = {
    "unity": "https://raw.githubusercontent.com/Kuan-Mi/UnityDLSSNR/4781026526ead653e0c594fbbc66c3a210489291/",
    "susemi": "https://raw.githubusercontent.com/grim-susemi/OptiScaler-Susemi/3def828dadbd79f887996a6733ae261abe0e669f/",
    "universal": "https://raw.githubusercontent.com/MotionflowOffical/UniversalDLSS5/e47518a74ff539df2a16c71f8aa5c0b7038e8c5a/",
}
REQUESTS = [
    ("unity", "RenderingPlugin/Source/NgxRuntime.cpp"),
    ("unity", "RenderingPlugin/Source/DlssNr.cpp"),
    ("unity", "RenderingPlugin/Source/DlssNrRuntime.cpp"),
    ("unity", "RenderingPlugin/CMakeLists.txt"),
    ("susemi", "tests/nr_ngx_routing_smoke.cpp"),
    ("susemi", "OptiScaler/dlssnr/DlssNr_CompatibilityRuntime.cpp"),
    ("susemi", "OptiScaler/dlssnr/DlssNr_RuntimeImports.h"),
    ("susemi", "INSTALL-DLSSNR.md"),
    ("universal", "LICENSE"),
    ("universal", "docs/superpowers/specs/2026-09-12-nr-caller-compat-design.md"),
]
PATTERN = re.compile(r"(?i)(init|createfeature|evaluatefeature|dlssnr|dlss_nr|signature|license|patch|caller|forwarder|0xBAD|result|application|download|github|NGX_VERSION)")

def read_url(url):
    request = urllib.request.Request(url, headers={"User-Agent": "TG-DLSS5-research"})
    with urllib.request.urlopen(request, timeout=25) as response:
        return response.read().decode("utf-8", errors="replace")

def inspect(item):
    repo, path = item
    url = BASES[repo] + path
    try:
        lines = read_url(url).splitlines()
        selected = []
        for index, line in enumerate(lines):
            if PATTERN.search(line):
                selected.append(f"{index + 1}: {line[:350]}")
        return f"SOURCE {url}\n" + "\n".join(selected[:90])
    except Exception as error:
        return f"SOURCE {url}\nERROR {error}"

with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
    for result in pool.map(inspect, REQUESTS):
        print(result)

for repo in ["NVIDIA/DLSS", "NVIDIA-RTX/Streamline"]:
    try:
        release = json.loads(read_url(f"https://api.github.com/repos/{repo}/releases/latest"))
        print("RELEASE", repo, release["tag_name"], release["published_at"])
        print("ASSETS", [(asset["name"], asset["size"]) for asset in release.get("assets", [])])
    except Exception as error:
        print("RELEASE ERROR", repo, error)
