"""Extract pinned rear-sensor tables for native RAW capture; never performs I/O.
Preserve register order, repeated indirect writes and per-command widths/delays.
"""
from pathlib import Path
import argparse,hashlib,json,runpy,struct

PROFILES={
 'main':('semco_imx689_MP','624e67f9bdf9b6e0bc47799757384cc653f5c0f887dd4c061e85452b7ead3073',0,1,2232,106,4000,3000,3,1,0x34,0x689),
 'ultra':('semco_imx586','2e429cc5e77cdabb3e3fd4f9ca640b10fbf889a756bd0929526130d307f3960d',1,1,1708,100,4000,3000,3,1,0x20,0x586),
 'tele':('semco_s5k3m5','15731f932bd6a15d848d3d050e9df40697ac6376f559dedbc0a4362797018c49',3,3,918,53,2104,1560,4,0,0x5a,0x30d5),
}

def require(ok,why):
 if not ok:raise ValueError(why)

def build(data,which):
 name,pin,slot,index,ni,nm,w,h,lanes,cphy,slave,sensor_id=PROFILES[which]
 require(hashlib.sha256(data).hexdigest()==pin,'unrecognized private module')
 report=runpy.run_path(str(Path(__file__).with_name('inspect-qti-sensor-v2.py')))['inspect'](data)
 require(report['module_configuration_camera_id']==slot,'wrong slot')
 probe=report['inferred_probe_fields']
 require(probe['slave_address_field']==slave and probe['expected_id_field']==sensor_id,'wrong sensor identity')
 at,count=struct.unpack_from('<II',data,152)
 sec={k:(a,n) for k,a,n in (struct.unpack_from('<3Q',data,at+i*24) for i in range(count))}
 at,size=sec[0];payload=sec[1][0];symbols={}
 for off in range(at,at+size,72):
  ident,n,kind,mode,start,length=struct.unpack_from('<Q32s4Q',data,off)
  symbols[ident]=(n.split(b'\0')[0].decode(),payload+start,length)
 def values(ref,expected):
  n,a,size=symbols[ref];require(n==expected and size%8==0,'invalid child symbol')
  return list(struct.unpack_from('<'+'Q'*(size//8),data,a))
 def table(ref):
  n,a,size=symbols[ref];require(n=='regSetting' and size%80==0,'invalid register table');rows=[]
  for off in range(a,a+size,80):
   sn,sid,reg,n,ref,aw,dw,op,dn,delayref=struct.unpack_from('<10Q',data,off)
   require(sn==0 and values(sid,'slaveAddr')==[] and n==1 and aw==2 and dw in (1,2) and op==0 and dn==1,'unsupported register operation')
   vs,ds=values(ref,'registerData'),values(delayref,'delayUs')
   require(len(vs)==len(ds)==1 and 0<=reg<=65535 and 0<=vs[0]<(1<<(dw*8)) and 0<=ds[0]<=100000,'out of range command')
   require(reg!=0x100,'stream control must be explicit')
   if 0xa00<=reg<=0xaff:
    require(which=='tele' and reg==0xa02 and dw==2 and vs[0]==0x7800,'unreviewed OTP-region command')
   rows.append(dict(address=reg,value=vs[0],data_bytes=dw,delay_us=ds[0]))
  return rows
 wrappers=[v for v in symbols.values() if v[0]=='initSettings'];require(len(wrappers)==1,'ambiguous init')
 delay,n,ref=struct.unpack_from('<3Q',data,wrappers[0][1]);require(delay==0 and n==ni,'wrong init wrapper')
 init=table(ref);mode=report['resolution_candidates'][index];regs=table(mode['register_symbol'])
 require(len(init)==ni and len(regs)==nm,'wrong table counts')
 require(mode['raw_csi_fields'][0]==lanes and mode['raw_csi_fields'][4]==cphy,'PHY metadata mismatch')
 require(any(q['raw_fields']==[0,43,0,0,w,h,10,1] for q in mode['streams']),'wrong RAW10 stream')
 v={r['address']:(r['value'],r['data_bytes']) for r in regs}
 def word(reg):
  if which=='tele':require(v[reg][1]==2,'expected word geometry');return v[reg][0]
  require(v[reg][1]==v[reg+1][1]==1,'expected byte geometry');return (v[reg][0]<<8)|v[reg+1][0]
 require((word(0x34c),word(0x34e))==(w,h),'register geometry mismatch')
 require((word(0x342),word(0x340))==tuple(mode['raw_timing_fields'][1:3]),'timing mismatch')
 stride=((w*5//4)+15)&~15
 return dict(status='OFFLINE_REAR_PLAN_CHECKED_NOT_NATIVE_TESTED',sensor=report['sensor_name'],slot=slot,module_sha256=pin,mode_index=index,
             width=w,height=h,stride=stride,payload_bytes=stride*h,bits=10,lanes=lanes,cphy=bool(cphy),vc=0,data_type=43,
             slave_address_7bit=slave>>1,id_register=probe['id_register_field'],sensor_id=sensor_id,mclk_hz=19200000,
             initialization=init,mode_settings=regs,raw_csi_fields=mode['raw_csi_fields'],raw_power_up=report['raw_power_up_triplets'],
             initialization_count=ni,mode_count=nm,native_frame_captured=False)

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sensor',choices=PROFILES,required=True);p.add_argument('--module',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 result=build(a.module.read_bytes(),a.sensor);require(not a.out.exists(),'output exists');a.out.write_text(json.dumps(result,indent=2),encoding='utf-8')
 print(a.sensor,'mode',result['mode_index'],result['width'],result['height'],'C-PHY' if result['cphy'] else 'D-PHY',result['payload_bytes'],'bytes; not yet executed')
