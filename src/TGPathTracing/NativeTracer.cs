// SPDX-License-Identifier: MIT
// TG Path Tracing's native ABI (native/pathtracing/include/tgpt_api.h), mirrored field for field.
using System;
using System.Runtime.InteropServices;
using System.Text;
namespace TGPathTracing
{
    internal static class Tgpt
    {
        internal const uint Abi = 0x00012700;
        // TGPT_Frame.maxRayDistance: no distance ends a ray (PT-63; the Ray Distance row and its 500 m are gone). A thousand
        // kilometres, past anything the game draws; the native field stays until the frame's layout next changes.
        internal const float NoRayEnd = 1e6f;
        internal const uint FrameReset = 1, FrameGuides = 2;
        internal const uint FrameOwnDepth = 1u << 14; // PT-112 (A), the rule: the traced frame's own depth and motion into HDRP's buffers
        internal const uint FrameTracedFog = 1u << 15; // PT-112 (B), the rule since 2026-10-11: the game's fog traced (TGPT_FRAME_TRACED_FOG)
        internal const uint FrameFogWalk = 1u << 16;   // with it: the light the fog scatters more than once (TGPT_FRAME_FOG_WALK)
        // TGPT_FOG_BOX_* (PT-112): a fog box's flags (FogBoxes).
        internal const uint FogBoxExponential = 1, FogBoxInvert = 2, FogBoxAlphaOnly = 4, FogBoxClamp = 8, FogBoxPoint = 16;
        // TGPT_SKY_* (PT-112, the sky, on trial): the sky's mode and flags, a celestial body's flags (SkyModel).
        internal const uint SkyHdrp = 0, SkyMeasure = 1, SkyOwn = 2;
        internal const uint SkyHdrpPixels = 1, SkyClouds = 2, SkyCloudsUpper = 4;
        internal const uint SkyBodyLights = 1, SkyBodyReflects = 2;
        internal const int SkyMaxBodies = 4;
        // TGPT_FRAME_MEGA_SHIFT, TGPT_FRAME_MEGA_MASK (PT-109, on trial): the hidden "Mega Geometry trial" in the flags' bits 2 to 5.
        internal const int FrameMegaShift = 2;
        internal const uint FrameMegaMask = 15;
        // TGPT_FRAME_OMM_SHIFT, TGPT_FRAME_OMM_MASK (PT-110, for measuring): the hidden "Micromaps trial" in the flags' bits 6 to 13.
        internal const int FrameOmmShift = 6;
        internal const uint FrameOmmMask = 255;
        // TGPT_FRAME_NRD_SHIFT, TGPT_FRAME_NRD_MASK (PT-127, on trial): the hidden "NRD trial" in the flags' bits 17 and 18.
        internal const int FrameNrdShift = 17;
        internal const uint FrameNrdMask = 3;
        internal const uint OptionNoSpatial = 1, OptionNoTemporal = 2, OptionNoDenoiser = 4, OptionNoSpecular = 8, OptionRayReconstruction = 16;
        internal const uint OptionRayRegeneration = 64, OptionNoPassThrough = 128, OptionDiVisibility = 256, OptionSpatialFeedback = 512, OptionNrd = 1024, OptionNoGiBoiling = 32768;
        internal const uint OptionNoRadianceCache = 65536;
        internal const uint OptionGlossyReflections = 262144;
        internal const uint OptionGiVisibility = 2097152; // TGPT_OPTION_GI_VISIBILITY: the Crevice Light row
        internal const uint OptionEmissiveLights = 131072;
        internal const uint LightHdrpShadows = 0x10000;
        internal const uint OptionGiOwnRays = 524288, OptionDiNoReuse = 1048576; // the capture's comparison of what boils
        internal const uint LightNoSpecular = 0x20000;
        internal const uint LightAtEmitter = 0x40000; // TGPT_LIGHT_AT_EMITTER: the light stands at its flame's centre
        internal const uint LightNoRange = 0x80000; // TGPT_LIGHT_NO_RANGE: falls off with distance alone; Range only says how far it is sampled as near
        internal const int LightHdrpRadiusShift = 20; // TGPT_LIGHT_HDRP_RADIUS_SHIFT, on trial: HDRP's radius in mm in the flags' top 12 bits
        internal const uint FeatureDxr12 = 1, FeatureSer = 2, FeatureRayRegeneration = 4, FeatureRayRegenerationActive = 8, FeatureNrd = 16, FeatureNrdActive = 32;
        internal const int EventTrace = 1, EventComposite = 2, EventMark = 8; // 8: PT-107's clock (TGPT_EVENT_MARK); 5 was Fog Adaptation's
        internal const uint OptionNoCaustics = 16777216; // TGPT_OPTION_NO_CAUSTICS: light through glass tinted along a straight line, no water caustics
        internal const uint MaterialNoDecals = 33554432;
        internal const uint OptionFaceted = 33554432; // TGPT_OPTION_FACETED: round things shaded by their flat faces as before (hidden "Smooth round surfaces" off)
        // TGPT_OPTION_GBUFFER_SURFACES: the route before PT-59, each pixel's surface from HDRP's G-buffer (hidden "Own camera rays" off).
        internal const uint OptionGBufferSurfaces = 2147483648;
        internal const uint OptionReflectionTopMip = 134217728; // TGPT_OPTION_REFLECTION_TOP_MIP: reflections read the top mip (hidden, for the harness)
        internal const uint OptionNoSubsurface = 268435456; // TGPT_OPTION_NO_SUBSURFACE: no walk inside subsurface materials (for comparing; no row sets it yet)
        internal const uint OptionDryGround = 536870912; // TGPT_OPTION_DRY_GROUND: the ground stays dry where the waves have been (hidden "Wet ground" off)
        // TGPT_SetSkinned's stride, on trial (PT-92): its bits 16-23 say at which byte a vertex holds its skinned normal (0: none).
        internal const int SkinnedNormalShift = 16;
        internal const uint OptionCausticsSplit = 1073741824; // TGPT_OPTION_CAUSTICS_SPLIT, on trial: the caustics' light on triangles whose corners met different surfaces lands (hidden "Caustics land split light")
        internal const uint OptionCausticsLit = 4194304; // TGPT_OPTION_CAUSTICS_LIT, on trial: under water the caustics' light comes from its direction (hidden "Caustics light keeps its direction")
        // TGPT_FRAME_CAUSTICS_GRID and its two further bits (bits of TGPT_Frame.flags), on trial (PT-114; hidden "Caustics grid trial" 1, 2, 3):
        // the caustics' lights picked in a pass of their own; + only the tiles with water under them; + no light traced that cannot land in view.
        internal const uint FrameCausticsGrid = 1u << 19, FrameCausticsGridWater = 1u << 25, FrameCausticsGridView = 1u << 26;
        internal const uint MaterialDoubleSided = 1, MaterialAlphaTest = 2, MaterialVertexColor = 4, MaterialAlphaTile = 8, MaterialCameraOnly = 16, MaterialGlass = 64;
        internal const uint MaterialMaskTile = 1024;
        internal const uint MaterialEmissiveAlbedo = 128;
        internal const uint MaterialTranslucent = 256;
        internal const uint MaterialThinCloth = 512;
        internal const uint MaterialSeeThrough = 2048; // TGPT_MATERIAL_SEE_THROUGH: glass HDRP draws transparent, drawn by the tracer
        internal const uint MaterialStrands = 4096;
        internal const uint MaterialSways = 8192;
        internal const uint MaterialEye = 16384; // TGPT_MATERIAL_EYE: an eye, shaded by the tracer as a wet surface
        internal const uint MaterialHairLobes = 32768; // TGPT_MATERIAL_HAIR_LOBES: hair with its highlights (Kajiya-Kay, RealHair's)
        internal const uint MaterialHairLayer = 65536; // TGPT_MATERIAL_HAIR_LAYER: hair drawn by the tracer as a layer over HDRP's surface
        internal const uint MaterialOwnSurface = 131072; // TGPT_MATERIAL_OWN_SURFACE: a glowing surface a kept light stands for: it lights nothing itself
        internal const uint MaterialFirstPerson = 1048576; // TGPT_MATERIAL_FIRST_PERSON: the hero's first-person body, whose stand-in head reflections pass
        internal const uint MaterialSubsurface = 2097152; // TGPT_MATERIAL_SUBSURFACE: light scatters inside it (wax, skin): Subsurface holds its profile's shape, SubsurfaceMask how much of its light does (PT-40)
        internal const uint TextureMaskOcclusion = 16; // TGPT_TEXTURE_MASK_OCCLUSION: the mask map's green is HDRP Lit's ambient occlusion (PT-46)
        // PT-64 (tgpt_api.h TGPT_TEXTURE_*): Shader Graph's Normal Strength; HDRP's Double-Sided "Flip" mode; the emissive map's
        // red alone; the mask map's occlusion through the material's own range (its ends, 8 bits each, from bits 12 and 20).
        internal const uint TextureGraphNormal = 32, TextureFlipBack = 64, TextureEmissiveRed = 128, TextureOcclusionRemap = 256;
        internal const int TextureOcclusionLowShift = 12, TextureOcclusionHighShift = 20;
        internal const int FirstDetailTile = 4096;     // runtime.hpp kMaxTiles: a detail tile's number, 512x512 (a hair color's)
        internal const uint InstanceAlphaTest = 1, InstanceHidden = 2, InstanceStandIn = 4;
        internal const uint InstanceWater = 64; // TGPT_INSTANCE_WATER: HDRP's water, met by the water query alone; its slot in bits 8 and 9
        internal const int MaxWater = 4;        // TGPT_MAX_WATER
        internal const int MaxWaterSurfaces = 16; // TGPT_MAX_WATER_SURFACES: HDRP's water surfaces by its index
        internal const uint NoStream = 0xFFFFFFFFu;
        internal const uint LightPoint = 0, LightSpot = 1, LightDirectional = 2;
        // TGPT_SHARE_*: why TGPT_RegisterSharedTexture gave a texture no number (PT-45).
        internal const int ShareNotTexture = -2, ShareUnsupported = -6, ShareFlags = -21, ShareShape = -22, ShareRefused = -23,
            ShareBeforeHook = -24, ShareOpenFailed = -25, ShareFull = -26, ShareSelfTest = -27, ShareBudget = -28;
    }
    // TGPT_VIEW_*: what the composite shows instead of the lit image.
    public enum DebugView { None = 0, Albedo = 1, Normals = 2, Coverage = 3, Materials = 4, Indirect = 5, IndirectRaw = 6, Direct = 7, Surfaces = 8, Specular = 9, Emissive = 10, Yardstick = 11 }

    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeSubmesh { internal uint IndexStart, IndexCount, BaseVertex, Topology; }
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeMeshDesc
    {
        internal uint Size, Abi;
        internal ulong Key;
        internal ulong Vb0, Vb1, Vb2, Vb3;
        internal uint Stride0, Stride1, Stride2, Stride3;
        internal uint PositionStream, PositionOffset, PositionFormat, PositionDimension;
        internal uint UvStream, UvOffset, UvFormat, UvDimension;
        internal uint ColorStream, ColorOffset, ColorFormat, ColorDimension;
        internal ulong IndexBuffer;
        internal uint IndexFormat, VertexCount;
        internal uint SubmeshCount, Reserved;
        internal IntPtr Submeshes;
        internal uint NormalStream, NormalOffset, NormalFormat, NormalDimension; // for glass's caustics (Tgpt.NoStream: none)
        internal uint Uv2Stream, Uv2Offset, Uv2Format, Uv2Dimension; // PT-64: Unity's TexCoord3, the trunk shader's bark (Tgpt.NoStream: none)
        internal uint TangentStream, TangentOffset, TangentFormat, TangentDimension;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeCpuMeshDesc
    {
        internal uint Size, Abi;
        internal ulong Key;
        internal IntPtr Positions, Uvs, Colors, Indices;
        internal uint VertexCount, IndexCount;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeMaterial
    {
        internal float BaseR, BaseG, BaseB, BaseA;
        internal float ScaleX, ScaleY, OffsetX, OffsetY;
        internal float EmissiveR, EmissiveG, EmissiveB;
        internal float Metallic;
        internal int AlbedoTile, EmissiveTile;
        internal float Smoothness, AlphaCutoff;
        internal uint Flags;
        internal int AlphaTile;             // with MaterialAlphaTile: a RegisterAlphaTexture result (AlphaCutoff then compares its coverage)
        internal int MaskTile;              // with MaterialMaskTile: a RegisterMaskTexture result
        internal uint MaskRemap;            // metallic's low and high end, then smoothness's, 8 bits each, low first
        // Hair's highlights (Kajiya-Kay, as the game's RealHair): HairLobeTile a RegisterHairLobes result, -1 none; the lobes'
        // shifts along the normal, the second's smoothness; the first's smoothness ends; each lobe's strength; the tint.
        internal int HairLobeTile;
        internal float HairShift, HairSecondaryShift, HairSecondarySmoothness;
        internal float HairSmoothnessMin, HairSmoothnessMax, HairStrength, HairSecondaryStrength;
        internal float HairTintR, HairTintG, HairTintB;
        // With MaterialHairLayer: RealHair's _AlphaPower (low 16 bits) and _AlphaRemap (high), each a half; 0 counts as 1.
        internal uint HairAlpha;
        // PT-46: the game's own color, normal and mask maps (NativeSharedTexture.Handle, -1 or 0 none), read at a hit in place of
        // the tiles; the normal map's strength; the mask map's channels (metallic bits 0-1, smoothness 2-3) and TextureMaskOcclusion.
        internal int ColorTexture, NormalTexture, MaskTexture;
        internal float NormalScale;
        internal uint TextureFlags;
        // PT-40 (TGPT_Material.subsurface): with MaterialSubsurface, Burley's shape of the material's diffusion profile per channel,
        // per metre of the world; with MaterialTranslucent, the share of the light from behind a sheet of it lets through per
        // channel (0: not known). HdrpPasses.ReadSubsurface fills it.
        internal float SubsurfaceR, SubsurfaceG, SubsurfaceB;
        // PT-64: the game's shader that computes the material at a hit (MaterialPrograms: 0 HDRP Lit's way) and its record's
        // first row; the emissive map itself (a NativeSharedTexture.Handle, -1 or 0 none); glass's own index of refraction (0 none).
        internal uint Program, ProgramRow;
        internal int EmissiveTexture;
        internal float Ior;
        internal float AbsorptionR, AbsorptionG, AbsorptionB;
        internal uint SurfaceModel;
        // PT-40 (TGPT_Material.subsurfaceMask): with MaterialSubsurface, the game's subsurface mask for the material, 0 to 1:
        // the share of its diffuse light that scatters under the surface. HdrpPasses.ReadSubsurface fills it.
        internal float SubsurfaceMask;
        internal uint OpticalReserved1, OpticalReserved2;
    }
    // TGPT_HairLobes: RealHair's strands for its highlights, for RegisterHairLobes (48 bytes).
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeHairLobes
    {
        internal uint Size, Abi;
        internal ulong FlowMap, MaskMap, SpecularMap; // ID3D11Texture2D*, 0 for none
        internal float SmoothnessPower, FlowFlipGreen, Reserved0, Reserved1;
    }
    // TGPT_HairColor: RealHair's color as its shader computes it, for RegisterHairColor (240 bytes).
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeHairColor
    {
        internal uint Size, Abi;
        internal ulong DiffuseMap, RootMap, IdMap, MaskMap, BlendMap; // ID3D11Texture2D*, 0 for none (white)
        internal float TilingX, TilingY, OffsetX, OffsetY;
        internal float DiffuseR, DiffuseG, DiffuseB, DiffuseA, RootR, RootG, RootB, RootA, EndR, EndG, EndB, EndA;
        internal float HighlightAR, HighlightAG, HighlightAB, HighlightAA, HighlightBR, HighlightBG, HighlightBB, HighlightBA;
        internal float AStart, APeak, AEnd, AOverlapEnd, BStart, BPeak, BEnd, BOverlapEnd;
        internal float BaseColorStrength, GlobalStrength, RootColorStrength, EndColorStrength;
        internal float InvertRootMap, HighlightBlend, HighlightAStrength, HighlightBStrength;
        internal float HighlightAOverlapInvert, HighlightBOverlapInvert, DiffuseStrength, BlendStrength;
        internal float AoStrength, AoOccludeAll, AlphaClip, AlphaPrepass;
    }
    // TGPT_EyeColor: the eye shader's color, for RegisterEyeColor (88 bytes).
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeEyeColor
    {
        internal uint Size, Abi;
        internal ulong ScleraMap, IrisMap;
        internal float ScleraR, ScleraG, ScleraB, ScleraA, IrisR, IrisG, IrisB, IrisA;
        internal float ScleraTiling, IrisTiling, PupilSize, RingSize;
        internal float RingIntensity, EmissionIntensity, Reserved0, Reserved1;
    }
    // TGPT_WaterCaustics: one of HDRP's water surfaces, its wave buffers and HDRP's numbers for them this frame.
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeWaterCaustics
    {
        internal uint Size, Abi;
        internal ulong Displacement, Normals; // HDRP's displacement and additional data arrays (0 for still water)
        internal float W00, W01, W02, W03, W10, W11, W12, W13, W20, W21, W22, W23; // world to the water's space, row-major 3x4
        internal float B0Scale, B0OffsetX, B0OffsetZ, B0Amplitude, B1Scale, B1OffsetX, B1OffsetZ, B1Amplitude, B2Scale, B2OffsetX, B2OffsetZ, B2Amplitude;
        internal float F0A, F0B, F1A, F1B, F2A, F2B; // each band's fade with the distance from the camera
        internal uint BandCount;
        internal float ExtinctionR, ExtinctionG, ExtinctionB; // the absorption under it, per metre
        internal float Top, Bottom; // the highest and lowest points of its water instances in the world (y)
        internal float MinX, MinZ, MaxX, MaxZ; // their extent in the world
        internal float MaxDisplacement; // how far HDRP's waves move the surface at most
        internal uint Flags; // 1: its smoothness is set (SmoothnessNear, SmoothnessFar, SmoothnessFadeStart, SmoothnessFadeDistance)
        internal float ScatteringR, ScatteringG, ScatteringB, SmoothnessNear;
        internal float FoamAmount, FoamIntensity, FoamRoughness, FoamTiling;
        internal float FoamMaskScaleX, FoamMaskScaleZ, FoamMaskOffsetX, FoamMaskOffsetZ;
        internal float FoamFlowX, FoamFlowZ, FoamTime, FoamCurrent;
        internal int FoamTexture, FoamMaskTexture;
        internal float SmoothnessFadeStart, SmoothnessFadeDistance; // metres from the camera: where near starts to go to far, and over how far
        internal float FoamColorR, FoamColorG, FoamColorB, SmoothnessFar;
    }
    // TGPT_ClothWind: the wind on a material the game sways as cloth (TG/Clothes/Cloth_Wind), as its vertex shader reads it.
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeClothWind
    {
        internal uint Material;
        internal float Speed, Wind, NoiseScale;
        internal float IntensityX, IntensityY, IntensityZ; internal uint Flags;
        internal float DirectionX, DirectionY, Reserved0, Reserved1;
    }
    // TGPT_PlantWind: the wind on a material the game sways as grass, leaves or a trunk (PlantWind), as its vertex shader reads it.
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativePlantWind
    {
        internal uint Material, Kind, Noise, Flags;
        internal float Strength, BendHeight, PivotOffsetY, FadeX;
        internal float FadeY, Reserved0, Reserved1, Reserved2;
    }
    // TGPT_PlantWindFrame: what every plant's wind shares in a frame.
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativePlantWindFrame
    {
        internal float Time, Speed, Direction, Reserved0;
        internal float ForwardX, ForwardY, ForwardZ, Reserved1;
    }
    // TGPT_FogBox: one of the game's local fog volumes as HDRP voxelizes it (FogBoxes, PT-112). Positions in the world.
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeFogBox
    {
        internal float CenterX, CenterY, CenterZ, Extinction;
        internal float RightX, RightY, RightZ, RcpDistFadeLen;
        internal float UpX, UpY, UpZ, EndTimesRcpDistFadeLen;
        internal float ExtentX, ExtentY, ExtentZ; internal uint Flags;
        internal float AlbedoR, AlbedoG, AlbedoB; internal int Mask;
        internal float PosFadeX, PosFadeY, PosFadeZ, MaskMost;
        internal float NegFadeX, NegFadeY, NegFadeZ, Reserved0;
        internal float TilingX, TilingY, TilingZ, Reserved1;
        internal float ScrollX, ScrollY, ScrollZ, Reserved2;
    }
    // TGPT_SkyBody, TGPT_SkyLayer, TGPT_Sky (PT-112, the sky, on trial: SkyModel). 112, 80 and 816 bytes.
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeSkyBody
    {
        internal float DirectionX, DirectionY, DirectionZ, AngularRadius;
        internal float ColorR, ColorG, ColorB; internal uint Flags;
        internal float SurfaceR, SurfaceG, SurfaceB, FlareSize;
        internal float FlareR, FlareG, FlareB, FlareFalloff;
        internal float SunTravelX, SunTravelY, SunTravelZ, Earthshine;
        internal float RightX, RightY, RightZ; internal int SurfaceTexture;
        internal float UpX, UpY, UpZ, Distance;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeSkyLayer
    {
        internal float OpacityR, OpacityG, OpacityB, OpacityA;
        internal float Rotation, Steps, Sigma, Altitude;
        internal float LightR, LightG, LightB; internal int Map;
        internal float ScrollX, ScrollZ, Scrolled; internal uint Distortion;
        internal float AmbientDimmer; internal int Flowmap; internal uint On; internal float Reserved;
    }
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeSky
    {
        internal uint Size, Abi, Mode, Flags;
        internal float PlanetCenterX, PlanetCenterY, PlanetCenterZ, PlanetRadius;
        internal float AirExtinctionR, AirExtinctionG, AirExtinctionB, AirScaleHeight;
        internal float AirScatteringR, AirScatteringG, AirScatteringB, AtmosphereDepth;
        internal float AerosolScatteringR, AerosolScatteringG, AerosolScatteringB, AerosolExtinction;
        internal float OzoneExtinctionR, OzoneExtinctionG, OzoneExtinctionB, AerosolScaleHeight;
        internal float GroundR, GroundG, GroundB, AerosolAnisotropy;
        internal float HorizonR, HorizonG, HorizonB, OzoneStart;
        internal float ZenithR, ZenithG, ZenithB, OzoneWidth;
        internal float ColorSaturation, AlphaSaturation, AlphaMultiplier, HorizonZenithShift;
        internal float Intensity; internal uint CloudsHash, BodyCount, Reserved0;
        internal NativeSkyBody Body0, Body1, Body2, Body3;
        internal float HdrpSunX, HdrpSunY, HdrpSunZ, CloudOpacity;
        internal NativeSkyLayer Layer0, Layer1;
        internal uint CloudWidth, CloudHeight, Reserved1, Reserved2;
    }
    // TGPT_Rain: the rain's wetness as the game's own pass makes it that frame (RainWetness; PT-108, on trial).
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeRain
    {
        internal uint Size, Abi;
        internal float Moisture, Soaked, ScreenNoise;
        internal int Noise;
        internal float R0, R1, R2, R3, R4, R5, R6, R7, R8, R9;
    }
    // TGPT_SkinnedInstance: a character this frame.
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeSkinnedInstance
    {
        internal float T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11; // skinned space to world, row-major 3x4
        internal int Mesh;
        internal uint FirstVertex, VertexCount, SubmeshCount;
        internal uint M0, M1, M2, M3, M4, M5, M6, M7;
        internal uint RenderingLayerMask;
        internal void SetMaterial(int i, uint m)
        {
            switch (i) { case 0: M0 = m; break; case 1: M1 = m; break; case 2: M2 = m; break; case 3: M3 = m; break;
                         case 4: M4 = m; break; case 5: M5 = m; break; case 6: M6 = m; break; default: M7 = m; break; }
        }
    }
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeInstance
    {
        internal float M00, M01, M02, M03, M10, M11, M12, M13, M20, M21, M22, M23; // object to world, row-major 3x4
        internal int Mesh;
        internal uint MaterialBase, Flags, Mask, RenderingLayerMask;
    }
    // TGPT_Moving: a thing the game moves, where HDRP draws it in the frame about to be handed over (the first 68 bytes as a
    // NativeInstance, MaterialBase into TGPT_SetMoving's own list) and where it was the frame before (its own motion).
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeMoving
    {
        internal float M00, M01, M02, M03, M10, M11, M12, M13, M20, M21, M22, M23;
        internal int Mesh;
        internal uint MaterialBase, Flags, Mask, RenderingLayerMask;
        internal float P00, P01, P02, P03, P10, P11, P12, P13, P20, P21, P22, P23; // the frame before
    }
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeLight
    {
        internal float PositionX, PositionY, PositionZ; internal uint Type;
        internal float ColorR, ColorG, ColorB, Range;
        internal float DirectionX, DirectionY, DirectionZ, Radius;
        internal float AngleScale, AngleOffset; internal uint Id, Flags; // Flags: 1 + its cookie, 0 without one
        internal float UpX, UpY, UpZ, CookieScale;
    }
    // TGPT_Yardstick: the tracer's own first hit against HDRP's G-buffer over one traced frame (tgpt_api.h says what each
    // number counts). Kinds: 5 kinds of instance by 6 places; Materials: 16 of (material, its pixels, those that differ, and of
    // those: in another place, another colour, another normal, another roughness).
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal unsafe struct NativeYardstick
    {
        internal uint Size, Abi, FrameIndex, Pixels;
        internal fixed uint Where[6];
        internal uint Normal, Colour, Roughness, Forward, Motion;
        internal fixed uint Kinds[30];
        internal uint MaterialCount;
        internal fixed uint Materials[112];
    }
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal unsafe struct NativeFrame
    {
        internal uint Size, Abi, Sequence, Flags;
        internal ulong GBuffer0, Normal, GBuffer2, GBuffer3, Depth, Motion, Exposure;
        internal uint RenderWidth, RenderHeight, DebugView;
        internal fixed float InvViewProj[16];
        internal fixed float ViewProj[16];
        internal fixed float PrevViewProj[16];
        internal fixed float CameraPosition[4];
        internal fixed float PrevCameraPosition[4];
        internal fixed float SunDirection[4];
        internal fixed float SunColor[4];
        internal uint FrameIndex, Bounces, SpatialSamples, GiHistory;
        internal uint Options, RaysPerPixel;
        internal float ReflectionBoost;
        internal float TracedGain;
        internal float HighlightKept, FrameReserved;
        internal float MaxRayDistance, SkyIntensity, IndirectColor;
        internal uint DiHistory, ThickProfiles;
        internal fixed float View[16];
        internal fixed float NonJitteredProj[16];
        internal fixed float Jitter[4];
        internal fixed float Fog[4];
        internal fixed float Forward[4];
        internal fixed float FogColor[4]; // PT-37: what HDRP's fog scatters of its color; w 1 in the Sky colour mode
        internal fixed float FogMip[4];   // HDRP's mip fog: near, far, max mip
        internal fixed float FogMedium[4]; // PT-112 B: the Fog volume's albedo (as its volumetric fog takes it) and anisotropy
    }
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeStatus
    {
        internal uint Size, Abi;
        internal uint DeviceReady, Supported;
        internal uint MeshesRegistered, MeshesReady, MeshesFailed, MeshesQueued;
        internal uint Instances, InstancesTraced, Materials, Textures;
        internal uint Lights, Disabled;
        internal ulong FramesTraced, FramesFailed;
        internal ulong Triangles, GeometryBytes, AccelerationBytes;
        internal float GpuMs, FrameMs;
        internal int LastError; internal uint RenderWidth, RenderHeight, Composited;
        internal float D3d11Ms, FlushMs, WaitMs, RecordMs;
        internal uint Features, MicromapMeshes, MicromapInstances, MicromapsWaiting;
        internal float CameraLight, CameraRadianceR, CameraRadianceG, CameraRadianceB;
        internal float TracedLog2Mid, TracedLog2High;
        internal uint TracedSamples, PickSurface;
        internal uint PickInstance, PickMaterial; internal float PickDistance; internal uint PickFound;
        internal uint WaterTraced, WaterApplied; // reserved status ABI fields from the removed separate water passes; zero
        internal uint Starting; internal float StartSeconds; // the passes are being made (frames wait for them); how long it took
        // PT-98, what the native side still owes of the traced world (Arrival.cs): tiles asked for and not baked yet (colour,
        // mask, alpha, hair, eye, cookie); listed instances left out of the last frame because their mesh is not built;
        // plants traced still until their group is made. 0 from a native side from before (NativeTracer.ReadStatus).
        internal uint TilesToBake, InstancesWaiting, PlantsWaiting, ArrivalReserved;
    }

    // A texture of the game's opened by the tracer's D3D12 device (TGPT_SharedTexture), and the registry's totals.
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeSharedTexture
    {
        internal uint Size, Abi;
        internal uint Width, Height, Mips, ArraySize, Format, Usage, BindFlags, MiscFlags; // as D3D11 holds it now
        internal ulong Bytes;                       // its video memory once opened
        internal uint Open, Opened, Freed, Hook;    // open now; opened and freed by the game since the start; the hook: 0 not in, 1 in, 2 failed,
                                                    // 3 waiting for its start-up self-test, 4 out (the self-test passed no format here)
        internal ulong OpenBytes;
        internal ulong Resource, View;              // diagnostics
        internal int Handle;                        // what a material names it by (NativeMaterial.ColorTexture, ...), -1 none
        internal uint SelfTest;                     // the start-up self-test, once ended: formats passed (bits 0-7), tested (8-15), its ms (16-31)
    }

    // DLSS Ray Reconstruction's guides (TGPT_Guides).
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal struct NativeGuides
    {
        internal uint Size, Abi, Width, Height;
        internal ulong DiffuseAlbedo, SpecularAlbedo, NormalRoughness; // ID3D11Resource*
        internal uint FrameIndex, ExtrasFrameIndex;
        internal ulong SpecularHitDistance, ColorBeforeTransparency;   // ID3D11Resource*, written by the composite
    }

    // The native side, bound by GetProcAddress once TG Graphics Bridge has loaded it.
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeDiagnostics
    {
        internal uint Size, Abi, PassesDone, PassesTotal, CacheHits, Reserved;
        internal ulong MeshGeometry, Acceleration, Scratch, Plants, Cloth, Skinned, Records, SharedTextures, Atlases, Targets, Histories, Caustics, WetStore, Yardstick, RadianceCache, Transfer, Denoiser, Retired, Total;
    }

    internal sealed class NativeTracer
    {
        internal const string NativeName = "TGPathTracing.Native.dll";
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint GetAbiFn();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int ReadStatusFn(ref NativeStatus status);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int TextFn(byte[] buffer, uint size);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private unsafe delegate int GetYardstickFn(NativeYardstick* yardstick);
        private GetYardstickFn getYardstick;
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private unsafe delegate int GetWalkPixelsFn(uint* frameIndex, uint* onSubsurface, uint* walked);
        private GetWalkPixelsFn getWalkPixels;
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int RegisterMeshFn(ref NativeMeshDesc mesh);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int RegisterCpuMeshFn(ref NativeCpuMeshDesc mesh);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int RegisterSkinnedMeshFn(ref NativeCpuMeshDesc mesh, [In] uint[] submeshStarts, uint submeshCount);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetSkinnedFn(ulong buffer, uint stride, [In] NativeSkinnedInstance[] instances, uint count);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetClothWindFn(float time, [In] NativeClothWind[] winds, uint count);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetPlantWindFn(ref NativePlantWindFrame frame, [In] NativePlantWind[] winds, uint count);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetWindNoiseFn(uint key, uint width, uint height, uint flags, [In] byte[] texels);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int RegisterTextureFn(ulong key, ulong texture);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int RegisterAlphaTextureFn(ulong key, ulong texture, float cutoff);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int RegisterMaskTextureFn(ulong key, ulong texture, uint channels);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int RegisterHairColorFn(ulong key, ref NativeHairColor hair);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int RegisterHairLobesFn(ulong key, ref NativeHairLobes lobes);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int ReleaseHairColorFn(ulong key);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int RegisterEyeColorFn(ulong key, ref NativeEyeColor eye);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetMaterialsFn(uint first, [In] NativeMaterial[] materials, uint count);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetMaterialProgramsFn(uint first, [In] uint[] rows, uint count);
        private SetMaterialProgramsFn setMaterialPrograms;
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetSceneFn([In] NativeInstance[] instances, uint count, [In] uint[] indices, uint indexCount);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetMovingFn([In] NativeMoving[] things, uint count, [In] uint[] indices, uint indexCount, [In] uint[] replaced, uint replacedCount);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetLightsFn([In] NativeLight[] lights, uint count);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetSkyFn(ulong texture);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int ClearFn();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SubmitFrameFn(ref NativeFrame frame);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetCompositeFn(uint sequence, ulong color);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetDepthPyramidFn(uint sequence, ulong pyramid);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int MainClockFn(uint sequence, uint frameCount, long[] clocks, uint count);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetWaterCausticsFn([In] NativeWaterCaustics[] surfaces, uint count);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetProfileReflectanceFn(float[] profiles, uint count);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr GetEventFn();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int GetGuidesFn(ref NativeGuides guides);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int RegisterSharedTextureFn(ulong texture, ref NativeSharedTexture info);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SwapSharedTextureFn(ulong oldTexture, ulong newTexture);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)] private static extern IntPtr GetModuleHandleW(string name);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)] private static extern IntPtr GetProcAddress(IntPtr module, string name);

        private ReadStatusFn readStatus; private TextFn unsupportedReason, stageTimes;
        private RegisterMeshFn registerMesh; private RegisterCpuMeshFn registerCpuMesh; private RegisterTextureFn registerTexture, registerDetailTexture, registerCookie;
        private RegisterAlphaTextureFn registerAlphaTexture;
        private RegisterMaskTextureFn registerMaskTexture;
        private RegisterHairColorFn registerHairColor;
        private ReleaseHairColorFn releaseHairColor;
        private RegisterHairLobesFn registerHairLobes;
        private RegisterEyeColorFn registerEyeColor;
        private RegisterSkinnedMeshFn registerSkinnedMesh; private SetSkinnedFn setSkinned; private SetClothWindFn setClothWind;
        private SetPlantWindFn setPlantWind; private SetWindNoiseFn setWindNoise;
        // PT-108, on trial: TGPT_SetRain. Bound if the native side has it (one from before does not: the trial then does
        // nothing, and RainWetness says so once), so a plugin with it still starts on an older native side.
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetRainFn(ref NativeRain rain);
        private SetRainFn setRain;
        internal bool HasRain => setRain != null;
        internal int SetRain(ref NativeRain rain) => Connected && setRain != null ? setRain(ref rain) : -100;
        // PT-112: TGPT_SetFogBoxes. Bound if the native side has it (FogBoxes says so once when it does not).
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetFogBoxesFn([In] NativeFogBox[] boxes, uint count);
        private SetFogBoxesFn setFogBoxes;
        internal bool HasFogBoxes => setFogBoxes != null;
        // PT-112, the sky, on trial: TGPT_SetSkyModel. Bound if the native side has it (SkyModel leaves HDRP's sky as it is
        // when it does not).
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetSkyModelFn(ref NativeSky sky);
        private SetSkyModelFn setSkyModel;
        internal bool HasSkyModel => setSkyModel != null;
        internal int SetSkyModel(ref NativeSky sky) => Connected && setSkyModel != null ? setSkyModel(ref sky) : -100;
        internal int SetFogBoxes(NativeFogBox[] boxes, int count) => Connected && setFogBoxes != null ? setFogBoxes(boxes, (uint)Math.Max(0, Math.Min(count, boxes.Length))) : -100;
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int SetDecalsFn([In] NativeDecal[] decals, uint count);
        private SetDecalsFn setDecals;
        internal int SetDecals(NativeDecal[] decals)
        {
            int result = Connected ? setDecals(decals, (uint)decals.Length) : -100;
            if (MissingWorld.Enabled) MissingWorld.Decals(decals, result);
            return result;
        }
        private SetMaterialsFn setMaterials; private SetSceneFn setScene; private SetMovingFn setMoving; private SetLightsFn setLights; private SetSkyFn setSky;
        private ClearFn clearScene; private SubmitFrameFn submitFrame; private SetCompositeFn setComposite; private GetGuidesFn getGuides;
        private SetDepthPyramidFn setDepthPyramid;

        private MainClockFn mainClock;
        private SetWaterCausticsFn setWaterCaustics;
        private SetProfileReflectanceFn setProfileReflectance;
        private RegisterSharedTextureFn registerSharedTexture;
        private SwapSharedTextureFn swapSharedTexture;
        internal IntPtr RenderEvent { get; private set; }
        internal bool Connected => readStatus != null && submitFrame != null && RenderEvent != IntPtr.Zero;
        internal string Detail { get; private set; } = NativeName + " is not loaded. It starts through TG Graphics Bridge: check BepInEx/TGGraphicsBridge/Autoload.log " +
            "and BepInEx/TGPathTracing/graphics-bridge-extension.json; start the game normally through Steam.";

        internal static unsafe void Validate()
        {
            if (Marshal.SizeOf(typeof(NativeSubmesh)) != 16 || Marshal.SizeOf(typeof(NativeMeshDesc)) != 192 || Marshal.SizeOf(typeof(NativeCpuMeshDesc)) != 56 ||
                Marshal.SizeOf(typeof(NativeMaterial)) != 204 || Marshal.SizeOf(typeof(NativeInstance)) != 68 || Marshal.SizeOf(typeof(NativeMoving)) != 116 || Marshal.SizeOf(typeof(NativeDecal)) != 928 || Marshal.SizeOf(typeof(NativeLight)) != 80 ||
                sizeof(NativeFrame) != 624 || Marshal.SizeOf(typeof(NativeStatus)) != 232 || Marshal.SizeOf(typeof(NativeGuides)) != 64 ||
                Marshal.SizeOf(typeof(NativeSkinnedInstance)) != 100 || Marshal.SizeOf(typeof(NativeWaterCaustics)) != 288 ||
                Marshal.SizeOf(typeof(NativeHairLobes)) != 48 || Marshal.SizeOf(typeof(NativeSharedTexture)) != 96 ||
                Marshal.SizeOf(typeof(NativeClothWind)) != 48 || sizeof(NativeYardstick) != 632 || Marshal.SizeOf(typeof(NativeRain)) != 64 ||
                Marshal.SizeOf(typeof(NativePlantWind)) != 48 || Marshal.SizeOf(typeof(NativePlantWindFrame)) != 32 || Marshal.SizeOf(typeof(NativeFogBox)) != 144)
                throw new InvalidOperationException("TG Path Tracing native ABI packing mismatch.");
        }
        private static T Bind<T>(IntPtr module, string symbol) where T : Delegate
        {
            IntPtr address = GetProcAddress(module, symbol);
            if (address == IntPtr.Zero) throw new EntryPointNotFoundException(symbol);
            return Marshal.GetDelegateForFunctionPointer<T>(address);
        }
        internal bool Discover()
        {
            if (Connected) return true;
            try
            {
                var module = GetModuleHandleW(NativeName);
                if (module == IntPtr.Zero) return false;
                uint abi = Bind<GetAbiFn>(module, "TGPT_GetAbiVersion")();
                if (abi != Tgpt.Abi) { Detail = "Native ABI 0x" + abi.ToString("X8") + " does not match this plugin's 0x" + Tgpt.Abi.ToString("X8") + ": reinstall TG Path Tracing."; return false; }
                unsupportedReason = Bind<TextFn>(module, "TGPT_GetUnsupportedReason");
                stageTimes = Bind<TextFn>(module, "TGPT_GetStageTimes");
                getYardstick = Bind<GetYardstickFn>(module, "TGPT_GetYardstick");
                getWalkPixels = Bind<GetWalkPixelsFn>(module, "TGPT_GetWalkPixels");
                registerMesh = Bind<RegisterMeshFn>(module, "TGPT_RegisterMesh");
                registerCpuMesh = Bind<RegisterCpuMeshFn>(module, "TGPT_RegisterCpuMesh");
                registerSkinnedMesh = Bind<RegisterSkinnedMeshFn>(module, "TGPT_RegisterSkinnedMesh");
                setSkinned = Bind<SetSkinnedFn>(module, "TGPT_SetSkinned");
                setClothWind = Bind<SetClothWindFn>(module, "TGPT_SetClothWind");
                setPlantWind = Bind<SetPlantWindFn>(module, "TGPT_SetPlantWind");
                setWindNoise = Bind<SetWindNoiseFn>(module, "TGPT_SetWindNoise");
                { IntPtr rain = GetProcAddress(module, "TGPT_SetRain"); setRain = rain != IntPtr.Zero ? Marshal.GetDelegateForFunctionPointer<SetRainFn>(rain) : null; }
                { IntPtr fogBoxes = GetProcAddress(module, "TGPT_SetFogBoxes"); setFogBoxes = fogBoxes != IntPtr.Zero ? Marshal.GetDelegateForFunctionPointer<SetFogBoxesFn>(fogBoxes) : null; }
                { IntPtr skyModel = GetProcAddress(module, "TGPT_SetSkyModel"); setSkyModel = skyModel != IntPtr.Zero ? Marshal.GetDelegateForFunctionPointer<SetSkyModelFn>(skyModel) : null; }
                registerTexture = Bind<RegisterTextureFn>(module, "TGPT_RegisterTexture");
                registerDetailTexture = Bind<RegisterTextureFn>(module, "TGPT_RegisterDetailTexture");
                registerAlphaTexture = Bind<RegisterAlphaTextureFn>(module, "TGPT_RegisterAlphaTexture");
                registerMaskTexture = Bind<RegisterMaskTextureFn>(module, "TGPT_RegisterMaskTexture");
                registerHairColor = Bind<RegisterHairColorFn>(module, "TGPT_RegisterHairColor");
                releaseHairColor = Bind<ReleaseHairColorFn>(module, "TGPT_ReleaseHairColor");
                registerHairLobes = Bind<RegisterHairLobesFn>(module, "TGPT_RegisterHairLobes");
                registerEyeColor = Bind<RegisterEyeColorFn>(module, "TGPT_RegisterEyeColor");
                registerCookie = Bind<RegisterTextureFn>(module, "TGPT_RegisterCookie");
                setMaterials = Bind<SetMaterialsFn>(module, "TGPT_SetMaterials");
                setMaterialPrograms = Bind<SetMaterialProgramsFn>(module, "TGPT_SetMaterialPrograms");
                setDecals = Bind<SetDecalsFn>(module, "TGPT_SetDecals");
                setScene = Bind<SetSceneFn>(module, "TGPT_SetScene");
                setMoving = Bind<SetMovingFn>(module, "TGPT_SetMoving");
                setLights = Bind<SetLightsFn>(module, "TGPT_SetLights");
                setSky = Bind<SetSkyFn>(module, "TGPT_SetSky");
                clearScene = Bind<ClearFn>(module, "TGPT_ClearScene");
                readDiagnostics = Bind<ReadDiagnosticsFn>(module, "TGPT_ReadDiagnostics");
                releaseMesh = Bind<ReleaseMeshFn>(module, "TGPT_ReleaseMesh");
                setComposite = Bind<SetCompositeFn>(module, "TGPT_SetComposite");
                setDepthPyramid = Bind<SetDepthPyramidFn>(module, "TGPT_SetDepthPyramid");
                mainClock = Bind<MainClockFn>(module, "TGPT_MainClock");
                setWaterCaustics = Bind<SetWaterCausticsFn>(module, "TGPT_SetWaterCaustics");
                setProfileReflectance = Bind<SetProfileReflectanceFn>(module, "TGPT_SetProfileReflectance");
                getGuides = Bind<GetGuidesFn>(module, "TGPT_GetGuides");
                registerSharedTexture = Bind<RegisterSharedTextureFn>(module, "TGPT_RegisterSharedTexture");
                swapSharedTexture = Bind<SwapSharedTextureFn>(module, "TGPT_SwapSharedTexture");
                var submit = Bind<SubmitFrameFn>(module, "TGPT_SubmitFrame");
                var read = Bind<ReadStatusFn>(module, "TGPT_ReadStatus");
                RenderEvent = Bind<GetEventFn>(module, "TGPT_GetRenderEvent")();
                if (RenderEvent == IntPtr.Zero) throw new InvalidOperationException("No render event callback.");
                submitFrame = submit; readStatus = read;
                Detail = "Bound " + NativeName + ", TG Graphics Bridge's path tracing extension, ABI 1.3.";
                return true;
            }
            catch (Exception ex) { readStatus = null; submitFrame = null; Detail = "Native binding failed: " + ex.Message; return false; }
        }
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int ReadDiagnosticsFn(ref NativeDiagnostics diagnostics);
        private ReadDiagnosticsFn readDiagnostics;
        internal bool ReadDiagnostics(out NativeDiagnostics diagnostics)
        {
            diagnostics = new NativeDiagnostics { Size = 176, Abi = Tgpt.Abi };
            return Connected && readDiagnostics(ref diagnostics) == 0;
        }
        // The record grew at its end on 2026-10-09 (PT-98: what the native side still owes). A native side from before
        // refuses the whole record and is asked for the part it knows (StatusSizeBefore): the new fields stay 0, and
        // ArrivalCounted says the native side does not count them.
        private const uint StatusSizeBefore = 216;
        private bool statusBefore;
        internal bool ArrivalCounted => Connected && !statusBefore;
        internal bool ReadStatus(out NativeStatus status)
        {
            status = new NativeStatus { Size = 232, Abi = Tgpt.Abi };
            if (!Connected) return false;
            if (!statusBefore)
            {
                int whole = readStatus(ref status);
                if (whole == 0) return true;
                if (whole != -1) return false; // -1: the record's size (or the ABI) was refused; anything else is no reason to ask for less
                status = new NativeStatus { Size = StatusSizeBefore, Abi = Tgpt.Abi };
                if (readStatus(ref status) != 0) return false;
                statusBefore = true;
                return true;
            }
            status.Size = StatusSizeBefore;
            return readStatus(ref status) == 0;
        }
        internal string UnsupportedReason() => Text(unsupportedReason);
        // What each stage of a frame costs the GPU, averaged over about the last second (the F8 report).
        internal string StageTimes() => Text(stageTimes);
        // The yardstick's numbers of the last frame counted; false until one was.
        internal unsafe bool Yardstick(out NativeYardstick yardstick)
        {
            yardstick = new NativeYardstick { Size = (uint)sizeof(NativeYardstick), Abi = Tgpt.Abi };
            if (getYardstick == null) return false;
            fixed (NativeYardstick* y = &yardstick) return getYardstick(y) == 0 && yardstick.FrameIndex != 0 && yardstick.Pixels != 0;
        }
        // PT-40, who takes the scattering walk, of the frame the yardstick last counted: the pixels on a material light walks
        // inside, and those of them that took the walk in that frame. False until a frame was counted.
        internal unsafe bool WalkPixels(out uint onSubsurface, out uint walked)
        {
            onSubsurface = walked = 0;
            if (getWalkPixels == null) return false;
            uint frame = 0, on = 0, took = 0;
            if (getWalkPixels(&frame, &on, &took) != 0 || frame == 0) return false;
            onSubsurface = on; walked = took;
            return true;
        }
        private static string Text(TextFn read)
        {
            if (read == null) return "";
            var buffer = new byte[1024];
            int length = read(buffer, (uint)buffer.Length);
            return length > 0 ? Encoding.UTF8.GetString(buffer, 0, Math.Min(length, buffer.Length - 1)) : "";
        }
        internal int RegisterMesh(ref NativeMeshDesc mesh) => Connected ? registerMesh(ref mesh) : -100;
        internal int RegisterCpuMesh(ref NativeCpuMeshDesc mesh) => Connected ? registerCpuMesh(ref mesh) : -100;
        // A character's triangles (indices into its own vertices; submeshes as consecutive ranges).
        // uvs: two floats a vertex (their textures: colors and alpha tests), or null.
        internal int RegisterSkinnedMesh(long key, uint[] indices, uint vertexCount, uint[] submeshStarts, float[] uvs)
        {
            if (!Connected) return -100;
            if (uvs != null && uvs.Length != vertexCount * 2) uvs = null;
            var handle = GCHandle.Alloc(indices, GCHandleType.Pinned);
            var uvHandle = uvs != null ? GCHandle.Alloc(uvs, GCHandleType.Pinned) : default;
            try
            {
                var d = new NativeCpuMeshDesc { Size = (uint)Marshal.SizeOf(typeof(NativeCpuMeshDesc)), Abi = Tgpt.Abi, Key = (ulong)key,
                    Indices = handle.AddrOfPinnedObject(), Uvs = uvs != null ? uvHandle.AddrOfPinnedObject() : IntPtr.Zero,
                    VertexCount = vertexCount, IndexCount = (uint)indices.Length };
                return registerSkinnedMesh(ref d, submeshStarts, (uint)submeshStarts.Length);
            }
            finally { handle.Free(); if (uvs != null) uvHandle.Free(); }
        }
        // The game's shader time this frame and the wind on every material it sways as cloth (kept until the next call).
        internal int SetClothWind(float time, NativeClothWind[] winds, uint count) =>
            Connected ? setClothWind(time, winds ?? new NativeClothWind[0], count) : -100;
        // This frame's wind on the game's grass, leaves and trunks, and every such material's own values (kept until the next call).
        internal int SetPlantWind(ref NativePlantWindFrame frame, NativePlantWind[] winds, uint count) =>
            Connected ? setPlantWind(ref frame, winds ?? new NativePlantWind[0], count) : -100;
        // A wind's noise map: one byte a texel, the row at v = 0 first (kept until the scene is cleared).
        internal int SetWindNoise(uint key, uint width, uint height, uint flags, byte[] texels) =>
            Connected && texels != null && texels.Length >= (long)width * height ? setWindNoise(key, width, height, flags, texels) : -100;
        // stride: a vertex's bytes in bits 0-15; on trial (PT-92), the byte of its skinned normal in bits 16-23 (SkinnedNormalShift).
        internal int SetSkinned(IntPtr buffer, uint stride, NativeSkinnedInstance[] instances, uint count)
        {
            int result = Connected ? setSkinned(unchecked((ulong)buffer.ToInt64()), stride, instances ?? new NativeSkinnedInstance[0], count) : -100;
            if (MissingWorld.Enabled) MissingWorld.Skinned(instances, result);
            return result;
        }
        internal int RegisterTexture(ulong key, IntPtr texture) => Connected ? registerTexture(key, unchecked((ulong)texture.ToInt64())) : -100;
        // A 512x512 detail tile for a texture the camera sees from the atlas (see-through glass), numbered from 4096; <0 once taken.
        internal int RegisterDetailTexture(ulong key, IntPtr texture) => Connected ? registerDetailTexture(key, unchecked((ulong)texture.ToInt64())) : -100;
        internal int RegisterAlphaTexture(ulong key, IntPtr texture, float cutoff) => Connected ? registerAlphaTexture(key, unchecked((ulong)texture.ToInt64()), cutoff) : -100;
        // channels: the texture's channel (0 red to 3 alpha) holding metallic, and smoothness's two bits above it.
        internal int RegisterMaskTexture(ulong key, IntPtr texture, uint channels) => Connected ? registerMaskTexture(key, unchecked((ulong)texture.ToInt64()), channels) : -100;
        // RealHair's color baked into a color tile, or <0: a detail tile (FirstDetailTile and up) shared by hair of the same maps and
        // values, while the pool has one, else a 64x64 tile of its own. The same key again (colored anew): the tile that holds the
        // new colors, which may be another one.
        internal int RegisterHairColor(ulong key, ref NativeHairColor hair) => Connected ? registerHairColor(key, ref hair) : -100;
        // A hair material that is gone lets go of its tile; 1 when a detail tile went back to the pool.
        internal int ReleaseHairColor(ulong key) => Connected ? releaseHairColor(key) : -100;
        internal int RegisterHairLobes(ulong key, ref NativeHairLobes lobes) => Connected ? registerHairLobes(key, ref lobes) : -100;
        // An eye's color baked into a detail tile (a 64x64 one once those are taken); the same key returns its tile, or <0.
        internal int RegisterEyeColor(ulong key, ref NativeEyeColor eye) => Connected ? registerEyeColor(key, ref eye) : -100;
        internal int RegisterCookie(ulong key, IntPtr texture) => Connected ? registerCookie(key, unchecked((ulong)texture.ToInt64())) : -100;
        internal int SetMaterials(uint first, NativeMaterial[] materials, uint count) => Connected ? setMaterials(first, materials, count) : -100;
        // PT-64: the material programs' rows, four words each (count rows from row first; 0, null, 0 forgets them all).
        internal int SetMaterialPrograms(uint first, uint[] rows, uint count) => Connected && setMaterialPrograms != null ? setMaterialPrograms(first, rows, count) : -100;
        internal int SetScene(NativeInstance[] instances, uint count, uint[] indices, uint indexCount)
        {
            int result = Connected ? setScene(instances, count, indices, indexCount) : -100;
            if (MissingWorld.Enabled) MissingWorld.Scene(instances, indices, result);
            return result;
        }
        internal int SetMoving(NativeMoving[] things, uint count, uint[] indices, uint indexCount, uint[] replaced, uint replacedCount)
        {
            int result = Connected ? setMoving(things ?? new NativeMoving[0], count, indices ?? new uint[0], indexCount, replaced ?? new uint[0], replacedCount) : -100;
            if (MissingWorld.Enabled) MissingWorld.Moving(things, indices, replaced, result);
            return result;
        }
        internal int SetLights(NativeLight[] lights, uint count) => Connected ? setLights(lights, count) : -100;
        internal int SetSky(IntPtr texture) => Connected ? setSky(unchecked((ulong)texture.ToInt64())) : -100;
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int ReleaseMeshFn(int handle);
        private ReleaseMeshFn releaseMesh;
        internal int ReleaseMesh(int handle) => Connected ? releaseMesh(handle) : -100;
        internal int ClearScene()
        {
            int result = Connected ? clearScene() : -100;
            if (result == 0 && MissingWorld.Enabled) MissingWorld.Arm(true);
            return result;
        }
        internal int SubmitFrame(ref NativeFrame frame) => Connected ? submitFrame(ref frame) : -100;
        internal int SetComposite(uint sequence, IntPtr color) => Connected ? setComposite(sequence, unchecked((ulong)color.ToInt64())) : -100;
        // PT-112 (A), on trial: HDRP's depth pyramid for the frame's own depth (TGPT_SetDepthPyramid).
        internal int SetDepthPyramid(uint sequence, IntPtr pyramid) => Connected && setDepthPyramid != null ? setDepthPyramid(sequence, unchecked((ulong)pyramid.ToInt64())) : -100;
        // PT-107 (R19), logging only: the main thread's clock for the frame with this sequence (TGPT_MainClock, 5 values).
        internal int MainClock(uint sequence, int frameCount, long[] clocks) =>
            Connected && mainClock != null && clocks != null ? mainClock(sequence, unchecked((uint)frameCount), clocks, (uint)clocks.Length) : -100;
        // Traced reflections on water for render event 4: HDRP's transparent SSR texture, its depth-stencil and normal buffers
        // at that step, and its reflection lighting multiplier for the frame.
        internal int SetWaterCaustics(NativeWaterCaustics[] surfaces, uint count) =>
            Connected && setWaterCaustics != null ? setWaterCaustics(surfaces, count) : -100;
        // Per diffusion profile: HDRP's mirror reflectance of its index of refraction (a leaf's, in place of 0.04), 0 none.
        internal int SetProfileReflectance(float[] fresnel0, uint count) =>
            Connected && setProfileReflectance != null ? setProfileReflectance(fresnel0, count) : -100;
        // PT-45: a texture of the game's (its native pointer) opened by the tracer's D3D12 device with no copy: its number, or
        // why it has none (Tgpt.Share*); info describes it and the registry's totals. The same texture again: the same number.
        internal int RegisterSharedTexture(IntPtr texture, out NativeSharedTexture info)
        {
            info = new NativeSharedTexture { Size = 96, Abi = Tgpt.Abi, Handle = -1 };
            return Connected && registerSharedTexture != null ? registerSharedTexture(unchecked((ulong)texture.ToInt64()), ref info) : -100;
        }
        // PT-45 phase 3: mip streaming made a hair or eye map anew (another native pointer): what was baked from the old one reads
        // the new one (register the new one first). The old pointer is never dereferenced.
        internal int SwapSharedTexture(IntPtr oldTexture, IntPtr newTexture) =>
            Connected && swapSharedTexture != null ? swapSharedTexture(unchecked((ulong)oldTexture.ToInt64()), unchecked((ulong)newTexture.ToInt64())) : -100;
        internal bool ReadGuides(out NativeGuides guides)
        {
            guides = new NativeGuides { Size = 64, Abi = Tgpt.Abi };
            return Connected && getGuides != null && getGuides(ref guides) == 0;
        }
    }
}
