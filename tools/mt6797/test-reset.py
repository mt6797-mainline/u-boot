#!/usr/bin/env python3
from pathlib import Path
import subprocess
import os
workspace=Path(os.environ.get('MT6797_WORKSPACE') or os.environ.get('X620_WORKSPACE', Path(__file__).resolve().parents[3]))
p=workspace/'out/mt6797-persistent'
p.mkdir(parents=True,exist_ok=True)
root=p.parents[1]
s=(root/'u-boot/arch/arm/mach-mediatek/mt6797/init.c').read_text(); reset=s[s.index('void reset_cpu(void)'):s.index('int print_cpuinfo')]
b=(root/'u-boot/board/mediatek/x620/boot.c').read_text();boot=b[b.index('static bool boot_requested;'):]
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
#define BIT(n) (1U<<(n))
#define dsb() do {} while(0)
#define wfi() do {} while(0)
static jmp_buf done;
static u32 old_mode, written_mode;
static int sequence, delay_us, commands, detach;
static char command[64];
static u32 readl(void *addr) { assert((uintptr_t)addr==0x10007000&&sequence==1); return old_mode; }
static void writel(u32 value,void *addr) {
 if(sequence==0) {assert((uintptr_t)addr==0x10007008&&value==0x1971);sequence=1;}
 else if(sequence==1) {assert((uintptr_t)addr==0x10007000);written_mode=value;sequence=2;}
 else {assert(sequence==2&&(uintptr_t)addr==0x10007014&&value==0x1209&&delay_us==100);sequence=3;longjmp(done,1);}
}
static void udelay(int n) {assert(sequence==2);delay_us=n;}
static void mt6797_relay_log(const char *s) {(void)s;}
static void g_dnl_trigger_detach(void) {detach=1;}
static const char *env_get(const char *s) {(void)s;return NULL;}
static int run_command(const char *s,int flags) {(void)flags;commands++;snprintf(command,sizeof(command),"%s",s);return 0;}
'''
post=r'''
int main(void) {
 for(volatile int i=0;i<256;i++) {
  old_mode=i;sequence=0;delay_us=0;
  if(!setjmp(done))reset_cpu();
  assert(sequence==3&&written_mode==((old_mode&~(BIT(0)|BIT(3)|BIT(4)|BIT(6)))|0x22000000|BIT(2)|BIT(4)));
 }
 mt6797_fastboot_prepare();assert(!mt6797_fastboot_run());
 fastboot_reboot();assert(detach&&commands==0);assert(mt6797_fastboot_run());assert(commands==1&&!strcmp(command,"reset"));
 mt6797_fastboot_prepare();fastboot_boot();assert(commands==1);assert(mt6797_fastboot_run());assert(commands==2&&!strcmp(command,"bootm 0x48000000"));
 puts("TOPRGU_FACTORY_SEQUENCE_AND_DEFERRED_RESET_OK");return 0;
}
'''
c=p/'test-reset.c';c.write_text(pre+reset+boot+post)
subprocess.run(['cc','-O2','-Wall','-Wextra',str(c),'-o',str(p/'test-reset')],check=True)
subprocess.run([str(p/'test-reset')],check=True)
nm=subprocess.check_output(['aarch64-linux-gnu-nm',str(root/'out/u-boot/u-boot')],text=True)
assert any(' W reset_misc' in l for l in nm.splitlines()),'PSCI reset_misc must not intercept board reset'
assert '# CONFIG_PSCI_RESET is not set' in (root/'out/u-boot/.config').read_text()
print('PSCI_RESET_DISABLED_AND_WEAK_RESET_MISC_VERIFIED')
