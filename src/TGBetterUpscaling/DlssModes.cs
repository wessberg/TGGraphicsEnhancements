// SPDX-License-Identifier: MIT
// DLSS's quality modes as NVIDIA's runtime reports them: the render size NGX recommends for each mode,
// through Unity's NVIDIA module, which HDRP set up for the game's own DLSS. Unity's DLSSQuality has no
// DLAA, but passes the value through to NGX, whose DLAA is 5; it is offered only when NGX reports a
// size for it.
using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.NVIDIA;
namespace TGBetterUpscaling
{
    internal static class DlssModes
    {
        internal static QualityMode[] Query(out string detail)
        {
            var modes = new List<QualityMode>();
            try
            {
                var device = GraphicsDevice.device;
                if (device == null) { detail = "Unity's NVIDIA module has no device, so DLSS keeps the game's render-scale slider"; return new QualityMode[0]; }
                uint width = (uint)Mathf.Max(Screen.width, 1), height = (uint)Mathf.Max(Screen.height, 1);
                foreach (int id in QualityModes.DlssIds)
                {
                    var data = new OptimalDLSSSettingsData();
                    if (device.GetOptimalSettings(width, height, (DLSSQuality)id, out data) && data.outRenderWidth > 0 && data.outRenderWidth <= width)
                        modes.Add(new QualityMode(id, QualityModes.DlssName(id), 100f * data.outRenderWidth / width));
                }
                detail = modes.Count == 0 ? "NGX reported no render size for any DLSS mode, so DLSS keeps the game's render-scale slider" : "";
            }
            catch (Exception ex)
            {
                detail = "DLSS's modes could not be queried (" + ex.GetType().Name + ": " + ex.Message + "), so DLSS keeps the game's render-scale slider";
                modes.Clear();
            }
            return QualityModes.Sorted(modes);
        }
    }
}
