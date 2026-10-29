# SPDX-License-Identifier: MIT
"""Reconstruct decal arithmetic from the shipped compiled pixel programs: translate shipped decal pixel arithmetic with exact literals.
Inputs stay in decals-research; generated material math and capture bindings are reviewed before use.
"""
import pathlib,json,struct,re,sys,ctypes,ast
sys.dont_write_bytecode=True
import argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--research',type=pathlib.Path,required=True,help='Private shader directories, each containing tree.json from UnityPy')
parser.add_argument('--output',type=pathlib.Path,required=True,help='Private output for bytecode, comparisons and generated arithmetic')
parser.add_argument('--python-deps',help="Existing directory containing lz4, when it is not on Python's path")
args=parser.parse_args()
if args.python_deps: sys.path.insert(0,args.python_deps)
import lz4.block
R=args.research; O=args.output; O.mkdir(parents=True,exist_ok=True)
NAMES=['KriptoFX_BFX_Decal','TG_Decal_Decal_EmberBurning','TG_Decal_Decal_WyrdGhost','TG_Decal_Decal_RadialGradient_3Panning','TG_Decal_Decal_RadialGradient_3Panning_Lit','TG_Decal_Decal_GlowingCracks','TG_VFX_VFXGraph_Decal_FireGround','TG_VFX_VFXGraph_Decal_ForeDwellers_Annihilation']
def disassemble(code):
 dll=ctypes.WinDLL('d3dcompiler_47.dll');p=ctypes.c_void_p();hr=dll.D3DDisassemble(ctypes.c_char_p(code),ctypes.c_size_t(len(code)),128,None,ctypes.byref(p));assert not hr
 v=ctypes.cast(ctypes.cast(p,ctypes.POINTER(ctypes.c_void_p))[0],ctypes.POINTER(ctypes.c_void_p));b=ctypes.WINFUNCTYPE(ctypes.c_void_p,ctypes.c_void_p)(v[3])(p);n=ctypes.WINFUNCTYPE(ctypes.c_size_t,ctypes.c_void_p)(v[4])(p);s=ctypes.string_at(b,n).decode('latin1');ctypes.WINFUNCTYPE(ctypes.c_ulong,ctypes.c_void_p)(v[2])(p);return s

def split(s):return re.split(r',\s*(?![^()]*\))',s)
def records(data,names):
 found=[]
 for at in range(0,len(data)-8,4):
  size=struct.unpack_from('<I',data,at)[0]
  if size<4 or size>512 or at+4+size>len(data):continue
  raw=data[at+4:at+4+size]
  if not re.fullmatch(rb'[A-Za-z_$][A-Za-z0-9_$]*',raw):continue
  end=(at+4+size+3)&~3;count=min(8,(len(data)-end)//4)
  found.append((at,raw.decode(),struct.unpack_from('<%di'%count,data,end)))
 return found

hlsl=[]; captures=[]; inventory=[]
JOBS=[(k,n,'MATERIAL_QUALITY_HIGH') for k,n in enumerate(NAMES,7)]+[(15,NAMES[5],'MATERIAL_QUALITY_MEDIUM'),(16,NAMES[5],'MATERIAL_QUALITY_LOW')]
for kind,dirname,quality in JOBS:
 p=R/dirname;t=json.loads((p/'tree.json').read_text());pf=t['m_ParsedForm'];props={v['m_Name']:v for v in pf['m_PropInfo']['m_Props']};blob=bytes(t['compressedBlob']);chunks=[lz4.block.decompress(blob[o:o+c],uncompressed_size=u) for o,c,u in zip(t['offsets'][0],t['compressedLengths'][0],t['decompressedLengths'][0])];table=[struct.unpack_from('<3I',chunks[0],4+12*i) for i in range(struct.unpack_from('<I',chunks[0])[0])]
 textures={};params={};funcs=[];env=set();meta=[]
 for passname in ['DBufferMesh','DecalMeshForwardEmissive']:
  pas=next((a for a in pf['m_SubShaders'][0]['m_Passes'] if a['m_State']['m_Name']==passname),None)
  if pas is None:continue
  prog=pas['progFragment'];names={v:k for k,v in pas['m_NameIndices']};entries=[x for g in prog['m_PlayerSubPrograms'] for x in g];pars=[x for g in prog['m_ParameterBlobIndices'] for x in g]
  candidates=[]
  for sp,par in zip(entries,pars):
   keys=[pf['m_KeywordNames'][x] for x in sp['m_KeywordIndices']]
   if any('INSTANCING' in k for k in keys) or any(k.startswith('MATERIAL_QUALITY_') and k!=quality for k in keys):continue
   off,size,seg=table[sp['m_BlobIndex']];data=chunks[seg][off:off+size];at=data.index(b'DXBC');size=struct.unpack_from('<I',data,at+24)[0];code=data[at:at+size];text=disassemble(code)
   candidates.append((len(text),sp,par,keys,text))
  _,sp,par,keys,text=max(candidates,key=lambda x:x[0]);
  off,size,seg=table[sp['m_BlobIndex']]; data=chunks[seg][off:off+size]; at=data.index(b'DXBC'); code=data[at:at+struct.unpack_from('<I',data,at+24)[0]]; (O/(str(kind)+'-'+passname+'.dxbc')).write_bytes(code);(O/(str(kind)+'-'+passname+'.txt')).write_text(text)
  off,size,seg=table[par];pb=chunks[seg][off:off+size];recs=records(pb,names.values());common=prog['m_CommonParameters'];buffers={};bindings={};tex={};samplerNames={}
  for cb in common['m_ConstantBuffers']:
   name=names[cb['m_NameIndex']];buffers[name]={}
   for v in cb['m_VectorParams']+cb['m_MatrixParams']:
    buffers[name][v['m_Index']] = (names[v['m_NameIndex']],v.get('m_Dim',4),v.get('m_RowCount',0))
  for v in common['m_ConstantBufferBindings']:bindings[v['m_Index']]=names[v['m_NameIndex']]
  for v in common['m_TextureParams']:
   tex[v['m_Index']]=names[v['m_NameIndex']];samplerNames[v['m_SamplerIndex']]=names[v['m_NameIndex']]
  current=None
  for at,name,ints in recs:
   if name in ['$Globals','UnityPerMaterial','UnityPerDraw','Decal','ShaderVariablesGlobal']:
    if len(ints)>2 and ints[0]==1 and ints[2]==0:bindings[ints[1]]=name;current=None
    elif len(ints)>1 and ints[0]>=16:buffers.setdefault(name,{});current=name
   elif len(ints)>=6 and ints[0] in (0,1) and ints[1] in (1,2,3,4) and ints[2] in (1,2,3,4) and ints[3] in (0,1) and ints[4]==0 and ints[5]>=0 and current:
    buffers[current][ints[5]]=(name,ints[2],ints[1] if ints[3] else 0)
   elif len(ints)>=4 and ints[0]==0 and ints[3] in (4,10):tex[ints[1]]=name;samplerNames[ints[2]]=name;current=None
  # texture properties shared by the two passes use one stable list of live handles
  for slot,name in tex.items():
   if name!='_ExposureTexture':textures.setdefault(name,len(textures))
  for off,(name,dim,matrix) in buffers.get('UnityPerMaterial',{}).items():params[off]=(name,dim)
  env |= {name for cb,fields in buffers.items() if cb not in ['UnityPerMaterial','UnityPerDraw'] for name,_,_ in fields.values()}
  meta.append({'pass':passname,'keywords':keys,'buffers':buffers,'bindings':bindings,'textures':tex})
  cbreads=sorted(set((int(a),int(b)) for a,b in re.findall(r'cb(\d+)\[(\d+)\]',text)))
  cbcode=[]
  for slot,row in cbreads:
   assert slot in bindings,(dirname,slot,names,bindings,recs)
   cb=bindings[slot];fields=buffers[cb]
   if cb=='UnityPerMaterial':expr='d.graph[%d]'%row
   elif cb=='UnityPerDraw':
    prop=next(((off,v) for off,v in fields.items() if off<=row*16<off+(v[2]*16 if v[2] else v[1]*4)),None);assert prop,(dirname,slot,row,fields)
    off,(name,dim,matrix)=prop
    if name=='unity_ObjectToWorld':expr='decal_graph_object_column(d,%du)'%(row-off//16)
    elif name=='unity_WorldToObject':expr='decal_graph_inverse_column(d,%du)'%(row-off//16)
    elif name=='unity_WorldTransformParams':expr='float4(0,0,0,1)'
    else:raise ValueError(name)
   else:
    vals=['0.0']*4
    for off,(name,dim,matrix) in fields.items():
     if matrix:
      if name=='_ViewMatrix':
       c=row-off//16
       if 0<=c<4:vals=['0.0','0.0',('d.graphViewZ.'+'xyzw'[c]),'0.0']
      else:raise ValueError(name)
     else:
      for k in range(dim):
       pos=off//4+k
       if pos//4!=row:continue
       if name=='_TimeParameters':value=['d.graphEnv.x','sin(d.graphEnv.x)','cos(d.graphEnv.x)','0.0'][k]
       elif name=='_GlobalMipBias':value='asfloat(g.hair.w)'
       elif name=='_WorldSpaceCameraPos_Internal':value='(g.cameraPos.'+'xyzw'[k]+'+g.cacheOrigin.'+'xyzw'[k]+')'
       elif name=='_DeExposureMultiplier':value='1.0'
       elif name=='_ProbeExposureScale':value='d.graphEnv2.x'
       elif name=='unity_OrthoParams':value='d.graphEnv.w' if k==3 else '0.0'
       elif name=='_GlobalSaturationAdjustment':value='d.graphEnv.z'
       else:raise ValueError((dirname,name))
       vals[pos%4]=value
    expr='float4('+','.join(vals)+')'
   cbcode.append('    const float4 cb%d_%d = %s;'%(slot,row,expr))
  def operand(s,lane):
   s=s.strip();neg=s.startswith('-');s=s[1:] if neg else s;absolute=s.startswith('|');s=s[1:-1] if absolute else s
   if s.startswith('l('):
    vals=split(s[2:-1]);vals=vals*4 if len(vals)==1 else vals
    # PRINT_HEX_LITERALS prints every immediate as the exact 32-bit word.
    expr='asfloat(uint4('+','.join(v+'u' if v.startswith('0x') else str(int(v))+'u' for v in vals)+'))'
   else:
    m=re.fullmatch(r'(cb(\d+)\[(\d+)\]|[rvo]\d+)\.([xyzw]+)',s);assert m,s
    reg=m[1];sw=m[4]
    if reg.startswith('cb'):reg='cb%s_%s'%(m[2],m[3])
    elif reg.startswith('r'):reg=reg+'[%d]'%lane
    elif reg.startswith('v'):reg='v%s_%d'%(reg[1:],lane)
    else:reg=reg+'[%d]'%lane
    expr=reg+'.'+(sw*4 if len(sw)==1 else sw)
   if absolute:expr='abs('+expr+')'
   return '-('+expr+')' if neg else expr
  def selected(s,mask,lane):return '('+operand(s,lane)+').'+mask
  temps=int(re.search(r'dcl_temps (\d+)',text)[1]);fname='decal_graph_%d_%s'%(kind,'paint' if passname=='DBufferMesh' else 'emission')
  body=['// '+pf['m_Name']+' / '+passname, 'DecalGraphOutput '+fname+'(DecalRecord d, DecalGraphInput at) {']+cbcode
  inputs=sorted(set(int(x) for x in re.findall(r'\bv(\d+)\.',text)))
  for lane in range(3):
   for vi in inputs:
    if vi==1:expr='at.tangent'
    elif vi==2:expr='float4(at.uv + '+['float2(0,0)','at.gx','at.gy'][lane]+',0,0)'
    elif vi==3 and kind==7:expr='at.color'
    elif vi==3:expr='float4(at.position + '+['float3(0,0,0)','at.dx','at.dy'][lane]+',1)'
    elif vi in (4,5):expr='float4(at.normal,0)'
    else:raise ValueError((dirname,vi))
    body.append('    const float4 v%d_%d = %s;'%(vi,lane,expr))
  body += ['    float4 r%d[3] = {float4(0,0,0,0),float4(0,0,0,0),float4(0,0,0,0)};'%i for i in range(temps)]+['    float4 o%d[3] = {float4(0,0,0,0),float4(0,0,0,0),float4(0,0,0,0)};'%i for i in range(3)]
  sample=0
  for line in text.splitlines():
   line=line.strip()
   if not line or line.startswith(('//','dcl_','ps_','\0')):continue
   op,_,args=line.partition(' ');a=split(args);sat=op.endswith('_sat');op=op[:-4] if sat else op
   if op=='ret':continue
   if op=='imul':assert a[0]=='null';a=a[1:]
   if op=='sincos':
    for l in range(3):
     body.append('    { const float4 angle = %s;'%operand(a[2],l))
     for dest,fn in zip(a[:2],['sin','cos']):
      if dest=='null':continue
      reg,mask=dest.split('.');body.append('    %s[%d].%s = %s(angle.%s);'%(reg,l,mask,fn,mask))
     body.append('    }')
    continue
   dest=a[0];mask=dest.split('.')[1];reg=dest.split('.')[0];width=len(mask);typ='uint'+(str(width) if width>1 else '')
   def S(i,l):return selected(a[i],mask,l)
   if op.startswith(('sample_','ld_')):
    if op.startswith('ld_'):
     slot=int(re.search(r't(\d+)',a[2])[1]);assert tex[slot]=='_ExposureTexture';expr='d.graphEnv.y'
     for l in range(3):body.append('    %s[%d].%s = %s;'%(reg,l,mask,expr))
     continue
    slot=int(re.search(r't(\d+)',a[2])[1]);texSw=a[2].split('.')[1];texidx=textures[tex[slot]];key='d.graphTextures[%d]'%texidx
    samplerSlot=int(re.search(r's(\d+)',a[3])[1])
    if samplerSlot in samplerNames:
     samplerIndex=4+textures[samplerNames[samplerSlot]];sampler='d.samplers[%d]'%samplerIndex;textureBias='d.samplerBias[%d]'%samplerIndex
    else:
     # Shipped graphs' unnamed parameter: empty name, sampler type, binding, Linear/Repeat.
     assert any(v['bindPoint']==samplerSlot and v['sampler']==1 for v in common['m_Samplers']) or struct.unpack_from('<4I',pb,len(pb)-16)==(0,4,samplerSlot,1),(dirname,passname,samplerSlot,pb[-16:])
     sampler='272u';textureBias='0.0'
    fallback={'white':'1.0','black':'float4(0,0,0,1)','bump':'float4(0.5,0.5,1,1)','gray':'float4(0.5,0.5,0.5,1)','grey':'float4(0.5,0.5,0.5,1)'}[props[tex[slot]]['m_DefTexture']['m_DefaultName']]
    if op.startswith('sample_b'):
     uv=[selected(a[1],'xy',l) for l in range(3)];bias=selected(a[4],'x',0)
     body.append('    const float2 sample%dX = (%s - %s) * exp2(%s), sample%dY = (%s - %s) * exp2(%s);'%(sample,uv[1],uv[0],bias,sample,uv[2],uv[0],bias))
    for l in range(3):
     uv=selected(a[1],'xy',l)
     if op.startswith('sample_b'):expr='decal_graph_sample(%s,%s,%s,%s,sample%dX,sample%dY,%s)'%(key,sampler,textureBias,uv,sample,sample,fallback)
     else:expr='decal_graph_sample_level(%s,%s,%s,%s,%s)'%(key,sampler,uv,selected(a[4],'x',l),fallback)
     body.append('    %s[%d].%s = (%s).%s;'%(reg,l,mask,expr,''.join(texSw['xyzw'.index(c)] for c in mask)))
    sample+=1;continue
   for l in range(3):
    x=S(1,l) if a[1]!='null' else '0.0'
    if op in ['add','mul','div']:expr='(%s %s %s)'%(x,{'add':'+','mul':'*','div':'/'}[op],S(2,l))
    elif op=='mad':expr='mad(%s,%s,%s)'%(x,S(2,l),S(3,l))
    elif op in ['min','max']:expr='%s(%s,%s)'%(op,x,S(2,l))
    elif op in ['dp2','dp3','dp4']:expr='dot(%s,%s)'%(selected(a[1],'xyzw'[:int(op[-1])],l),selected(a[2],'xyzw'[:int(op[-1])],l))
    elif op in ['sqrt','rcp','rsq','log','exp','frc','round_ni']:expr='%s(%s)'%({'rsq':'rsqrt','log':'log2','exp':'exp2','frc':'frac','round_ni':'floor'}.get(op,op),x)
    elif op=='mov':expr=x
    elif op=='movc':expr='select(asuint(%s)!=0u,%s,%s)'%(x,S(2,l),S(3,l))
    elif op in ['eq','ne','lt','ge']:expr='asfloat(%s(%s %s %s)*0xffffffffu)'%(typ,x,{'eq':'==','ne':'!=','lt':'<','ge':'>='}[op],S(2,l))
    elif op in ['and','xor','iadd','imul','ushr']:expr='asfloat(asuint(%s) %s asuint(%s))'%(x,{'and':'&','xor':'^','iadd':'+','imul':'*','ushr':'>>'}[op],S(2,l))
    elif op=='ftoi':expr='asfloat(int%s(%s))'%(str(width) if width>1 else '',x)
    elif op=='utof':expr='float%s(asuint(%s))'%(str(width) if width>1 else '',x)
    elif op=='sincos':
     # Destination 1 may be null; destination 0 is sine in these programs.
     assert a[1]=='null',(dirname,line);expr='sin(%s)'%selected(a[2],mask,l)
    else:raise ValueError(op)
    if sat:expr='saturate('+expr+')'
    body.append('    %s[%d].%s = %s;'%(reg,l,mask,expr))
  body+=['    DecalGraphOutput result; result.color=o0[0]; result.normal=o1[0]; result.mask=o2[0]; return result;','}'];hlsl.extend(body);funcs.append(fname)
 assert len(textures)<=8,(dirname,textures)
 captures.append({'kind':kind,'shader':pf['m_Name'],'quality':quality,'properties':{off:{'name':name,'dim':dim,'type':props[name]['m_Type'],'flags':props[name]['m_Flags']} for off,(name,dim) in params.items()},'textures':textures,'functions':funcs})
 inventory.append({'kind':kind,'shader':pf['m_Name'],'env':sorted(env),'passes':meta})
(O/'graphs.hlsli').write_text('\n'.join(hlsl)+'\n');(O/'capture.json').write_text(json.dumps(captures,indent=2));(O/'inventory.json').write_text(json.dumps(inventory,indent=2));print('Generated',len(hlsl),'lines for',len(captures),'shaders')
