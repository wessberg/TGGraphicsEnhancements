# SPDX-License-Identifier: MIT
"""Prepare independent original-program comparisons using the shipped property defaults and nonzero overrides."""
import json,pathlib,struct,math,argparse
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--research',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();R=a.research;O=a.output
caps=json.loads((O/'capture.json').read_text());inv=json.loads((O/'inventory.json').read_text());jobs=[]
for cap,info in zip(caps,inv):
 kind=cap['kind'];tree=next(json.loads(p.read_text()) for p in R.glob('*/tree.json') if json.loads(p.read_text())['m_ParsedForm']['m_Name']==cap['shader']);props={p['m_Name']:p for p in tree['m_ParsedForm']['m_PropInfo']['m_Props']};mat=[0.0]*96
 for off,p in cap['properties'].items():
  source=props[p['name']];val=[source['m_DefValue[%d]'%i] for i in range(4)]
  if p['type']==0:val=[0.4,0.7,0.2,1]
  if p['name'] in ['_Gradient1Sharpness','_Gradient2Sharpness']:val[0]=8
  if p['name']=='_Gradient1Size':val[0]=0.15
  if p['name']=='_Gradient2Size':val[0]=0.65
  if p['name']=='_GradientStrength':val[0]=3
  if p['name'] in ['_Alpha_Power','_Mask_Power']:val[0]=1
  if p['name']=='_Inner_Color_power':val[0]=2
  if p['name'] in ['_Map_1_Tiling','_Heigth_Map_3_Tiling']:val=[1.5,1.5,0,0]
  if p['name'] in ['_Parallax_Amplitude_1','_Pallarax_Amplitude_3']:val[0]=0.03
  if p['name'] in ['_NormalStrength','_Normal_strength']:val[0]=0.7
  if p['name'] in ['_EmissionIntensity','_EmissiveIntensity']:val[0]=2
  if p['name'] in ['_Opacity','_Alpha','_AlphaOutput']:val[0]=0.85
  if p['name'] in ['_Animator','_ChargeLevel','_EffectValue']:val[0]=0.3
  if p['name']=='_UseSmoke':val[0]=0
  if p['name']=='_SunPos':val=[1,1,1,0]
  if p['name']=='_CameraFade':val=[100,0,0,0]
  if p['name']=='_UseYFade':val[0]=0
  if p['type']==0:
   val[:3]=[x/12.92 if x<=0.04045 else ((x+0.055)/1.055)**2.4 for x in val[:3]]
  for i in range(p['dim']):mat[int(off)//4+i]=val[i]
 world=[1,0,0,0,0,1,0,0,0,0,1,0]; env=[1.375,1.7,0.05,0]; env2=[1.05,0,0,0];view=[0,0,-1,0]
 data=struct.pack('<96f8i36f24I',*mat,*range(8),*world,*world,*env,*env2,*view,*([0]*24));assert len(data)==656
 (O/('record-%d.bin'%kind)).write_bytes(data)
 for pas in info['passes']:
  name=pas['pass'];filename='%d-%s'%(kind,name);cbs=[];tex=[]
  for slot,cb in pas['bindings'].items():
   buf=[0.0]*(224*4 if cb=='ShaderVariablesGlobal' else 96)
   for off,(prop,dim,matrix) in pas['buffers'][cb].items():
    off=int(off)//4
    if cb=='UnityPerMaterial':buf=mat[:];break
    if prop in ['unity_ObjectToWorld','unity_WorldToObject']:v=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]
    elif prop=='_ViewMatrix':v=[1,0,0,0,0,1,0,0,0,0,-1,0,0,0,0,1]
    elif prop=='_GlobalMipBias':v=[0]
    elif prop=='_ProbeExposureScale':v=[env2[0]]
    elif prop=='_TimeParameters':v=[env[0],math.sin(env[0]),math.cos(env[0]),0]
    elif prop=='_WorldSpaceCameraPos_Internal':v=[0,2,-3,0]
    elif prop=='unity_OrthoParams':v=[0,0,0,0]
    elif prop=='unity_WorldTransformParams':v=[0,0,0,1]
    elif prop=='_GlobalSaturationAdjustment':v=[env[2]]
    else:raise ValueError(prop)
    buf[off:off+len(v)]=v
   cbs.append((int(slot),struct.pack('<%df'%len(buf),*buf)))
  for slot,prop in pas['textures'].items():tex.append((int(slot),-2 if prop=='_ExposureTexture' else cap['textures'][prop]))
  payload=struct.pack('<4I',kind,name!='DBufferMesh',len(cbs),len(tex))+data
  for slot,b in cbs:payload+=struct.pack('<2I',slot,len(b))+b
  for slot,index in tex:payload+=struct.pack('<Ii',slot,index)
  (O/(filename+'.bin')).write_bytes(payload);jobs.append(filename)
(O/'jobs.txt').write_text('\n'.join(jobs))
print('Prepared',len(jobs),'original shader jobs')
