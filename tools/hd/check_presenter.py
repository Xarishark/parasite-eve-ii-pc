#!/usr/bin/env python3
"""Exercise the real hidden GPU presenter with synthetic pixels and corrupt packs."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess

from PIL import Image
from pack import build


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe',type=Path,default=Path('build-port/background_presenter_probe'))
    parser.add_argument('--output',type=Path,default=Path('build-hd-study/presenter-check'))
    args=parser.parse_args();root=args.output.resolve();root.mkdir(parents=True,exist_ok=True)
    (root/'report.json').unlink(missing_ok=True)
    pixels=[]
    for y in range(240):
        for x in range(320):
            value=0x8000|((x//8)&31)|(((y//8)&31)<<5)|((((x+y)//16)&31)<<10)
            if (x+y)%7==0:value&=0x7fff
            if (x+y)%31==0:value=0
            pixels.append(value)
    native=struct.pack('<76800H',*pixels);source=root/'source.rgb555';source.write_bytes(native)
    # Distinct nonblack materials make missing coverage and misplaced cells observable.
    art=Image.new('RGB',(1792,960),(192,64,128))
    key,metadata,atlas=build(native,art,-64,384)
    pack=root/'pack';pack.mkdir(exist_ok=True)
    metaName=f'{key:016x}.pe2bg';imageName=f'{key:016x}.png'
    (pack/metaName).write_bytes(metadata);atlas.save(pack/imageName)
    results={}
    def run(case,wide,directory=None,scale=4,reload=False,buffer_y=0):
        output=root/(case+'.ppm');output.unlink(missing_ok=True)
        env={k:v for k,v in os.environ.items() if not k.startswith('PE2_')}
        env.update(PSYZ_HEADLESS='gpu',SDL_AUDIODRIVER='dummy')
        env['PE2_PROBE_BUFFER_Y']=str(buffer_y)
        if directory:env['PE2_HD_BACKGROUNDS']=str(directory)
        if reload:env['PE2_PROBE_RELOAD']='1'
        process=subprocess.run([str(args.probe.resolve()),str(source),str(wide),str(output),str(scale)],env=env,capture_output=True,text=True,timeout=30)
        (root/(case+'.log')).write_text(process.stdout+process.stderr)
        if process.returncode:raise RuntimeError(f'{case} failed: {process.stdout} {process.stderr}')
        match=re.search(r'Native differences: (\d+); rendered (\d+)x(\d+)',process.stdout)
        assert match and int(match[1])==0,case
        results[case]=dict(native_differences=int(match[1]),width=int(match[2]),height=int(match[3]))
        return output
    baseline=run('baseline',0)
    nativeWide=run('native-wide',1)
    assert results['native-wide']['width']>results['baseline']['width']*1.3
    with Image.open(nativeWide) as image:
        for x in [0,image.width-1]:
            assert image.crop((x,0,x+1,image.height)).getbbox() is None
    assert run('native-wide-buffer272',1,buffer_y=272).read_bytes()==nativeWide.read_bytes()
    hd=run('hd',0,pack);wide=run('wide',1,pack)
    reload=run('reload',1,pack,reload=True)
    for suffix in ['disabled','missing']:
        assert Path(str(reload)+'.'+suffix+'.ppm').read_bytes()==nativeWide.read_bytes(),suffix
    for suffix in ['reenabled','restored','same-folder']:
        assert Path(str(reload)+'.'+suffix+'.ppm').read_bytes()==wide.read_bytes(),suffix
    assert (root/'reload.log').read_text().count('[background] loaded ')==4
    assert hd.read_bytes()!=baseline.read_bytes()
    with Image.open(hd) as image:
        # LoadImage backgrounds are opaque: native RGB555 zero/STP must not
        # punch black holes through a valid material, even in classic 4:3.
        assert all(abs(low-value)<=1 and abs(high-value)<=1 for (low,high),value in zip(image.getextrema(),(192,64,128)))
    assert results['wide']['width']>results['baseline']['width']*1.3
    assert results['wide']['height']==results['baseline']['height']
    with Image.open(wide) as image:
        # Solid art should cover the entire wider image, including all four corners.
        for x,y in [(0,0),(image.width-1,0),(0,image.height-1),(image.width-1,image.height-1)]:
            assert max(abs(a-b) for a,b in zip(image.getpixel((x,y)),(192,64,128)))<=1,(x,y)
    for case,wide_mode,directory,reference in [('baseline',0,None,baseline),('hd',0,pack,hd),('wide',1,pack,wide)]:
        lower=run(case+'-buffer272',wide_mode,directory,buffer_y=272)
        assert lower.read_bytes()==reference.read_bytes(),(case,'physical buffers differ')
    nativeBaseline=run('native-baseline',0,scale=1)
    assert run('native-pack',1,pack,scale=1).read_bytes()==nativeBaseline.read_bytes()
    for case in ['bad-magic','wrong-key','missing-atlas','bad-atlas-header','trailing-metadata','oversized-span','oversized-png','transparent-material']:
        directory=root/case;directory.mkdir(exist_ok=True)
        data=bytearray(metadata);shutil.copyfile(pack/imageName,directory/imageName)
        if case=='bad-magic':data[0]=0
        if case=='wrong-key':data[8]^=1
        if case=='missing-atlas':(directory/imageName).unlink()
        if case=='bad-atlas-header':(directory/imageName).write_bytes(b'invalid')
        if case=='trailing-metadata':data+=b'x'
        if case=='oversized-span':struct.pack_into('<i',data,24,-112)
        if case=='oversized-png':
            image=bytearray((directory/imageName).read_bytes());struct.pack_into('>I',image,16,0x7fffffff);(directory/imageName).write_bytes(image)
        if case=='transparent-material':
            with Image.open(directory/imageName) as image:
                image=image.convert('RGBA');image.putpixel((2,2),(192,64,128,0));image.save(directory/imageName)
        (directory/metaName).write_bytes(data)
        assert run(case,1,directory).read_bytes()==nativeWide.read_bytes(),case
    (root/'report.json').write_text(json.dumps(dict(passed=True,cases=results),indent=2)+'\n')
    print('PASS: original native pixels, HD material, full 16:9 coverage, live pack disable/reload, native 16:9 with black sides, safe 1x fallback, eight malformed-pack fallbacks')


if __name__=='__main__':main()
