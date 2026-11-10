[size=5][b]FSR and XeSS Upscaling[/b][/size]

This mod adds AMD FSR and Intel XeSS upscaling to Tainted Grail: The Fall of Avalon, next to the game's own STP and DLSS. They are two more choices in the game's Upscaling setting. DLSS, FSR and XeSS also get their own quality modes, like Quality, Balanced and Performance, instead of the game's render resolution slider.

FSR and XeSS receive the game's own motion vectors and depth where the game would otherwise run STP. The mod calls AMD's and Intel's runtimes directly.

FSR and XeSS run on NVIDIA, AMD and Intel cards alike. FSR uses the version AMD picks for your card: FSR 4 on Radeon RX 6000 and newer cards and some Ryzen processors, FSR 3.1 on everything else. XeSS runs on any card with Shader Model 6.4, and fastest on Intel Arc cards.

[size=4][b]Part of the game's own graphics settings[/b][/size]

All options live in the game's own [i]Video[/i] settings:

[list]
[*][b]Upscaling:[/b] The game's Upscaling setting, now with FSR and XeSS after None, STP and DLSS. Choices your card can't run are left out, the same way the game leaves out DLSS on cards without it.[/*]
[*][b]Quality Mode:[/b] For DLSS, FSR and XeSS, the upscaler's own quality modes. You only see the modes your card and that upscaler offer, so DLSS can show DLAA, and XeSS has a few more modes than FSR.[/*]
[*][b]Upscaling Quality:[/b] The game's render resolution slider, which STP still uses.[/*]
[*][b]FSR Sharpness:[/b] FSR's own sharpening, from 0% (off) to 100%. It starts at 50%. It only shows up for FSR, since DLSS and XeSS don't have sharpening of their own.[/*]
[/list]

The game remembers your choice like any other setting. If you remove the mod, the game goes back to its own default upscaler.

If you use [i]FoA Mod Manager[/i], it shows the same options with the same choices, and a change in one place shows up in the other.

[size=4][b]Why quality modes instead of the slider[/b][/size]

The game only has a render resolution slider. That's fine for STP, but DLSS, FSR and XeSS are built around their own quality modes, and you get more out of them that way:

[list]
[*][b]The right model for each mode:[/b] NVIDIA's driver picks the DLSS model that works best for each quality mode. With the slider, the game picked the DLSS mode from the slider's position, so the model and the render resolution didn't always match. Now DLSS knows exactly which mode you picked.[/*]
[*][b]The right resolution for each upscaler:[/b] Each vendor recommends its own render resolution for each mode, so the same mode gives you a similar balance of image quality and performance, whichever upscaler you pick. XeSS, for example, costs more than FSR at the same render resolution, but it reconstructs a better image. That's why XeSS Performance renders at 43.5% of your screen's width and height, and FSR Performance at 50%.[/*]
[/list]

The first time you start the game with this version, each upscaler starts on the mode that matches your slider, rounded down, so you won't lose any frames. The game's graphics presets (Low to Ultra) set the modes the same way.

[size=4][b]Newer DLSS[/b][/size]

The mod also includes a newer version of NVIDIA's DLSS for the game's own DLSS upscaling. The game ships with DLSS 3.8.10, and the mod includes 310.9.16. The game uses the newer one automatically, and nothing in your game folder gets replaced. If another mod already put the same or a newer DLSS version in your game folder, the game keeps using that one. The same goes for mods that bring their own set of DLSS files, like DLSS 5: the game keeps using theirs, since they're made to work together.

[size=4][b]How they compare[/b][/size]

I measured all four upscalers against a reference image of the same scene, built from 64 frames at full resolution. This was at 4K with a 33% render resolution on an RTX 5080:

[list]
[*]Standing still, STP and XeSS came closest to the reference, then DLSS, then FSR.[/*]
[*]With the camera moving, DLSS and XeSS kept the most detail, and STP lost the most.[/*]
[*]FSR 3.1 is the softest of the four at such a low render resolution. Turning up FSR Sharpness helps.[/*]
[*]All four ran within about a millisecond of each other.[/*]
[/list]

That's one scene at one setting, so try them yourself. You can switch between them in the Video settings without restarting the game.

[size=4][b]Compatibility[/b][/size]

[list]
[*]Works with my [url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/329]Frame Generation[/url] mod.[/*]
[*]Works with [b]RenoDX HDR[/b] and ReShade.[/*]
[*]The [b]RenoDX DLSS 5[/b] addon only runs while the game's own DLSS upscaling is on, unless you change [i]DirectNeuralRenderingRequireDlss[/i] in its ReShade settings. With FSR or XeSS selected, it stays off.[/*]
[*]Works with FoA Mod Manager (see above). Avalon Mod Manager doesn't list the mod, since its options are in the Video settings.[/*]
[*]OptiScaler isn't supported (see below).[/*]
[/list]

[size=4][b]OptiScaler isn't supported[/b][/size]

This mod doesn't work together with OptiScaler. Both hook into the same parts of the game, like its DLSS and the way it puts frames on the screen, and they get in each other's way. You don't need OptiScaler here anyway. This mod gives you FSR and XeSS upscaling next to the game's own DLSS, and my [url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/329]Frame Generation[/url] mod adds DLSS, FSR and XeSS frame generation. Together they cover what most people use OptiScaler for, natively and in the game's own settings.

If you used OptiScaler before, remove it completely from your game folder before you install this mod:

[list=1]
[*]If you installed OptiScaler through Vortex or another mod manager, uninstall it there. If you used OptiScaler's setup script, run the [i]Remove OptiScaler.bat[/i] it left in your game folder.[/*]
[*]Otherwise, delete the files you copied into the game folder yourself: the renamed OptiScaler DLL (usually [b]dxgi.dll[/b], [b]winmm.dll[/b], [b]version.dll[/b], [b]dbghelp.dll[/b], [b]d3d12.dll[/b], [b]wininet.dll[/b] or [b]winhttp.dll[/b]), [b]OptiScaler.ini[/b], [b]OptiScaler.log[/b], and the files that came with it, like [b]amd_fidelityfx_dx12.dll[/b], [b]amd_fidelityfx_vk.dll[/b], [b]libxess.dll[/b], [b]libxess_dx11.dll[/b], [b]nvngx.dll[/b], [b]fakenvapi.dll[/b], [b]fakenvapi.ini[/b], [b]dlssg_to_fsr3_amd_is_better.dll[/b] and the [b]D3D12_Optiscaler[/b] folder. An [b]nvapi64.dll[/b] in the game folder belongs to it too; the real one lives in Windows.[/*]
[*]Be careful with [b]dxgi.dll[/b], since ReShade usually installs itself under that name as well. Right-click the file, open [i]Properties[/i] and then [i]Details[/i]: if it says ReShade, keep it. Also leave the game's own [b]D3D12[/b] folder alone. Only [b]D3D12_Optiscaler[/b] belongs to OptiScaler.[/*]
[/list]

[size=4][b]Installation[/b][/size]

Download the archive for your game version. The normal version uses BepInEx 6. Use the Mono archive if you switched to the game's Mono modding branch in Steam; that version uses BepInEx 5.

You can use this mod on its own. If you also use my [url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/329]Frame Generation[/url] mod, install both the same way: they share the part they have in common, and neither needs anything else. Vortex won't report a conflict between them. Frame Generation needs to be version 1.1.0 or newer for that; with an older version installed, this mod stays off until you update it.

[size=3][b]Installation for the Mono/modding game version[/b][/size]

[size=2][b]Requirements[/b][/size]

[list]
[*]First, install [url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/50]BepInEx 5.4 (Mono/modding branch)[/url] if you haven't already. Extract it into your game folder.[/*]
[*]Download the [i]TGBetterUpscaling - Mono branch[/i] version of this mod from the 'Files' page, and extract the .zip archive in the root of your game directory.[/*]
[/list]

[size=3][b]Installation for the normal game version[/b][/size]

[size=2][b]Requirements[/b][/size]

[list]
[*]First, install [url=https://builds.bepinex.dev/projects/bepinex_be/755/BepInEx-Unity.IL2CPP-win-x64-6.0.0-be.755%2B3fab71a.zip]BepInEx 6 (normal version)[/url] if you haven't already. Extract it into your game folder.[/*]
[*]Download the [i]TGBetterUpscaling[/i] version of this mod from the 'Files' page, and extract the .zip archive in the root of your game directory.[/*]
[/list]

[quote]I linked the BepInEx 6 build I've used without crashes. For the normal game version, choose [i]BepInEx Unity (IL2CPP) for Windows (x64) games[/i].[/quote]

[quote]Note: If you install via Vortex, make sure the mod type is set to 'Engine Injector' to make sure it is applied from the root of the game directory.[/quote]

You don't need to download any DLL files yourself. The AMD, Intel and NVIDIA files the mod needs are included in the download, together with their licenses.

The installed folders should look like this:

[code]Fall of Avalon.exe
...
BepInEx/
  patchers/
    TGBetterUpscaling.Autoload.dll
  plugins/
    TGBetterUpscaling/
      TGBetterUpscaling.dll
      TGGraphicsBridge.dll
  TGBetterUpscaling/
    graphics-bridge-extension.json
    Native/
      TGBetterUpscaling.Native.dll
      TGGraphicsBridge.Native.dll
      FidelityFX/   (AMD)
      XeSS/         (Intel)
      DLSS/         (NVIDIA)[/code]

[size=4][b]How to use it[/b][/size]

[list=1]
[*]Open [i]Options > Video[/i].[/*]
[*]Pick [i]DLSS[/i], [i]FSR[/i] or [i]XeSS[/i] under [i]Upscaling[/i], and pick a [i]Quality Mode[/i].[/*]
[*]Apply Changes.[/*]
[/list]

[size=4][b]Good to know[/b][/size]

[list]
[*]Upscaling renders the game at a lower resolution and rebuilds the full resolution image, so a lower quality mode gives you more frames per second. Ultra Performance renders at a third of your screen's width and height. DLAA and Native AA render at full resolution and only use the upscaler to smooth edges.[/*]
[*]If FSR or XeSS stops working on your card during a session, the mod switches back to the game's STP for the rest of that session instead of leaving you with a broken image.[/*]
[*][i]Performance Overlay[/i], at the very bottom of the [i]Video[/i] settings, shows your frame rate, frame times, render resolution, CPU, GPU and video memory use, and what the upscaler is doing, in a corner of the screen. You pick the corner and the text size right below it.[/*]
[*]Your settings are saved in [b]BepInEx/config/com.wessberg.tgbetterupscaling.cfg[/b].[/*]
[*]If something doesn't work, open [i]Options > Video[/i] first. If the mod found a problem, the setting it affects has a warning icon, and you can hover over it to see what to do. To report a bug, select [i]Create Troubleshooting Report[/i] under [i]Performance Overlay[/i] at the bottom of the [i]Video[/i] settings and go back to the game. For about two minutes it tries each upscaler your card supports (and each frame generation and low latency setting, if you also use Frame Generation), then puts your settings back and saves a report with the results and logs as a text file in your [i]BepInEx[/i] folder. Please open it and paste its text into your bug report.[/*]
[/list]

[size=4][b]Recommended mods[/b][/size]

I also maintain these graphics mods:

[list]
[*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/329]Frame Generation[/url]: [i]Adds native frame generation to the game, with NVIDIA DLSS, AMD FSR and Intel XeSS, and the matching low latency modes: NVIDIA Reflex, AMD Anti-Lag 2 and Intel XeLL.[/i][/*]
[*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/134]Global Illumination[/url]: [i]Adds HDRP screen-space global illumination. Settings reload while the game runs.[/i][/*]
[*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/133]All Lights Cast Shadows[/url]: [i]Makes lights cast real-time shadows. You can limit how many lights are upgraded, with nearby lights taking priority.[/i][/*]
[*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/135]Contact Shadows[/url]: [i]Adds screen-space shadows where objects meet. Nearby lights take priority, and you can limit how many are active.[/i][/*]
[*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/138]Animation Framerate Unlock[/url]: [i]Updates affected animations with each rendered frame, removing the 50 Hz limit that makes horse movement look choppy.[/i][/*]
[*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/139]Conversation Lighting Overhaul[/url]: [i]Adds depth of field, zoom and a shadow-casting portrait light to NPC conversations.[/i][/*]
[*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/136]Film Grain[/url]: [i]Adds adjustable HDRP film grain, with settings that reload while the game runs.[/i][/*]
[*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/137]Chromatic Aberration[/url]: [i]Controls colour fringing near the edges of the screen. Settings reload while the game runs.[/i][/*]
[*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/145]Better Volumetric Fog[/url]: [i]A mod that fixes the game’s grainy, blocky, low-resolution volumetric fog by increasing the internal volumetric VBuffer quality. Preserves the vanilla look and intensity, while making them much cleaner and higher resolution. Includes simple quality presets and a configurable toggle hotkey. Supports all game versions.[/i][/*]
[/list]
