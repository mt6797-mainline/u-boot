// SPDX-License-Identifier: GPL-2.0+
#include <cpu_func.h>
#include <init.h>
#include <asm/armv8/mmu.h>
#include <asm/global_data.h>
#include <asm/system.h>

DECLARE_GLOBAL_DATA_PTR;

static struct mm_region x620_mem_map[] = {
	{ .virt = 0, .phys = 0, .size = 0x40000000,
	  .attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
		   PTE_BLOCK_NON_SHARE | PTE_BLOCK_PXN | PTE_BLOCK_UXN },
	{ .virt = 0x40000000, .phys = 0x40000000, .size = 0x20000000,
	  .attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) | PTE_BLOCK_OUTER_SHARE },
	/* Covers the measured LK framebuffer, outside the allocator's RAM. */
	{ .virt = 0xbd000000, .phys = 0xbd000000, .size = 0x02000000,
	  .attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL_NC) |
		   PTE_BLOCK_OUTER_SHARE | PTE_BLOCK_PXN | PTE_BLOCK_UXN },
	{ }
};
struct mm_region *mem_map = x620_mem_map;

int dram_init(void)
{
	/* Conservative allocation window inside the first live LK memory bank. */
	gd->ram_base = 0x48000000;
	gd->ram_size = 0x08000000;
	return 0;
}

int dram_init_banksize(void)
{
	gd->bd->bi_dram[0].start = 0x48000000;
	gd->bd->bi_dram[0].size = 0x08000000;
	return 0;
}

void reset_cpu(void)
{
	psci_system_reset();
}

int print_cpuinfo(void)
{
	puts("CPU:   MediaTek MT6797 (stock-LK handoff)\n");
	return 0;
}
