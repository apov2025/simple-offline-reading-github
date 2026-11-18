#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""运行真实USB选择函数，检查输出使能和保持释放时的低脉冲。/ Execute the real USB selector and detect unintended low pulses."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'boards/metalio/upstream/display/epd_board_metalio_eink4_plus.c').read_text()
start=source.index('void epd_board_metalio_eink4_plus_ana_sw_set(')
end=source.index('\nesp_err_t ',start)
function=source[start:end]
harness=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#define ANA_SW_GPIO 36
#define GPIO_MODE_OUTPUT 1
static int latch, enabled, held, held_level, pad=1, falls;
static void update(void){int next=held?held_level:enabled?latch:1;if(pad && !next)falls++;pad=next;}
int gpio_reset_pin(int pin){assert(pin==36);latch=0;enabled=0;update();return 0;}
int gpio_set_level(int pin,int value){assert(pin==36);latch=value;update();return 0;}
int gpio_set_direction(int pin,int mode){assert(pin==36 && mode==1);enabled=1;update();return 0;}
int gpio_hold_en(int pin){assert(pin==36);held_level=pad;held=1;return 0;}
int gpio_hold_dis(int pin){assert(pin==36);held=0;update();return 0;}
'''
main=r'''
int main(void){
 epd_board_metalio_eink4_plus_ana_sw_set(true);
 assert(pad==1 && falls==0);
 /* 主机复位清配置，保持的引脚不能短暂切到另一USB通路。/ A host reset clears configuration but must not drop a retained USB route. */
 latch=0;enabled=0;update();
 epd_board_metalio_eink4_plus_ana_sw_set(true);
 assert(pad==1 && falls==0);
 epd_board_metalio_eink4_plus_ana_sw_set(false);
 assert(pad==0 && falls==1);
 epd_board_metalio_eink4_plus_ana_sw_set(true);
 assert(pad==1 && falls==1);
 puts("Metalio USB route: no unintended low pulses at init/reset; intentional switching PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='metalio-usb-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(harness+function+main)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
