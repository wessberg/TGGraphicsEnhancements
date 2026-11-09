[size=5][b]TG Neural Rendering[/b][/size]

This mod adds NVIDIA DLSS 5 Neural Rendering to Tainted Grail: The Fall of Avalon.

It runs on the game's own scene colour, depth and motion data and is placed before the game's camera effects and interface. It can run after DLSS upscaling for the best image, or before upscaling for lower GPU cost. The setting is part of the game's own Video menu, so it works like a built-in graphics option.

This is a native integration. It does not use OptiScaler, a driver override or a second graphics device.

[size=4][b]Runtime DLL and RTX support[/b][/size]

This download includes NVIDIA's signed Neural Rendering DLL, version 310.9.16.0, unmodified, from NVIDIA's [url=https://github.com/NVIDIAGameWorks/rtx-remix/releases]RTX Remix release[/url]. It's NVIDIA's faster model. NVIDIA lists RTX 50 cards for it, and I've tested it on my RTX 5080. There's nothing else to install.

[list]
[*][b]Official DLL:[/b] the included 310.9.16.0, or the older NVIDIA-signed 310.8.0.0. Both use the normal NVIDIA loading path.[/*]
[*][b]Community DLL:[/b] compatible patched runtimes use the mod's separate compatibility adapter. This includes the community patch intended for RTX 2000, 3000 and 4000 cards.[/*]
[/list]

To use another DLL, close the game and copy it over the included one. Do this again after you update the mod, because the update puts NVIDIA's DLL back:

[code]Fall of Avalon/BepInEx/TGNeuralRendering/Native/DLSS/nvngx_dlssnr.dll[/code]

Restart the game. The mod automatically selects the right loading path. There is no extra compatibility setting, and compatible community patches can be swapped in without updating the mod. A patch that changes the runtime's interface may still need a mod update.

The community DLL passes standalone checks on my RTX 5080 for rendering, SDR/HDR, Ray Reconstruction compatibility and multiple NR passes. Its in-game check is still pending. I have not yet tested it on RTX 2000, 3000 or 4000 hardware, so support for those cards is not confirmed. Passing on a 5080 cannot establish that it works on an older card.

The [url=https://discord.com/invite/F6AUTeWJHM]RenoDX Discord[/url] is a community source for DLSS 5 files. It is separate from NVIDIA's official distribution. Follow the terms that come with your copy; the mod does not download the DLL for you. [code]nvngx_dlss.dll[/code] and [code]nvngx_dlssg.dll[/code] are different runtimes and cannot be renamed to replace it.

[size=4][b]Video settings[/b][/size]

Open [i]Options > Video[/i]. The rows appear in the game's own Video settings and FoA Mod Manager shows the same rows and choices.

[list]
[*][b]Neural Rendering:[/b] Off or DLSS. The row is disabled unless DLSS is the selected upscaler and the required runtime is available.[/*]
[*][b]Placement:[/b] Quality or Performance. Quality is the default and runs after DLSS upscaling to preserve more detail. Performance runs before DLSS upscaling, processing fewer pixels for lower GPU cost, but can soften detail and produce larger colour or brightness changes after movement stops.[/*]
[*][b]Strength:[/b] Controls how much of the Neural Rendering result is applied. 0 keeps the original scene. 1 gives the full effect.[/*]
[*][b]Advanced:[/b] Shows Local Tone, Style, Passes, Detail, Automatic Mask and Skin Detail.[/*]
[*][b]Local Tone (under Advanced):[/b] Controls the model's lighting and colour changes. Lower values keep the game's original grading closer while retaining detail work.[/*]
[/list]

The default profile is NVIDIA's [i]Default[/i] style with one pass, full Strength, full Local Tone, full Detail, full Skin Detail and Automatic Mask Off. The advanced Style choices use NVIDIA's names: Default, A, B and C. Passes run Neural Rendering again on the previous result. Each pass uses the same controls, so extra passes cost more GPU time and memory and can exaggerate colour changes or artifacts. Strength is applied within each pass. It is not a final opacity slider.

Ray Reconstruction requires the Quality placement, so the Performance choice is disabled while that denoiser needs to run after upscaling. Changing to FSR, XeSS, STP or None disables Neural Rendering until DLSS is selected again. Your saved choices are kept.

[size=4][b]HDR, ReShade and other TG mods[/b][/size]

TG Neural Rendering works on its own. You do not need TG Frame Generation, TG Better Upscaling, TG Path Tracing, ReShade or RenoDX.

It also works alongside them when their own requirements are met:

[list]
[*][b]ReShade:[/b] works with ReShade enabled.[/*]
[*][b]RenoDX HDR:[/b] works with RenoDX's HDR conversion after Neural Rendering.[/*]
[*][b]TG Frame Generation:[/b] Neural Rendering runs once on each real rendered frame. Generated frames are not processed a second time.[/*]
[*][b]TG Better Upscaling:[/b] Neural Rendering is available when DLSS is selected. FSR and XeSS remain their own upscaling choices.[/*]
[*][b]TG Path Tracing:[/b] it shares the same graphics bridge. Combined scene validation is still in progress.[/*]
[*][b]RenoDX DLSS 5 add-on:[/b] TG Neural Rendering is its own DLSS 5 integration. Do not stack both integrations unless you are deliberately comparing them.[/*]
[/list]

The TG Graphics Bridge is bundled with this mod. You do not need to download it separately. If you install several TG graphics mods, install matching versions so they share the same bridge build.

[size=4][b]Installation[/b][/size]

This mod supports both versions of the game. Download the archive that matches your installation:

[list]
[*][b]Mono version:[/b] the [url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/50]BepInEx 5 modding branch[/url].[/*]
[*][b]Normal version:[/b] the standard game with [url=https://builds.bepinex.dev/projects/bepinex_be/755/BepInEx-Unity.IL2CPP-win-x64-6.0.0-be.755%2B3fab71a.zip]BepInEx 6[/url].[/*]
[/list]

Install the matching BepInEx version first, then extract the TG Neural Rendering archive into the game folder. If you use Vortex, install the archive as an [i]Engine Injector[/i] so its BepInEx folder lands at the game root.

Close the game before updating the mod or replacing the runtime DLL. The compatibility adapter comes with the mod; you do not need to install it separately or rename the game executable.

[size=4][b]Known limitation[/b][/size]

Neural Rendering can change local lighting and colour as the camera moves through some scenes. This is especially visible while walking through the tavern. The integration is still in testing, so treat the Quality placement and the default one-pass profile as the reference settings while this is investigated.

[size=4][b]OptiScaler[/b][/size]

OptiScaler is not supported with this mod. It hooks the same upscaling and presentation paths and can interfere with the native integration. Remove it completely before testing TG Neural Rendering.

[size=4][b]Troubleshooting[/b][/size]

If the Neural Rendering row is disabled, hover over it. The tooltip tells you whether DLSS is not selected, the runtime DLL is missing or invalid, or another requirement is unavailable. Make sure the game is closed while you copy the DLL and restart it afterward.

For a bug report, open [i]Options > Video[/i], use the TG graphics troubleshooting report option and include the generated report from your [i]BepInEx[/i] folder. Include the runtime version and where you obtained the DLL, but do not upload NVIDIA's proprietary DLL itself.
