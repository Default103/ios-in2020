"""Generate bounded C-PHY1.2.1 register operations from pinned OnePlus source.
No sensor I/O; caller supplies a reviewed per-trio bit rate and settle count.
"""
from pathlib import Path
import argparse,hashlib,json,re,struct,zlib
PIN='f972041defde9b6ac22c07eb2fb0ec10f2cdec1e4975ea0aa469c57679afbd80'

def build(source,settle,bitrate):
 if hashlib.sha256(source).hexdigest()!=PIN:raise ValueError('unrecognized PHY source')
 if not 0<settle<=65535 or not 0<bitrate<=10260000000:raise ValueError('unsupported C-PHY rate/settle')
 text=source.decode()
 def block(start):
  a=text.index('{',start);depth=0
  for b in range(a,len(text)):
   depth+=(text[b]=='{')-(text[b]=='}')
   if not depth:return text[a:b+1]
  raise ValueError('unterminated array')
 def rows(s):return re.findall(r'\{\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),\s*(CSIPHY_\w+)\s*\}',s)
 ops=[]
 common=rows(block(text.index('=',text.index('csiphy_common_reg_1_2_1'))))
 lane=rows(block(text.index('=',text.index('csiphy_3ph_v1_2_1_reg'))))
 if len(common)!=6 or len(lane)!=102:raise ValueError('unexpected array sizes')
 def append(row):
  off,value,delay,kind=row;off,value,delay=int(off,0),int(value,0),int(delay,0)
  if delay or off%4 or off>=4096 or value>255:raise ValueError('unsupported operation')
  if kind=='CSIPHY_LANE_ENABLE':value=0x2a
  elif kind=='CSIPHY_SETTLE_CNT_LOWER_BYTE':value=settle&255
  elif kind=='CSIPHY_SETTLE_CNT_HIGHER_BYTE':value=settle>>8
  elif kind not in ('CSIPHY_DEFAULT_PARAMS','CSIPHY_3PH_REGS'):raise ValueError('unknown operation type')
  ops.append((off,value))
 for row in common:
  if row[3]!='CSIPHY_2PH_REGS':append(row)
 for row in lane:append(row)
 rates=[]
 begin=text.index('data_rate_delta_table_1_2_1')
 for m in re.finditer(r'\.bandwidth\s*=\s*(\d+)',text[begin:]):
  start=begin+m.end();count=re.search(r'\.data_rate_reg_array_size\s*=\s*(\d+)',text[start:])
  regs=rows(block(text.index('=',text.index('.csiphy_data_rate_regs',start))))
  if len(regs)!=int(count[1]):raise ValueError('rate table size mismatch')
  rates.append((int(m[1]),regs))
 selected=next((q for q in rates if bitrate<=q[0]),None)
 if selected is None:raise ValueError('rate not supported')
 for row in selected[1]:append(row)
 if len(ops)>128:raise ValueError('native PHY operation bound exceeded')
 body=b''.join(struct.pack('<II',*q) for q in ops)
 return struct.pack('<II',len(ops),zlib.crc32(body))+body,dict(cphy=True,lanes=3,lane_enable=0x2a,settle=settle,per_trio_bits_per_second=bitrate,selected_bandwidth=selected[0],operations=len(ops),native_tested=False)

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--settle',type=int,required=True);p.add_argument('--bitrate',type=int,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 if a.out.exists():raise ValueError('output exists')
 blob,meta=build(a.source.read_bytes(),a.settle,a.bitrate);a.out.write_bytes(blob);print(json.dumps(meta,indent=2))
