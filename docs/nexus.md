[size=5][b]Frame Generation[/b][/size]

This mod adds native frame generation to Tainted Grail: The Fall of Avalon, with NVIDIA DLSS, AMD FSR and Intel XeSS. It also adds the matching low latency modes: NVIDIA Reflex, AMD Anti-Lag 2 and Intel XeLL.

It supports the latest features, like DLSS Dynamic Multi-Frame Generation, and is integrated cleanly into the game's existing graphics settings menus. You would never guess it was a mod if you didn't already know!

[img]https://i.imgur.com/QqHVCZZ.jpeg[/img]


Again, it's [i]native[/i] frame generation that generates frames based on the game's own motion vectors and depth, like you know from other games with an official implementation. It's not a driver hack, and it's not going through OptiScaler.

And, it is fully compatible with both RenoDX HDR and RenoDX DLSS 5!

[size=4][b]Part of the game's own graphics settings[/b][/size]

All options live in the game's own [i]Video[/i] settings, right below the upscaler. The menu only offers what your graphics card supports, and rows only show up when they apply to your current choices.

[list]
[*][b]Frame Generation:[/b] Off, DLSS, FSR or XeSS, whichever your card supports.[/*]
[*][b]Frame Multiplier:[/b] How many frames you see per rendered frame. Up to 6x with DLSS on RTX 50 cards and up to 4x with XeSS on Intel Arc cards. FSR, and XeSS on other cards, add one generated frame per rendered frame (2x). The mod asks each technology what your card supports.[/*]
[*][b]Frame Generation Mode:[/b] With DLSS on RTX 50 cards, choose [i]Fixed[/i] for a constant multiplier or [i]Dynamic[/i], which picks the number of generated frames each frame to reach your [i]Target Framerate[/i].[/*]
[*][b]Low Latency:[/b] NVIDIA Reflex (with or without Boost), AMD Anti-Lag 2 or Intel XeLL, whichever your card supports. Generating frames adds a little input delay, so low latency can't be turned off while frame generation is on. DLSS always uses Reflex, and XeSS always uses XeLL.[/*]
[/list]

If you use [i]FoA Mod Manager[/i], it shows the same options with the same choices, and a change in one place shows up in the other.

[size=4][b]Works without DLSS too[/b][/size]

When DLSS upscaling is on, frame generation uses the same motion vectors and depth that DLSS gets. With TAA, STP or no anti-aliasing, it takes them straight from the game's renderer instead. Either way the generated frames follow the game's real motion, and FSR and XeSS frame generation work on AMD and Intel cards too.

I'm also looking into a companion mod that adds FSR and XeSS upscaling to the game.

[size=4][b]Compatibility[/b][/size]

[list]
[*]Fully compatible with [b]RenoDX HDR[/b] and the [b]RenoDX DLSS 5[/b] addon.[/*]
[*]Works with ReShade.[/*]
[*]Works with FoA Mod Manager (see above). Avalon Mod Manager doesn't list the mod, since its options are in the Video settings.[/*]
[/list]

[size=4][b]Installation[/b][/size]

This mod is for the special 'mono' branch of the game built for modding, which you can opt into via Steam.

[size=2][b]Requirements[/b][/size]

[list]
[*]First, install [url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/50]BepInEx 5.4 (Mono/modding branch)[/url] if you haven't already. To install it, you simply need to extract the contents of the file inside your game directory.[/*]
[*]Download this mod from the 'Files' page, and extract the .zip archive into the BepInEx folder from the root of your game directory. (so it becomes e.g. Tainted Grail FoA/[/*]
[/list]

You don't need to download any DLL files yourself. The NVIDIA, AMD and Intel files the mod needs are included in the download, together with their licenses.

This is how the directory should end up looking:

[code]Fall of Avalon.exe
...
BepInEx/
  patchers/
    TGFG.Autoload.dll
  plugins/
    TGFrameGeneration/
      TGFrameGeneration.dll
  TGFrameGeneration/
    Native/
      TGFG.Native.dll
      Streamline/   (NVIDIA)
      FidelityFX/   (AMD)
      XeSS/         (Intel)[/code]

Start the game from Steam as usual. There's no launcher, and you don't need any launch options.

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
[*]The game's HUD is part of the picture that gets generated, so fast-moving HUD elements can smear a little on generated frames.[/*]
[*]Your settings are saved in [b]BepInEx/config/com.wessberg.tgframegeneration.cfg[/b].[/*]
[*]To stop the mod's graphics part from loading at all, create an empty file named [b]disable-autoload.txt[/b] in [b]BepInEx/TGFrameGeneration[/b]. Setting Frame Generation to Off keeps it loaded.[/*]
[*]If something doesn't work, please include [b]BepInEx/TGFrameGeneration/Autoload.log[/b] and [b]BepInEx/TGFrameGeneration/Native/TGFG.Native.log[/b] when you report it.[/*]
[/list]

[size=4][b]Recommended mods[/b][/size]

I maintain other graphics enhancing mods that work nicely together with this one:

[list]
[*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/134]Global Illumination[/url]: [i]Fully customizable and hot-reloadable mod that injects HDRP Screen-Space Global Illumination into the game.[/i][/*]
[*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/133]All Lights Cast Shadows[/url]: [i]Fully customizable and hot-reloadable mod that allows you to make every light in the game cast real-time shadows! The mod comes with a smart budget system that prioritizes nearby lights and lets you define a max amount of lights to upgrade to keep performance in check.[/i][/*]
[*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/135]Contact Shadows[/url]: [i]Fully customizable and hot-reloadable mod that adds Contact Shadows to the game, which is a screen-space effect that creates small, detailed shadows at the contact points between objects, greatly improving visual depth and grounding. Includes a smart budget system that prioritizes nearby lights and lets you control how many are active at once.[/i][/*]
[*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/VOLUMETRIC_FIX_ID]Volumetric Fix[/url]: [i]Fixes the grainy, blocky, low-resolution volumetric fog that is especially noticeable indoors when looking at some bright lamps, candles, and window light shafts.[/i][/*]
[*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/138]Animation Framerate Unlock[/url]: [i]A mod that most importantly fixes the game's choppy, low-FPS-looking horse animations by moving the affected animators off the 50hz physics tick and onto per rendered frame updates instead so they're linked to the framerate.[/i][/*]
[*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/139]Conversation Lighting Overhaul[/url]: [i]A mod that enhances NPC conversations with depth of field, zoom, and a shadow-casting portrait spotlight, giving speakers a more cinematic look.[/i][/*]
[*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/136]Film Grain[/url]: [i]Fully customizable and hot-reloadable mod that injects HDRP Film Grain post-processing into the game, adding a cinematic grain overlay that gives the image a filmic, textured look complementing the game's dark atmosphere.[/i][/*]
[*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/137]Chromatic Aberration[/url]: [i]Fully customizable and hot-reloadable mod that gives you control over Chromatic Aberration in the game. Adds subtle color fringing toward the edges of the screen, simulating the look of an imperfect camera lens.[/i][/*]
[*][url=https://www.nexusmods.com/taintedgrailthefallofavalon/mods/145]Better Volumetric Fog[/url]: [i]A mod that fixes the game’s grainy, blocky, low-resolution volumetric fog by increasing the internal volumetric VBuffer quality. Preserves the vanilla look and intensity, while making them much cleaner and higher resolution. Includes simple quality presets and a configurable toggle hotkey. Supports all game versions.[/i][/*]
[/list]
