#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""验证实际刷新出口的电源生命周期。/ Test power lifetime of actual presentation code."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'main/board/metalio_display.c').read_text()
s='\n'.join(l for l in s.splitlines() if not l.startswith('#include'))
b=(r/'main/board/metalio_main.c').read_text()
b=b[b.index('    epd_poweron();'):b.index('    reader_board_set_light(')]
h=r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define APP_GC16_EVERY 14
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef struct {int unused;} EpdiyHighlevelState;
typedef struct {int unused;} EpdWaveform;
typedef struct {int x,y,width,height;} EpdRect;
enum EpdDrawMode {MODE_DU=1,MODE_GC16=2,MODE_GL16=5};
enum EpdDrawError {EPD_DRAW_SUCCESS=0,EPD_DRAW_EMPTY_LINE_QUEUE=0x400};
static const EpdWaveform waveform={0};
#define READER_WAVEFORM waveform
static bool powered, output_enabled, no_change, power_ok=true;
static int scans, clears, ons, offs;
static enum EpdDrawError injected, clear_injected;
static const EpdWaveform* selected;
int64_t esp_timer_get_time(void){return 0;}
void epd_poweron(void){assert(!powered);powered=true;output_enabled=true;ons++;}
void epd_poweroff(void){powered=false;output_enabled=false;offs++;}
bool epd_board_metalio_eink4_plus_tps_ok(void){return power_ok;}
void epd_hl_waveform(EpdiyHighlevelState* h,const EpdWaveform* w){(void)h;selected=w;}
void epd_hl_set_all_white(EpdiyHighlevelState* h){(void)h;}
void epd_clear(void){assert(powered);clears++;}
enum EpdDrawError epd_clear_checked(void){epd_clear();return clear_injected;}
void epd_lcd_set_pixel_clock_MHz(int mhz){assert(mhz==12);}
static enum EpdDrawError scan(void){assert(powered && power_ok);scans++;if(!no_change)output_enabled=false;return injected;}
enum EpdDrawError epd_hl_update_screen_full(EpdiyHighlevelState* h,enum EpdDrawMode m,int t){(void)h;(void)m;(void)t;return scan();}
enum EpdDrawError epd_hl_update_screen(EpdiyHighlevelState* h,enum EpdDrawMode m,int t){return epd_hl_update_screen_full(h,m,t);}
enum EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState* h,enum EpdDrawMode m,int t){return epd_hl_update_screen_full(h,m,t);}
enum EpdDrawError epd_hl_update_area(EpdiyHighlevelState* h,enum EpdDrawMode m,int t,EpdRect a){(void)a;return epd_hl_update_screen_full(h,m,t);}
'''
m=r'''int main(void){
 EpdiyHighlevelState hl={0};EpdRect area={0,0,20,20};
 boot_clear();assert(!powered && !output_enabled && clears==1);
 no_change=true;assert(update_display_mode(&hl,MODE_DU)==EPD_DRAW_SUCCESS);
 assert(!powered && !output_enabled);no_change=false;
 for(int fail=0;fail<2;fail++){
  injected=clear_injected=fail?EPD_DRAW_EMPTY_LINE_QUEUE:EPD_DRAW_SUCCESS;
  assert(update_display_full(&hl)==injected);assert(!powered);
  assert(update_display_mode(&hl,MODE_DU)==injected);assert(!powered);
  assert(update_display_area_with(&hl,&waveform,MODE_DU,area)==injected);assert(!powered);
  assert(update_display_from_white(&hl)==injected);assert(!powered);
  int before=scans;
  guard_draw_result(&hl,EPD_DRAW_EMPTY_LINE_QUEUE);assert(!powered && !output_enabled);
  if(fail){
   assert(scans==before); // Failed physical clear must not be followed by a white-baseline draw.
   int before_on=ons;
   assert(update_display_mode(&hl,MODE_DU)==EPD_DRAW_EMPTY_LINE_QUEUE);
   assert(scans==before && ons==before_on && !powered);
  }
 }
 // 模拟另一次开机：清屏成功但重绘失败也必须锁住后续扫描。
 // Model a fresh boot: redraw failure after a successful clear also inhibits later scans.
 scan_fault=false;clear_injected=EPD_DRAW_SUCCESS;injected=EPD_DRAW_EMPTY_LINE_QUEUE;
 guard_draw_result(&hl,EPD_DRAW_EMPTY_LINE_QUEUE);assert(scan_fault && !powered);
 int before=scans, before_on=ons;
 assert(update_display_from_white(&hl)==EPD_DRAW_EMPTY_LINE_QUEUE);
 assert(scans==before && ons==before_on);
 // 模拟新开机后电源失败。/ Model a power failure on a fresh boot.
 scan_fault=false;power_ok=false;before=scans;
 assert(update_display_full(&hl)==EPD_DRAW_EMPTY_LINE_QUEUE && !powered && !output_enabled);
 guard_draw_result(&hl,EPD_DRAW_EMPTY_LINE_QUEUE);assert(scans==before && !powered);
 assert(scans>0 && ons==offs && selected==&waveform);
 puts("Metalio display: boot clear, sequential/full/area/from-white/error recovery all return with panel power off PASS");
}'''
with tempfile.TemporaryDirectory(prefix='metalio-display-') as t:
 p=Path(t);(p/'test.c').write_text(h+s+'\nvoid boot_clear(void){EpdiyHighlevelState hl={0};\n'+b+'\n}\n'+m)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
