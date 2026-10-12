// SPDX-License-Identifier: GPL-2.0+
#include <command.h>
#include <console.h>
#include <cpu_func.h>
#include <env.h>
#include <event.h>
#include <init.h>
#include <lmb.h>
#include <time.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/libfdt.h>
#include <asm/unaligned.h>
#include <asm/cache.h>
#include <asm/arch/x620.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include "x620.h"

DECLARE_GLOBAL_DATA_PTR;
#define LOG_BASE 0x444bf000UL
#define LOG_LIMIT (4096 - 12)

void x620_log(const char *message)
{
	u32 n = readl((void *)(LOG_BASE + 8));
	volatile u8 *data = (void *)(LOG_BASE + 12);

	if (readl((void *)LOG_BASE) != 0x43474244 || n > LOG_LIMIT)
		return;
	while (*message && n < LOG_LIMIT)
		data[n++] = *message++;
	writel(n, (void *)(LOG_BASE + 4));
	writel(n, (void *)(LOG_BASE + 8));
	flush_dcache_range(LOG_BASE, LOG_BASE + 4096);
}

void x620_record_exception(const char *kind, struct pt_regs *regs)
{
	static bool recorded;
	ulong far = 0, el = current_el();
	char line[240];

	/* Preserve the first exception even if printing its report faults again. */
	if (recorded)
		return;
	recorded = true;
	x620_log("X620_EXCEPTION_BEGIN\n");
	if (el == 1)
		asm volatile("mrs %0, far_el1" : "=r" (far));
	else if (el == 2)
		asm volatile("mrs %0, far_el2" : "=r" (far));
	else if (el == 3)
		asm volatile("mrs %0, far_el3" : "=r" (far));
	snprintf(line, sizeof(line),
		 "X620_EXCEPTION %s EL=%lu ESR=%lx FAR=%lx ELR=%lx PC=%lx LR=%lx SPSR=%lx\n",
		 kind, el, regs->esr, far, regs->elr,
		 regs->elr - (gd->flags & GD_FLG_RELOC ? gd->reloc_off : 0),
		 regs->regs[30], regs->spsr);
	x620_log(line);
	snprintf(line, sizeof(line), "X620_FAULT_ARGS X0=%lx X1=%lx X2=%lx X3=%lx\n",
		 regs->regs[0], regs->regs[1], regs->regs[2], regs->regs[3]);
	x620_log(line);
}

static const void *lk_fdt(void)
{
	ulong addr = x620_entry_state[0];
	const void *fdt = (void *)addr;

	if (addr < 0x40000000 || addr > 0x45e00000 || (addr & 7))
		return NULL;
	if (fdt_check_header(fdt) || fdt_totalsize(fdt) > 0x200000)
		return NULL;
	return fdt;
}

int x620_get_framebuffer(ulong *base, u32 *size)
{
	const void *fdt = lk_fdt();
	const u8 *tag;
	u64 addr;
	u32 bytes;
	int chosen, len;

	if (!fdt)
		return -ENODEV;
	chosen = fdt_path_offset(fdt, "/chosen");
	if (chosen < 0)
		return -ENODEV;
	tag = fdt_getprop(fdt, chosen, "atag,videolfb", &len);
	/* Packed MediaTek tag: le64 base; le32 found/fps/size; panel string. */
	if (!tag || len < 27 || !get_unaligned_le32(tag + 8) ||
	    memcmp(tag + 20, "378C81\0", 7))
		return -ENODEV;
	addr = get_unaligned_le64(tag);
	bytes = get_unaligned_le32(tag + 16);
	if ((addr & 4095) || addr < 0xbd000000 || bytes < 4352 * 1920 ||
	    bytes > 0x02000000 || addr + bytes > 0xbf000000)
		return -EINVAL;
	*base = addr;
	*size = bytes;
	return 0;
}

int board_early_init_f(void)
{
	x620_log("X620_UBOOT_EARLY_C\n");
	return 0;
}

int board_init(void)
{
	gd->bd->bi_boot_params = 0x47000000;
	x620_log("X620_UBOOT_RELOCATED\n");
	return 0;
}

int board_late_init(void)
{
	static const struct {
		ulong base;
		ulong size;
	} protected[] = {
		{ 0x40000000, 0x1000 },
		{ 0x44000000, 0x400000 }, /* LK hardware DT workspace */
		{ 0x44400000, 0x100000 }, /* RAM console, pstore, mini dump */
		{ 0x44600000, 0x70000 }, /* ATF */
		{ 0x44800000, 0x100000 }, /* preloader data */
		{ 0x46000000, 0x400000 }, /* resident LK */
		{ 0x48000000, 0x4000000 }, /* fastboot download buffer */
	};
	int i, ret;

	/* OS payload placement needs low DDR although malloc/relocation does not. */
	ret = lmb_add(0x40000000, 0x08000000);
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(protected); i++) {
		phys_addr_t addr = protected[i].base;

		ret = lmb_alloc_mem(LMB_MEM_ALLOC_ADDR, 0, &addr,
				    protected[i].size, LMB_NOOVERWRITE);
		if (ret < 0)
			return ret;
	}
	env_set_hex("lk_fdt_addr", x620_entry_state[0]);
	x620_log("X620_UBOOT_READY\n");
	return 0;
}

static int x620_last_stage(void)
{
	x620_log("X620_LAST_STAGE_INIT\n");
	return 0;
}
EVENT_SPY_SIMPLE(EVT_LAST_STAGE_INIT, x620_last_stage);

static int x620_main_loop(void)
{
	u64 frequency, counter;
	char state[192];

	x620_log("X620_MAIN_LOOP\n");
	asm volatile("mrs %0, cntfrq_el0" : "=r" (frequency));
	asm volatile("mrs %0, cntpct_el0" : "=r" (counter));
	snprintf(state, sizeof(state),
		 "X620_CONTEXT EL=%llu SCTLR=%llx FDT=%llx CNTFRQ=%llu CNTPCT=%llx UART_LSR=%x\n",
		 x620_entry_state[4] >> 2, x620_entry_state[5], x620_entry_state[0],
		 frequency, counter, readl((void *)0x11002014));
	x620_log(state);
	snprintf(state, sizeof(state),
		 "X620_DISPLAY CON=%x PITCH=%x LAYERS=%x DSI_STATUS=%x DSI_START=%x MUTEX=%x\n",
		 readl((void *)0x1400b030), readl((void *)0x1400b044),
		 readl((void *)0x1400b02c), readl((void *)0x1401c00c),
		 readl((void *)0x1401c000), readl((void *)0x1401f020));
	x620_log(state);
	return 0;
}
EVENT_SPY_SIMPLE(EVT_MAIN_LOOP, x620_main_loop);

static int x620_post_preboot(void)
{
	x620_log("X620_POST_PREBOOT\n");
	return 0;
}
EVENT_SPY_SIMPLE(EVT_POST_PREBOOT, x620_post_preboot);

static int do_x620diag(struct cmd_tbl *cmdtp, int flag, int argc,
		       char *const argv[])
{
	ulong fb;
	u32 size;
	int ret = x620_get_framebuffer(&fb, &size);
	char marker[192];

	x620_log("X620_DIAG_BEGIN\n");
	printf("X620 U-Boot relay: EL%llu entry-SCTLR=%llx\n",
	       x620_entry_state[4] >> 2, x620_entry_state[5]);
	x620_log("X620_DIAG_HEADER_PRINTED\n");
	printf("LK args: %llx %llx %llx %llx; DTB %s\n",
	       x620_entry_state[0], x620_entry_state[1], x620_entry_state[2],
	       x620_entry_state[3], lk_fdt() ? "valid" : "invalid");
	printf("Relocated to %lx; RAM allocator 48000000..50000000\n",
	       gd->relocaddr);
	printf("AP WDT mode=%08x status=%08x\n",
	       readl((void *)0x10007000), readl((void *)0x1000700c));
	if (!ret) {
		printf("LK framebuffer=%lx size=%x; panel=378C81\n", fb, size);
		printf("OVL en=%x layers=%x addr=%x pitch=%x\n",
		       readl((void *)0x1400b00c), readl((void *)0x1400b02c),
		       readl((void *)0x1400bf40), readl((void *)0x1400b044));
		printf("DSI mode=%x status=%x mutex0=%x\n",
		       readl((void *)0x1401c014), readl((void *)0x1401c00c),
		       readl((void *)0x1401f020));
	} else {
		printf("LK framebuffer unavailable (%d)\n", ret);
	}
	snprintf(marker, sizeof(marker),
		 "X620_DIAG EL=%llu SCTLR=%llx FDT=%llx RELOC=%lx WDT=%x\n",
		 x620_entry_state[4] >> 2, x620_entry_state[5], x620_entry_state[0],
		 gd->relocaddr, readl((void *)0x10007000));
	x620_log(marker);
	x620_log("X620_DIAG_DONE\n");
	return 0;
}

static int do_x620stay(struct cmd_tbl *cmdtp, int flag, int argc,
		       char *const argv[])
{
	ulong started = get_timer(0), last = 0;
	char marker[64];

	puts("X620_UBOOT_READY: watching uptime; Ctrl-C for shell\n");
	while (!ctrlc()) {
		ulong seconds = get_timer(started) / 1000;

		if (seconds >= last + 5) {
			last = seconds;
			snprintf(marker, sizeof(marker), "X620_ALIVE %lu seconds\n", seconds);
			puts(marker);
			x620_log(marker);
		}
		mdelay(50);
	}
	return 0;
}

U_BOOT_CMD(x620diag, 1, 0, do_x620diag, "show stock LK handoff and relay state", "");
U_BOOT_CMD(x620stay, 1, 0, do_x620stay, "stay in relay and display uptime", "");
