// SPDX-License-Identifier: MIT
// Compile with the real TextureShare.cs. Only Unity/native calls are stubbed, so registration,
// refusal reporting, retry, material repair and reset run through the production implementation.
using System;
using System.Collections.Generic;
namespace UnityEngine {
    public class Texture {
        public string name = "shield wood";
        public int width = 2048, height = 2048;
        public uint updateCount;
        public int GetInstanceID() => 7;
        public IntPtr GetNativeTexturePtr() => new IntPtr(1234);
    }
    public class Texture2D : Texture { public bool streamingMipmaps; public int loadedMipmapLevel; }
    public class Material {
        public bool HasProperty(string s) => false;
        public bool IsKeywordEnabled(string s) => false;
        public float GetFloat(string s) => 0;
        public Texture GetTexture(string s) => null;
    }
    public static class Time { public static float unscaledTime; }
}
namespace TGPathTracing {
    internal static class Tgpt {
        public const int ShareFlags=-1, ShareShape=-2, ShareRefused=-3, ShareBeforeHook=-4,
            ShareOpenFailed=-5, ShareFull=-6, ShareSelfTest=-7, ShareBudget=-8,
            ShareUnsupported=-9, ShareNotTexture=-10;
    }
    internal struct NativeSharedTexture {
        internal uint Format, Hook, SelfTest, Open, Opened, Freed;
        internal ulong Bytes, OpenBytes;
        internal int Handle;
    }
    internal struct NativeMaterial { internal int ColorTexture, NormalTexture, MaskTexture, EmissiveTexture; }
    internal class NativeTracer {
        internal bool Connected = true;
        internal int Result = Tgpt.ShareBudget, MaterialSends;
        internal int RegisterSharedTexture(IntPtr pointer, out NativeSharedTexture info) {
            info = new NativeSharedTexture { Bytes=4194304, Handle=123, Format=71, OpenBytes=4194304 };
            return Result;
        }
        internal void SwapSharedTexture(IntPtr from, IntPtr to) { }
        internal void SetMaterials(uint index, NativeMaterial[] materials, int count) { ++MaterialSends; }
    }
    internal class TestLog {
        internal string Error = "";
        internal void LogInfo(string s) { }
        internal void LogError(string s) { Error = s; }
    }
    internal static class Session { internal static NativeTracer Tracer=new NativeTracer(); internal static TestLog Log=new TestLog(); }
    internal static class MaterialPrograms { internal static void SetWord(int field, int value) { } }
    internal static class TextureShareLifetimeTest {
        static void Check(bool ok, string message) { if(!ok) throw new Exception(message); Console.WriteLine("ok: " + message); }
        static int Main() {
            var texture = new UnityEngine.Texture2D();
            TextureShare.BeginMaterial();
            Check(TextureShare.Handle(texture, TextureShare.FieldColor)==-1, "refusal supplies no substitute handle");
            TextureShare.Bind(0);
            Check(TextureShare.Incomplete, "a refused real texture blocks presentation");
            Check(TextureShare.FailureText.Contains((4194304 / 1048576.0).ToString("0.0")) && TextureShare.FailureText.Contains("paused"), "the player sees the missing memory and paused state");
            Check(Session.Log.Error.Contains("shield wood") && Session.Log.Error.Contains("2048x2048") && Session.Log.Error.Contains("4194304"), "the log names the missing texture, dimensions and bytes");
            var materials = new List<NativeMaterial> { new NativeMaterial { ColorTexture=-1 } };
            Session.Tracer.Result=2;
            UnityEngine.Time.unscaledTime=11;
            TextureShare.Refresh(materials,1,Session.Tracer);
            Check(!TextureShare.Incomplete && materials[0].ColorTexture==123 && Session.Tracer.MaterialSends==1, "retry repairs the real material before presentation resumes");
            TextureShare.Reset(); Session.Tracer.Result=Tgpt.ShareRefused;
            TextureShare.Register(texture);
            Check(TextureShare.Incomplete, "driver refusal is visible too");
            TextureShare.Reset();
            Check(!TextureShare.Incomplete, "unloaded scene cannot leave a stale refusal");
            Session.Tracer.Result=Tgpt.ShareShape;
            TextureShare.Handle(new UnityEngine.Texture());
            Check(TextureShare.Incomplete, "unsupported texture types cannot silently turn into constants or tiles");
            TextureShare.Reset();
            return 0;
        }
    }
}
