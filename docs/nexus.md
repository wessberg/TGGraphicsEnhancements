[size=5][b]Frame Generation[/b][/size]

This mod adds native frame generation to Tainted Grail: The Fall of Avalon, with NVIDIA DLSS, AMD FSR and Intel XeSS. It also adds the matching low latency modes: NVIDIA Reflex, AMD Anti-Lag 2 and Intel XeLL.

It supports the latest features, like DLSS Dynamic Multi-Frame Generation, and is integrated cleanly into the game's existing graphics settings menus. You would never guess it was a mod if you didn't already know!

[img]https://i.imgur.com/QqHVCZZ.jpeg[/img]

Again, it's [i]native[/i] frame generation that generates frames based on the game's own motion vectors and depth, like you know from other games with an official implementation. It's not a driver hack, and it's not going through OptiScaler.

And, it is fully compatible with both RenoDX HDR and RenoDX DLSS 5!

[size=4][b]Part of the game's own graphics settings[/b][/size]

All options live in the game's own [i]Video[/i] settings, right below the upscaler. The menu only offers what your graphics card supports, and rows only show up when they apply to your current choices.

[list]
[\*][b]Frame Generation:[/b] Off, DLSS, FSR or XeSS, whichever your card supports.[/*]
[\*][b]Frame Multiplier:[/b] How many frames you see per rendered frame. Up to 6x with DLSS on RTX 50 cards and up to 4x with XeSS on Intel Arc cards. FSR, and XeSS on other cards, add one generated frame per rendered frame (2x). The mod asks each technology what your card supports.[/*]
[\*][b]Frame Generation Mode:[/b] With DLSS on RTX 50 cards, choose [i]Fixed[/i] for a constant multiplier or [i]Dynamic[/i], which picks the number of generated frames each frame to reach your [i]Target Framerate[/i].[/*]
[\*][b]Low Latency:[/b] NVIDIA Reflex (with or without Boost), AMD Anti-Lag 2 or Intel XeLL, whichever your card supports. Generating frames adds a little input delay, so low latency can't be turned off while frame generation is on. DLSS always uses Reflex, and XeSS always uses XeLL.[/*]
[/list]

If you use [i]FoA Mod Manager[/i], it shows the same options with the same choices, and a change in one place shows up in the other.

[size=4][b]Works without upscaling too[/b][/size]

When DLSS upscaling is on, frame generation uses the same motion vectors and depth that DLSS gets. With TAA, STP or no anti-aliasing, it takes them straight from the game's renderer instead. Either way the generated frames follow the game's real motion, and FSR and XeSS frame generation work on AMD and Intel cards too.

If you'd like FSR or XeSS upscaling too, my [url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/UPSCALING-ID]FSR and XeSS Upscaling[/url] mod adds them to the game's own Upscaling setting, and frame generation works with those too.

[size=4][b]Compatibility[/b][/size]

[list]
[*]Fully compatible with [b]RenoDX HDR[/b] and the [b]RenoDX DLSS 5[/b] addon.[/*]
[*]Works with ReShade.[/*]
[*]Works with FoA Mod Manager (see above). Avalon Mod Manager doesn't list the mod, since its options are in the Video settings.[/*]
[*]OptiScaler isn't supported (see below).[/*]
[/list]

[size=4][b]OptiScaler isn't supported[/b][/size]

This mod doesn't work together with OptiScaler. Both hook into the same parts of the game, like its DLSS and the way it puts frames on the screen, and they get in each other's way. You don't need OptiScaler here anyway. This mod gives you DLSS, FSR and XeSS frame generation, and my [url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/UPSCALING-ID]FSR and XeSS Upscaling[/url] mod adds FSR and XeSS upscaling next to the game's own DLSS. Together they cover what most people use OptiScaler for, natively and in the game's own settings.

If you used OptiScaler before, remove it completely from your game folder before you install this mod:

[list=1]
[*]If you installed OptiScaler through Vortex or another mod manager, uninstall it there. If you used OptiScaler's setup script, run the [i]Remove OptiScaler.bat[/i] it left in your game folder.[/*]
[*]Otherwise, delete the files you copied into the game folder yourself: the renamed OptiScaler DLL (usually [b]dxgi.dll[/b], [b]winmm.dll[/b], [b]version.dll[/b], [b]dbghelp.dll[/b], [b]d3d12.dll[/b], [b]wininet.dll[/b] or [b]winhttp.dll[/b]), [b]OptiScaler.ini[/b], [b]OptiScaler.log[/b], and the files that came with it, like [b]amd_fidelityfx_dx12.dll[/b], [b]amd_fidelityfx_vk.dll[/b], [b]libxess.dll[/b], [b]libxess_dx11.dll[/b], [b]nvngx.dll[/b], [b]fakenvapi.dll[/b], [b]fakenvapi.ini[/b], [b]dlssg_to_fsr3_amd_is_better.dll[/b] and the [b]D3D12_Optiscaler[/b] folder. An [b]nvapi64.dll[/b] in the game folder belongs to it too; the real one lives in Windows.[/*]
[*]Be careful with [b]dxgi.dll[/b], since ReShade usually installs itself under that name as well. Right-click the file, open [i]Properties[/i] and then [i]Details[/i]: if it says ReShade, keep it. Also leave the game's own [b]D3D12[/b] folder alone. Only [b]D3D12_Optiscaler[/b] belongs to OptiScaler.[/*]
[/list]

[size=4][b]Installation[/b][/size]

This mod supports both the standard version of the game, as well as the special 'modding' version built for the special 'mono' branch of the game, that you can opt into via Steam.

Make sure to download the version of the mod that matches your game.
As a rule of thumb, if you just installed the game and aren't sure, you're going to want to use the [i]BepInEx 6 (Normal/IL2CPP branch)[/i] version, as that matches the default game version.

You can use this mod on its own. If you also use my [url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/UPSCALING-ID]FSR and XeSS Upscaling[/url] mod, install both the same way: they share the part they have in common, and neither needs anything else. Vortex won't report a conflict between them.

[size=3][b]Installation for the Mono/modding game version[/b][/size]

[size=2][b]Requirements[/b][/size]

[list]
[*]First, install [url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/50]BepInEx 5.4 (Mono/modding branch)[/url] if you haven't already. To install it, you simply need to extract the contents of the file inside your game directory.[/*]
[*]Download the [i]TGFrameGeneration - Mono branch[/i] version of this mod from the 'Files' page, and extract the .zip archive in the root of your game directory.[/*]
[/list]

[size=3][b]Installation for the standard game version[/b][/size]

[size=2][b]Requirements[/b][/size]

[list]
[*]First, install [url=https://builds.bepinex.dev/projects/bepinex_be/755/BepInEx-Unity.IL2CPP-win-x64-6.0.0-be.755%2B3fab71a.zip]BepInEx 6 (Normal/IL2CPP branch)[/url] if you haven't already. To install it, you simply need to extract the contents of the file inside your game directory.[/*]
[*]Download the [i]TGFrameGeneration[/i] version of this mod from the 'Files' page, and extract the .zip archive in the root of your game directory.[/*]
[/list]

[quote][b]NOTE[/b]: BepInEx 6 (running IL2CPP) is still experimental, and the latest build that is available on nexusmods can crash during story moments or scene transitions. However, there are more recent, more stable builds that I recommend, which is why I've linked to the most recent build as a requirement here instead. I haven't experienced any crashes with that build yet. You can always find the most recent nightly build [url=https://builds.bepinex.dev/projects/bepinex_be/755/BepInEx-Unity.IL2CPP-win-x64-6.0.0-be.755%2B3fab71a.zip]here[/url]. Always pick the [i]BepInEx Unity (IL2CPP) for Windows (x64) games[/i] build for whatever version you select.[/quote]

[quote]Note: If you install via Vortex, make sure the mod type is set to 'Engine Injector' to make sure it is applied from the root of the game directory.[/quote]

You don't need to download any DLL files yourself. The NVIDIA, AMD and Intel files the mod needs are included in the download, together with their licenses.

For both versions of the mod, this is how the directory should end up looking:

[code]Fall of Avalon.exe
...
BepInEx/
  patchers/
    TGFrameGeneration.Autoload.dll
  plugins/
    TGFrameGeneration/
      TGFrameGeneration.dll
      TGGraphicsBridge.dll
  TGFrameGeneration/
    graphics-bridge-extension.json
    Native/
      TGFG.Native.dll
      TGGraphicsBridge.Native.dll
      Streamline/   (NVIDIA)
      FidelityFX/   (AMD)
      XeSS/         (Intel)[/code]

[quote][b]Updating from an older version[/b]: Older versions of this mod started up through [b]BepInEx/patchers/TGFG.Autoload.dll[/b]. The new [b]TGFrameGeneration.Autoload.dll[/b] does that job now, so if you installed by hand, delete the old file after updating. Vortex removes it for you when you update the mod.[/quote]

[size=4][b]How to use it[/b][/size]

[list=1]
[*]Open [i]Options > Video[/i].[/*]
[*]Pick a technology under [i]Frame Generation[/i], and a multiplier if your card offers more than 2x.[/*]
[*]Apply Changes.[/*]
[/list]

Frame generation pauses in menus and while the cursor is visible. You can press [b]F8[/b] at any time to turn it off.

[size=4][b]Good to know[/b][/size]

[list]
[*]Frame generation works best when the game already runs well before generation. AMD recommends at least 60 fps before frame generation for FSR.[/*]
[*][i]Performance Overlay[/i], at the very bottom of the [i]Video[/i] settings, shows your frame rate, frame times, render resolution, CPU, GPU and video memory use, and what the frame generation is doing, in a corner of the screen. You pick the corner and the text size right below it.[/*]
[*]Your settings are saved in [b]BepInEx/config/com.wessberg.tgframegeneration.cfg[/b].[/*]
[*]If something doesn't work, open [i]Options > Video[/i] first. If the mod found a problem, the setting it affects has a warning icon, and you can hover over it to see what to do. To report a bug, select [i]Create Troubleshooting Report[/i] under [i]Performance Overlay[/i] at the bottom of the [i]Video[/i] settings and go back to the game. For about two minutes it tries each upscaler, frame generation and low latency setting your card supports, then puts your settings back and saves a report with the results and logs as a text file in your [i]BepInEx[/i] folder. Please open it and paste its text into your bug report.[/*]
[/list]

[size=4][b]Recommended mods[/b][/size]

I maintain other graphics enhancing mods that work nicely together with this one:

[list]
[*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/UPSCALING-ID]FSR and XeSS Upscaling[/url]: [i]Adds AMD FSR and Intel XeSS upscaling to the game's own Upscaling setting, next to STP and DLSS.[/i][/*]
[\*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/134]Global Illumination[/url]: [i]Fully customizable and hot-reloadable mod that injects HDRP Screen-Space Global Illumination into the game.[/i][/*]
[\*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/133]All Lights Cast Shadows[/url]: [i]Fully customizable and hot-reloadable mod that allows you to make every light in the game cast real-time shadows! The mod comes with a smart budget system that prioritizes nearby lights and lets you define a max amount of lights to upgrade to keep performance in check.[/i][/*]
[\*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/135]Contact Shadows[/url]: [i]Fully customizable and hot-reloadable mod that adds Contact Shadows to the game, which is a screen-space effect that creates small, detailed shadows at the contact points between objects, greatly improving visual depth and grounding. Includes a smart budget system that prioritizes nearby lights and lets you control how many are active at once.[/i][/*]
[\*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/138]Animation Framerate Unlock[/url]: [i]A mod that most importantly fixes the game's choppy, low-FPS-looking horse animations by moving the affected animators off the 50hz physics tick and onto per rendered frame updates instead so they're linked to the framerate.[/i][/*]
[\*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/139]Conversation Lighting Overhaul[/url]: [i]A mod that enhances NPC conversations with depth of field, zoom, and a shadow-casting portrait spotlight, giving speakers a more cinematic look.[/i][/*]
[\*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/136]Film Grain[/url]: [i]Fully customizable and hot-reloadable mod that injects HDRP Film Grain post-processing into the game, adding a cinematic grain overlay that gives the image a filmic, textured look complementing the game's dark atmosphere.[/i][/*]
[\*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/137]Chromatic Aberration[/url]: [i]Fully customizable and hot-reloadable mod that gives you control over Chromatic Aberration in the game. Adds subtle color fringing toward the edges of the screen, simulating the look of an imperfect camera lens.[/i][/*]
[\*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/145]Better Volumetric Fog[/url]: [i]A mod that fixes the game’s grainy, blocky, low-resolution volumetric fog by increasing the internal volumetric VBuffer quality. Preserves the vanilla look and intensity, while making them much cleaner and higher resolution. Includes simple quality presets and a configurable toggle hotkey. Supports all game versions.[/i][/*]
[/list]
