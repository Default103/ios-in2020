"""Bounded read-only HFS+ extraction of a native IN2020 RAW frame.
No mounts, device writes, extents-overflow guesses or recovery camera backend.
"""
import argparse
import json
import struct
import subprocess
import unicodedata
import zlib
from pathlib import Path


def require(ok, message):
    if not ok:
        raise ValueError(message)


def be16(b, at=0): return struct.unpack_from('>H', b, at)[0]
def be32(b, at=0): return struct.unpack_from('>I', b, at)[0]
def be64(b, at=0): return struct.unpack_from('>Q', b, at)[0]


class HFS:
    def __init__(self, read, capacity):
        self.read, self.capacity = read, capacity
        h = self.exact(1024, 512)
        require(h[:2] in (b'H+', b'HX') and be16(h, 2) in (4, 5), 'not HFS+/HFSX')
        self.case_sensitive = h[:2] == b'HX'
        self.block, blocks = be32(h, 40), be32(h, 44)
        require(512 <= self.block <= 1024*1024 and self.block & (self.block-1) == 0,
                'invalid allocation block size')
        require(0 < blocks*self.block <= capacity, 'volume exceeds partition')
        self.volume_size = blocks*self.block
        self.catalog = self.fork(h[272:352])
        header = self.fread(self.catalog, 0, 64)
        require(struct.unpack_from('b', header, 8)[0] == 1, 'catalog header node missing')
        self.root, self.first, self.node_size = be32(header, 16), be32(header, 24), be16(header, 32)
        require(512 <= self.node_size <= 32768 and self.node_size & (self.node_size-1) == 0,
                'invalid catalog node size')
        require(self.root and self.first, 'empty catalog')

    def exact(self, offset, length):
        require(0 <= offset and 0 <= length <= 16*1024**2 and offset+length <= self.capacity,
                'out of partition read')
        data = self.read(offset, length)
        require(len(data) == length, 'short partition read')
        return data

    def fork(self, data):
        require(len(data) == 80, 'invalid fork record')
        size, extents = be64(data), []
        require(size <= self.capacity, 'oversized fork')
        for i in range(8):
            start, count = struct.unpack_from('>II', data, 16+i*8)
            if count:
                require((start+count)*self.block <= self.volume_size, 'extent outside volume')
                extents.append((start*self.block, count*self.block))
        require(sum(n for _, n in extents) >= size, 'extents overflow unsupported; refusing incomplete fork')
        return size, extents

    def fread(self, fork, offset, length):
        size, extents = fork
        require(0 <= offset and 0 <= length <= 16*1024**2 and offset+length <= size,
                'fork read outside logical size')
        out = bytearray()
        for start, count in extents:
            if offset >= count:
                offset -= count
                continue
            n = min(length-len(out), count-offset)
            out.extend(self.exact(start+offset, n))
            offset = 0
            if len(out) == length:
                break
        require(len(out) == length, 'incomplete fork read')
        return bytes(out)

    def node(self, number):
        b = self.fread(self.catalog, number*self.node_size, self.node_size)
        count = be16(b, 10)
        require(count <= (self.node_size-16)//4, 'invalid node record count')
        offsets = [be16(b, self.node_size-2*(i+1)) for i in range(count+1)]
        require(all(14 <= a < c <= self.node_size-2*(count+1)
                    for a, c in zip(offsets, offsets[1:])), 'invalid node record offsets')
        records = []
        for a, c in zip(offsets, offsets[1:]):
            r = b[a:c]
            require(len(r) >= 8, 'short catalog key')
            keylen, parent, chars = be16(r), be32(r, 2), be16(r, 6)
            require(keylen == 6+chars*2 and 2+keylen <= len(r), 'invalid catalog key length')
            name = r[8:8+chars*2].decode('utf-16-be', errors='strict')
            records.append(((parent, name), r[2+keylen:]))
        return struct.unpack_from('b', b, 8)[0], be32(b), records

    def parent_leaf(self, parent):
        # Parent IDs are the primary catalog sort key. Seek strictly before
        # the target parent; this also catches names split across leaf nodes
        # without assuming Python implements HFS Unicode name collation.
        current, visited = self.root, set()
        while True:
            require(current not in visited and len(visited)<32, 'catalog index cycle/height')
            visited.add(current)
            kind, following, rows = self.node(current)
            if kind == -1:
                return current
            require(kind == 0 and rows, 'invalid catalog index node')
            selected = None
            for (pid, name), data in rows:
                require(len(data) == 4, 'invalid catalog index pointer')
                child = be32(data)
                require(child != 0, 'catalog index points to header')
                if selected is None:
                    selected = child
                if pid >= parent:
                    break
                selected = child
            current = selected

    def list_parent(self, parent):
        # Scan from a parent-ID lower bound, then follow the leaf chain.
        current, visited, found = self.parent_leaf(parent), set(), []
        while current:
            require(current not in visited and len(visited) < 200000, 'catalog leaf cycle/limit')
            visited.add(current)
            kind, following, rows = self.node(current)
            require(kind == -1, 'non-leaf in catalog leaf chain')
            for (pid, name), data in rows:
                if pid == parent:
                    found.append((name, data))
                elif pid > parent:
                    return found
            current = following
        return found

    def lookup(self, path):
        parent = 2
        parts = [p for p in path.split('/') if p]
        require(parts and all(p not in ('.', '..') for p in parts), 'invalid path')
        for i, part in enumerate(parts):
            def fold(s):
                s = unicodedata.normalize('NFD', s)
                return s if self.case_sensitive else s.casefold()
            matches = [data for name, data in self.list_parent(parent) if fold(name) == fold(part)]
            require(len(matches) == 1, 'missing/ambiguous path component: '+part)
            data = matches[0]
            require(len(data) >= 12, 'short catalog record')
            if i != len(parts)-1:
                require(be16(data) == 1, 'path parent is not a folder')
                parent = be32(data, 8)
            else:
                require(be16(data) == 2 and len(data) >= 248, 'not a complete file record')
                return self.fork(data[88:168])


def decode_frame(blob):
    require(len(blob) >= 64, 'short frame header')
    magic, version, w, h, stride, bits, crc, flags, reserved, time, r0, r1 = struct.unpack_from('<8s8I3Q', blob)
    require(magic == b'I20FRAME' and version == 1, 'unknown native frame format')
    require((w,h,stride) in ((2328,1748,2912),(2104,1560,2640)) and (bits,flags,reserved,r0,r1)==(10,1,0,0,0), 'unexpected frame metadata')
    raw = blob[64:]
    require(len(raw) == stride*h and zlib.crc32(raw) == crc, 'frame length/CRC mismatch')
    return raw, dict(width=w, height=h, stride=stride, bits=bits, crc32=f'{crc:08x}', capture_time=time,
                     format='MIPI RAW10; Bayer order not yet verified')


class ADBRead:
    def __init__(self, adb, serial):
        self.adb, self.serial, self.cache = adb, serial, {}
        devices = subprocess.check_output([adb, 'devices'], text=True)
        rows = [r.split() for r in devices.splitlines()[1:] if r.strip()]
        require(rows == [[serial, 'recovery']] or rows == [[serial, 'device']], 'device inventory not unique')
        require(self.shell('id -u').strip() == b'0', 'root recovery required')
        require(self.shell('getprop ro.twrp.version').strip(), 'TWRP identity missing')
        self.capacity = int(self.shell('blockdev --getsize64 /dev/block/by-name/userdata').strip())
        require(self.capacity == 108049805312, 'unexpected userdata partition size')
        mounts = self.shell('cat /proc/mounts').decode()
        require(not any('/dev/block/sda23 ' in l or ' /data ' in l for l in mounts.splitlines()),
                'userdata must remain unmounted for consistent readback')

    def shell(self, command):
        return subprocess.check_output([self.adb, '-s', self.serial, 'exec-out', command], timeout=45)

    def __call__(self, offset, length):
        result = bytearray()
        while length:
            chunk, within = divmod(offset, 65536)
            if chunk not in self.cache:
                count = min(65536, self.capacity-chunk*65536)//4096
                b = self.shell(f'dd if=/dev/block/by-name/userdata bs=4096 skip={chunk*16} count={count} 2>/dev/null')
                require(len(b) == count*4096, 'short ADB block read')
                self.cache[chunk] = b
            b = self.cache[chunk]
            n = min(length, len(b)-within)
            require(n > 0, 'invalid cached read')
            result.extend(b[within:within+n])
            offset += n
            length -= n
        return bytes(result)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--adb', required=True)
    p.add_argument('--serial', required=True)
    p.add_argument('--path', required=True, help='Exact successful native export path from kernel log')
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    require(any(a.path.startswith('/private/var/tmp/in2020-'+sensor+'-raw10-') for sensor in ('front','tele')) and
            a.path.endswith('.bin') and a.path.rsplit('-',1)[1][:-4].isdigit(), 'unexpected native export path')
    require(not a.out.exists(), 'output already exists')
    reader = ADBRead(a.adb, a.serial)
    volume = HFS(reader, reader.capacity)
    fork = volume.lookup(a.path)
    require(fork[0] in (64+2912*1748,64+2640*1560), 'unexpected native file size')
    blob = volume.fread(fork, 0, fork[0])
    raw, metadata = decode_frame(blob)
    a.out.mkdir(parents=True)
    (a.out/'native-frame.bin').write_bytes(blob)
    (a.out/'frame.raw10').write_bytes(raw)
    metadata['native_path'] = a.path
    (a.out/'frame.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    print('Verified native RAW10:', len(raw), 'bytes; CRC32', metadata['crc32'])


if __name__ == '__main__':
    main()
