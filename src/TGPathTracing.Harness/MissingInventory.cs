// SPDX-License-Identifier: MIT
// Only the missing recipe calls this. The installed plugin does not depend on the harness.
using System;
using System.Collections;
using System.IO;
using System.Reflection;
using System.Text;
using Newtonsoft.Json.Linq;
using UnityEngine;

namespace TGPathTracing.Harness
{
    internal static class MissingInventory
    {
        private static Type diagnostic;
        private static object Call(string method,params object[] args)
        {
            if(diagnostic==null) diagnostic=EffectsRead.Type("TGPathTracing.MissingWorld");
            if(diagnostic==null) throw new InvalidOperationException("installed TGPathTracing.dll has no PT-83 missing diagnostic; merge/build/deploy the managed plugin first");
            var m=diagnostic.GetMethod(method,BindingFlags.Static|BindingFlags.NonPublic|BindingFlags.Public);
            if(m==null) throw new MissingMethodException(diagnostic.FullName,method);
            return m.Invoke(null,args);
        }
        internal static void Arm(StepResult r)
        { try { Call("Arm",true); } catch(Exception e) { r.error+=" Missing inventory: "+e.GetBaseException().Message; } }
        internal static void Disarm()
        { if(diagnostic!=null) try { Call("Arm",false); } catch(Exception e) { Runner.Note("Missing inventory disarm: "+e.GetBaseException().Message); } }
        internal static IEnumerator Run(string dir,Scenario scenario,Step step,StepResult r)
        {
            // End of frame sees the pose/submissions of that frame, not the next frame's simulation.
            yield return new WaitForEndOfFrame();
            float start=Time.realtimeSinceStartup;
            if(step.traced)
            {
                while(Time.realtimeSinceStartup-start<30)
                {
                    bool ready=false; try { ready=(bool)Call("Ready"); } catch { break; }
                    if(ready) break;
                    yield return new WaitForEndOfFrame();
                }
            }
            try
            {
                if(scenario.missingBoxes==null || scenario.missingBoxes.Count==0) throw new InvalidOperationException("scenario has no missingBoxes");
                var inventory=JObject.Parse((string)Call("Capture",scenario.missingBoxes.ToString(),r.traced));
                inventory["scenario"]=scenario.key; inventory["expectedTraced"]=step.traced; inventory["stepError"]=r.error;
                inventory["utc"]=DateTime.UtcNow.ToString("o"); inventory["timeScale"]=Time.timeScale;
                inventory["waitSeconds"]=Time.realtimeSinceStartup-start;
                File.WriteAllText(Path.Combine(dir,"missing.json"),inventory.ToString(),new UTF8Encoding(false));
                Runner.Note("Missing inventory "+scenario.key+"/"+step.name+": "+((JArray)inventory["rows"]).Count+" candidates");
                // One context image, outside the ordinary image-sequence analysis. It establishes the
                // boxes' camera view; the inventory does not assert pixel visibility from bounds alone.
                var picture=ScreenCapture.CaptureScreenshotAsTexture();
                if(picture==null) throw new InvalidOperationException("context screenshot was null");
                try { File.WriteAllBytes(Path.Combine(dir,"missing-view.jpg"),picture.EncodeToJPG(90)); }
                finally { UnityEngine.Object.Destroy(picture); }
            }
            catch(Exception e)
            {
                r.error+=" Missing inventory: "+e.GetBaseException().Message;
                File.WriteAllText(Path.Combine(dir,"missing-error.txt"),e.ToString(),new UTF8Encoding(false));
                Runner.Note(r.error);
            }
            finally { Disarm(); }
        }
    }
}
