"""Resolve all rear-camera wiring from the measured FDT and pinned TLMM source.
Reports PMIC GPIOs distinctly; never treats their cells as TLMM GPIO numbers.
"""
from pathlib import Path
import argparse,json,re,struct

def require(ok,why):
 if not ok:raise ValueError(why)

def inspect(fdt,pinctrl):
 def cells(key):
  b=bytes.fromhex(fdt[key]['hex']);require(len(b)%4==0,'unaligned FDT value');return list(struct.unpack('>'+'I'*(len(b)//4),b))
 handles={cells(k)[0]:k.rsplit('/',1)[0] for k in fdt if k.endswith('/phandle')}
 require(len(handles)==sum(k.endswith('/phandle') for k in fdt),'duplicate phandle')
 def gpio(controller,pin,flags):
  require(flags==0 and controller in handles,'unsupported GPIO flags/controller')
  node=handles[controller]
  if node=='/soc/pinctrl@f000000':
   m=re.search(r'PINGROUP\(\s*'+str(pin)+r'\s*,\s*(NORTH|SOUTH|WEST)\b',pinctrl)
   require(m is not None,'pin not in pinned TLMM table')
   base={'NORTH':0xf900000,'SOUTH':0xf500000,'WEST':0xf100000}[m[1]]+pin*4096
   return dict(controller='TLMM',pin=pin,region=m[1],register_base=base)
  require('pm8009@a/pinctrl@c000' in node and 0<=pin<4,'unreviewed PMIC GPIO')
  return dict(controller='PM8009',pin=pin,dt_node=node,native_output_not_implemented=True)
 result=[]
 for slot in (0,1,3):
  cci=0 if slot in (0,1) else 1
  node=f'/soc/qcom,cci@{0xac4f000+cci*0x1000:x}/qcom,cam-sensor@{slot}'
  require(cells(node+'/cell-index')==[slot],'wrong slot')
  phy=cells(node+'/csiphy-sd-index')[0]
  master=cells(node+'/cci-master')[0];require(master in (0,1),'wrong CCI master')
  gp=cells(node+'/gpios');require(len(gp)%3==0,'bad GPIO list')
  pins=[gpio(*gp[i:i+3]) for i in range(0,len(gp),3)]
  cl=cells(node+'/clocks');require(len(cl)==2 and cl[1] in (0x49,0x4b,0x4d),'wrong rear MCLK')
  mclk=(cl[1]-0x49)//2;require(pins[0]['controller']=='TLMM' and pins[0]['pin']==94+mclk,'MCLK pin/clock disagreement')
  cgp=cells(node.rsplit('/',1)[0]+'/gpios');i2c=[gpio(*cgp[i:i+3]) for i in (master*6,master*6+3)]
  phy_nodes=[k.rsplit('/',1)[0] for k in fdt if 'csiphy@' in k and k.endswith('/cell-index') and cells(k)==[phy]]
  require(len(phy_nodes)==1,'ambiguous PHY')
  pn=phy_nodes[0];reg=cells(pn+'/reg');irq=cells(pn+'/interrupts');require(len(reg)==2 and reg[1]==8192 and irq[0]==0,'unexpected PHY registers/IRQ')
  supplies={}
  for k in fdt:
   if k.startswith(node+'/') and k.endswith('-supply'):
    target=handles[cells(k)[0]];supplies[k[len(node)+1:-7]]=dict(node=target,minimum_uv=cells(target+'/regulator-min-microvolt')[0] if target+'/regulator-min-microvolt' in fdt else None,maximum_uv=cells(target+'/regulator-max-microvolt')[0] if target+'/regulator-max-microvolt' in fdt else None)
  result.append(dict(slot=slot,cci=cci,master=master,phy=phy,phy_base=reg[0],phy_irq=irq[1]+32,mclk=mclk,mclk_cmd=0x5000+mclk*0x1c,mclk_branch=0x5018+mclk*0x1c,pins=pins,i2c=i2c,supplies=supplies,native_frame_verified=False))
 return result

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--fdt',type=Path,required=True);p.add_argument('--pinctrl',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 require(not a.out.exists(),'output exists');r=inspect(json.loads(a.fdt.read_text(encoding='utf-8')),a.pinctrl.read_text());a.out.write_text(json.dumps(r,indent=2),encoding='utf-8');print('Three rear routes resolved; not hardware frame tests')
