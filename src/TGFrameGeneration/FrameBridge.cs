// SPDX-License-Identifier: MIT
using System;
using UnityEngine;
using UnityEngine.Rendering;
#if BEPINEX_IL2CPP
using Il2CppInterop.Runtime;
#endif
namespace TGFrameGeneration
{
    internal static class FrameBridge
    {
        private static bool initialized, frameOpen, cameraQueued, failed;
        private static uint frameId, previousFrameId;
        private static int previousCameraId;
        private static bool haveHistory;
        private static Matrix4x4 previousViewProjection;
        private static Vector3 previousPosition;
        private static Quaternion previousRotation;
        private static CommandBuffer boundary, cameraBegin, cameraEnd;
        internal static string Detail { get; private set; }="No frame submitted";
#if BEPINEX_IL2CPP
        private static Il2CppSystem.Action<ScriptableRenderContext,Camera> beginHandler,endHandler;
#else
        private static Action<ScriptableRenderContext,Camera> beginHandler,endHandler;
#endif
        internal static void Initialize()
        {
            if(initialized) return;
            boundary=new CommandBuffer { name="TGFG simulation/render boundary" };
            cameraBegin=new CommandBuffer { name="TGFG main camera begin" };
            cameraEnd=new CommandBuffer { name="TGFG main camera end" };
#if BEPINEX_IL2CPP
            beginHandler=DelegateSupport.ConvertDelegate<Il2CppSystem.Action<ScriptableRenderContext,Camera>>(new Action<ScriptableRenderContext,Camera>(BeginCamera));
            endHandler=DelegateSupport.ConvertDelegate<Il2CppSystem.Action<ScriptableRenderContext,Camera>>(new Action<ScriptableRenderContext,Camera>(EndCamera));
#else
            beginHandler=BeginCamera; endHandler=EndCamera;
#endif
            RenderPipelineManager.beginCameraRendering+=beginHandler;
            RenderPipelineManager.endCameraRendering+=endHandler;
            initialized=true;
        }
        internal static void BeginFrame()
        {
            if(!initialized || failed || Session.Bridge==null || !Session.Bridge.Connected) return;
            try {
                frameId=unchecked((uint)Time.frameCount); cameraQueued=false;
                int result=Session.Bridge.Begin(frameId); frameOpen=result==0;
                if(!frameOpen) Detail="Native frame begin not ready: "+result;
            } catch(Exception ex) { Fail(ex); }
        }
        internal static void EndSimulation()
        {
            if(!frameOpen || failed) return;
            try {
                Session.Bridge.End(frameId);
                Queue(boundary,0);
                // This boundary also runs in menus; camera callbacks are not
                // required to send basic Reflex/PCL frame markers.
                Graphics.ExecuteCommandBuffer(boundary);
            } catch(Exception ex) { Fail(ex); }
        }
        private static void Queue(CommandBuffer commands,int eventId)
        {
            commands.Clear();
            commands.IssuePluginEventAndData(Session.Bridge.RenderEvent,eventId,new IntPtr((long)frameId));
        }
        // Null when Camera.main is usable as the gameplay camera; otherwise why not. Only called for
        // Camera.main itself, so other cameras never build strings per frame.
        private static string Rejection(Camera camera)
        {
            if(camera.orthographic) return "orthographic";
            if(camera.stereoEnabled) return "stereo";
            if(camera.targetTexture!=null) return "renders to texture "+camera.targetTexture.name;
            if(!camera.enabled || !camera.gameObject.activeInHierarchy) return "disabled";
            // Compare the rendered pixel size with the output instead of the normalized rect: FoA's
            // camera rect is full-screen but not bit-exact 0/0/1/1. The native side still checks the
            // DLSS output size against this output size before using any frame.
            int w=camera.pixelWidth, h=camera.pixelHeight;
            if(Mathf.Abs(w-Screen.width)>2 || Mathf.Abs(h-Screen.height)>2)
            {
                var r=camera.rect;
                return "viewport "+w+"x"+h+" of "+Screen.width+"x"+Screen.height+" (rect "+r.x.ToString("F5")+","+r.y.ToString("F5")+","+r.width.ToString("F5")+","+r.height.ToString("F5")+")";
            }
            return null;
        }
        private static int lastQueuedCamera, queuedCameraId;
        private static readonly System.Collections.Generic.HashSet<int> loggedOthers=new System.Collections.Generic.HashSet<int>();
        private static string lastMainRejection;
        private static int lastPacketResult;
        private static void BeginCamera(ScriptableRenderContext context,Camera camera)
        {
            if(!frameOpen || failed || cameraQueued || camera==null || camera.cameraType!=CameraType.Game) return;
            var main=Camera.main;
            int id=camera.GetInstanceID();
            if(main==null || id!=main.GetInstanceID())
            {
                // Other game cameras (e.g. a minimap mod's) render every frame: note each one once.
                if(main!=null && loggedOthers.Count<32 && loggedOthers.Add(id))
                    Session.Log?.LogInfo("Frame generation ignores camera '"+camera.name+"'; the gameplay camera is Camera.main ('"+main.name+"')");
                return;
            }
            string rejection=Rejection(camera);
            if(rejection!=null)
            {
                if(rejection!=lastMainRejection) { lastMainRejection=rejection; Detail="Camera.main not usable: "+rejection; Session.Log?.LogWarning("Frame generation cannot use '"+camera.name+"': "+rejection); }
                return;
            }
            lastMainRejection=null;
            try {
                NativeCamera packet=MakeCamera(camera);
                int result=Session.Bridge.SetCamera(ref packet);
                if(result!=0)
                {
                    haveHistory=false;
                    if(result!=lastPacketResult) { lastPacketResult=result; Detail="Camera packet rejected: "+result; Session.Log?.LogWarning("Native side rejected the camera packet: "+result); }
                    return;
                }
                lastPacketResult=0;
                // HDRP raises beginCameraRendering during culling; the context flushes this marker at
                // the first Submit of the frame, which precedes the camera's own DLSS pass.
                Queue(cameraBegin,1); context.ExecuteCommandBuffer(cameraBegin);
                cameraQueued=true; ++CamerasQueued; queuedCameraId=id;
                if(id!=lastQueuedCamera)
                {
                    lastQueuedCamera=id;
                    Detail="Gameplay camera '"+camera.name+"' selected";
                    Session.Log?.LogInfo("Frame generation camera: "+camera.name+" ("+camera.pixelWidth+"x"+camera.pixelHeight+")");
                }
            } catch(Exception ex) { Fail(ex); }
        }
        internal static ulong CamerasQueued { get; private set; }
        // The frame of the gameplay camera, while that camera renders (between its begin and end markers).
        internal static bool GameplayFrame(Camera camera, out uint id)
        {
            id=frameId;
            return frameOpen && !failed && cameraQueued && camera!=null && camera.GetInstanceID()==queuedCameraId;
        }
        private static void EndCamera(ScriptableRenderContext context,Camera camera)
        {
            if(!frameOpen || failed || !cameraQueued || camera==null || camera.GetInstanceID()!=queuedCameraId) return;
            // HDRP raises endCameraRendering after its final Submit for the camera, so anything put in
            // the context here would never execute this frame. Graphics.ExecuteCommandBuffer goes
            // straight to the render thread, right behind the camera's submitted commands.
            try { Queue(cameraEnd,2); Graphics.ExecuteCommandBuffer(cameraEnd); }
            catch(Exception ex) { Fail(ex); }
        }
        private static unsafe NativeCamera MakeCamera(Camera camera)
        {
            // Transpose Unity's column-vector transforms into Streamline row-vector
            // row-major matrices. Clip projection must match the captured depth.
            // FoA/HDRP camera-relative rendering and Y orientation need in-game validation.
            Matrix4x4 projection=GL.GetGPUProjectionMatrix(camera.nonJitteredProjectionMatrix,true);
            if(Mathf.Abs(projection.determinant)<1e-12f) throw new InvalidOperationException("Singular camera projection.");
            Matrix4x4 viewProjection=projection*camera.worldToCameraMatrix;
            var position=camera.transform.position; var rotation=camera.transform.rotation;
            bool reset=!haveHistory || frameId!=previousFrameId+1 || camera.GetInstanceID()!=previousCameraId ||
                Vector3.Distance(position,previousPosition)>5 || Quaternion.Angle(rotation,previousRotation)>45;
            Matrix4x4 clipToPrevious=reset?Matrix4x4.identity:previousViewProjection*viewProjection.inverse;
            Matrix4x4 inverseProjection=projection.inverse, previousToClip=clipToPrevious.inverse, view=camera.worldToCameraMatrix;
            NativeCamera packet=default;
            packet.Size=(uint)Abi.CameraSize; packet.Abi=Abi.Version; packet.FrameId=frameId; packet.ResetHistory=reset?1u:0u;
            packet.Width=(uint)Screen.width; packet.Height=(uint)Screen.height;
            packet.CameraId=unchecked((uint)camera.GetInstanceID());
            for(int r=0;r<4;++r) for(int c=0;c<4;++c) {
                int i=r*4+c;
                packet.ViewToClip[i]=projection[c,r]; packet.ClipToView[i]=inverseProjection[c,r];
                packet.ClipToPrev[i]=clipToPrevious[c,r]; packet.PrevToClip[i]=previousToClip[c,r];
                packet.WorldToView[i]=view[c,r];
            }
            var up=camera.transform.up; var right=camera.transform.right; var forward=camera.transform.forward;
            packet.Position[0]=position.x; packet.Position[1]=position.y; packet.Position[2]=position.z;
            packet.Up[0]=up.x; packet.Up[1]=up.y; packet.Up[2]=up.z;
            packet.Right[0]=right.x; packet.Right[1]=right.y; packet.Right[2]=right.z;
            packet.Forward[0]=forward.x; packet.Forward[1]=forward.y; packet.Forward[2]=forward.z;
            packet.Near=camera.nearClipPlane; packet.Far=camera.farClipPlane;
            packet.FovRadians=camera.fieldOfView*Mathf.Deg2Rad; packet.Aspect=camera.aspect;
            previousViewProjection=viewProjection; previousPosition=position; previousRotation=rotation;
            previousCameraId=camera.GetInstanceID(); previousFrameId=frameId; haveHistory=true;
            return packet;
        }
        private static void Fail(Exception ex)
        {
            failed=true; frameOpen=false;
            Detail="Render bridge disabled after error: "+ex.Message;
            Session.Log?.LogError(Detail); Session.Settings.SetFrameGenerationOff();
        }
        internal static void Dispose()
        {
            if(!initialized) return;
            RenderPipelineManager.beginCameraRendering-=beginHandler;
            RenderPipelineManager.endCameraRendering-=endHandler;
            // Unity copies the command buffer when executing it. Native callback
            // pointers remain valid because TGFG.Native is process-pinned.
            boundary?.Release(); cameraBegin?.Release(); cameraEnd?.Release();
            initialized=false; frameOpen=false;
        }
    }
}
