#!/usr/bin/env python3
from pathlib import Path
import subprocess
import os
workspace=Path(os.environ.get('X620_WORKSPACE', Path(__file__).resolve().parents[3]))
base=workspace/'out/mt6797-persistent'
base.mkdir(parents=True,exist_ok=True)
source=(base.parents[1]/'u-boot/board/mediatek/x620/mt6797-keys.c').read_text()
code=source[source.index('#define KP_MEM1'):]
pre=r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t u32; typedef uint16_t u16; typedef unsigned long ulong;
#define BIT(n) (1U<<(n))
static unsigned clock_ms, mode, fsm;
static bool valid_wrap=true;
static unsigned physical_keys(void) {
 if(mode==1&&clock_ms>=120) return 1;
 if(mode==2) return 2;
 if(mode==3&&clock_ms>=120&&clock_ms<140) return 1;
 return 0;
}
static u16 readw(void *reg) { assert((uintptr_t)reg==0x10010004); return mode==4?0:((physical_keys()&1)?0xfffe:0xffff); }
static u32 readl(void *reg) { assert((uintptr_t)reg==0x1000d0a4); return (valid_wrap?BIT(21):0)|(fsm<<16)|((physical_keys()&2)?0:BIT(2)); }
static void writel(u32 v,void *reg) { if((uintptr_t)reg==0x1000d0a0) {assert(v==((0x220>>1)<<16));fsm=6;} else {assert((uintptr_t)reg==0x1000d0a8&&v==1);fsm=0;} }
static void writew(u16 v,void *reg) { assert(((uintptr_t)reg==0x10010018&&v==0x400)||((uintptr_t)reg==0x10010024&&v==1)); }
static void mdelay(unsigned ms) {clock_ms+=ms;}
static ulong get_timer(ulong start) {return clock_ms-start;}
static void schedule(void) {}
static void x620_log(const char *s) {(void)s;}
#define readl_poll_timeout(a,v,cond,t) ({int result=-ETIMEDOUT; for(int i=0;i<3;i++){v=readl(a);if(cond){result=0;break;}} result;})
'''
post=r'''
int main(void) {
 mode=0; assert(!mt6797_wait_boot_key()&&clock_ms==5020);
 mode=1;clock_ms=0;assert(mt6797_wait_boot_key()&&clock_ms>=150&&clock_ms<200);
 mode=2;clock_ms=0;assert(mt6797_wait_boot_key()&&clock_ms==50);
 mode=3;clock_ms=0;assert(!mt6797_wait_boot_key()&&clock_ms==5020);
 mode=4;clock_ms=0;assert(!mt6797_wait_boot_key()&&clock_ms==5020);
 mode=1;clock_ms=0;valid_wrap=false;assert(mt6797_wait_boot_key());
 puts("VOLUME_KEYS_DEBOUNCE_AND_FIVE_SECOND_TIMEOUT_OK");
 return 0;
}
'''
p=base/'test-keys.c';p.write_text(pre+code+post)
subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror',str(p),'-o',str(base/'test-keys')],check=True)
subprocess.run([str(base/'test-keys')],check=True)
