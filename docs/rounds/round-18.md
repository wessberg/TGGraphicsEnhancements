# Round 18 - 2026-10-09 - Defender blocks released Frame Generation 1.2.3

The owner cannot enable FG after unpacking the archive again. Screenshot at 17:19 shows
the misleading "Missing files" alert. They have just released FG/BU/NR 1.2.3 on Nexus and
need to know exactly what changed, especially whether the unsigned NR community support
caused the detection. They restored/unflagged the game copy themselves during this session.

## Finding

The October 9 release and the saved October 7 FG build differ in exactly SIX bytes, all
inside two linker timestamps. Every other byte of the 608,256-byte DLL is identical:
executable code, imports, embedded shaders, data, resources and relocations. No new
executable code from community-runtime support exists in this particular binary difference.

| File | SHA256 | Defender custom scan |
|---|---|---|
| FG previous public Mono 1.2.0 | `18ae557686ddeecdf8cc4512413d947064b51c6fc21859bb47fd815ec8406256` | No threats |
| FG October 7 21:23:43 local | `55843c485e3796ace2e7819fef78b7685bbcaaaeae4d7695ace106e021b996cb` | No threats |
| FG October 9 13:34:01 local, released 1.2.3 | `83947a2ac8f1615e70aa668a619855989090a46e81e820cc421fdda0143cb73a` | `Trojan:Win32/Wacatac.H!ml` |
| Installed BU, bridge, NR release DLLs | R17 release identities | No threats, each scanned separately |

The COFF timestamp is at file offset 272; the debug-directory timestamp is at 527924.
Their values change from 1791401023 (2026-10-07 19:23:43 UTC) to 1791545641
(2026-10-09 11:34:01 UTC). Changed offsets: 272, 273, 274, 527924, 527925, 527926.
The `.text` section is 439,296 bytes and is identical. Full-file comparison also excludes
differences outside those timestamps. No timestamps or binaries were patched for this test.

Both normal and Mono FG zips in `artifacts/Nexus` AND `artifacts/Nexus-1.2.3-backup` contain
the flagged 83947a2a DLL. The restored game DLL hashes to that same value. This affects the
released payload, not just a local PT build. Other users can encounter it; prevalence is unknown.

This establishes the exact binary change and differing antivirus results. It does NOT expose
Microsoft's internal classification rule or prove a particular cloud/reputation mechanism.
The strong inference is a false positive on the rebuilt file's identity/metadata. Do not claim
that changing timestamps makes software safe, or that one clean local scan guarantees other PCs.

## Evidence and chronology

- Defender event 1116 at 15:44:54, quarantine 1117 at 15:45:08; game copy.
- Again at 17:18:19 (threat history 17:18:18), quarantine at 17:18:29 after unpacking again.
- BepInEx PID 22088: native file read fails with Windows error 225, before the bridge DLL
  loads. TGFG managed plugin loads; native FG does not, hence the disabled row.
- At 17:24:49, simply hashing `build/nr/TGFG.Native.dll` with Python triggers the same
  detection, quarantine at 17:24:59. This build output is now MISSING because Defender
  removed it. No game or NR/community helper ran in that reproduction.
- All these events report definitions 1.459.636.0 and engine 1.1.26080.3. Event 2000 records
  the definitions update at 13:18:25, BEFORE the successful R17 gameplay smoke at 13:54.
  The same 83947a2a hash appears in that successful smoke's autoload log. Do not assert
  that an intervening definitions update caused the first block; no such update was found.
- Custom scans use the current platform 4.18.26080.4 `MpCmdRun -Scan -ScanType 3 -File ...
  -DisableRemediation`. This option ignores file exclusions and reports without remediation.
  The released DLL remains a positive control after the owner's local restore/allow action.
  No Defender setting, threat allow rule or signature was changed by the agent.

Reproducible scan script and raw evidence: `.testruns/fg-defender/Compare-Defender.ps1`,
`scan-results.json`, `defender-events.json`, `binary-diff.json`, `timestamp-diff.json`,
`release-oct9-identity.json`, `pre-community-oct7-identity.json`.
Old comparison file: `build/nr/install/TGFrameGeneration/TGFG.Native.dll`.
Released scan file: `build/nr/mono-r17/verified/TGFrameGeneration-20261009-114849-173/BepInEx/TGFrameGeneration/Native/TGFG.Native.dll`.

## Source check of the community theory

Community loading is in `native/neural/src/community_adapter.cpp`, compiled only into
`TGNeuralRendering.nvngx.dll`. It is not linked into FG or the shared bridge. FG's shared
`neural_runtime_folder` still requires an NVIDIA signature before adding the NR folder to
Streamline's search paths. Its earlier source changes since public 1.2.0 are shared DLSS
input observation, shared NGX locking and the signed NR search folder, plus version text.
Those are present in the October 7 binary with identical code. The helper's execution
cannot explain the standalone file-read reproduction. Do not remove community support
based on this detection alone.

## Outcome and next work

Diagnosis complete; public remediation remains open. No production source, build, package,
game files or antivirus settings changed by the agent. No game launched, stopped or retested.
107 source checks pass. The owner restoring the file is not a public fix or new in-game FG
confirmation. The misleading antivirus-to-"Missing files" alert is a separate open defect.

Microsoft submission text is prepared at `.testruns/fg-defender/microsoft-submission.txt`;
NOT submitted. The developer submission page is https://www.microsoft.com/en-us/wdsi/filesubmission.
No Nexus notice, file replacement, release withdrawal, upload, commit or push performed.

## ▶ Starter prompt (R19)

Read BACKLOG, PROJECT-CONTEXT and this report. FG 1.2.3's released DLL is blocked by Defender;
both game versions ship the same hash. The old October 7 DLL scans clean with exactly the
same executable code: only six timestamp bytes differ. Preserve both artifacts and scan logs.
Do not speculate that the unsigned NR helper introduced malicious-looking code into FG.
Next: Microsoft false-positive review of the exact released file and release mitigation with
the owner. A clean scan of an old/rebuilt file is not a guarantee that its future detection
stays clean. The agent has not altered antivirus policy or submitted anything externally.
The build/nr/TGFG.Native.dll output was quarantined during the diagnosis; account for that
before another package/build. Preserve the PT work and its R6 handoff; no GPU work here.
