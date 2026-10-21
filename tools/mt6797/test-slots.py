#!/usr/bin/env python3
from pathlib import Path
import subprocess
import os
workspace=Path(os.environ.get('X620_WORKSPACE', Path(__file__).resolve().parents[3]))
base=workspace/'out/mt6797-persistent'
base.mkdir(parents=True,exist_ok=True)
root=base.parents[1]
s=(root/'u-boot/board/mediatek/x620/mt6797-storage.c').read_text()
functions=s[s.index('int board_fastboot_mmc_part('):s.index('static int do_mt6797boot(')]
pre=r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
typedef uint8_t u8; typedef uint32_t u32; typedef uint64_t u64;
#define SZ_1M (1024*1024U)
#define RELAY_BYTES SZ_1M
#define BOOT_BYTES (16*SZ_1M)
#define IO_BYTES (256*1024U)
#define ALIGN(x,a) (((x)+(a)-1)&~((a)-1))
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define ARCH_DMA_MINALIGN 64
#define ALLOC_CACHE_ALIGN_BUFFER(t,n,s) t n[s] __attribute__((aligned(64)))
struct fdt_header { u32 fields[10]; };
struct blk_desc { int dummy; } device;
struct disk_partition { unsigned long start,size,blksz; char name[32]; };
static u8 disk[BOOT_BYTES+1024];
static int writes, fault_read, wrong_size;
static void schedule(void) {}
static u32 get_unaligned_le32(const void *p) { u32 x; memcpy(&x,p,4); return x; }
static u64 get_unaligned_le64(const void *p) { u64 x; memcpy(&x,p,8); return x; }
static struct blk_desc *blk_get_dev(const char *s,int n) { assert(!strcmp(s,"mmc")&&!n); return &device; }
static int part_get_info_by_name(struct blk_desc *d,const char *s,struct disk_partition *p) {
 assert(d==&device&&!strcmp(s,"boot")); p->start=1; p->size=BOOT_BYTES/512-wrong_size; p->blksz=512; return 31;
}
static unsigned long blk_dwrite(struct blk_desc *d,unsigned long b,unsigned long n,const void *p) {
 assert(d==&device&&b>=1&&(b+n)*512<=sizeof(disk)); memcpy(disk+b*512,p,n*512); writes++; return n;
}
static unsigned long blk_dread(struct blk_desc *d,unsigned long b,unsigned long n,void *p) {
 assert(d==&device&&b>=1&&(b+n)*512<=sizeof(disk)); memcpy(p,disk+b*512,n*512); if(fault_read) ((u8*)p)[0]^=1; return n;
}
static void fastboot_fail(const char *s,char *r) { snprintf(r,128,"FAIL%s",s); }
static void fastboot_okay(const char *s,char *r) { (void)s; strcpy(r,"OKAY"); }
'''
post=r'''
static u8 *load(const char *p,u32 *n) { FILE *f=fopen(p,"rb"); assert(f); fseek(f,0,SEEK_END); *n=ftell(f); rewind(f); u8 *b=malloc(*n); assert(fread(b,1,*n,f)==*n); fclose(f); return b; }
int main(int argc,char **argv) {
 assert(argc==3); u32 n,rn; u8 *b=load(argv[1],&n),*relay=load(argv[2],&rn); char response[128]; u32 size;
 struct disk_partition p; struct blk_desc *d;
 assert(!mt6797_image_size(b,n,false,&size)&&size==n);
 assert(!mt6797_image_size(relay,rn,true,&size)&&size==rn);
 assert(board_fastboot_mmc_part("boot",&d,&p)==31&&p.start==1+RELAY_BYTES/512&&p.size==15*SZ_1M/512);
 assert(board_fastboot_mmc_part("uboot",&d,&p)==31&&p.start==1&&p.size==RELAY_BYTES/512);
 memset(disk,0xa5,sizeof(disk));
 board_fastboot_mmc_write("boot",b,n,response); assert(!strcmp(response,"OKAY"));
 assert(!memcmp(disk+512+RELAY_BYTES,b,n));
 for(unsigned i=512;i<512+RELAY_BYTES;i++) assert(disk[i]==0xa5);
 int count=writes;
 const char *bad[]={"gpt","recovery","userdata","lk","zimage","0:31","0.0:31","uboot-extra"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) { board_fastboot_mmc_write(bad[i],b,n,response); assert(!strncmp(response,"FAIL",4)&&writes==count); }
 board_fastboot_mmc_write("uboot",b,n,response); assert(!strncmp(response,"FAIL",4)&&writes==count);
 board_fastboot_mmc_write("boot",relay,rn,response); assert(!strncmp(response,"FAIL",4)&&writes==count);
 board_fastboot_mmc_write("boot",b,n-512,response); assert(!strncmp(response,"FAIL",4)&&writes==count);
 wrong_size=1; board_fastboot_mmc_write("boot",b,n,response); assert(!strncmp(response,"FAIL",4)&&writes==count); wrong_size=0;
 for(unsigned off=0;off<1660;off++) { u8 original=b[off]; b[off]^=0xff; if(mt6797_image_size(b,n,false,&size)) { board_fastboot_mmc_write("boot",b,n,response); assert(!strncmp(response,"FAIL",4)&&writes==count); } b[off]=original; }
 fault_read=1; board_fastboot_mmc_write("boot",b,n,response); assert(!strncmp(response,"FAIL",4));
 for(unsigned i=0;i<512;i++) assert(disk[512+RELAY_BYTES+i]==0);
 for(unsigned i=512;i<512+RELAY_BYTES;i++) assert(disk[i]==0xa5);
 fault_read=0; board_fastboot_mmc_write("boot",b,n,response); assert(!strcmp(response,"OKAY"));
 board_fastboot_mmc_write("uboot",relay,rn,response); assert(!strcmp(response,"OKAY"));
 assert(!memcmp(disk+512,relay,rn)); assert(!memcmp(disk+512+RELAY_BYTES,b,n));
 board_fastboot_mmc_erase("boot",response); assert(!strcmp(response,"OKAY")); assert(!memcmp(disk+512,relay,rn));
 for(unsigned i=0;i<512;i++) assert(disk[512+RELAY_BYTES+i]==0);
 count=writes; board_fastboot_mmc_erase("uboot",response); assert(!strncmp(response,"FAIL",4)&&writes==count);
 puts("SLOT_BOUNDARIES_AND_READBACK_FAILURE_OK"); free(b); free(relay); return 0;
}
'''
p=base/'test-slots.c';p.write_text(pre+functions+post)
subprocess.run(['cc','-O1','-g','-Wall','-Wextra','-fstack-protector-strong','-D_FORTIFY_SOURCE=3',str(p),'-o',str(base/'test-slots')],check=True)
subprocess.run([str(base/'test-slots'),str(root/'dist/linux-boot.img'),str(root/'dist/boot.img')],check=True)
