// SPDX-License-Identifier: MIT
// Generated binding layout for the shipped decal graphs; see tools/research/lift_decal_graphs.py.
using System;
using System.Reflection;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
namespace TGPathTracing {
internal static class DecalGraphCapture {
    private const BindingFlags Any = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
    private static readonly FieldInfo GlobalBuffer = typeof(HDRenderPipeline).GetField("m_ShaderVariablesGlobalCB", Any);
    private static readonly FieldInfo TimeField = GlobalBuffer?.FieldType.GetField("_TimeParameters", Any);
    private static readonly FieldInfo ProbeField = GlobalBuffer?.FieldType.GetField("_ProbeExposureScale", Any);
    internal static uint Kind(string shader) {
        switch(shader) {
        case "KriptoFX/BFX_Decal": return 7u;
        case "TG/Decal/Decal_EmberBurning": return 8u;
        case "TG/Decal/Decal_WyrdGhost": return 9u;
        case "TG/Decal/Decal_RadialGradient_3Panning": return 10u;
        case "TG/Decal/Decal_RadialGradient_3Panning_Lit": return 11u;
        case "TG/Decal/Decal_GlowingCracks": return Shader.IsKeywordEnabled("MATERIAL_QUALITY_LOW") ? 16u : Shader.IsKeywordEnabled("MATERIAL_QUALITY_MEDIUM") ? 15u : 12u;
        case "TG/VFX/VFXGraph_Decal_FireGround": return 13u;
        case "TG/VFX/VFXGraph_Decal_ForeDwellers_Annihilation": return 14u;
        default: return 0;
        }
    }
    internal static unsafe void Read(Material material, ref NativeDecal d, Matrix4x4 world, HDCamera hd, ref bool missing) {
        // Read HDRP's current constant buffer, not an assumed exposure/time or a stale Shader global.
        object globals = GlobalBuffer?.GetValue(RenderPipelineManager.currentPipeline);
        if(globals==null || TimeField==null || ProbeField==null) { missing=true; return; }
        float time = globals != null && TimeField != null ? ((Vector4)TimeField.GetValue(globals)).x : Time.time;
        float probe = globals != null && ProbeField != null ? Convert.ToSingle(ProbeField.GetValue(globals)) : 1;
        float exposure = 1 / Mathf.Max(SceneCapture.ExposureScale(0), 1e-12f);
        d.GraphEnv = new Vector4(time, exposure, Shader.GetGlobalFloat("_GlobalSaturationAdjustment"), hd.camera.orthographic ? 1 : 0);
        d.GraphEnv2 = new Vector4(probe, 0, 0, 0);
        d.GraphViewZ = hd.camera.worldToCameraMatrix.GetRow(2); d.GraphViewZ.w = 0;
        fixed (float* p = d.Graph) fixed (int* maps = d.GraphTextures) {
            for(int i=0;i<96;++i) p[i]=0;
            for(int i=0;i<8;++i) maps[i]=-1;
            switch(d.Kind) {
            case 7u: {
                Vector4 p0 = (Vector4)DecalCapture.ColorValue(material, "_TintColor").linear;
                p[0] = p0.x;
                p[1] = p0.y;
                p[2] = p0.z;
                p[3] = p0.w;
                p[8] = DecalCapture.Float(material, "_Cutout");
                Vector4 p16 = DecalCapture.Vector(material, "_SunPos");
                p[16] = p16.x;
                p[17] = p16.y;
                p[18] = p16.z;
                p[19] = DecalCapture.Float(material, "_LightIntencity");
                maps[0] = DecalCapture.Map(material, "_MainTex", ref missing);
                maps[1] = DecalCapture.Map(material, "_CutoutTex", ref missing);
                break;
            }
            case 8u: {
                p[18] = DecalCapture.Float(material, "_Opacity");
                Vector4 p24 = (Vector4)DecalCapture.ColorValue(material, "_ColorBase").linear;
                p[24] = p24.x;
                p[25] = p24.y;
                p[26] = p24.z;
                p[27] = p24.w;
                p[28] = DecalCapture.Float(material, "_NormalDisapire");
                p[30] = DecalCapture.Float(material, "_NormalStrength");
                p[16] = DecalCapture.Float(material, "_EmissiveMaskPower");
                Vector4 p20 = (Vector4)DecalCapture.ColorValue(material, "_EmissiveColor").linear;
                p[20] = p20.x;
                p[21] = p20.y;
                p[22] = p20.z;
                p[23] = p20.w;
                p[29] = DecalCapture.Float(material, "_EmmisiveIntense");
                maps[0] = DecalCapture.Map(material, "_SampleTexture2D_6e30d047e7a147d184008ec084421565_Texture_1_Texture2D", ref missing);
                maps[1] = DecalCapture.Map(material, "_SampleTexture2D_86eadc32f6914d33a553c55af3a93f19_Texture_1_Texture2D", ref missing);
                maps[2] = DecalCapture.Map(material, "_Texture2DAsset_c2b280a6867d45dc96f983f5e1dc8f9e_Out_0_Texture2D", ref missing);
                maps[3] = DecalCapture.Map(material, "_SampleTexture2D_b6bddb7ab41a4f3dadf0cf70fc0e8863_Texture_1_Texture2D", ref missing);
                break;
            }
            case 9u: {
                Vector4 p20 = (Vector4)DecalCapture.ColorValue(material, "_Color1").linear;
                p[20] = p20.x;
                p[21] = p20.y;
                p[22] = p20.z;
                p[23] = p20.w;
                p[24] = DecalCapture.Float(material, "_UseSmoke");
                p[25] = DecalCapture.Float(material, "_FrameOffset");
                p[26] = DecalCapture.Float(material, "_Alpha");
                Vector4 p28 = DecalCapture.Vector(material, "_Speed");
                p[28] = p28.x;
                p[29] = p28.y;
                maps[0] = DecalCapture.Map(material, "_SampleTexture2D_a3a0db7dbbb24affa1c0a8a59615a9dd_Texture_1_Texture2D", ref missing);
                maps[1] = DecalCapture.Map(material, "_SampleTexture2D_b7052f5c812e4594a7af32fb9247d342_Texture_1_Texture2D", ref missing);
                maps[2] = DecalCapture.Map(material, "_SampleTexture2D_cbafffec34ad4f0cb877722c4f52962b_Texture_1_Texture2D", ref missing);
                maps[3] = DecalCapture.Map(material, "_SampleTexture2D_cd7b6aeaeda64cbca468ed078882de9e_Texture_1_Texture2D", ref missing);
                maps[4] = DecalCapture.Map(material, "_SampleTexture2D_f9b71e63f7b04bf8a9c18bfa3c518198_Texture_1_Texture2D", ref missing);
                break;
            }
            case 10u: {
                Vector4 p0 = (Vector4)DecalCapture.ColorValue(material, "_ColorInside").linear;
                p[0] = p0.x;
                p[1] = p0.y;
                p[2] = p0.z;
                p[3] = p0.w;
                Vector4 p4 = (Vector4)DecalCapture.ColorValue(material, "_Color").linear;
                p[4] = p4.x;
                p[5] = p4.y;
                p[6] = p4.z;
                p[7] = p4.w;
                p[8] = DecalCapture.Float(material, "_Gradient1Sharpness");
                p[9] = DecalCapture.Float(material, "_Gradient1Size");
                p[10] = DecalCapture.Float(material, "_Gradient2Sharpness");
                Vector4 p16 = DecalCapture.Vector(material, "_Tiling");
                p[16] = p16.x;
                p[17] = p16.y;
                Vector4 p18 = DecalCapture.Vector(material, "_PanningSpeed");
                p[18] = p18.x;
                p[19] = p18.y;
                p[20] = DecalCapture.Float(material, "_Gradient1AddToAlpha");
                p[21] = DecalCapture.Float(material, "_GradientStrength");
                p[22] = DecalCapture.Float(material, "_EffectValue");
                p[23] = DecalCapture.Float(material, "_Gradient2AddToAlpha");
                Vector4 p24 = DecalCapture.Vector(material, "_CameraFade");
                p[24] = p24.x;
                p[25] = p24.y;
                p[27] = DecalCapture.Float(material, "_Gradient2Size");
                p[28] = DecalCapture.Float(material, "_PosYAdd");
                p[29] = DecalCapture.Float(material, "_SpeedMultiplier");
                p[30] = DecalCapture.Float(material, "_PosYMultiply");
                p[31] = DecalCapture.Float(material, "_UseYFade");
                p[32] = DecalCapture.Float(material, "_UseTime");
                Vector4 p33 = DecalCapture.Vector(material, "_UVFix");
                p[33] = p33.x;
                p[34] = p33.y;
                p[35] = p33.z;
                Vector4 p36 = DecalCapture.Vector(material, "_Tilling2nd3rdNoisePanner");
                p[36] = p36.x;
                p[37] = p36.y;
                p[38] = DecalCapture.Float(material, "_ChargeLevel");
                p[26] = DecalCapture.Float(material, "_EmissionIntensity");
                maps[0] = DecalCapture.Map(material, "_Noise", ref missing);
                break;
            }
            case 11u: {
                Vector4 p0 = (Vector4)DecalCapture.ColorValue(material, "_ColorRim").linear;
                p[0] = p0.x;
                p[1] = p0.y;
                p[2] = p0.z;
                p[3] = p0.w;
                Vector4 p4 = (Vector4)DecalCapture.ColorValue(material, "_ColorEnd").linear;
                p[4] = p4.x;
                p[5] = p4.y;
                p[6] = p4.z;
                p[7] = p4.w;
                Vector4 p8 = (Vector4)DecalCapture.ColorValue(material, "_Color").linear;
                p[8] = p8.x;
                p[9] = p8.y;
                p[10] = p8.z;
                p[11] = p8.w;
                p[12] = DecalCapture.Float(material, "_GradientAffectsColor");
                p[20] = DecalCapture.Float(material, "_Compare");
                p[21] = DecalCapture.Float(material, "_Gradient1Sharpness");
                p[22] = DecalCapture.Float(material, "_Gradient1Size");
                p[23] = DecalCapture.Float(material, "_NormalStrength");
                p[25] = DecalCapture.Float(material, "_Gradient2Sharpness");
                Vector4 p32 = DecalCapture.Vector(material, "_Tiling");
                p[32] = p32.x;
                p[33] = p32.y;
                Vector4 p34 = DecalCapture.Vector(material, "_PanningSpeed");
                p[34] = p34.x;
                p[35] = p34.y;
                p[36] = DecalCapture.Float(material, "_Gradient1AddToAlpha");
                p[37] = DecalCapture.Float(material, "_Smoothness");
                p[38] = DecalCapture.Float(material, "_GradientStrength");
                p[39] = DecalCapture.Float(material, "_EffectValue");
                p[40] = DecalCapture.Float(material, "_Gradient2AddToAlpha");
                Vector4 p41 = DecalCapture.Vector(material, "_CameraFade");
                p[41] = p41.x;
                p[42] = p41.y;
                p[43] = DecalCapture.Float(material, "_Gradient2Size");
                p[44] = DecalCapture.Float(material, "_PosYAdd");
                p[45] = DecalCapture.Float(material, "_SpeedMultiplier");
                p[46] = DecalCapture.Float(material, "_PosYMultiply");
                p[47] = DecalCapture.Float(material, "_UseYFade");
                p[48] = DecalCapture.Float(material, "_UseTime");
                p[49] = DecalCapture.Float(material, "_UsePolarCoordinates");
                p[51] = DecalCapture.Float(material, "_UseRim");
                Vector4 p52 = DecalCapture.Vector(material, "_BottomCenterCut");
                p[52] = p52.x;
                p[53] = p52.y;
                p[54] = p52.z;
                p[55] = DecalCapture.Float(material, "_AlphaOutput");
                p[56] = DecalCapture.Float(material, "_ChargeLevel");
                p[50] = DecalCapture.Float(material, "_Emission");
                maps[0] = DecalCapture.Map(material, "_NormalMap", ref missing);
                maps[1] = DecalCapture.Map(material, "_Noise", ref missing);
                break;
            }
            case 12u: {
                p[7] = DecalCapture.Float(material, "_Normal_strength");
                Vector4 p16 = (Vector4)DecalCapture.ColorValue(material, "_Inner_Color").linear;
                p[16] = p16.x;
                p[17] = p16.y;
                p[18] = p16.z;
                p[19] = p16.w;
                p[20] = DecalCapture.Float(material, "_Parallax_Amplitude_1");
                p[28] = DecalCapture.Float(material, "_Diffuse_to_Alpha_Min");
                Vector4 p29 = DecalCapture.Vector(material, "_Master_Tiling");
                p[29] = p29.x;
                p[30] = p29.y;
                p[36] = DecalCapture.Float(material, "_Pallarax_Amplitude_3");
                Vector4 p37 = DecalCapture.Vector(material, "_Map_1_Tiling");
                p[37] = p37.x;
                p[38] = p37.y;
                Vector4 p44 = DecalCapture.Vector(material, "_Heigth_Map_3_Tiling");
                p[44] = p44.x;
                p[45] = p44.y;
                p[46] = DecalCapture.Float(material, "_Mask_Power");
                p[52] = DecalCapture.Float(material, "_Inner_Color_power");
                p[54] = DecalCapture.Float(material, "_Alpha_Power");
                p[68] = DecalCapture.Float(material, "_Tint");
                Vector4 p72 = (Vector4)DecalCapture.ColorValue(material, "_TintColor").linear;
                p[72] = p72.x;
                p[73] = p72.y;
                p[74] = p72.z;
                p[75] = p72.w;
                Vector4 p5 = DecalCapture.Vector(material, "_Noise_Tiling");
                p[5] = p5.x;
                p[6] = p5.y;
                p[77] = DecalCapture.Float(material, "_EmissionPowerMask");
                Vector4 p40 = (Vector4)DecalCapture.ColorValue(material, "_Glow_Color").linear;
                p[40] = p40.x;
                p[41] = p40.y;
                p[42] = p40.z;
                p[43] = p40.w;
                p[76] = DecalCapture.Float(material, "_RotateCordinateslUV");
                p[55] = DecalCapture.Float(material, "_Glow");
                p[56] = DecalCapture.Float(material, "_Noise_Multiply");
                Vector4 p64 = DecalCapture.Vector(material, "_Noise_Speed");
                p[64] = p64.x;
                p[65] = p64.y;
                p[66] = DecalCapture.Float(material, "_PolarCoordinates");
                p[67] = DecalCapture.Float(material, "_RadialMaskEmission");
                maps[0] = DecalCapture.Map(material, "_Mask", ref missing);
                maps[1] = DecalCapture.Map(material, "_Normal_Map", ref missing);
                maps[2] = DecalCapture.Map(material, "_SampleTexture2D_2996dfed630e40f5a34708da3826001d_Texture_1_Texture2D", ref missing);
                maps[3] = DecalCapture.Map(material, "_Noise_Texture", ref missing);
                break;
            }
            case 13u: {
                p[16] = DecalCapture.Float(material, "_OnlyGlow");
                p[17] = DecalCapture.Float(material, "_Alpha");
                Vector4 p20 = (Vector4)DecalCapture.ColorValue(material, "_GlowCoor").linear;
                p[20] = p20.x;
                p[21] = p20.y;
                p[22] = p20.z;
                p[23] = p20.w;
                p[18] = DecalCapture.Float(material, "_EmissiveIntensity");
                p[24] = DecalCapture.Float(material, "_EmissiveHueShift");
                maps[0] = DecalCapture.Map(material, "_SampleTexture2D_4ae7e257997640b18a82117fde36e339_Texture_1_Texture2D", ref missing);
                maps[1] = DecalCapture.Map(material, "_SampleTexture2D_875ab4a44abc4d3f8c487961576292bb_Texture_1_Texture2D", ref missing);
                maps[2] = DecalCapture.Map(material, "_SampleTexture2D_ddb0fda3cfea48e2b3b82f0048ba2c14_Texture_1_Texture2D", ref missing);
                break;
            }
            case 14u: {
                Vector4 p16 = DecalCapture.Vector(material, "_UVSpeed2");
                p[16] = p16.x;
                p[17] = p16.y;
                Vector4 p18 = DecalCapture.Vector(material, "_UVSpeed");
                p[18] = p18.x;
                p[19] = p18.y;
                p[20] = DecalCapture.Float(material, "_UVTilling");
                p[21] = DecalCapture.Float(material, "_NormalStrengyh");
                p[29] = DecalCapture.Float(material, "_BaseColorIntense");
                p[30] = DecalCapture.Float(material, "_Animator");
                Vector4 p24 = (Vector4)DecalCapture.ColorValue(material, "_Color").linear;
                p[24] = p24.x;
                p[25] = p24.y;
                p[26] = p24.z;
                p[27] = p24.w;
                p[28] = DecalCapture.Float(material, "_EmissiveIntensity");
                maps[0] = DecalCapture.Map(material, "_SampleTexture2D_0afecae06a8f4f68b942b8e8bfeb6486_Texture_1_Texture2D", ref missing);
                maps[1] = DecalCapture.Map(material, "_SampleTexture2D_71cc351a522a4c1ba69c18b07a22ca0d_Texture_1_Texture2D", ref missing);
                maps[2] = DecalCapture.Map(material, "_SampleTexture2D_da8a60593f984e2e8b68866ce5573677_Texture_1_Texture2D", ref missing);
                maps[3] = DecalCapture.Map(material, "_SampleTexture2D_ddcf76b56c734feaa4db66dc04b0e589_Texture_1_Texture2D", ref missing);
                break;
            }
            case 15u: {
                p[7] = DecalCapture.Float(material, "_Normal_strength");
                Vector4 p16 = (Vector4)DecalCapture.ColorValue(material, "_Inner_Color").linear;
                p[16] = p16.x;
                p[17] = p16.y;
                p[18] = p16.z;
                p[19] = p16.w;
                p[20] = DecalCapture.Float(material, "_Parallax_Amplitude_1");
                p[28] = DecalCapture.Float(material, "_Diffuse_to_Alpha_Min");
                Vector4 p29 = DecalCapture.Vector(material, "_Master_Tiling");
                p[29] = p29.x;
                p[30] = p29.y;
                p[36] = DecalCapture.Float(material, "_Pallarax_Amplitude_3");
                Vector4 p37 = DecalCapture.Vector(material, "_Map_1_Tiling");
                p[37] = p37.x;
                p[38] = p37.y;
                Vector4 p44 = DecalCapture.Vector(material, "_Heigth_Map_3_Tiling");
                p[44] = p44.x;
                p[45] = p44.y;
                p[46] = DecalCapture.Float(material, "_Mask_Power");
                p[52] = DecalCapture.Float(material, "_Inner_Color_power");
                p[54] = DecalCapture.Float(material, "_Alpha_Power");
                p[68] = DecalCapture.Float(material, "_Tint");
                Vector4 p72 = (Vector4)DecalCapture.ColorValue(material, "_TintColor").linear;
                p[72] = p72.x;
                p[73] = p72.y;
                p[74] = p72.z;
                p[75] = p72.w;
                Vector4 p5 = DecalCapture.Vector(material, "_Noise_Tiling");
                p[5] = p5.x;
                p[6] = p5.y;
                p[77] = DecalCapture.Float(material, "_EmissionPowerMask");
                Vector4 p40 = (Vector4)DecalCapture.ColorValue(material, "_Glow_Color").linear;
                p[40] = p40.x;
                p[41] = p40.y;
                p[42] = p40.z;
                p[43] = p40.w;
                p[76] = DecalCapture.Float(material, "_RotateCordinateslUV");
                p[55] = DecalCapture.Float(material, "_Glow");
                p[56] = DecalCapture.Float(material, "_Noise_Multiply");
                Vector4 p64 = DecalCapture.Vector(material, "_Noise_Speed");
                p[64] = p64.x;
                p[65] = p64.y;
                p[66] = DecalCapture.Float(material, "_PolarCoordinates");
                p[67] = DecalCapture.Float(material, "_RadialMaskEmission");
                maps[0] = DecalCapture.Map(material, "_Mask", ref missing);
                maps[1] = DecalCapture.Map(material, "_Normal_Map", ref missing);
                maps[2] = DecalCapture.Map(material, "_SampleTexture2D_2996dfed630e40f5a34708da3826001d_Texture_1_Texture2D", ref missing);
                maps[3] = DecalCapture.Map(material, "_Noise_Texture", ref missing);
                break;
            }
            case 16u: {
                p[7] = DecalCapture.Float(material, "_Normal_strength");
                Vector4 p16 = (Vector4)DecalCapture.ColorValue(material, "_Inner_Color").linear;
                p[16] = p16.x;
                p[17] = p16.y;
                p[18] = p16.z;
                p[19] = p16.w;
                p[20] = DecalCapture.Float(material, "_Parallax_Amplitude_1");
                p[28] = DecalCapture.Float(material, "_Diffuse_to_Alpha_Min");
                Vector4 p29 = DecalCapture.Vector(material, "_Master_Tiling");
                p[29] = p29.x;
                p[30] = p29.y;
                Vector4 p37 = DecalCapture.Vector(material, "_Map_1_Tiling");
                p[37] = p37.x;
                p[38] = p37.y;
                p[46] = DecalCapture.Float(material, "_Mask_Power");
                p[52] = DecalCapture.Float(material, "_Inner_Color_power");
                p[54] = DecalCapture.Float(material, "_Alpha_Power");
                p[68] = DecalCapture.Float(material, "_Tint");
                Vector4 p72 = (Vector4)DecalCapture.ColorValue(material, "_TintColor").linear;
                p[72] = p72.x;
                p[73] = p72.y;
                p[74] = p72.z;
                p[75] = p72.w;
                Vector4 p5 = DecalCapture.Vector(material, "_Noise_Tiling");
                p[5] = p5.x;
                p[6] = p5.y;
                p[77] = DecalCapture.Float(material, "_EmissionPowerMask");
                Vector4 p40 = (Vector4)DecalCapture.ColorValue(material, "_Glow_Color").linear;
                p[40] = p40.x;
                p[41] = p40.y;
                p[42] = p40.z;
                p[43] = p40.w;
                p[76] = DecalCapture.Float(material, "_RotateCordinateslUV");
                p[55] = DecalCapture.Float(material, "_Glow");
                p[56] = DecalCapture.Float(material, "_Noise_Multiply");
                Vector4 p64 = DecalCapture.Vector(material, "_Noise_Speed");
                p[64] = p64.x;
                p[65] = p64.y;
                p[66] = DecalCapture.Float(material, "_PolarCoordinates");
                p[67] = DecalCapture.Float(material, "_RadialMaskEmission");
                maps[0] = DecalCapture.Map(material, "_Mask", ref missing);
                maps[1] = DecalCapture.Map(material, "_Normal_Map", ref missing);
                maps[2] = DecalCapture.Map(material, "_SampleTexture2D_2996dfed630e40f5a34708da3826001d_Texture_1_Texture2D", ref missing);
                maps[3] = DecalCapture.Map(material, "_Noise_Texture", ref missing);
                break;
            }
            }
        }
    }
}
}
