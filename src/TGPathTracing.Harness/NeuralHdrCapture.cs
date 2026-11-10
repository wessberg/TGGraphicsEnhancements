// SPDX-License-Identifier: MIT
// Opt-in final-output capture requires ReShade and RenoDX to be enabled externally.
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using UnityEngine;
namespace TGPathTracing.Harness
{
    internal static class NeuralHdrCapture
    {
        [DllImport("kernel32", CharSet = CharSet.Unicode)] private static extern IntPtr GetModuleHandle(string name);
        [DllImport("kernel32", CharSet = CharSet.Ansi)] private static extern IntPtr GetProcAddress(IntPtr module, string name);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        private delegate int Request([MarshalAs(UnmanagedType.LPWStr)] string path, int frame);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int Status(out int width, out int height, out int format);
        internal static IEnumerator Burst(string dir, Step step, StepResult result)
        {
            var module = GetModuleHandle("TGNR.HdrCapture.addon64");
            var requestAddress = module == IntPtr.Zero ? IntPtr.Zero : GetProcAddress(module, "TGNR_HDR_Request");
            var statusAddress = module == IntPtr.Zero ? IntPtr.Zero : GetProcAddress(module, "TGNR_HDR_Status");
            if (requestAddress == IntPtr.Zero || statusAddress == IntPtr.Zero)
            { result.error += " The temporary HDR capture add-on is not loaded by ReShade."; yield break; }
            if (!step.neural.HasValue || step.slide != 0 || step.walkForward != 0 || step.perfSeconds != 0)
            { result.error += " HDR capture requires an explicit stationary NR comparison without performance timing."; yield break; }
            var request = (Request)Marshal.GetDelegateForFunctionPointer(requestAddress, typeof(Request));
            var status = (Status)Marshal.GetDelegateForFunctionPointer(statusAddress, typeof(Status));
            var frames = new List<FrameInfo>();
            for (int i = 0; i < step.frames; ++i)
            {
                for (int j = 1; j < Math.Max(1, step.stride); ++j) yield return null;
                yield return new WaitForEndOfFrame();
                if (!Runner.GameplayReady(out string failure))
                { result.menuOpen = true; result.error += " HDR capture left gameplay: " + failure; break; }
                var path = Path.Combine(dir, "f" + i.ToString("00") + ".hdrraw");
                var info = new FrameInfo { file = Path.GetFileName(path), frame = Time.frameCount,
                    realtime = Time.realtimeSinceStartup, scaledTime = Time.time };
                Neural.Sample(info);
                var camera = Camera.main;
                if (camera != null)
                {
                    var p = camera.transform.position; var e = camera.transform.eulerAngles;
                    info.px = p.x; info.py = p.y; info.pz = p.z; info.rx = e.x; info.ry = e.y; info.rz = e.z; info.fov = camera.fieldOfView;
                }
                if (request(path, info.frame) != 1) { result.error += " HDR capture request was refused."; break; }
                float deadline = Time.realtimeSinceStartup + 15;
                int state, width, height, format;
                do
                {
                    yield return null; // EndOfFrame precedes the presentation callback being captured.
                    state = status(out width, out height, out format);
                } while (state == 1 && Time.realtimeSinceStartup < deadline);
                if (state != 2)
                {
                    result.error += " HDR capture failed or timed out. " + (File.Exists(path + ".error.txt") ? File.ReadAllText(path + ".error.txt") : "");
                    break;
                }
                result.width = width; result.height = height; result.scale = 1; result.format = "PresentedHDR/" + format;
                frames.Add(info);
            }
            result.frames = frames.ToArray();
        }
    }
}
