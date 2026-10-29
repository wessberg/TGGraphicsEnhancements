# SPDX-License-Identifier: MIT
"""Emit the cases for DecalGraphCapture.Read from lift_decal_graphs.py's capture.json."""
import argparse,json,pathlib
p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);a=p.parse_args()
lines=[]
for cap in json.loads(a.capture.read_text()):
 lines.append('            case %du: {'%cap['kind'])
 for off,prop in cap['properties'].items():
  off=int(off)//4; name=json.dumps(prop['name']); dim=prop['dim']
  if dim==1:lines.append('                p[%d] = DecalCapture.Float(material, %s);'%(off,name))
  else:
   expr='(Vector4)DecalCapture.ColorValue(material, %s).linear'%name if prop['type']==0 else 'DecalCapture.Vector(material, %s)'%name
   lines.append('                Vector4 p%d = %s;'%(off,expr))
   for k in range(dim):lines.append('                p[%d] = p%d.%s;'%(off+k,off,'xyzw'[k]))
 for name,slot in cap['textures'].items():lines.append('                maps[%d] = DecalCapture.Map(material, %s, ref missing);'%(slot,json.dumps(name)))
 lines+=['                break;','            }']
a.output.write_text('\n'.join(lines)+'\n')
