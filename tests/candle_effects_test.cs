// SPDX-License-Identifier: MIT
// Execute the real EffectPasses class against a small Unity API stand-in: checks
// restoration, output identification and discovery, not shader rendering.
using System;
using System.Collections.Generic;
namespace UnityEngine.Rendering { public struct ShaderTagId { public string name; public ShaderTagId(string v) { name=v; } } }
namespace UnityEngine {
    public static class Time { public static float unscaledTime; }
    public class Shader { public string name; public string[] modes; public Rendering.ShaderTagId FindPassTagValue(int i,Rendering.ShaderTagId tag) { return new Rendering.ShaderTagId(modes[i]); } }
    public class Material {
        public Shader shader; public int passCount { get { return shader.modes.Length; } }
        public Dictionary<string,float> values=new Dictionary<string,float>(); public HashSet<string> keywords=new HashSet<string>(); public bool distortion=true;
        public bool HasProperty(string p) { return values.ContainsKey(p); } public float GetFloat(string p) { return values[p]; }
        public void SetFloat(string p,float v) { values[p]=v; } public bool IsKeywordEnabled(string k) { return keywords.Contains(k); }
        public void EnableKeyword(string k) { keywords.Add(k); } public void DisableKeyword(string k) { keywords.Remove(k); }
        public bool GetShaderPassEnabled(string p) { return distortion; } public void SetShaderPassEnabled(string p,bool b) { distortion=b; }
    }
    public static class Resources { public static List<Material> all=new List<Material>(); public static T[] FindObjectsOfTypeAll<T>() { return (T[])(object)all.ToArray(); } }
}
namespace TGPathTracing {
    internal static class Session { internal static Logger Log=new Logger(); }
    internal class Logger { public void LogInfo(string v) { } public void LogWarning(string v) { throw new Exception(v); } }
    internal static class EffectPassesTest {
        static void Check(bool v,string why) { if(!v) throw new Exception(why); Console.WriteLine("ok: "+why); }
        static UnityEngine.Material Quad(string name) { var m=new UnityEngine.Material { shader=new UnityEngine.Shader { name="Hidden/VFX/"+name+"/Flame/Output Particle|Shader Graph Quad - Unlit", modes=new[]{"ForwardOnly"} } }; m.values["_TransparentWritingMotionVec"]=1; m.values["_SrcBlend"]=1; m.values["_DstBlend"]=1; m.values["_ZWrite"]=0; m.keywords.Add("_TRANSPARENT_WRITES_MOTION_VEC"); return m; }
        public static void Main() {
            UnityEngine.Material candle=Quad("AnUnknownFlame"), fire=Quad("ACompletelyDifferentFire"), ordinary=Quad("ParticleWithRealMotion"); ordinary.values["_AddPrecomputedVelocity"]=1;
            var haze=new UnityEngine.Material { shader=new UnityEngine.Shader { name="UnknownEffect", modes=new[]{"DistortionVectors","ForwardOnly"} } };
            var alreadyOff=new UnityEngine.Material { shader=haze.shader, distortion=false };
            UnityEngine.Resources.all.AddRange(new[]{candle,fire,ordinary,haze,alreadyOff});
            EffectPasses.Hold(true);
            Check(!haze.distortion&&!alreadyOff.distortion,"distortion output disabled by LightMode regardless of name");
            Check(candle.GetFloat("_TransparentWritingMotionVec")==0&&fire.GetFloat("_TransparentWritingMotionVec")==0,"unknown flame and fire output types keep background motion");
            Check(ordinary.GetFloat("_TransparentWritingMotionVec")==1,"precomputed particle motion left alone");
            candle.values["_TransparentWritingMotionVec"]=1; EffectPasses.Hold(true); Check(candle.GetFloat("_TransparentWritingMotionVec")==0,"tracked material state reasserted");
            var late=Quad("NewlyLoadedTorch"); late.values["_TransparentWritingMotionVec"]=0; late.keywords.Clear(); UnityEngine.Resources.all.Add(late); UnityEngine.Time.unscaledTime=2; EffectPasses.Hold(true);
            EffectPasses.Hold(false);
            Check(haze.distortion&&!alreadyOff.distortion,"Off restores enabled and previously disabled distortion states");
            Check(candle.GetFloat("_TransparentWritingMotionVec")==1&&candle.IsKeywordEnabled("_TRANSPARENT_WRITES_MOTION_VEC"),"Off restores original motion property and keyword after repeated On");
            Check(late.GetFloat("_TransparentWritingMotionVec")==0&&!late.IsKeywordEnabled("_TRANSPARENT_WRITES_MOTION_VEC"),"late loaded material's original state restored");
            EffectPasses.Hold(true); EffectPasses.Hold(false); Check(haze.distortion,"second activation and disposal restore again");
        }
    }
}
