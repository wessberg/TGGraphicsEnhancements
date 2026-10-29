# Can our D3D12 device open the game's own D3D11 textures, with no copy?

Agent report, 2026-10-06 evening. Measured offline with a small probe program; nothing in the game or the mods changed.

## Verdict (three lines)

1. Yes, on this PC (RTX 5080, driver 32.0.16.1692): every kind of texture a game makes (BC1 to BC7, RGBA8, sRGB, all
   mips, arrays, cube maps) opened on our D3D12 device with no copy, and every mip read back bit-exact: 324 of 327 cases.
2. It takes the old sharing flag (`D3D11_RESOURCE_MISC_SHARED` alone). The newer one, with an NT handle, refuses BC4,
   BC5, BC6H, BC7 and cube maps. The flag can only be set as the game creates the texture: a hook, tested in the probe.
3. Cost: no extra memory for textures over 64 KB (small ones round up to 64 KB), same sampling speed, about 0.13 ms
   to open each one. Windows counts an opened texture twice for the game, but the card holds it once. AMD and Intel
   are untested, and so is the game itself.

## Words used here

- **D3D11 / D3D12**: Direct3D 11 is what the game draws with; Direct3D 12 is our tracer's own device on the same GPU.
- **BC1 ... BC7**: the compressed formats games store textures in. BC1 and BC3 colour, BC4 one channel (masks),
  BC5 two channels (normal maps), BC6H bright light (sky, light probes), BC7 high-quality colour. **RGBA8**: plain
  8 bits per channel. **sRGB**: the same bytes, read with the gamma curve. **Typeless**: a texture that can be read
  either way.
- **Mip**: a smaller copy of a texture (half, quarter, ...) used for far-away surfaces. **Cube map**: six square
  faces, for sky and reflections.
- **Sharing flag**: a bit set when a texture is born that lets another device open it. **Handle**: the ticket D3D11
  hands out for a shared texture; D3D12 trades it in for its own view of the same memory. The new kind (**NT
  handle**) is a Windows kernel object that must be closed; the old kind is just a number.
- **Fence**: a counter one device raises and the other waits for, so both agree on what happened first.
- **Vtable slot**: the position of a function in a Direct3D object's table of functions. A hook swaps that function
  for ours.

## The probe and the machine

`share-probe` (C++, VS 2022 x64), one D3D11 device at feature level 11_1 and one D3D12 device on the same adapter.
Source and raw output: `%TEMP%\claude\...\scratchpad\share-probe\` (`probe.cpp`, `results-*.txt`; a scratch folder,
copy it if it should be kept). It ran only while the GPU was free (no game, no harness run, `-GpuCheck` quiet).

What the runtime reports:

| | |
|---|---|
| Adapter | NVIDIA GeForce RTX 5080, 16 GB, driver 32.0.16.1692, Windows 11 10.0.26200 |
| D3D11 `ExtendedResourceSharing` | 1 (yes) |
| D3D11 `SharedResourceTier` | 3 (the highest) |
| D3D11 `ExtendedNV12SharedTextureSupported` | 1 |
| D3D11 "shareable" per format (`FORMAT_SUPPORT2_SHAREABLE`) | yes for BC1, BC3, RGBA8 (all variants); **no** for BC4, BC5, BC6H, BC7 |
| D3D12 `SharedResourceCompatibilityTier` | 2 (the highest) |
| D3D12 `UnalignedBlockTexturesSupported` | 1 |

The "no" for BC4 to BC7 is about the new flag. The old flag shares them anyway on this driver (the matrix).

## How the game makes its textures (the harness's mirror run, 21:44)

From `artifacts/harness/20261006-214300/mirror-tavern/2-mirror-reference/textures.txt`. Only 2 textures were within
40 m there (most of the world is drawn by the game's own renderers; a fuller count comes in a later run):

- usage DEFAULT (not IMMUTABLE), bind 0x8 (read by shaders, nothing else), no CPU access, misc 0 (no flags at all);
- formats 77 and 78: BC3 and BC3 sRGB, typed (not typeless);
- one with all its mips, one with a single mip;
- Unity's mip streaming is on (budget 1548 MB); both stream their mips, one had its top mip not loaded at that moment.

That is the plainest way Direct3D 11 makes a texture, and both flags share it.

## The matrix: one line per texture

Each texture filled with random bytes (as many as its format takes), made on D3D11 with a sharing flag, its handle
opened on D3D12, then checked four ways. **yes** means all four passed:

1. D3D11 created it with the flag;
2. D3D12 opened the handle (and sees the same size, mips, slices and format);
3. D3D12 made a view over all mips (a 2D, 2D-array or cube view as fits), with no debug-layer warning, and a shader
   reading every texel of every mip through it got exactly what the same shader got through D3D11 (sRGB and both
   views of a typeless texture included);
4. D3D12 copied every mip out to a readback buffer, and every byte equals what D3D11 was given. D3D11's own copy was
   checked too.

Debug layers were on for D3D11 and D3D12. Three ways to give the data, like a game does: at creation (DEFAULT or
IMMUTABLE), or later with `UpdateSubresource`. "New flag" is `SHARED | SHARED_NTHANDLE` (0x802) with an NT handle,
"old flag" is `SHARED` (0x2) with `IDXGIResource::GetSharedHandle`. **as DEFAULT: yes** = the new flag refuses
IMMUTABLE ("SHARED_NTHANDLE requires D3D11_USAGE_DEFAULT"), and all four pass once the usage is DEFAULT. **no** =
D3D11 refuses the flag ("The Format ... is not supported as a D3D11_RESOURCE_MISC_SHARED_NTHANDLE format", and for
cubes "SHARED_NTHANDLE and TEXTURECUBE are mutually exclusive"); the same texture without the flag works. **not
valid** = not a legal texture even unshared (a BC texture whose size isn't a multiple of 4).

| format | size | mips | slices | new flag: DEFAULT + data | new flag: IMMUTABLE + data | new flag: DEFAULT, data later | old flag: DEFAULT + data | old flag: IMMUTABLE + data | old flag: DEFAULT, data later |
|---|---|---|---|---|---|---|---|---|---|
| BC1_UNORM | 2048x2048 | 12 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_UNORM | 2048x2048 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_UNORM | 1024x512 | 11 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_UNORM | 1024x512 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_UNORM | 1000x600 | 10 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_UNORM | 1000x600 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_UNORM | 1024x512 | 11 | 2 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_SRGB | 2048x2048 | 12 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_SRGB | 2048x2048 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_SRGB | 1024x512 | 11 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_SRGB | 1024x512 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_SRGB | 1000x600 | 10 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_SRGB | 1000x600 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_SRGB | 1024x512 | 11 | 2 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_TYPELESS | 2048x2048 | 12 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_TYPELESS | 2048x2048 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_TYPELESS | 1024x512 | 11 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_TYPELESS | 1024x512 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_TYPELESS | 1000x600 | 10 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_TYPELESS | 1000x600 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_TYPELESS | 1024x512 | 11 | 2 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_UNORM | 2048x2048 | 12 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_UNORM | 2048x2048 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_UNORM | 1024x512 | 11 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_UNORM | 1024x512 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_UNORM | 1000x600 | 10 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_UNORM | 1000x600 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_UNORM | 1024x512 | 11 | 2 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_SRGB | 2048x2048 | 12 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_SRGB | 2048x2048 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_SRGB | 1024x512 | 11 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_SRGB | 1024x512 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_SRGB | 1000x600 | 10 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_SRGB | 1000x600 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_SRGB | 1024x512 | 11 | 2 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_TYPELESS | 2048x2048 | 12 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_TYPELESS | 2048x2048 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_TYPELESS | 1024x512 | 11 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_TYPELESS | 1024x512 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_TYPELESS | 1000x600 | 10 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_TYPELESS | 1000x600 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_TYPELESS | 1024x512 | 11 | 2 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC4_UNORM | 2048x2048 | 12 | 1 | no | no | no | yes | yes | yes |
| BC4_UNORM | 2048x2048 | 1 | 1 | no | no | no | yes | yes | yes |
| BC4_UNORM | 1024x512 | 11 | 1 | no | no | no | yes | yes | yes |
| BC4_UNORM | 1024x512 | 1 | 1 | no | no | no | yes | yes | yes |
| BC4_UNORM | 1000x600 | 10 | 1 | no | no | no | yes | yes | yes |
| BC4_UNORM | 1000x600 | 1 | 1 | no | no | no | yes | yes | yes |
| BC4_UNORM | 1024x512 | 11 | 2 | no | no | no | yes | yes | yes |
| BC5_UNORM | 2048x2048 | 12 | 1 | no | no | no | yes | yes | yes |
| BC5_UNORM | 2048x2048 | 1 | 1 | no | no | no | yes | yes | yes |
| BC5_UNORM | 1024x512 | 11 | 1 | no | no | no | yes | yes | yes |
| BC5_UNORM | 1024x512 | 1 | 1 | no | no | no | yes | yes | yes |
| BC5_UNORM | 1000x600 | 10 | 1 | no | no | no | yes | yes | yes |
| BC5_UNORM | 1000x600 | 1 | 1 | no | no | no | yes | yes | yes |
| BC5_UNORM | 1024x512 | 11 | 2 | no | no | no | yes | yes | yes |
| BC6H_UF16 | 2048x2048 | 12 | 1 | no | no | no | yes | yes | yes |
| BC6H_UF16 | 2048x2048 | 1 | 1 | no | no | no | yes | yes | yes |
| BC6H_UF16 | 1024x512 | 11 | 1 | no | no | no | yes | yes | yes |
| BC6H_UF16 | 1024x512 | 1 | 1 | no | no | no | yes | yes | yes |
| BC6H_UF16 | 1000x600 | 10 | 1 | no | no | no | yes | yes | yes |
| BC6H_UF16 | 1000x600 | 1 | 1 | no | no | no | yes | yes | yes |
| BC6H_UF16 | 1024x512 | 11 | 2 | no | no | no | yes | yes | yes |
| BC7_UNORM | 2048x2048 | 12 | 1 | no | no | no | yes | yes | yes |
| BC7_UNORM | 2048x2048 | 1 | 1 | no | no | no | yes | yes | yes |
| BC7_UNORM | 1024x512 | 11 | 1 | no | no | no | yes | yes | yes |
| BC7_UNORM | 1024x512 | 1 | 1 | no | no | no | yes | yes | yes |
| BC7_UNORM | 1000x600 | 10 | 1 | no | no | no | yes | yes | yes |
| BC7_UNORM | 1000x600 | 1 | 1 | no | no | no | yes | yes | yes |
| BC7_UNORM | 1024x512 | 11 | 2 | no | no | no | yes | yes | yes |
| BC7_SRGB | 2048x2048 | 12 | 1 | no | no | no | yes | yes | yes |
| BC7_SRGB | 2048x2048 | 1 | 1 | no | no | no | yes | yes | yes |
| BC7_SRGB | 1024x512 | 11 | 1 | no | no | no | yes | yes | yes |
| BC7_SRGB | 1024x512 | 1 | 1 | no | no | no | yes | yes | yes |
| BC7_SRGB | 1000x600 | 10 | 1 | no | no | no | yes | yes | yes |
| BC7_SRGB | 1000x600 | 1 | 1 | no | no | no | yes | yes | yes |
| BC7_SRGB | 1024x512 | 11 | 2 | no | no | no | yes | yes | yes |
| BC7_TYPELESS | 2048x2048 | 12 | 1 | no | no | no | yes | yes | yes |
| BC7_TYPELESS | 2048x2048 | 1 | 1 | no | no | no | yes | yes | yes |
| BC7_TYPELESS | 1024x512 | 11 | 1 | no | no | no | yes | yes | yes |
| BC7_TYPELESS | 1024x512 | 1 | 1 | no | no | no | yes | yes | yes |
| BC7_TYPELESS | 1000x600 | 10 | 1 | no | no | no | yes | yes | yes |
| BC7_TYPELESS | 1000x600 | 1 | 1 | no | no | no | yes | yes | yes |
| BC7_TYPELESS | 1024x512 | 11 | 2 | no | no | no | yes | yes | yes |
| RGBA8_UNORM | 2048x2048 | 12 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_UNORM | 2048x2048 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_UNORM | 1024x512 | 11 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_UNORM | 1024x512 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_UNORM | 1000x600 | 10 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_UNORM | 1000x600 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_UNORM | 1024x512 | 11 | 2 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_SRGB | 2048x2048 | 12 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_SRGB | 2048x2048 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_SRGB | 1024x512 | 11 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_SRGB | 1024x512 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_SRGB | 1000x600 | 10 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_SRGB | 1000x600 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_SRGB | 1024x512 | 11 | 2 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_TYPELESS | 2048x2048 | 12 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_TYPELESS | 2048x2048 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_TYPELESS | 1024x512 | 11 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_TYPELESS | 1024x512 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_TYPELESS | 1000x600 | 10 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_TYPELESS | 1000x600 | 1 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_TYPELESS | 1024x512 | 11 | 2 | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC6H_UF16 | 256x256 | 9 | 6 (cube) | no | no | no | yes | yes | yes |
| RGBA8_SRGB | 256x256 | 9 | 6 (cube) | no | no | no | yes | yes | yes |
| BC7_SRGB | 1001x601 | 10 | 1 | not valid | not valid | not valid | not valid | not valid | not valid |
| RGBA8_SRGB | 1001x601 | 10 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |

In short: the old flag gives yes in every valid case, IMMUTABLE and cube maps included, with no change to the
texture's usage. The new flag covers only BC1, BC3 and RGBA8, and only as DEFAULT.

Two other ways were tried for the formats the new flag refuses:

- A texture made on D3D12 (shared heap) and opened by D3D11 with `OpenSharedResource1`: refused (E_INVALIDARG) for
  every format, BC3 too. Not pursued: the old flag already covers everything.
- The old flag: D3D11 creates BC4, BC5, BC6H, BC7 and cube maps with it, and D3D12 opens them (the table's right
  half).

## The cost

### Video memory

Measured with `IDXGIAdapter3::QueryVideoMemoryInfo` over batches of textures (each texture filled at creation).
The fair baseline is the unshared IMMUTABLE batch: the unshared DEFAULT batches read low because the driver reused
memory the previous batch had just freed.

| texture (texels each) | count | unshared | shared, new flag | shared, old flag |
|---|---|---|---|---|
| BC3 sRGB 2048, all mips (5461 KB) | 100 | 5504 KB each | 5504 KB | - |
| BC7 sRGB 2048, all mips (5461 KB) | 100 | 5504 KB | refused | 5504 KB (DEFAULT and IMMUTABLE) |
| RGBA8 sRGB 2048, all mips (21845 KB) | 25 | 21888 KB | 21888 KB | - |
| BC1 sRGB 512, all mips (171 KB) | 500 | 192 KB | 192 KB | - |
| BC3 / BC7 sRGB 128, all mips (21 KB) | 4000 | 24 KB | 64 KB (BC3) | 64 KB (BC7) |
| BC1 64x64, one mip (2 KB) | 4000 | 2 KB | 64 KB | 64 KB |

A shared texture is a memory block of its own, in steps of 64 KB. Textures over 64 KB cost the same as unshared.
Small ones round up to 64 KB: 4000 tiny textures took 250 MB instead of 8 to 94 MB.

### Real memory against counted memory

40 BC3 (new flag) and 40 BC7 (old flag) textures, 2048 with all mips, 213 MB of texels per set, opened on D3D12.
Three readings: DXGI's count for the process, Windows' per-process counter (what Task Manager shows), and the whole
card's memory in use:

| | DXGI's count | Windows, this process | the whole card |
|---|---|---|---|
| D3D11 and D3D12 both hold them | +430 MB | +430 MB | **+216 MB** (BC3), **+216 MB** (BC7) |
| after D3D11 let go | +215 MB | (lags) | +216 MB (BC7): still there |
| after D3D12 let go too | 0 | (lags) | +2.5 MB (BC7): freed |

(The BC3 set's last two card readings moved by +45 and +1671 MB: another program allocated at the time.)

So: the card holds a shared texture once, but Windows counts it once per device that has it open. And D3D12's
reference keeps the memory alive after the game lets go. A small object attached to each texture
(`SetPrivateDataInterface`) told us when D3D11 let go: 40 of 40 notices, in both sets.

### Time and handles

| step | per texture | 4000 small textures |
|---|---|---|
| create with a flag, large texture (2048) | 0.35 to 0.38 ms, same as unshared | |
| create with a flag, small texture | 0.06 to 0.08 ms (unshared 0.005 to 0.024) | +0.25 to 0.3 s in all |
| new flag: `CreateSharedHandle` | 0.001 to 0.004 ms | 5 ms |
| old flag: `GetSharedHandle` | under 0.001 ms | 1 ms |
| D3D12 `OpenSharedHandle` | 0.10 to 0.15 ms | 0.46 to 0.51 s |

Kernel handles: the new flag adds one per texture until it is closed (4622 -> 8622, then back to 4622 after
`CloseHandle`). The old flag adds none (4629 -> 4629). No limit or failure at 4000.

### Sampling speed in D3D11 (rough)

A compute shader samples a 2048 texture 16 times per thread over 2048x2048 threads, trilinear filtering; median of
25 runs, timed with D3D11 timestamp queries. Other programs share the GPU, so treat 10 % as noise. Two runs:

| texture | unshared IMMUTABLE | unshared DEFAULT | new flag | old flag DEFAULT | old flag IMMUTABLE |
|---|---|---|---|---|---|
| BC3, neighbouring texels (run 2 / run 3) | 0.089 / 0.091 ms | 0.089 / 0.090 | 0.089 / 0.090 | - / 0.090 | - / 0.091 |
| BC3, scattered texels | 1.749 / 1.757 ms | 1.752 / 1.757 | 1.749 / 1.913 | - / 1.763 | - / 1.761 |
| BC7, neighbouring | 0.089 / 0.090 ms | 0.089 / 0.090 | refused | - / 0.090 | - / 0.090 |
| BC7, scattered | 1.749 / 1.757 ms | 1.750 / 1.757 | refused | - / 1.949 | - / 1.759 |
| RGBA8, neighbouring | 0.094 / 0.097 ms | 0.094 / 0.097 | 0.094 / 0.097 | - / 0.096 | - / 0.096 |
| RGBA8, scattered | 2.100 / 3.321 ms | 2.099 / 3.684 | 2.099 / 3.087 | - / 2.680 | - / 3.439 |

The quiet run (run 2) gives identical numbers. In run 3 the scattered numbers jump around for unshared textures as
much as for shared ones (RGBA8 unshared: 2.10 to 3.68 ms). No sign that sharing slows the game's own drawing.

### The hook, tried on the probe's own device

`ID3D11Device` vtable slot 5 (`CreateTexture2D`) swapped for a function that adds the flag when the texture qualifies:

- an IMMUTABLE BC3 sRGB 1024 texture made through the normal call came out shared (usage DEFAULT, misc 0x802); its
  handle opened on D3D12;
- a render target made through the same call was left alone;
- an IMMUTABLE BC7 texture, which the new flag refuses, still reached the caller unshared: the fallback works;
- `ID3D11Device3::CreateTexture2D1` (slot 54) does **not** pass through slot 5.

That run used the new flag. With the old flag the hook only adds 0x2 and changes nothing else.

## How it would go into the game (design only, nothing wired)

1. **Catch the creation.** The flag can only be set when a texture is born; it can't be added later. The bridge
   already sees the game's D3D11 device the moment the game makes its swapchain (`native/bridge/src/device.cpp`
   `observe_game_device`, `hooks.cpp` `combined_hook`), long before the game loads a level. There, hook
   `ID3D11Device::CreateTexture2D`, **vtable slot 5** (0-2 are QueryInterface/AddRef/Release, 3 CreateBuffer, 4
   CreateTexture1D), with the bridge's own `add_vtable_hook(device, 5, detour)`, the way frame generation already
   hooks slot 15 (`CreatePixelShader`). It patches the function the slot points to, which every D3D11 device in the
   process shares, ours included: step aside under the bridge's `InternalGuard`. Hook `CreateTexture2D1` (**slot 54**)
   too, since it bypasses slot 5. Log once which of the two the game calls.
2. **Which textures get the flag.** Only those that are read by shaders and nothing else: usage DEFAULT or IMMUTABLE;
   bind flags exactly SHADER_RESOURCE (0x8); no CPU access; one sample; misc flags none except TEXTURECUBE; larger
   than 64x64. Leave alone: render targets and depth (the game draws into them; they aren't material textures), UAVs,
   staging and dynamic textures, anything already shared, anything with GENERATE_MIPS. Skip 64x64 and smaller because
   the tracer's atlas already holds those at full size (its tiles are 64x64) and sharing would round each up to 64 KB.
   Both of the game's textures in the harness sample qualify.
3. **What changes in the description.** `MiscFlags |= D3D11_RESOURCE_MISC_SHARED` (0x2). Nothing else: usage, format,
   size and mips stay as the game asked. If the creation fails with the flag, call again without it: the game never
   sees a failure, and the tracer keeps its atlas copy for that one texture.
4. **How D3D12 learns of each texture.** Nothing happens at creation beyond the flag: no handle, no D3D12 work. The
   tracer already gets each material's textures from C# (`Texture.GetNativeTexturePtr` gives the `ID3D11Texture2D`).
   The first time the tracer needs one: `IDXGIResource::GetSharedHandle` (a number, nothing to close), then
   `ID3D12Device::OpenSharedHandle`, on a worker thread (the D3D12 device allows that; about 0.13 ms each). Keep the
   `ID3D12Resource` and a shader view in a table keyed by the D3D11 pointer. The tracer's shaders then read textures
   by their number in that table ("bindless" textures) instead of a tile in the atlas.
5. **Knowing when the game frees a texture.** D3D12's reference keeps the memory alive after the game lets go
   (measured above). So attach a tiny object to each opened texture with `ID3D11Texture2D::SetPrivateDataInterface`
   (our own GUID). D3D11 releases it when the texture dies, and its `Release` tells the tracer to drop the D3D12 side
   once the GPU work that read it is done. Without that, every texture the streaming swaps out would leak.
6. **When it is safe to read.** D3D11 and D3D12 run on separate queues and only agree on order through a fence. The
   bridge has one (`CrossApiFence` in `d3d_interop.hpp`), and the tracer already waits on the game's frame before it
   traces. Data given at creation, or written later with `UpdateSubresource` or `CopySubresourceRegion`, is visible
   to any D3D12 work that waits on a fence value D3D11 signalled after it (the probe synchronised exactly so). Rule:
   the hook stamps each texture with a counter, and the tracer uses a texture only from the first frame whose fence
   signal came after that stamp. Textures can be created on loading threads mid-frame. The opened texture starts in
   D3D12's COMMON state, so reading it in a shader needs no barrier. It falls back to COMMON when the command list
   ends, which is the state D3D11 expects.
7. **Mip streaming** (on in this game, budget 1548 MB). Unity keeps only the mips the camera needs and loads the large
   ones when the camera comes near, and drops them again when memory is short. A share belongs to one D3D11 texture
   object, so what matters is whether Unity writes the new mips into the same object or makes a new one.
   - What I expect (from how D3D11 works, not measured here): a new object. A D3D11 texture can't change its size or
     mip count after it is made. To go from "top mip missing" (1024 wide, 11 mips) to "all mips" (2048, 12), Unity
     has to make a new texture, copy the mips it has on the GPU and upload the new one; dropping mips is the same in
     reverse. That the game makes its textures DEFAULT rather than IMMUTABLE fits this, since an IMMUTABLE texture
     can't be written after it is born. A full-size texture with its large mips left empty would save no memory,
     which is the whole point of streaming.
   - With a new object each time, the new texture is born through the same hook, so it gets the flag
     automatically. What the tracer must follow is the pointer: `GetNativeTexturePtr` returns the new one after the
     swap. From C#, watch each traced texture's `Texture2D.loadedMipmapLevel` (and `Texture.updateCount`) every
     frame; both are cheap to read. When either changes, ask `GetNativeTexturePtr` again and open the new texture.
     The old one's death notice (step 5) drops its D3D12 side. A camera move that streams mips means a few such
     reopenings per frame, not thousands.
   - With the same object (if I am wrong), there is nothing to do. The new mips are in place and visible to D3D12
     after the next fence, and the shader's view already covers every mip.
   - The design works either way. To settle it in game (a harness step, no change to the tracer): for a few streamed
     textures, log `GetNativeTexturePtr` and the D3D11 description (width, mips) with `loadedMipmapLevel`. Then set
     `Texture2D.requestedMipmapLevel = 0`, wait until `loadedMipmapLevel` is 0, and log again. Same pointer and same
     width: in place. New pointer and a larger width: a new object.
   - Either way a mirror shows what the game has loaded: a texture still on its small mips looks as soft in the mirror
     as it does on screen. The tracer could ask for the top mip of what a mirror sees (`requestedMipmapLevel`), within
     the game's own streaming budget.
8. **When the hook is in.** Because the flag is set at birth, the hook must be in whenever TG Path Tracing is
   installed, even while path tracing is off: a texture born unshared stays unshared until the game makes it again.
   With path tracing off, the only cost is the 64 KB rounding of textures just over 64x64. Nothing is opened, so
   nothing is counted twice.

## Risks

- **One GPU, one driver.** All of this is NVIDIA (RTX 5080, 32.0.16.1692). That the old flag takes BC4 to BC7 and cube
  maps goes beyond what the driver advertises. AMD or Intel may refuse some formats, or refuse to open an old-style
  handle in D3D12. The hook's fallback keeps the game working, and the tracer keeps its atlas copy for whatever isn't
  shared. It needs a run of `share-probe matrix` and `share-probe legacy` on an AMD card before it is promised to
  players.
- **Counted twice.** While D3D12 has a texture open, Windows counts it once for each device in the game's process
  (real memory once). If Windows or the driver acts on the counted number, a nearly full card might see textures
  moved to system memory, which shows as stutter. Not measured. Opening only what the tracer reads keeps the count
  down.
- **Old handles are global.** Any program on the PC that guesses the number could open the texture. Game textures
  aren't secret. Microsoft recommends the NT handles for new code, but those refuse the formats we need.
- **Leaks.** A D3D12 reference that is never dropped keeps the texture's memory after the game frees it (measured).
  The death notice in step 5 is not optional.
- **Threads.** Unity may create textures on loading threads. The hook must only change the description and stamp a
  counter: no use of the game's immediate context, no D3D12 calls.
- **Other mods that wrap the device** (ReShade, RenoDX): the hook sits on d3d11's own function underneath them, so it
  still sees the game's textures. Textures a wrapper shares itself are skipped (already shared).
- **The tracer's side is the real work, and this probe doesn't touch it.** Shaders that read thousands of textures by
  number, pick the right mip by distance (ray cones), and decode Unity's normal-map packing (BC3 "DXT5nm") instead of
  the atlas's baked colour. Its frame-time cost is unknown.

## Still unknown

- **The game's full texture list**: which formats besides BC3 (BC7? BC5 normals? BC6H cube maps?), how many, and how
  many are 64x64 or smaller. The table covers all of these formats, so this decides the memory overhead, not whether
  sharing works. The fuller `textures.txt` run answers it.
- **Mip streaming**: whether Unity 6 on D3D11 makes a new texture object when it streams mips in or out (design step 7
  has the harness step that settles it).
- **`CreateTexture2D` or `CreateTexture2D1`**: which one the game calls (one log line from each hook).
- **AMD and Intel** drivers.
- **Whether the double count matters** to Windows' memory manager on a card that is nearly full.
- **The in-game memory overhead** over the game's real texture count, and the tracer-side frame time of bindless
  textures.

## Other cards

Agent report, 2026-10-06 night to 2026-10-07. The same probe, now able to pick the GPU, run on the second GPU in this
PC and on Windows' own software renderer, plus what Microsoft documents and what other programs ran into. It answers,
in part, the "AMD and Intel drivers" line of "Still unknown" above. Nothing in the game or the mods changed.

### Verdict (three lines)

1. **NVIDIA: tested here, not documented.** RTX 5080 (driver 32.0.16.1692): 324 of 324 valid cases bit-exact with the
   old flag (the sections above). RTX 20, 30 and 40 use the same driver package, so likely the same; not measured.
2. **AMD: tested here, not documented.** The Ryzen 9 9950X3D's built-in Radeon (RDNA 2, driver 32.0.21043.5001) gives
   exactly the RTX's table: 324 of 324, no debug-layer warning, memory freed, 26 of 26 in the self-test. RX 7000 and
   RX 9000 run AMD's other driver branch: not measured.
3. **Intel: unknown.** No Intel GPU here, no document, and Chrome's only D3D11-to-D3D12 sharing workaround is for Intel
   Core Ultra GPUs (a corrupted picture). Microsoft promises nothing for compressed textures on any card, so the mod
   must test sharing on the player's own card at start and fall back per format (the plan at the end).

Windows' software renderer (WARP) gives the same table as both cards. So what is allowed is Windows' rule, the same on
every card; whether the bytes come out right is each driver's own work.

**Which cards matter.** The sharing is for TG Path Tracing, and the tracer refuses any card without DirectX Raytracing
tier 1.1 (`native/pathtracing/src/tracer.cpp`, the `RaytracingTier` check). That leaves NVIDIA RTX 20 and newer, AMD
RX 6000 and newer (and built-in Radeons of RDNA 2 or newer, like this one), and Intel's Arc cards and Core Ultra GPUs
with ray tracing. Older cards (GTX, RX 5000 and older) can't run the tracer, so the hook should never switch on there.

### Words used here

- **Driver**: the GPU maker's software that turns Direct3D calls into work for the card. NVIDIA, AMD and Intel each
  write their own; the D3D11 and D3D12 halves are separate pieces of it.
- **Integrated GPU**: a small GPU inside the CPU that uses the PC's main memory. Its driver is the same one AMD ships
  for its graphics cards of the same generation.
- **WARP**: Microsoft's software GPU, part of Windows. It runs Direct3D on the CPU. It has no maker's driver in the
  way, so it shows what Windows' own Direct3D layer (the "runtime") allows.
- **Bit-exact**: every byte D3D12 read is the byte D3D11 was given.

### What ran

`S\share-probe-other\` (S = this session's scratchpad): a copy of `share-probe` with a second argument that picks the
adapter (`Radeon`, `vendor:0x1002`, `warp`, `warpdt`; both devices are made on that adapter, and the log checks they
are), memory counters limited to that adapter (dedicated plus shared memory, since an integrated GPU lives in main
memory), a CPU wait in place of the shared fence where an adapter has none (none needed it), and a `selftest` step
(below). The same matrix, cost and lifetime steps as on the RTX; on WARP a `warpcost` step without the sampling test
(WARP runs on the CPU, so its speed says nothing). Each step ran only while no game, no harness run and no other GPU
work was going. Raw output: `results-<step>-<adapter>.txt` in that folder.

What the runtime reports is the same on all three:

| | RTX 5080 | Radeon (RDNA 2, integrated) | WARP |
|---|---|---|---|
| Driver | 32.0.16.1692 | 32.0.21043.5001 | 10.0.26100.9278 (Windows) |
| D3D11 and D3D12 device on the same adapter | yes | yes | yes (WARP through D3D_DRIVER_TYPE_WARP and through EnumWarpAdapter is the same adapter) |
| Shared fence, D3D12 to D3D11 | works | works | works |
| D3D11 `ExtendedResourceSharing` / `SharedResourceTier` | 1 / 3 | 1 / 3 | 1 / 3 |
| D3D12 `SharedResourceCompatibilityTier` | 2 | 2 | 2 |
| "Shareable" per format (new flag) | BC1, BC3, RGBA8 yes; BC4 to BC7 no | the same | the same |

The new flag's refusal of BC4 to BC7 and cubes is identical everywhere, WARP included, and its message comes from the
runtime: it is Windows' rule, not NVIDIA's. The old flag is not checked by Windows at all (Microsoft's words below), so
whether it works is up to each driver.

### The matrix on the Radeon

Same cases, same four checks, debug layers on. "7 shapes" = 2048x2048, 1024x512 and 1000x600, each with all mips and
with one mip, plus 1024x512 with all mips and 2 slices. Every row below is identical to the RTX's row; a script
compared the two tables case by case.

| format | size | mips | slices | new flag: DEFAULT + data | new flag: IMMUTABLE + data | new flag: DEFAULT, data later | old flag: DEFAULT + data | old flag: IMMUTABLE + data | old flag: DEFAULT, data later |
|---|---|---|---|---|---|---|---|---|---|
| BC1_UNORM | 7 shapes | | | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_SRGB | 7 shapes | | | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_TYPELESS | 7 shapes | | | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_UNORM | 7 shapes | | | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_SRGB | 7 shapes | | | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_TYPELESS | 7 shapes | | | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC4_UNORM | 7 shapes | | | no | no | no | yes | yes | yes |
| BC5_UNORM | 7 shapes | | | no | no | no | yes | yes | yes |
| BC6H_UF16 | 7 shapes | | | no | no | no | yes | yes | yes |
| BC7_UNORM | 7 shapes | | | no | no | no | yes | yes | yes |
| BC7_SRGB | 7 shapes | | | no | no | no | yes | yes | yes |
| BC7_TYPELESS | 7 shapes | | | no | no | no | yes | yes | yes |
| RGBA8_UNORM | 7 shapes | | | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_SRGB | 7 shapes | | | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_TYPELESS | 7 shapes | | | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC6H_UF16 | 256x256 | 9 | 6 (cube) | no | no | no | yes | yes | yes |
| RGBA8_SRGB | 256x256 | 9 | 6 (cube) | no | no | no | yes | yes | yes |
| BC7_SRGB | 1001x601 | 10 | 1 | not valid | not valid | not valid | not valid | not valid | not valid |
| RGBA8_SRGB | 1001x601 | 10 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |

Old flag: 324 of 324 valid cases bit-exact (the 3 "not valid" are invalid unshared too). New flag: 192 of 327, the
same refusals as on the RTX. A texture made on D3D12 and opened by D3D11 (`OpenSharedResource1`): refused
(E_INVALIDARG) for every format, as on the RTX.

### The matrix on WARP

| format | size | mips | slices | new flag: DEFAULT + data | new flag: IMMUTABLE + data | new flag: DEFAULT, data later | old flag: DEFAULT + data | old flag: IMMUTABLE + data | old flag: DEFAULT, data later |
|---|---|---|---|---|---|---|---|---|---|
| BC1_UNORM | 7 shapes | | | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_SRGB | 7 shapes | | | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC1_TYPELESS | 7 shapes | | | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_UNORM | 7 shapes | | | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_SRGB | 7 shapes | | | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC3_TYPELESS | 7 shapes | | | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC4_UNORM | 7 shapes | | | no | no | no | yes | yes | yes |
| BC5_UNORM | 7 shapes | | | no | no | no | yes | yes | yes |
| BC6H_UF16 | 7 shapes | | | no | no | no | yes | yes | yes |
| BC7_UNORM | 7 shapes | | | no | no | no | yes | yes | yes |
| BC7_SRGB | 7 shapes | | | no | no | no | yes | yes | yes |
| BC7_TYPELESS | 7 shapes | | | no | no | no | yes | yes | yes |
| RGBA8_UNORM | 7 shapes | | | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_SRGB | 7 shapes | | | yes | as DEFAULT: yes | yes | yes | yes | yes |
| RGBA8_TYPELESS | 7 shapes | | | yes | as DEFAULT: yes | yes | yes | yes | yes |
| BC6H_UF16 | 256x256 | 9 | 6 (cube) | no | no | no | yes | yes | yes |
| RGBA8_SRGB | 256x256 | 9 | 6 (cube) | no | no | no | yes | yes | yes |
| BC7_SRGB | 1001x601 | 10 | 1 | not valid | not valid | not valid | not valid | not valid | not valid |
| RGBA8_SRGB | 1001x601 | 10 | 1 | yes | as DEFAULT: yes | yes | yes | yes | yes |

The same 324 of 324 with the old flag, the same refusals with the new one.

### Cost on the Radeon

The same `cost` and `legacycost` steps as on the RTX. This GPU is tiny (2 compute units, no memory of its own beyond a
2 GB slice of main memory), so its times are slow in absolute terms; what counts is shared against unshared.

| | RTX 5080 | Radeon (integrated) | WARP |
|---|---|---|---|
| Memory, large texture (BC3 or BC7 2048, all mips, 5461 KB of texels) | 5504 KB shared and unshared | 6144 KB shared and unshared | 5461 KB shared (old flag) |
| Memory, mid-size (BC1 512, all mips, 171 KB) | 192 KB shared and unshared | 207 KB unshared, 261 KB shared (new flag) | 172 KB shared (new flag) |
| Memory, small (BC3 or BC7 128, all mips, 21 KB) | 24 KB unshared, **64 KB** shared | 24 to 25 KB, shared or not | not run |
| Memory, tiny (BC1 64x64, one mip, 2 KB) | 2 KB unshared, **64 KB** shared | 2.3 KB unshared, 4 KB shared | 4 KB shared (old flag) |
| The card holds an opened texture | once | once | once |
| Windows counts it | once per device that has it open | the same | once |
| D3D11's death notice | 40 of 40 | 40 of 40, both flags | 40 of 40, both flags |
| Create with a flag, small texture | 0.06 to 0.08 ms (unshared 0.005 to 0.024) | 0.10 to 0.11 ms (unshared 0.025 to 0.03) | 0.011 ms (unshared 0.006) |
| D3D12 `OpenSharedHandle` | 0.10 to 0.15 ms | 0.24 to 0.33 ms | 0.02 ms |
| Kernel handles, old flag | none added | none added | none added |
| Sampling speed in D3D11, shared against unshared | the same | the same (all within 3 %) | not run (CPU) |
| The `CreateTexture2D` hook | works; slot 54 bypasses slot 5 | the same | the same |

So AMD's driver rounds small shared textures to 4 KB, not 64 KB: the small-texture cost seen on the RTX does not
appear here. Opening is about twice as slow, still a fraction of a millisecond, on a worker thread. WARP counts only
what is in use (it showed nothing for textures D3D11 alone holds), so its memory column is a lower bound.

### Lifetime on each card

40 BC7 sRGB textures, 2048 with all mips (213 MB of texels), old flag, opened on D3D12; then D3D11 lets go, then D3D12.
Memory of the whole card (for WARP: what Windows counts for it):

| | RTX 5080 | Radeon (integrated) | WARP |
|---|---|---|---|
| D3D11 and D3D12 both hold them | +216 MB | +242 MB | +213 MB |
| after D3D11 let go | +216 MB: D3D12 keeps them | +241 MB: D3D12 keeps them | +213 MB: D3D12 keeps them |
| after D3D12 let go too | +2.5 MB: freed | 0: freed | 0: freed |
| DXGI's count for the process while both hold | +430 MB (twice) | +480 MB (twice) | +213 MB (once) |
| D3D11's death notice | 40 of 40 | 40 of 40 | 40 of 40 |

The behaviour is the same on all three: the memory lives as long as either device holds the texture, and goes when
both let go. So TGPT must let go of its D3D12 copy of a texture when the game's death notice arrives, or the game's
streamed-out textures stay in memory (step 5 of "How it would go into the game" above). That holds on every card here.

One AMD detail: the first set of 40 in each run took twice the memory and left about 240 MB behind after both devices
let go. A run with four sets in a row (`lifeorder`: old flag, new flag, new flag, old flag) showed it is "first big
allocation of the run", with either flag: sets 2 to 4 came back to exactly 0. It looks like the AMD driver keeping a
pool of memory, not a leak per texture. Not checked: whether unshared textures do the same.

### The start-up self-test, measured

`share-probe selftest <adapter>` is the test the mod would run at start (step 3 of the plan below): 11 formats at
128x128 and 1024x1024, two cube maps and a 2-slice array, all mips, old flag, debug layers off; every texel compared
through a shader on both sides, every byte through a copy back to the CPU.

| | RTX 5080 | Radeon (integrated) | WARP (on the CPU) |
|---|---|---|---|
| passed | 26 of 26 | 26 of 26 | 26 of 26 |
| time, all of it | 77 ms | 215 ms | 1601 ms |
| largest single case | 11 ms (RGBA8 1024) | 25 ms (BC7 1024, 2 slices) | 229 ms (BC7 1024, 2 slices) |

The time includes making about 25 MB of random bytes on the CPU and waiting for both devices after each case, so a
real one can be faster. With 256x256 only (14 cases) the Radeon took 55 ms. WARP is only there to show Windows' rule:
no player runs the game on it.

### What Microsoft documents

Microsoft owns Direct3D, so this is the strongest evidence there is. In short: nothing about compressed textures.

- **The guaranteed list** ([Direct3D 11.1 features, "Extended support for shared Texture2D
  resources"](https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-1-features#extended-support-for-shared-texture2d-resources)):
  every card can share RGBA8, BGRA8, RGB10A2 and RGBA16F textures with one mip that are both render target and shader
  resource. With `ExtendedResourceSharing` (every card from the last decade) the list grows to about 50 uncompressed
  and video formats, mips and arrays. No BC format is on either list. Cube maps and IMMUTABLE are named as things you
  "still can't share".
- **Between D3D11 and D3D12** ([D3D11_SHARED_RESOURCE_TIER](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_shared_resource_tier),
  [D3D12_SHARED_RESOURCE_COMPATIBILITY_TIER](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_shared_resource_compatibility_tier)):
  tier 0 is the basic list above; tier 1 adds the typeless versions and "is built into WDDM 2.4" (Windows 10, 2018);
  tier 2 adds NV12 video; D3D11's tier 3 adds R11G11B10_FLOAT. No tier mentions BC formats. All three adapters here
  report the top tier.
- **The old flag is not checked** ([D3D11_RESOURCE_MISC_FLAG](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_resource_misc_flag)):
  with SHARED_NTHANDLE "the runtime then must confirm that the shared resource works on all hardware". "Without this
  flag set, the runtime does not strictly validate shared resource parameters (that is, formats, flags, usage, and so
  on). When the runtime does not validate shared resource parameters, behavior of much of the Direct3D API might be
  undefined and might vary from driver to driver." The same page still says of the old flag "The only resources that
  can be shared are 2D non-mipmapped textures", a sentence from Direct3D 10 that all three drivers here go far beyond.
- **Old handles**: [GetSharedHandle](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiresource-getsharedhandle)
  is "not recommended" since Direct3D 11.1; the handle is valid as long as the memory lives. That D3D12's
  [OpenSharedHandle](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-opensharedhandle)
  accepts an old handle: I found no Microsoft page that says so. It does on all three adapters here.
- **One GPU only** ([Shared heaps](https://learn.microsoft.com/en-us/windows/win32/direct3d12/shared-heaps)): "There
  is no mechanism to share cross-adapter resources across D3D API versions." The D3D12 device must be on the game's
  GPU. The bridge already refuses a D3D12 device on any other adapter (`native/bridge/src/device.cpp`, "not on the
  game's adapter").

So with the old flag the mod relies on behaviour no vendor promises. Three drivers (NVIDIA, AMD, Microsoft's) agree
today; that is evidence, not a guarantee, and a driver update could change it.

### What others ran into

| Who | What they do | What went wrong | How strong |
|---|---|---|---|
| Chrome ([gpu_driver_bug_list.json](https://chromium.googlesource.com/chromium/src/+/main/gpu/config/gpu_driver_bug_list.json), entry 467, crbug 417604761) | shares textures between its D3D11 parts and its D3D12 parts (video, WebGPU) on every PC | "Share nv12 texture from D3D11 to D3D12 may show corruption on some Intel GPUs": switched off on Intel Meteor Lake and Arrow Lake (Core Ultra), with no driver-version limit, so on every driver for those chips (re-read 2026-10-07). No other D3D11-to-D3D12 sharing workaround for any vendor in the list; the one other "shared" entry (423) is ChromeOS on old AMD chips. | strong: a workaround shipped to every Chrome user. NV12 is a video format, not ours, but it is the same kind of failure: no error, a wrong picture. |
| OptiScaler ([Config.md](https://github.com/optiscaler/OptiScaler/blob/master/Config.md), `with_dx12/dx11_with_dx12.cpp`) | for D3D11 games, copies the game's frame, depth and motion textures into textures it shares with its own D3D12 device (NT handle, or the old one with `DontUseNTShared`) | its docs: "can cause issues with unstable GPU drivers (especially on Intel)"; six sync methods to pick per game | medium: a mod's maintainers, not tied to one bug. Uncompressed copies only. |
| Mirillis Action! on [NVIDIA's developer forum](https://forums.developer.nvidia.com/t/driver-crash-with-re-shared-d3d11on12-textures-since-driver-580-88/347923) | shares a D3D11On12 texture with a second and a third D3D11 device | crashes since NVIDIA driver 580.88; "works on AMD and Intel"; NVIDIA filed it, unfixed as of September 2026 | strong for NVIDIA (acknowledged). Different path from ours, but it shows a working sharing path can break with a driver update. |
| OBS game capture ([d3d11-capture.cpp](https://github.com/obsproject/obs-studio/blob/master/plugins/win-capture/graphics-hook/d3d11-capture.cpp)) | inside a D3D11 game, copies each frame into a texture with the old flag (`GetSharedHandle`); OBS opens it in its own process | nothing vendor-specific found | strong that the old handle works for plain colour textures on every vendor; says nothing about BC or D3D12. |
| Osiris VR viewer ([issue 5](https://github.com/BerZerker96/Osiris-Vr-Viewer/issues/5)) | imports a D3D11 shared texture into Vulkan | crash in Intel Arc A750's Vulkan driver (32.0.101.8826) | weak: one user, Vulkan rather than D3D12. |
| DXVK and vkd3d-proton (Linux, Proton) ([DXVK PR 5257](https://github.com/doitsujin/dxvk/pull/5257), [vkd3d-proton changelog](https://github.com/HansKristian-Work/vkd3d-proton/blob/master/CHANGELOG.md)) | D3D11 and D3D12 become Vulkan; sharing between them exists since vkd3d-proton 2.7 and was rewritten in November 2025 | the rewrite logs "warning messages for missing" formats | weak for us: whether our mods run on Proton at all is unknown. |
| Special K, ReShade add-ons, Lossless Scaling, Streamline, D3D11On12 | | nothing found | Lossless Scaling captures the finished frame; Streamline's frame generation is D3D12 only; D3D11On12 runs D3D11 on top of one D3D12 device, so nothing is shared. I found no program that shares a game's own compressed textures from D3D11 to D3D12. |
| Our own mods | TGFG, TGBU and TGPT share render targets (copies, NT handle, uncompressed) with the bridge's D3D12 device on every vendor | no sharing failure on record in BACKLOG or the round notes | medium: players on Radeon run them (round 1), but nobody checks their bytes. |

What this says per vendor:

- **NVIDIA.** One driver package covers RTX 20 to RTX 50, so the 5080 result very likely holds for all of them. GTX 10
  and older left the main driver after the 580 branch
  ([TechPowerUp](https://www.techpowerup.com/338497/nvidias-v580-driver-branch-ends-support-for-maxwell-pascal-and-volta-gpus)),
  and path tracing needs ray tracing hardware anyway.
- **AMD.** Since Adrenalin 25.10.2, RDNA 1 and 2 (RX 5000, RX 6000, and integrated GPUs like this one) get a
  maintenance branch and RDNA 3 and 4 (RX 7000, RX 9000) the main one
  ([TechPowerUp](https://www.techpowerup.com/342449/no-more-game-optimizations-for-amd-radeon-rx-6000-and-rx-5000-still-part-of-main-driver-branch)).
  This test covers RDNA 2. RDNA 3 and 4 are newer hardware on the other branch, so they need their own check. RDNA 1
  has no ray tracing and can't run TGPT.
- **Intel.** Arc A and B cards and the Core Ultra integrated GPUs share one driver. Nothing tested, nothing documented,
  and Chrome's one D3D11-to-D3D12 sharing workaround is for Intel. The most likely place for a wrong picture.

### What the mod must do so a player never sees a broken picture

A test at start, on the player's own card, and copies for anything that fails. Today's copies (the atlas) stay in the
mod as the fallback.

1. **When.** The moment the bridge sees the game's D3D11 device (where the `CreateTexture2D` hook goes in), before the
   hook starts adding the flag. On a worker thread, so the game doesn't wait. Textures the game makes before the test
   ends are born unshared and simply stay on copies; the menu and the first level load come later.
2. **On which devices.** A small D3D11 device of our own on the game's adapter (same LUID) and the bridge's shared
   D3D12 device. Never the game's own D3D11 context: it belongs to the game's thread. If the D3D12 device is not on the
   game's adapter, nothing is shared at all.
3. **What.** For each format the game uses (seen so far: BC3 and BC3 sRGB; until the harness's full texture list says
   which ones, test all of BC1, BC3, BC4, BC5, BC6H, BC7 and RGBA8, plain and sRGB where they exist): one texture at 128x128 and one at 1024x1024, all mips
   down to 1x1 (small and large textures get different memory layouts, and the smallest mips are packed together), a
   cube map for each format the game uses for cubes, and a 2-slice array. Fill them with known random bytes at
   creation, made with the old flag exactly as the hook would make them. Then: `GetSharedHandle`, `OpenSharedHandle`
   on D3D12, check the size, mips, slices and format D3D12 sees, make a view over all mips, let a small shader read
   every texel of every mip on both sides and compare, and copy every mip back to the CPU and compare bytes. The same
   fence handoff the tracer uses, so a broken fence fails the test too. That is `share-probe selftest`, ready to port.
4. **The result.** A list of formats (and "cube", "array") that passed. The hook adds the flag only to textures whose
   format and shape are on it. A format not on the test list never gets the flag. Anything that fails, at any step,
   stays on today's copies.
5. **At run time too.** If `OpenSharedHandle` fails for one of the game's textures, or D3D12 sees a different size or
   format, that texture goes to copies and is counted.
6. **Every start, no cache.** It costs a fraction of a second on a worker thread, and it catches a driver update that
   breaks something (NVIDIA's 580.88 broke a sharing path that had worked for years).
7. **Cost.** Measured in the self-test table above: 77 ms on the RTX 5080 and 215 ms on the small integrated Radeon
   for 26 cases, on a worker thread, so the game does not wait. About 25 MB of test textures, freed as soon as it ends.
   A format that fails costs nothing more than today: it stays on the copies the mod makes now.
8. **How it reports.** No row and no alert icon: the picture is right either way and there is nothing for the player
   to do. One line in TGPT's native log and in `BepInEx/LogOutput.log`. The first line is this PC's real result; the
   other two show the wording for a failure (the card and numbers in the second are made up):
   - `Texture sharing self-test on AMD Radeon(TM) Graphics (driver 32.0.21043.5001): 26 of 26 passed in 215 ms; the game's textures are read directly.`
   - `Texture sharing self-test on <card> (driver <version>): 23 of 26 passed in 120 ms. BC6H failed (D3D12 read different bytes at mip 3, 128x128); BC7 2-slice failed (D3D12 open 0x80070057). Those stay on copies; everything else is read directly.`
   - `Texture sharing self-test could not run (the D3D12 device is on another GPU than the game). All textures stay on copies.`
   And in the F8 capture's `report.txt`: the same line plus counts, "game textures read directly: N; through copies:
   M (BC6H: a, opened with another size: b)". That tells us from a player's capture which path ran.
9. **What it can't catch.** A fault that only shows when the card runs out of memory and Windows moves textures out
   and back (never tested here), a texture size or layout the test doesn't cover, and timing faults that only show
   under a real game's load. For the first, a harness run on a nearly full card; for the others, while the atlas still
   exists the F8 capture could compare a few shared textures' smallest mips with the atlas's copy and say in
   `report.txt` if they disagree.

### Still unknown (other cards)

- **Intel**, any card. One run of `share-probe matrix`, `legacy` and `selftest` on an Arc or a Core Ultra laptop
  settles it (`share-probe.exe legacy Intel`; it needs only the exe, no SDK).
- **AMD RDNA 3 and RDNA 4** (RX 7000, RX 9000): the main driver branch and a different memory layout. The same run.
- **NVIDIA RTX 20 to 40**: same driver package, very likely the same, not run.
- **The game's own textures on these cards.** The probe makes its own textures. In game, Unity's textures, mip
  streaming and memory pressure come on top.
- **Proton on Linux and Steam Deck**: whether the bridge runs there at all.
- **The AMD driver's 240 MB kept after the first big set**: whether unshared textures do the same (then it is just how
  the driver works) and whether it ever goes back. Harmless at this size; worth one more run if memory gets tight.

Copying the probe: `share-probe.exe` and `probe.cpp` live in this session's scratch folder (`share-probe-other\`), which
Windows may clear. Copy them somewhere kept before handing the exe to a tester with an Intel or RX 7000/9000 card.
