/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __CONFIG_MT6797_X620_H
#define __CONFIG_MT6797_X620_H
#define CFG_SYS_SDRAM_BASE 0x48000000
#define CFG_EXTRA_ENV_SETTINGS \
	"stdin=serial\0" \
	"stdout=serial,vidconsole\0" \
	"stderr=serial,vidconsole\0" \
	"kernel_addr_r=0x40200000\0" \
	"fdt_addr_r=0x47000000\0" \
	"ramdisk_addr_r=0x45000000\0"
#endif
