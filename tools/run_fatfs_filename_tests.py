#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""用实际 SDK 文件名解码验证中文名称，不访问设备或存储卡。
Test Chinese filenames with the actual SDK decoder, without accessing a device or card.
"""
from pathlib import Path
import argparse
import os
import subprocess
import sys
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdkconfig-header', type=Path, required=True)
p.add_argument('--idf-path', type=Path, default=os.environ.get('IDF_PATH'))
a = p.parse_args()
if a.idf_path is None:
    p.error('Set IDF_PATH or pass --idf-path')
src = a.idf_path / 'components/fatfs/src'
# 字节来自FAT目录中的UTF-16长名称；由真实get_fileinfo转换。
# UTF-16 long names from FAT directory metadata are converted by real get_fileinfo.
cases = [('思源宋体-S.ttf', '_____S~2TTF'), ('思源黑体-S.ttf', '_____S~1TTF'),
         ('Reading.ttf', 'READING TTF'), ('Cafeé.ttf', 'CAFE~1  TTF')]
with tempfile.TemporaryDirectory(prefix='fatfs-names-') as temp:
    work = Path(temp)
    (work / 'sdkconfig.h').write_bytes(a.sdkconfig_header.read_bytes())
    (work / 'freertos').mkdir()
    (work / 'freertos/FreeRTOS.h').write_text('#include <stddef.h>\n#define portTICK_PERIOD_MS 1\n')
    (work / 'freertos/semphr.h').write_text('typedef void* SemaphoreHandle_t;\n')
    body = ['#include <stdio.h>\n#include "ff.c"\nint main(void) { int failures=0;']
    for name, short in cases:
        assert len(short) == 11, short
        units = ','.join(str(ord(c)) for c in name) + ',0'
        body.append('''{
WCHAR name[] = {%s}; BYTE entry[32]={0}; memcpy(entry,"%s",11);
FATFS fs={0}; fs.lfnbuf=name;
FF_DIR dir={0}; dir.obj.fs=&fs; dir.dir=entry; dir.sect=1;
FILINFO info={0}; get_fileinfo(&dir,&info);
if(strcmp(info.fname,"%s")) { printf("FAIL: expected %%s, got %%s\\n","%s",info.fname); failures++; }
}''' % (units, short, name, name))
    body.append('if(!failures) puts("FatFs filename decoding: 4 cases PASS"); return failures ? 1 : 0; }')
    (work / 'test.c').write_text('\n'.join(body))
    strip = '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections'
    subprocess.run(['cc','-O2','-ffunction-sections','-fdata-sections',f'-I{work}',f'-I{src}',str(work/'test.c'),str(src/'ffunicode.c'),strip,'-o',str(work/'test')],check=True)
    sys.exit(subprocess.run([str(work/'test')]).returncode)
