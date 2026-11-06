"""Synthetic HFS+ and native RAW bounds/CRC tests; no device access."""
import importlib.util
import struct
import zlib
from pathlib import Path

s=importlib.util.spec_from_file_location('reader',Path(__file__).with_name('read-in2020-native-raw.py'))
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)

def check(ok):
    if not ok: raise AssertionError('check failed')
def reject(fn):
    try: fn()
    except (ValueError,struct.error): return
    raise AssertionError('accepted invalid data')

def fork(size,start,count):
    return struct.pack('>QII',size,0,count)+struct.pack('>II',start,count)+bytes(56)
def record(parent,name,data):
    name=name.encode('utf-16-be')
    return struct.pack('>HIH',6+len(name),parent,len(name)//2)+name+data

def fixture():
    b=bytearray(64*4096)
    b[1024:1028]=b'H+\0\x04'
    struct.pack_into('>II',b,1064,4096,64)
    b[1296:1376]=fork(8192,2,2)
    b[8192+8]=1
    struct.pack_into('>I',b,8192+16,1)
    struct.pack_into('>I',b,8192+24,1)
    struct.pack_into('>H',b,8192+32,4096)
    records=[]
    for parent,name,cid in [(2,'private',10),(10,'var',11),(11,'tmp',12)]:
        data=bytearray(88);struct.pack_into('>H',data,0,1);struct.pack_into('>I',data,8,cid)
        records.append(record(parent,name,data))
    data=bytearray(248);struct.pack_into('>H',data,0,2);struct.pack_into('>I',data,8,13)
    data[88:168]=fork(6,4,1)
    records.append(record(12,'in2020-front-raw10-123.bin',data))
    at=12288;b[at+8]=255;struct.pack_into('>H',b,at+10,len(records))
    cursor=14
    for i,r in enumerate(records):
        struct.pack_into('>H',b,at+4096-2*(i+1),cursor)
        b[at+cursor:at+cursor+len(r)]=r;cursor+=len(r)
    struct.pack_into('>H',b,at+4096-2*(len(records)+1),cursor)
    b[16384:16390]=b'RAW123'
    return b

b=fixture();reads=[]
def read(off,n):
    check(off>=0 and off+n<=len(b));reads.append((off,n));return bytes(b[off:off+n])
v=m.HFS(read,len(b));f=v.lookup('/private/var/tmp/in2020-front-raw10-123.bin')
check(v.fread(f,0,6)==b'RAW123')
reject(lambda:v.lookup('/private/var/tmp/missing'))
reject(lambda:v.fread(f,0,7))
reject(lambda:v.lookup('/private/../tmp'))
reject(lambda:m.HFS(lambda off,n:bytes(n-1),len(b)))
b[1024:1026]=b'XX';reject(lambda:m.HFS(read,len(b)));b=fixture()
struct.pack_into('>H',b,12288+4096-2,0);reject(lambda:m.HFS(read,len(b)).lookup('/private/var/tmp/a'))
b=fixture();struct.pack_into('>Q',b,1296,9000);reject(lambda:m.HFS(read,len(b)))
# An index-root fixture exercises the bounded parent-ID seek.
b=fixture()
b[1296:1376]=fork(12288,2,3)
struct.pack_into('>I',b,8192+16,2)
old=fork(6,4,1);at=b.find(old,12288);check(at>=0)
b[at:at+80]=fork(6,6,1);b[24576:24582]=b'RAW123'
node=16384;b[node:node+4096]=bytes(4096)
r=record(2,'',struct.pack('>I',1))
struct.pack_into('>H',b,node+10,1)
b[node+14:node+14+len(r)]=r
struct.pack_into('>H',b,node+4094,14)
struct.pack_into('>H',b,node+4092,14+len(r))
v=m.HFS(read,len(b));f=v.lookup('/private/var/tmp/in2020-front-raw10-123.bin')
check(v.fread(f,0,6)==b'RAW123')
struct.pack_into('>I',b,node+22,2)
reject(lambda:m.HFS(read,len(b)).lookup('/private/var/tmp/in2020-front-raw10-123.bin'))
raw=bytes([0xa3])*(2912*1748)
header=struct.pack('<8s8I3Q',b'I20FRAME',1,2328,1748,2912,10,zlib.crc32(raw),1,0,123,0,0)
check(m.decode_frame(header+raw)[0]==raw)
reject(lambda:m.decode_frame(header+raw[:-1]))
reject(lambda:m.decode_frame(header+raw[:-1]+b'\x00'))
reject(lambda:m.decode_frame(bytes(64)+raw))
print('HFS readonly path/extents/bounds and native RAW header/CRC PASS')
