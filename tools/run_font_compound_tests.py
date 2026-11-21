#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""相邻及嵌套复合字形的真实渲染回归。/ Real-render regression for adjacent/nested compounds."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[1]

def fixture(path):
    # 原创三角形夹具，无外部字体依赖。/ Original triangle fixture, no external font dependency.
    p = struct.pack
    triangle = p('>hhhhhHH', 1, 0, 0, 500, 700, 2, 0) + bytes([1, 1, 1]) + p('>hhhhhh', 0, 500, -500, 0, 0, 700)
    def compound(parts):
        out = p('>hhhhh', -1, 0, 0, 1000, 700)
        for i, (gid, x) in enumerate(parts):
            out += p('>HHhh', 3 | (32 if i + 1 < len(parts) else 0), gid, x, 0)
        return out
    glyphs = [b'', triangle, compound([(1, 0)]), compound([(4, 400), (2, 0)]), triangle]
    glyf = b''; offsets = []
    for glyph in glyphs:
        offsets.append(len(glyf)); glyf += glyph + b'\0' * (-len(glyph) % 4)
    offsets.append(len(glyf))
    head = bytearray(54); struct.pack_into('>I', head, 0, 0x10000); struct.pack_into('>H', head, 18, 1000); struct.pack_into('>h', head, 50, 1)
    hhea = bytearray(36); struct.pack_into('>Ihhh', hhea, 0, 0x10000, 800, -200, 0); struct.pack_into('>H', hhea, 34, 5)
    cmap4 = p('>HHHHHHH',4,32,0,4,4,1,0) + p('>HHHHHHHHH',0x44,0xffff,0,0x41,0xffff,0xffc0,1,0,0)
    tables = {'head':head,'hhea':hhea,'hmtx':p('>Hh',1000,0)*5,'maxp':p('>IH',0x10000,5)+bytes(26),'loca':p('>6I',*offsets),'glyf':glyf,'cmap':p('>HHHHI',0,1,3,1,12)+cmap4}
    out = bytearray(p('>IHHHH',0x10000,7,64,2,48)+bytes(16*7))
    for i,(tag,data) in enumerate(sorted(tables.items())):
        pos=len(out);out+=data;out+=b'\0'*(-len(out)%4)
        struct.pack_into('>4sIII',out,12+i*16,tag.encode(),0,pos,len(data))
    path.write_bytes(out + bytes(max(0, 1024 - len(out))))

with tempfile.TemporaryDirectory(prefix='font-compound-') as temp:
    temp = Path(temp)
    font=temp/'adjacent.ttf';fixture(font)
    exe=temp/'font-compound'
    includes=['sim/idf','main','main/book','components/epdiy/src','components/epdiy/include']
    subprocess.run(['cc','-std=gnu17','-O1','-g','-fsanitize=address,undefined','-DSIM_HOST=1',*[f'-I{ROOT / x}' for x in includes],str(ROOT/'tools/font_compound_host_test.c'),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe),str(font),str(ROOT/'builtin/storage/fonts/PicoHei-Regular.ttf'),*[str(Path(x).resolve()) for x in sys.argv[1:]]],check=True)
