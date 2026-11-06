"""Preview a verified native RAW10 frame as an uncalibrated grayscale mosaic."""
import argparse
import importlib.util
import json
from pathlib import Path
import numpy as np
from PIL import Image


def unpack(raw, width, height, stride):
    if width % 4 or len(raw) != height*stride or stride < width*5//4:
        raise ValueError('invalid RAW10 geometry')
    q=np.frombuffer(raw,dtype=np.uint8).reshape(height,stride)[:,:width*5//4].reshape(height,width//4,5).astype(np.uint16)
    out=np.empty((height,width),dtype=np.uint16)
    for i in range(4):out[:,i::4]=(q[:,:,i]<<2)|((q[:,:,4]>>(i*2))&3)
    return out


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('frame',type=Path);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    spec=importlib.util.spec_from_file_location('native_reader',Path(__file__).with_name('read-in2020-native-raw.py'))
    reader=importlib.util.module_from_spec(spec);spec.loader.exec_module(reader)
    raw,meta=reader.decode_frame(a.frame.read_bytes())
    if a.out.exists():raise ValueError('output exists')
    # Independent known packed vector: pixels 0,1021,514,255.
    if unpack(bytes([0,255,128,63,228]),4,1,5).tolist()!=[[0,1021,514,255]]:raise ValueError('RAW10 unpack failed')
    pixels=unpack(raw,meta['width'],meta['height'],meta['stride'])
    a.out.mkdir(parents=True)
    Image.fromarray(pixels<<6).save(a.out/'mosaic16.png')
    preview=Image.fromarray((pixels>>2).astype(np.uint8))
    preview.thumbnail((1164,874));preview.save(a.out/'preview-gray.png')
    # Display-only percentile stretch; keep the original RAW and mosaic intact.
    low,high=np.percentile(pixels,[1,99.9])
    if high>low:
        visible=(np.clip((pixels.astype(np.float32)-low)/(high-low),0,1)**0.5*255).astype(np.uint8)
        stretched=Image.fromarray(visible);stretched.thumbnail((1164,874))
        stretched.save(a.out/'preview-gray-stretched.png')
    meta['display_stretch']={'low_percentile_1':float(low),'high_percentile_99_9':float(high),'gamma':0.5}
    meta.update(minimum=int(pixels.min()),maximum=int(pixels.max()),mean=float(pixels.mean()),
                unique_values=int(np.unique(pixels).size),preview='Uncalibrated Bayer mosaic, grayscale; no color or exposure corrections')
    (a.out/'preview.json').write_text(json.dumps(meta,indent=2),encoding='utf-8')
    print(json.dumps(meta,indent=2))


if __name__=='__main__':main()
