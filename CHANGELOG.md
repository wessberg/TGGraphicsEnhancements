# Unreleased

From 1.1.0 on, the version numbers are the ones the mods have on Nexus Mods, and TG Frame Generation,
TG Better Upscaling and the bridge they share all carry the same one. Earlier entries use the repository's
own numbers: v0.5.0 is the release published on Nexus as Frame Generation 1.0.2.

Troubleshooting (1.2.0)

- **Create Troubleshooting Report**'s tooltip and start message say what the test tries with the mods
  installed, and for about how long: each upscaler with TG Better Upscaling alone, each frame
  generation and low latency setting with TG Frame Generation alone, both with both. The report names
  the mods that took part.
- **Create Troubleshooting Report**, a button nested under Performance Overlay at the bottom of the
  Video settings, there with the overlay on or off (and a Video entry in FoA Mod Manager). Back in the game it runs a test of about two minutes:
  the player's own settings first, then each upscaler the game's Upscaling row offers with frame
  generation off, then frame generation off with each low latency technology, then each frame
  generation technology with each low latency technology it can run with (Off where that is allowed)
  at 2x, and once more at the highest multiplier where there is one. Each step is applied, left to
  settle and measured for a few seconds: the rendered frame rate and its 1% low, CPU, GPU and video
  memory use, frames on screen, the presenting and latency technology, and where the presentation
  bridge spends its time per frame. Then the player's settings come back and the report is saved as
  one text file in the BepInEx folder, beside LogOutput.log (`TG Troubleshooting Report <date>.txt`,
  about 25 KB), to paste into a bug report on Nexus. It has a summary (system, mods, problems found,
  the test results, what the native side knows), then this session's part of each log, filtered to
  what explains a problem: from BepInEx's log the mods' lines, warnings and errors with their stack
  traces and plugin loading; from the preloader's and native logs this session's lines (found by the
  process ID), with frame generation's 5 s pipeline lines thinned to the first and last few and one
  per test step; errors and warnings from Streamline's and the game's logs; the mods' config values;
  and where each of the mods' files is, with its size, date, version and whether it is a Vortex link.
  Repeated lines are counted, and the Windows user folder is replaced by `%USERPROFILE%`. A test step never writes the
  player's settings: frame generation's steps override what it requests, upscaling's steps select
  the row in the game but leave the mod's config alone, and when the game pauses the player's
  settings come back until play resumes, so the settings screen never shows a test value. If the game
  closes during a test, the next start puts upscaling back from the config. Selecting the button
  again stops the test and saves what was measured; before the test starts, it saves the logs alone.
- The mods' problems are shown on the Video rows they concern, not as rows of their own: the Frame
  Generation row for TG Frame Generation and whatever interferes with frame generation, the game's
  Dynamic Resolution row (the upscaler, which TG Better Upscaling extends) for TG Better Upscaling
  and whatever interferes with upscaling. Such a row gets an amber alert icon after its name and the
  most serious problem as its tooltip, as "Source: Label. What to do" in a sentence or two ("TG Better
  Upscaling: Wrong folder. Its files are in ..."). The full explanation goes in the report. The top of
  the performance overlay lists all of them the same short way, wrapped to under half the screen.
  Each mod tells the bridge when it has started; one that BepInEx loaded but that never started (an
  error while starting, before it could log anything, which this game's missing Unity log hides) gets
  a "Didn't start" alert on its row. When TG Frame Generation has a problem and no row of its own, a
  stand-in Frame Generation row reads Off with the icon and the reason. Tooltips name the game's two versions as the Nexus files do (the
  normal version, and the Mono one whose files end in "mono"), never by runtime names. They cover:
  - where a mod is installed wrong: its archive's BepInEx folder unpacked into BepInEx, into
    BepInEx/plugins (what a mod manager does with a plugin-type mod; with Vortex the fix is the
    Engine Injector type), into a folder of its own, or its contents straight into the game folder;
    a missing preloader or other files; the build for the other game branch; a plugin BepInEx found
    and didn't load; TG Frame Generation 1.0 still installed; a `disable-autoload.txt`;
  - other software that conflicts or limits: OptiScaler (running, or files left over), DLSS Enabler,
    the DLSSG-to-FSR3 mod, FSR 3 mods, Special K, RivaTuner Statistics Server with a frame rate limit
    (read from its profiles), Lossless Scaling running, and from the NVIDIA driver's profile for the
    game: Max Frame Rate, forced VSync, Smooth Motion (ID from NVIDIA Profile Inspector, not yet
    checked against a driver with it on) and a DLSS Frame Generation override that forces it off.
- When frame generation turns on and the framerate on screen doesn't rise by 15% over the rate just
  before, a message says so during play and a row in the Video settings explains it with what this
  session shows: the game's Framerate Limit, VSync (at the display's refresh rate or every second
  refresh), a limit outside the game, video memory nearly full, rendering that fell much more than
  frame generation costs (with Anti-Lag 2 on, hybrid graphics, or AMD Fluid Motion Frames as things
  to try), or nothing known, with the troubleshooting report as the next step. Dynamic mode that
  reaches its target counts as a gain.
- DLSS Frame Generation with VSync on, while the display is connected to another GPU than the one
  the game renders on (a laptop's panel on its integrated GPU), shows a "High latency" alert: NVIDIA's
  DLSS-G guide says high latency is expected with VSync on such a system. The fix it gives is VSync
  off, or the laptop's setting that makes the dedicated GPU drive the display.
- Frame generation's row reads Off, greyed out, with the alert icon when it can't run, and its tooltip gives the first thing
  that went wrong with what to do: the install problem, OptiScaler, a preloader that didn't run,
  another Streamline, and for a Direct3D 12 device Windows refused, why. 0x887E0003 names the Agility
  SDK versions, the Windows build, and whether the bridge corrected the game's request or something
  loaded Direct3D 12 before it could, but only when the bridge saw that mismatch. Otherwise it says
  the game's D3D12Core.dll is missing or doesn't match, and to verify the game's files. The row's
  own value stays one word, and the tooltip is the short form. When DLSS Frame Generation isn't offered on an NVIDIA GPU, the
  tooltip says why from Streamline's own answer: Hardware-accelerated GPU scheduling off, an old
  driver or Windows, or a GPU before the RTX 40 series.
- The native logs have more to go on: the Windows build, the game's adapter, driver version and video
  memory, which GPU drives the display (a laptop's hybrid graphics shows as the display on the other
  GPU), the NVIDIA driver settings above, and in frame generation's 5 s pipeline line, where the
  presenting thread's time goes per frame (waiting for the GPU, the SDK's Present, handing the frame
  back to D3D11) and the frame interval with the frames over 50 ms. New optional exports carry these
  to the plugins: `TGGraphicsBridge_ReadDiagnostics`, `TGGraphicsBridge_LogNote`,
  `TGFG_ReadDiagnostics`, `TGFG_ReadTimings`, `TGFG_LogNote`. The ABIs are unchanged.
- The bridge is 1.2.0, so a copy bundled with a 1.1.0 mod stays idle beside it.

Frame generation and compatibility fixes (1.2.0), from a review of hardware the mods haven't been
tested on (only an RTX 5080 on Windows 11 has been)

- The game's next frame no longer waits for frame generation to finish the current one. After
  every Present, D3D11 used to wait on the GPU until D3D12 had finished the whole present, frame
  generation included, before any of the next frame's work could run. With DLSS-G (which blocks the
  presenting queue until its work is done) and FSR (whose generation runs on that queue) the game's
  rendering and frame generation took turns instead of overlapping. That fits the reports of frame
  generation lowering the framerate on slower GPUs. D3D11 now waits only for what it is about to overwrite: a
  back buffer until D3D12 copied it, and an input texture until the present that used it three
  frames earlier retired. Usually neither has to wait; the pipeline line counts the times one did.
  Each direction has its own shared fence now (D3D11 signals the producer fence, only the D3D12 queue
  the consumer fence): a fence Signal sets its value, and with D3D11 no longer waiting for every
  retirement, a single fence signalled by both could have moved backwards.
- Reflex, XeLL and Anti-Lag 2 now wait in Unity's PlayerLoop right after
  `TimeUpdate.WaitForLastPresentationAndUpdateTime`: after Unity's own wait for the previous frame's
  presentation (Intel's XeLL guide wants other waits before XeLL's) and before any input is read,
  Unity's or Rewired's. Their SDKs ask for the wait just before input is sampled; it ran in a
  MonoBehaviour's Update, after the input it was meant to precede, which made the input each frame
  used older by the wait. If another mod replaces
  the PlayerLoop, frames begin in Update as before. The native frame ID is now a counter that rises
  once per frame instead of `Time.frameCount`, which can still hold the previous frame's number there.
- VSync is turned off for DLSS Frame Generation when the screen is connected to another GPU than the
  one the game renders on: a laptop's screen on its integrated GPU (the default hybrid mode of most
  gaming laptops), or a monitor in the motherboard's port. NVIDIA's DLSS-G guide expects high input
  delay with VSync and frame generation where Windows composes the frames instead of flipping them
  straight to the screen, and says it is better not to enable VSync there. Frames copied to another
  GPU's screen are usually composed, but not always: hybrid laptops can flip them straight to the screen
  too (the two-copy hybrid path, Windows 11's cross-adapter scan-out), where VSync off would tear. So
  while VSync is held off, the bridge presents without the tearing flag (`TGFG_SetNoTearing`, a new
  optional export): the newest frame shows at the next refresh, with no VSync wait and no tear either
  way. The mod turns the game's own
  VSync toggle off once, as a player would, and sets Target FPS to Unlimited: the game shares that row
  between VSync rates and frame limits and would otherwise move it to the nearest limit, a cap the
  player never chose. It greys the toggle out with a tooltip saying why and shows a short message; a
  limit the player picks afterwards stays. Choosing FSR or XeSS, or a screen on the rendering GPU, offers the toggle again; VSync
  stays off until the player turns it on. VSync the NVIDIA driver forces on can't be changed from the
  game: the "High latency" alert then says where to change it in the NVIDIA App. An advanced setting
  turns this off. Which GPU the screen is connected to is checked again every 5 s, so moving the
  window or a laptop switching its screen between GPUs is noticed, and Streamline's measured copy time
  between the GPUs is in the pipeline line (`crossAdapterCopy`).
- Alerts for memory while frame generation runs: "VRAM full" when the game stays over the video memory
  Windows gives it (with how much frame generation added), and "RAM full" when less than 1 GB of system
  memory is free or 95% is in use. Both make the game stutter as memory moves out and back. System
  memory also counts as a cause when frame generation gives no framerate gain, and the report lists
  free memory.
- DLSS-G's UI recomposition is off. It was on whenever a HUD-less image was supplied, but NVIDIA's
  guide allows it only when a UI buffer is tagged as well, and the bridge tags none. DLSS-G still
  gets the verified HUD-less image, and it no longer spends recomposition's memory and GPU time.
- DLSS-G below its minimum output size (`minWidthOrHeight`) is off until the resolution changes,
  as NVIDIA's guide requires, with the reason "resolution too low". The runtime's
  resolution-too-low status used to end frame generation for the session.
- NVIDIA Reflex is offered in Low Latency only when Streamline reports it available
  (`lowLatencyAvailable`: an NVIDIA GPU from Maxwell on with driver 456 or later). NVIDIA's guide
  requires this for the Reflex choice; DLSS-G still runs Reflex whatever the choice. A query that
  fails is asked again before the plugin reads the capabilities, and without an answer Reflex isn't
  offered.
- XeSS Frame Generation generates only from frames XeLL slept for, and only while XeLL's low latency
  mode is on. Intel requires XeLL with every XeSS-FG frame; the first frame after switching to XeSS
  began before XeLL existed. A sleep mode XeLL refused is not asked for again every frame (each
  attempt costs a GPU drain).
- The Base FPS limit with XeSS Frame Generation capped the frames on screen, not the rendered ones:
  XeLL's limit counts every Present. XeLL now gets it multiplied by the presents per rendered frame.
- XeSS Frame Generation and XeLL are loaded only where Intel supports them: Windows 10 21H1 (build
  19043) or later and the Visual C++ runtime 14.40.33810 or later, checked on the copy they would
  bind to (one already loaded, one next to them or the game, or System32's). Elsewhere the Frame
  Generation row's tooltip says which one is missing.
- The three native DLLs link the C and C++ runtimes statically. The game ships no `msvcp140.dll`, so
  they used whatever System32 had. Without the Visual C++ Redistributable they didn't load at all,
  and with one older than 14.40 the first `std::mutex` lock crashed the game: MSVC 14.40 and later
  construct `std::mutex` in a way older runtimes can't lock. They no longer call
  `DisableThreadLibraryCalls`, which Microsoft forbids with the static runtime, and their extension
  factories catch everything before it reaches the bridge's runtime. NVIDIA's, AMD's and Intel's DLLs
  still need the redistributable, so the bridge records the copy they would use (`windows.vcruntime`,
  in the report), a "Visual C++ runtime: Missing" alert appears when there is none in Windows or the game
  folder, and a Streamline that failed to load with Windows error 126 or 127 says which it was.
- The troubleshooting report is written whole and then renamed, so a full disk or the game closing
  leaves no half report. A log longer than the 8 MB the report reads says the session may have started
  before them, instead of that its native part didn't start.

Error handling (1.2.0), from a final review of every failure path

- No C++ exception leaves a native entry point: Unity's render-thread callbacks, the exports the
  plugins call every frame, the performance sampler's thread (which now starts over after an error)
  and the bridge's exports catch everything. The three native logs never throw, since they are called
  from catch handlers, some in functions that must not throw.
- A removed device signals its fences to their maximum, which also woke the CPU fence waits: they
  now check again after waking, so device removal is reported as such instead of passing as a
  finished frame. A bridge whose GPU work was never confirmed finished keeps its GPU objects for the
  rest of the session instead of freeing them under running work; the upscaler does the same on a new
  game device.
- A settings file that can't be written (read-only, locked by an antivirus scan, a full disk) no longer
  stops a mod: settings bind with BepInEx's saving off and every save goes through one place, which
  keeps the values for the session and shows a "Can't save" alert on the mod's row. Each plugin's
  start is guarded and logs why it failed, and the preloader starts the bridge even if it can't create
  its log folder.
- The troubleshooting test run starts only once its recovery file is written, keeps that file until
  TG Better Upscaling has put the upscaler back, reports a step that couldn't be applied as such
  instead of measuring it, says which mod couldn't put your settings back, and puts them back if the
  report's own code fails mid-run.
- After FSR or XeSS stops with an error, STP really takes over at the chosen render scale. The game's
  upscaling controller knows no FSR or XeSS, so handing it that choice turned upscaling off and
  rendered at full resolution. Video memory has to stay over 95% of the budget on two 10 s checks in
  a row before FSR and XeSS give way, not on one sample over 90%.
- TG Better Upscaling extends the upscaling row for DLSS's quality modes even where FSR and XeSS are
  unavailable. Its "Add upscalers to Video settings" setting says that the game applies its upscaler
  through that row.
- The game builds its display and VSync rows again when the window moves to another monitor; the
  Exclusive Fullscreen restriction and the VSync hold follow the new rows. An Apply press in the Video
  tab counts once, so a later Restore Defaults can't apply display rows the player is still editing.
- The display check follows the game's window when the swapchain is replaced on the same device.
  Anti-Lag 2's and XeLL's per-frame errors are counted and logged. GPU usage is the busiest engine of
  the game's GPU, as Task Manager counts it, not engines of one type added together.
- A disable-autoload.txt in the bridge's folder shows on both mods' rows. RivaTuner's frame limit
  and Lossless Scaling are checked again every 30 s, so their alerts clear once they are changed or
  closed.

Two mods and a shared bridge (1.1.0)

- The repository now builds two mods on a shared part. **TG Graphics Bridge** is the DX11-to-DX12
  bridge and its BepInEx preloader, taken out of TG Frame Generation: it owns the graphics hooks,
  observes the game's device, creates one D3D12 device inside the game's swapchain creation, and
  loads the native part of each mod built on it (`BepInEx/<mod>/graphics-bridge-extension.json`).
  **TG Frame Generation** is now one of those extensions. **TG Better Upscaling** is new.
- The bridge is bundled with each mod, not a download of its own, under the mod's own folders
  (`BepInEx/patchers/<mod>.Autoload.dll`, `BepInEx/plugins/<mod>/TGGraphicsBridge.dll`,
  `BepInEx/<mod>/Native/TGGraphicsBridge.Native.dll`), so installing both deploys no file twice and
  Vortex reports no conflict. With both installed, the preloader bundled with the newest bridge
  starts it once for both, and BepInEx loads the newest copy of its plugin; BepInEx logs that it
  skips the other copy.
- TG Better Upscaling adds AMD FSR (FSR 4 on Radeon RX 9000, FSR 3.1 elsewhere) and Intel XeSS to the
  game's own Upscaling row, after None, STP and DLSS, with the row's quality slider as the render
  scale, and an FSR Sharpness row under FSR. They run where HDRP runs STP, on HDRP's
  own color, depth and motion vectors, on the bridge's D3D12 device, and return the upscaled image in
  STP's output texture, so post-processing, RenoDX, ReShade and frame generation see an upscaled frame
  as they would after STP. A refused frame is enlarged instead of left black; five in a row turn that
  technology off for the session and STP takes over.
- FSR has no version row: it runs AMD's default for the GPU, which is FSR 4 on a Radeon RX 9000 and
  the newest FSR 3.1 elsewhere (3.1.5 on an RTX 5080, checked). AMD asks that a version is either
  left to that default or chosen by the player, never picked by the application; the native log names
  the version AMD chose and the newest it offers.
- A performance overlay, switched on in the last row of the Video settings (Performance Overlay, with
  Overlay Position and Overlay Size below it), FoA Mod Manager or TG Graphics Bridge's config: frame
  rate and frame times, render and output resolution, VSync and the frame cap, CPU, GPU and video
  memory use, and a section from each mod (the upscaler, its mode and version, the DLSS model and DLL;
  frame generation's technology, mode, multiplier and frame rate on screen). The DLSS model comes from
  the NVIDIA driver's profile for the game when the NVIDIA App overrides it (preset, performance mode,
  render scale, DLAA), and is the DLL's default for the mode otherwise.
- DLSS, FSR and XeSS get their own quality modes in the Video tab (Quality Mode) instead of the game's
  render-scale slider, which STP keeps. Only the modes each runtime reports are offered, at the ratio
  it reports: NGX's recommended size per DLSS mode through Unity's NVIDIA module (DLAA only when NGX
  sizes it), FSR's per-mode ratio from AMD's runtime, XeSS's seven presets from Intel's. DLSS gets the
  mode as its NGX quality level, so NVIDIA's driver picks that mode's model; the game had given DLSS a
  level derived from the slider's position. XeSS initializes with the chosen preset. A config without
  a mode starts each upscaler on the highest-quality mode at or below the game's render scale (never
  more pixels than before), and the game's graphics presets set the modes by the same rule. The
  upscaling ABI is 1.2 (`TGUP_GetModes`, `TGUP_Frame.mode`).
- FSR gets HDRP's exposure, as HDRP's own DLSS pass takes it (a readback requested every frame,
  limited to 0.2..2): as its pre-exposure, which it divides out so that its history holds while the
  game's exposure adapts, and as its exposure texture, so it still works on the color HDRP tonemaps.
  It had a fixed pre-exposure of 1, so every exposure change looked like a change in brightness to
  its history. XeSS, which has no pre-exposure input, keeps working on the pre-exposed color.
- With FSR, textures get AMD's recommended mip bias, `log2(render / output width) - 1`: HDRP applies
  `log2(render / output width)` wherever STP runs (Intel's guidance for XeSS), and FSR adds the
  extra -1 through the camera's material mip bias while it is selected.
- TG Better Upscaling brings NVIDIA's DLSS 310.9.1 DLL (`Native/DLSS/nvngx_dlss.dll`, from the DLSS
  SDK) for the game's own DLSS, which ships 3.8.10. When NVIDIA's runtime loads an older
  `nvngx_dlss.dll`, it gets the bundled one; an equal or newer file, such as a DLSS mod's, is kept.
  Nothing in the game folder is replaced. A DLSS mod's own set of DLLs is kept whole as well: with
  DLSS neural rendering (`nvngx_dlssnr.dll`, DLSS 5) or frame generation or ray reconstruction of the
  same release beside the game folder's `nvngx_dlss.dll`, that file stays, since the RenoDX DLSS 5
  add-on's frame rate stopped following the render resolution with ours in its place.
  Stage-Runtime.ps1 takes `-DlssRoot`.
- Only FSR gets a sharpness row, at 50% by default. DLSS has no sharpening any more (NVIDIA's headers mark it "not
  supported" and the game's DLSS 310 ignores it) and XeSS never had one.
- The bridge hooks the swapchain methods of DXGI factories up front, on a factory of its own created
  past any hook already on `dxgi.dll`'s exports, before any extension loads. Up to 1.0.x frame
  generation saw the game's swapchain only because something else created a DXGI factory where its
  hooks saw it, which happened with NVIDIA's runtimes loaded; with ReShade installed as `dxgi.dll`,
  which forwards the game's factory calls past later hooks, TG Better Upscaling alone never saw it, and
  frame generation on AMD and Intel GPUs most likely didn't either. Installed first, the bridge's
  hook also runs inside ReShade's, so ReShade leaves the bridge's D3D12 device and frame generation's
  swapchain alone, and the RenoDX DLSS add-on still gets a device of its own.
- When frame generation's native side reports nothing (it didn't load or start, another mod already
  loaded NVIDIA Streamline, or it couldn't take over the game's presentation), the Frame Generation row
  offers Off only, greyed out, and its tooltip says why. Up to 1.0.x it showed DLSS Frame Generation
  with multi-frame choices on any GPU in that case, which did nothing. A report that arrives later
  replaces those rows.
- With TG Frame Generation 1.0.x still installed, which runs a graphics bridge of its own, the bridge
  does not start and says to update Frame Generation to 1.1.0; two bridges cannot share the game's
  device.
- Frame generation and upscaling work on Windows 10 and older Windows 11 builds. The game's exe asks
  Windows for Direct3D 12 Agility SDK 610 from its `D3D12` folder, which holds D3D12Core.dll 611.
  Where Windows' own runtime is newer (Windows 11 24H2 and later), Windows uses its own and never looks
  there; where it is older, Windows loads the game's file and refuses every D3D12 device over the
  mismatch (0x887E0003, D3D12_ERROR_INVALID_REDIST), so up to 1.0.x frame generation never started
  there. Before any runtime loads `d3d12.dll`, which reads the request as it loads, the bridge sets the
  request to the version the game ships, and logs the three versions it found. Reproduced and checked
  on Windows 11 with an exe asking for 618 beside a D3D12Core.dll 619: 0x887E0003 before, a device
  from that file after.

Frame generation quality

- DLSS Frame Generation and Intel XeSS-FG receive depth, motion vectors and a camera in the same
  orientation as the frame they interpolate. HDRP renders Y-inverted, so both were reprojecting from
  mirrored buffers and visibly warping during camera movement; all three backends now look alike.
- The HUD-less image is HDRP's colour at `AfterPostProcess`, taken in the frame it belongs to, instead
  of a swapchain capture that was one frame late.
- First-person hands no longer flicker: Kandra's previous skinned positions survive the extra
  camera-rendering contexts that used to overwrite them within a frame.
- The HUD stays out of frame generation with ReShade and RenoDX, by replaying RenoDX's recognised
  final-output shader on the capture, and by accepting ReShade's wrapped D3D11 device.
- FSR is given the resolution the game renders at rather than the display resolution, and near and far
  swapped for inverted depth, as AMD's own integrations do.
- FSR resets its history when the HUD-less source changes, and rebuilds its context when input flags do.
- DLSS-G is told to allocate its UI recomposition path only while a verified HUD-less image is supplied.
- A still image - a menu, a pause, a loading screen - no longer counts against the HUD-less capture.

Display settings

- Turning frame generation on moves the game's own display setting from Exclusive Fullscreen to
  Borderless Fullscreen, and stops offering Exclusive while it is on. Frame generation cannot run in
  Exclusive Fullscreen: DXGI will not give its swapchain a window that already holds the display.
  `Use borderless for frame generation` in the advanced settings turns this off.
- Low Latency can be Off while FSR frame generation runs; AMD pairs FSR with Anti-Lag 2, which NVIDIA
  hardware does not have.

Stability

- Frame generation survives a display-mode change. Unity replaces the swapchain rather than resizing
  it, and the replacement was refused because a bridge already existed, which left the game presenting
  through its own swapchain for the rest of the session.
- Fixed a crash when the game kept using a bridge it had replaced. Twenty-two swapchain methods
  forwarded to a presentation swapchain that was no longer there, three of them as tail calls through
  its vtable.
- Frame generation starts on an sRGB back buffer instead of failing to create the bridge. The view that
  failed exists only to verify the HUD-less capture, so the bridge now continues without it.
- The HUD-less capture has to earn its verification again whenever the buffers change; the previous
  result outlived the buffers it was measured on.
- A `ResizeBuffers` asking for the configuration already in effect is skipped. FoA issues seven in a
  minute of Options use, each costing a GPU drain, a buffer rebuild and a frame over 100 ms.
- Fixed the IL2CPP crash when opening Options with upscaling active.

DLSS-G correctness

- `slDLSSGGetState` is no longer given an options pointer, which it applies rather than reads.
- The capabilities query stays at startup; deferring it to the present thread broke the Video tab twice.
- The pipeline line reports DLSS-G's own presented-frame count, and prints `unreported` for FSR rather
  than a zero that read as failure.

Diagnostics

- Reflex and XeLL are measured on one comparable figure, `simToPresent`, alongside each technology's
  own. Every average divides by the frames behind it, which `renderLatency` did not.
- A refused bridge says what the game asked for - size, format, buffers, swap effect, flags and whether
  the request was windowed or exclusive fullscreen - on all three creation paths.
- The log records whether the game protects its immediate context, and says so once if the bridge ever
  uses that context from a second thread.
- FSR debug flag 32 saves three source colour, depth and motion frames with their metadata, once.
- Added a D3D11 WARP regression test for the HUD-less colour conversion shader, and portable tests for
  the clip-space orientation change.
- New optional exports `TGFG_SetHudlessCapture`, `TGFG_SetHudless` and `TGFG_SetDebugFlags`.

Packaging

- The Nexus downloads carry `vortex_override_instructions.json`, which sets the mod type Vortex calls
  "Engine Injector". Vortex was installing the BepInEx folder under its own mod directory instead of
  the game root, so nothing loaded. Manual installs and other mod managers are unaffected.

# v0.5.0 - AMD FSR and Intel XeSS frame generation, one Low Latency selector

- The plugin is now "TG Frame Generation" with the ID com.wessberg.tgframegeneration. Settings
  live in BepInEx/config/com.wessberg.tgframegeneration.cfg; the old
  tgfoa.experimental.dlss-frame-generation.cfg and BepInEx/TGFrameGeneration/VideoSettings.cfg
  are migrated on first start and renamed to *.migrated.
- FoA Mod Manager lists exactly the Video tab's six rows, with the same names, choices, labels,
  order and visibility rules. Choices follow what this GPU reports, and a change made there
  shows up in the Video tab once the Options screen is closed. The other options sit in a hidden
  [Advanced] section.
- Avalon Mod Manager's MODS tab no longer lists TGFG: TGFG adds itself to that manager's
  blacklist for the session, without saving its config. Its Apply writes back the values staged
  when the Options screen opened, which undid Video-tab changes.
- Low Latency can be Off only while frame generation is Off; FSR now runs with Reflex or Anti-Lag
  2. The row is greyed out when only one choice remains (XeSS with XeLL).
- Frame generation no longer needs DLSS upscaling. Without DLSS, a custom pass before
  post-processing hands the native side HDRP's own depth and motion vectors (TGFG_SetHdrpInputs,
  render event 3), with HDRP's TAA jitter and the motion vector scale HDRP gives DLSS. DLSS's
  inputs take precedence while it runs. FSR and XeSS frame generation therefore work on AMD and
  Intel GPUs. If the custom pass can't be installed, the rows are greyed out unless DLSS is the
  selected upscaler.
- The standard version (IL2CPP, BepInEx 6) has the same Video rows and HDRP input path as the Mono
  build, from the same sources. TGFG's subclasses of the game's Setting and DependentOption and of
  HDRP's CustomPass are registered with Il2CppInterop, which routes the base class's virtual calls
  to them. Il2CppInterop allocates those objects without running the IL2CPP base constructor, so
  TGFG runs it (Il2CppTypes.Construct).
- Packages include NVIDIA's license notices next to the Streamline runtime
  (LICENSE-Streamline.txt, LICENSE-NVIDIA-RTX-SDKs.txt, LICENSE-NVIDIA-Reflex.txt), staged from
  the SDK release by Stage-Runtime.ps1.
- Packaging writes zip entries with forward slashes (Compress-Archive in Windows PowerShell used
  backslashes) and copies each package to artifacts/Nexus as "TGFrameGeneration - mono.zip" or
  "TGFrameGeneration.zip". Build paths are mapped, so the DLLs no longer name local folders.
  A package holds the BepInEx folder and TGFG-Package-manifest.json; TGFG-PLAYER-INSTALL.txt is
  no longer included.
- Removed leftovers of the 0.3 patch workflow: APPLY.md, integrate-autoload.patch,
  source-manifest.json, the validation logs, docs/VALIDATION.md, the retired Launch-Game.ps1 and
  the 0.3 example config.
- Video tab: Frame Generation [Off | DLSS | FSR | XeSS]. A technology is listed only when its
  runtime is installed and reports support on the GPU. FSR runs through AMD's FSR API (FSR SDK
  2.3.0: FSR 4 ML frame generation on Radeon RX 9000, FSR 3.1 analytic frame generation elsewhere)
  and AMD's frame generation swapchain. XeSS runs through Intel's XeSS-FG swapchain (XeSS SDK
  3.0.2). All three use the same inputs (see the HDRP input path above).
- Frame multipliers come from the runtimes: DLSS from Streamline's numFramesToGenerateMax, XeSS
  from xefgSwapChainGetProperties (up to 4x on Intel Arc, 2x elsewhere). The FSR API has no
  multi-frame control, so FSR is 2x. Frame Multiplier offers up to the highest reported maximum,
  blocks values above the selected technology's maximum and clamps the value when switching
  technology; the native side clamps the request as well. Neither FSR nor XeSS has dynamic frame
  generation, so Dynamic stays DLSS-only. The 6x cap is gone (the ABI bound is 16).
- Switching technology recreates only the private presentation swapchain, between two frames: the
  Streamline swapchain for Off/DLSS, the SDK's own swapchain for FSR/XeSS. The game's D3D11
  buffers are unchanged. An SDK that fails is disabled for the session and presentation falls
  back to the standard swapchain.
- Low Latency [Off | NVIDIA Reflex | NVIDIA Reflex + Boost | Intel XeLL | AMD Anti-Lag 2] replaces
  the NVIDIA Reflex row and lists what the GPU supports. DLSS Frame Generation requires Reflex and
  XeSS Frame Generation runs with XeLL only (Intel's requirement), so the row forbids the other
  choices for those. XeLL on its own needs an Intel GPU; Anti-Lag 2 needs an AMD GPU. The 0.4.0
  "NVIDIA Reflex" value migrates to "Low latency"; the advanced base FPS limit feeds whichever
  technology is active.
- The bridge no longer requires DLSS-G: Streamline binds when DLSS-G or Reflex is supported (RTX
  20/30 get FSR/XeSS with Reflex), other GPUs get a plain D3D12 device, and a Streamline start
  failure no longer stops the other technologies.
- A game waiting on the swapchain's frame-latency object gets a bridge-owned semaphore that
  survives swapchain recreation; the private swapchain no longer carries that flag. HDR metadata
  is re-applied to a recreated swapchain.
- Native ABI v5 (0x00050000): the request gains backend and latencyMode (was reflexMode) and
  baseFpsLimit (was reflexBaseFpsLimit); the status reports available backends, per-backend
  maxima, latency technologies, the active output and latency technology, the GPU vendor and
  backend failures; the camera packet carries the world-to-view matrix XeSS needs.
- Packages add Native/FidelityFX (AMD-signed loader and frame generation DLLs, license notice)
  and Native/XeSS (Intel-signed libxess_fg.dll and libxell.dll, license notice).
  Stage-Runtime.ps1 takes -FidelityFXRoot and -XeSSRoot; Build-Native.ps1 takes the FSR, XeSS and
  Anti-Lag 2 SDK roots, defaulting to folders next to the script.
- Managed and native versions are 0.5.0.

# v0.4.0 - native Video settings rows, Reflex modes, startup crash fix

- Video tab: Frame Generation [Off | DLSS], Frame Generation Mode [Fixed | Dynamic] (shown only
  when the runtime reports Multi Frame Generation and Dynamic support), Frame Multiplier
  [2x .. max reported] (Fixed), Max Frame Multiplier [Up to 2x .. max] and Target Framerate
  [Auto | 60 .. display maximum] (Dynamic; the max is sent as numFramesToGenerate, which the
  Streamline 2.14.1 guide documents as ignored in Dynamic mode, so it is best-effort) and
  NVIDIA Reflex [Off | On | On + Boost] are real game Setting/PrefOption rows registered through
  SettingsMaster.AddGraphicSetting, placed after Dynamic Resolution, with the game's Apply
  Changes / Undo / Restore Defaults. Reflex Off is forbidden while Frame Generation is on.
- Video settings persist in BepInEx/TGFrameGeneration/VideoSettings.cfg, outside the plugin's
  main config, so generic settings managers no longer re-apply stale copies over them. The
  old UGUI adapter and its Mode/Fixed multiplier/target entries in the main config are gone.
- Native ABI v4 (0x00040000): TGFG_Request gains reflexMode (Off/On/On + Boost). Reflex
  follows the user setting and is forced to at least On while FG is requested.
- Managed and native versions are 0.4.0.
- DLSS 5 (RenoDX DLSS addon) compatibility: NVIDIA's sl.interposer.dll is staged and installed,
  byte-identical and still NVIDIA-signed, as `Native/Streamline/TGFG.Streamline.dll`. The addon
  looks Streamline up by module name, hooks the interposer's exports and DXGI factories, and
  releases (E_UNEXPECTED) every swapchain ReShade did not observe, including TGFG's private
  output. Deployment backs up and removes the old `Native/Streamline/sl.interposer.dll`. The
  native log reports another mod's Streamline if one is loaded later.
- ReShade compatibility: NVIDIA's Unity plugin evaluates DLSS on ReShade's proxy context, not the
  real immediate context. The bridge tags the real context with private data (which ReShade
  forwards) and matches on it, so depth/motion capture works under ReShade + RenoDX.
- Video tab: Frame Multiplier is shown only in Fixed mode; Dynamic mode shows Max Frame Multiplier
  and Target Framerate instead. The pipeline log line includes Reflex's own latency reports.
- Fixes found in the first in-game FG runs: the end-of-camera marker never executed under HDRP
  (endCameraRendering fires after the last Submit); FoA's camera rect is not bit-exact full screen;
  the R32G32_FLOAT motion copy is not NT-handle shareable on NVIDIA D3D11 (now R16G16_FLOAT).
  A "Pipeline 5s:" line in TGFG.Native.log shows where frames drop out.

- Native: removed 64 KB stack buffers (GetModuleFileNameW/GetSystemDirectoryW) from the
  GetProcAddress hook and bootstrap paths. The hook runs on every thread in the process,
  including NVIDIA driver worker threads with small stacks, and the 64 KB frame probe caused
  a STATUS_STACK_OVERFLOW at the first Streamline Present. Paths are now heap-grown.
- Build-Native.ps1: passes -DCMAKE_BUILD_TYPE=Release for single-config generators; the
  NMake build previously linked the debug CRT.

# v0.3.1 - repository-integrated Steam autoload

- Adds src/TGFG.Autoload in the existing solution and gameplay build graph.
- Same root Build.Local.props, two runtime-specific managed pairs.
- Build.ps1 -Deploy stages/installs complete plugin/preloader/native/runtime package.
- Package.ps1 produces a launcher-free player ZIP from this repo's artifacts.
- Test-Setup.ps1 checks the installed Steam-autoload layout, not developer launcher files.
- Replaces stale launcher advice and adds an autoload log tail to gameplay diagnostics.
- Retains all native v0.3.0 source, SDK build scripts and ABI unchanged.
- No Windows compilation, PowerShell execution or in-game validation claimed.

# Changelog

## 0.3.0 — DX11 rendering / private DX12 Streamline presentation

Replaced the D3D12-source backend with a direct D3D11-to-D3D12 bridge. Added a
D3D11-facing swapchain, shared textures/fences, typed D3D11 NGX capture, protected
input conversion, HDR output conversion, expanded diagnostics and local setup
checks. Kept the Mono/IL2CPP managed targets and existing in-game menu adapter.

Changed the early launcher to require `-force-d3d11` and rendezvous after Windows
loader initialization at the restored PE entrypoint. ABI changed from v2 to v3.
Added Windows HLSL compilation tests and portable interop policy checks. No
OptiScaler/FidelityFX rendering backend is included. Windows build/game testing
has not been performed in the preparation environment.

The HUD-less capture export is implemented, but no verified FoA/RenoDX HUD-less
stage is hooked. Indexed source buffers, nonblocking presentation, full device
recovery and zero-overhead Off are not claimed.

## 0.2.0

Earlier direct Streamline prototype, requiring actual D3D12 game rendering.
Not compatible with this version's native ABI.

## 0.1.0

Earlier architecture was superseded. It is not a dependency of this package.
