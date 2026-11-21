#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Offline tests for the native Pico wallpaper interchange format."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from PIL import Image, ImageCms

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('wallpaper',HERE/'prepare_wallpaper.py')
w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)

class WallpaperTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
    def tearDown(self):self.temp.cleanup()
    def cli(self,*args,good=True):
        result=subprocess.run([sys.executable,str(HERE/'prepare_wallpaper.py'),*map(str,args)],capture_output=True,text=True)
        if good:self.assertEqual(result.returncode,0,result.stderr)
        else:self.assertNotEqual(result.returncode,0)
        return result
    def test_native_nibble_order_and_length(self):
        im=Image.new('L',w.SIZE,0);im.putpixel((1,0),255);im.putpixel((2,0),255)
        raw=w.encode_raw(im,'none')
        self.assertEqual(len(raw),415872)
        self.assertEqual(raw[:2],bytes([0xf0,0x0f]))
        self.assertEqual(w.decode_raw(raw).tobytes(),im.tobytes())
    def test_black_white_and_all_sixteen_levels(self):
        for level in (0,255):
            raw=w.encode_raw(Image.new('L',w.SIZE,level),'none')
            self.assertEqual(set(raw),{0 if level==0 else 255})
        ramp=Image.new('L',w.SIZE);ramp.putdata([(x%16)*17 for y in range(w.HEIGHT) for x in range(w.WIDTH)])
        self.assertEqual(w.decode_raw(w.encode_raw(ramp,'none')).tobytes(),ramp.tobytes())
        self.assertEqual(set(ramp.tobytes()),set(range(0,256,17)))
    def test_transparent_pixels_become_white(self):
        source=self.root/'transparent.png';Image.new('RGBA',(80,120),(0,0,0,0)).save(source)
        self.cli(source,'-o',self.root/'wall.4bpp')
        self.assertEqual(set((self.root/'wall.4bpp').read_bytes()),{255})
    def test_contain_keeps_square_and_cover_is_explicit(self):
        source=self.root/'square.png';Image.new('RGB',(100,100),'black').save(source)
        self.cli(source,'-o',self.root/'contain.4bpp')
        im=w.decode_raw((self.root/'contain.4bpp').read_bytes())
        self.assertEqual(im.getpixel((0,265)),255);self.assertEqual(im.getpixel((0,266)),0)
        self.assertEqual(im.getpixel((683,949)),0);self.assertEqual(im.getpixel((683,950)),255)
        self.cli(source,'-o',self.root/'cover.4bpp','--fit','cover')
        self.assertEqual(set((self.root/'cover.4bpp').read_bytes()),{0})
    def test_exif_is_applied_before_layout(self):
        source=self.root/'rotated.jpg';im=Image.new('RGB',(40,20),'white')
        for y in range(20):
            for x in range(20):im.putpixel((x,y),(0,0,0))
        exif=Image.Exif();exif[274]=6;im.save(source,quality=100,subsampling=0,exif=exif)
        self.cli(source,'-o',self.root/'wall.4bpp')
        out=w.decode_raw((self.root/'wall.4bpp').read_bytes())
        self.assertLess(out.getpixel((342,100)),34);self.assertGreater(out.getpixel((342,1100)),221)
        self.assertEqual(json.loads((self.root/'wall.json').read_text())['source']['oriented_size'],[20,40])
    def test_deterministic_dither_uses_sixteen_levels(self):
        im=Image.new('L',w.SIZE);im.putdata([int(x*255/(w.WIDTH-1)) for y in range(w.HEIGHT) for x in range(w.WIDTH)])
        a=w.encode_raw(im,'fs');b=w.encode_raw(im,'fs')
        self.assertEqual(a,b);self.assertEqual(set(w.decode_raw(a).tobytes()),set(range(0,256,17)))
        self.assertNotEqual(a,w.encode_raw(im,'none'))
    def test_preview_and_metadata_describe_final_bytes(self):
        source=self.root/'photo.png';Image.linear_gradient('L').resize((90,160)).save(source)
        self.cli(source,'-o',self.root/'wall.4bpp')
        raw=(self.root/'wall.4bpp').read_bytes();meta=json.loads((self.root/'wall.json').read_text())
        with Image.open(self.root/'wall.preview.png') as preview:self.assertEqual(preview.tobytes(),w.decode_raw(raw).tobytes())
        self.assertEqual(meta['raw_sha256'],hashlib.sha256(raw).hexdigest())
        self.cli('--validate',self.root/'wall.4bpp','--force')
        with Image.open(self.root/'wall.preview.png') as preview:self.assertEqual(preview.tobytes(),w.decode_raw(raw).tobytes())
    def test_source_and_existing_outputs_are_protected(self):
        source=self.root/'source.png';Image.new('RGB',(30,20),'red').save(source);original=source.read_bytes()
        self.cli(source,'-o',self.root/'wall.4bpp');outputs={p.name:p.read_bytes() for p in self.root.glob('wall.*')}
        self.cli(source,'-o',self.root/'wall.4bpp',good=False)
        self.assertEqual(outputs,{p.name:p.read_bytes() for p in self.root.glob('wall.*')})
        self.cli(source,'-o',source,'--force',good=False);self.assertEqual(source.read_bytes(),original)
        self.cli(source,'-o',self.root/'wall.4bpp','--force');self.assertEqual(source.read_bytes(),original)
    def test_bad_raw_and_bad_icc_fail_clearly(self):
        raw=self.root/'bad.4bpp';raw.write_bytes(b'\0'*32)
        result=self.cli('--validate',raw,good=False);self.assertIn('415872',result.stderr)
        source=self.root/'badicc.png';Image.new('RGB',(20,30),'white').save(source,icc_profile=b'invalid profile')
        result=self.cli(source,good=False);self.assertIn('ICC',result.stderr)
        self.cli(source,'--ignore-icc');self.assertEqual(set(source.with_suffix('.4bpp').read_bytes()),{255})
    def test_partial_alpha_and_no_preview_source_overwrite(self):
        source=self.root/'wall.preview.png';Image.new('RGBA',(68,121),(0,0,0,128)).save(source)
        original=source.read_bytes()
        self.cli(source,'-o',self.root/'wall.4bpp','--force',good=False)
        self.assertEqual(source.read_bytes(),original)
        self.cli(source,'-o',self.root/'safe.4bpp')
        self.assertEqual(w.decode_raw((self.root/'safe.4bpp').read_bytes()).getpixel((342,608)),119)
    def test_filename_limit_counts_utf8_bytes(self):
        source=self.root/'image.png';Image.new('RGB',(10,10),'white').save(source)
        name='字'*31+'.4bpp'
        result=self.cli(source,'-o',self.root/name,good=False)
        self.assertIn('95 UTF-8 bytes',result.stderr)
    def test_device_filename_must_be_loadable(self):
        source=self.root/'image.png';Image.new('RGB',(10,10),'white').save(source)
        for name in ('001.4BPP','.hidden.4bpp'):
            target=self.root/name
            self.cli(source,'-o',target,good=False)
            self.assertFalse(target.exists())
    def test_cli_determinism_and_existing_preview_guard(self):
        source=self.root/'image.png';Image.linear_gradient('L').save(source)
        self.cli(source,'-o',self.root/'one.4bpp')
        self.cli(source,'-o',self.root/'two.4bpp')
        for suffix in ('.4bpp','.preview.png','.json'):
            self.assertEqual((self.root/('one'+suffix)).read_bytes(),(self.root/('two'+suffix)).read_bytes())
        (self.root/'blocked.preview.png').write_bytes(b'keep this')
        self.cli(source,'-o',self.root/'blocked.4bpp',good=False)
        self.assertFalse((self.root/'blocked.4bpp').exists())
        self.assertEqual((self.root/'blocked.preview.png').read_bytes(),b'keep this')
    def test_valid_srgb_icc(self):
        source=self.root/'profile.png';profile=ImageCms.ImageCmsProfile(ImageCms.createProfile('sRGB')).tobytes()
        Image.new('RGB',(20,30),'white').save(source,icc_profile=profile)
        self.cli(source);self.assertEqual(set(source.with_suffix('.4bpp').read_bytes()),{255})

if __name__=='__main__':unittest.main(verbosity=2)
