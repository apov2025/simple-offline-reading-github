#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""验证真实前光出口的硬件占空比上限。/ Test hardware duty limits at the actual frontlight output."""
from pathlib import Path
import subprocess, tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'main/board/metalio_board.c').read_text()
f=s[s.index('void reader_board_set_light('):s.index('void metalio_reader_light_sleep(')]
h=r'''#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "reader_controls.h"
#define LEDC_LOW_SPEED_MODE 0
#define LEDC_CHANNEL_0 0
#define LEDC_CHANNEL_1 1
static bool sleeping;
static uint32_t duty[2];
int ledc_set_duty(int mode,int channel,uint32_t value){assert(mode==0);duty[channel]=value;return 0;}
int ledc_update_duty(int mode,int channel){(void)mode;(void)channel;return 0;}
'''
m=r'''int main(void){
 // 官方 frontlight.cc 的 10bit PWM 最高10%，不是UI的100%输出满占空。
 // Official frontlight.cc caps 10-bit PWM at 10%, independent of UI percentage.
 for(int sleep=0;sleep<2;sleep++)for(int level=0;level<256;level++)for(int warmth=0;warmth<256;warmth++){
  sleeping=sleep;reader_board_set_light(level,warmth);
  assert(duty[0]<=102 && duty[1]<=102);
  if(sleep || level==0 || level>3 || warmth>2)assert(duty[0]==0 && duty[1]==0);
 }
 sleeping=false;reader_board_set_light(3,0);assert(duty[0]==102 && duty[1]==0);
 reader_board_set_light(3,2);assert(duty[0]==0 && duty[1]==102);
 puts("Metalio light: actual hardware output bounded to vendor 10% limit; off/sleep/invalid inputs PASS");
}'''
with tempfile.TemporaryDirectory(prefix='metalio-light-') as t:
 p=Path(t);(p/'test.c').write_text(h+f+m)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(r/'main/board'),str(p/'test.c'),str(r/'main/board/reader_controls.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
