// SPDX-License-Identifier: MIT
// One frame as shown, written as its raw bytes: the screen's own pixels (what the player sees, after the upscaler and
// post-processing), so the analysis (tools/harness/analyze.py) has every pixel of every frame, not a JPEG's guess at
// them. Rows start at the image's bottom, as Unity keeps them; the step's report names the size and the format.
using System;
using System.Collections.Concurrent;
using System.IO;
using Unity.Collections;
using Unity.Collections.LowLevel.Unsafe;
using UnityEngine;
namespace TGPathTracing.Harness
{
    internal static class FrameGrab
    {
        private static byte[] buffer = new byte[0];

        // Call at the end of a frame (WaitForEndOfFrame). scale 2 writes the mean of every 2x2 pixels, and so on.
        internal static unsafe bool Grab(string path, int scale, out int width, out int height, out string format, out string error)
        {
            width = height = 0; format = ""; error = "";
            Texture2D shot = null;
            // Native Unity capture failures otherwise disappear when BepInEx WriteUnityLog is false.
            // Scope this listener to the capture; no player logging configuration is changed.
            var messages = new ConcurrentQueue<string>();
            Application.LogCallback captureLog = (message, stack, type) =>
            {
                if (type == LogType.Error || type == LogType.Exception || type == LogType.Warning)
                    messages.Enqueue(type + ": " + message);
            };
            Application.logMessageReceivedThreaded += captureLog;
            try
            {
                shot = ScreenCapture.CaptureScreenshotAsTexture();
                if (shot == null)
                {
                    error = "no screenshot (screen " + Screen.width + "x" + Screen.height +
                        ", focused " + Application.isFocused + ", active target " +
                        (RenderTexture.active != null ? RenderTexture.active.graphicsFormat.ToString() : "backbuffer") +
                        "): " + string.Join(" | ", messages.ToArray());
                    return false;
                }
                NativeArray<byte> raw = shot.GetRawTextureData<byte>();
                int w = shot.width, h = shot.height;
                int channels = w * h > 0 ? raw.Length / (w * h) : 0;
                format = shot.format.ToString();
                if (channels != 3 && channels != 4) { error = "unexpected screenshot format " + format + " (" + raw.Length + " bytes for " + w + "x" + h + ")"; return false; }
                if (scale < 1) scale = 1;
                int ow = w / scale, oh = h / scale, count = ow * oh * channels;
                if (buffer.Length != count) buffer = new byte[count];
                byte* source = (byte*)NativeArrayUnsafeUtility.GetUnsafeReadOnlyPtr(raw);
                fixed (byte* target = buffer)
                {
                    if (scale == 1) Buffer.MemoryCopy(source, target, count, count);
                    else
                    {
                        int n = scale * scale, half = n / 2;
                        for (int y = 0; y < oh; y++)
                            for (int x = 0; x < ow; x++)
                                for (int c = 0; c < channels; c++)
                                {
                                    int sum = half;
                                    for (int j = 0; j < scale; j++)
                                    {
                                        byte* row = source + ((long)(y * scale + j) * w + x * scale) * channels + c;
                                        for (int i = 0; i < scale; i++) sum += row[i * channels];
                                    }
                                    target[(y * ow + x) * channels + c] = (byte)(sum / n);
                                }
                    }
                }
                using (var stream = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.None, 1 << 20))
                    stream.Write(buffer, 0, count);
                width = ow; height = oh;
                return true;
            }
            catch (Exception ex) { error = ex.Message; return false; }
            finally
            {
                Application.logMessageReceivedThreaded -= captureLog;
                if (shot != null) UnityEngine.Object.Destroy(shot);
            }
        }
    }
}
