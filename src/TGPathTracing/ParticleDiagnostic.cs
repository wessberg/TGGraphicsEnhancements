// SPDX-License-Identifier: MIT
using System;
using System.Runtime.InteropServices;

namespace TGPathTracing
{
    // An explicit diagnostic export, usable with Mode Off. No frame-option bit or renderer setting.
    internal static class ParticleDiagnostic
    {
        [DllImport("TGPathTracing.Native", CallingConvention = CallingConvention.Cdecl)]
        private static extern int TGPT_VfxSimulationSetEnabled(uint enabled);
        private static bool wasEnabled;
        private static bool warned;
        private static float retryAt;
        internal static void Tick()
        {
            bool enabled = Session.Settings?.ParticleSimulationDiagnostic.Value == true;
            if (enabled == wasEnabled || UnityEngine.Time.realtimeSinceStartup < retryAt) return;
            retryAt = UnityEngine.Time.realtimeSinceStartup + 1;
            try
            {
                int result = TGPT_VfxSimulationSetEnabled(enabled ? 1u : 0u);
                if (result != 0) throw new InvalidOperationException("The harness startup marker is required.");
                wasEnabled = enabled; warned = false;
            }
            catch (Exception error)
            {
                if (!warned) Session.Log?.LogWarning("Particle simulation diagnostic waiting: " + error.Message);
                warned = true; // Device/extension startup may finish on a later frame.
            }
        }
    }
}
