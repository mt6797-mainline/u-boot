// SPDX-License-Identifier: GPL-2.0+
/* X620 factory wiring: keypad HW key 0 = VOL+, MT6351 HOMEKEY = VOL-. */
#include <stdio.h>
#include <time.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <u-boot/schedule.h>
#include "x620.h"

#define KP_MEM1		((void *)0x10010004)
#define KP_DEBOUNCE	((void *)0x10010018)
#define KP_EN		((void *)0x10010024)
#define WACS2_CMD	((void *)0x1000d0a0)
#define WACS2_RDATA	((void *)0x1000d0a4)
#define WACS2_VLDCLR	((void *)0x1000d0a8)
#define WACS_FSM(v)	(((v) >> 16) & 7)

static unsigned int mt6797_volume_keys(void)
{
	u32 value;
	u16 matrix = readw(KP_MEM1);
	unsigned int keys = matrix && !(matrix & BIT(0)) ? BIT(0) : 0;
	int ret;

	/* Read HOMEKEY_DEB (TOPSTATUS 0x220 bit 2, active low) over LK's WACS2. */
	value = readl(WACS2_RDATA);
	if (!(value & BIT(21)))
		return keys;
	/* Retire a late read completion instead of leaving subsequent polls stuck. */
	if (WACS_FSM(value) == 6)
		writel(1, WACS2_VLDCLR);
	ret = readl_poll_timeout(WACS2_RDATA, value, WACS_FSM(value) == 0, 2000);
	if (ret)
		return keys;
	writel((0x220 >> 1) << 16, WACS2_CMD);
	ret = readl_poll_timeout(WACS2_RDATA, value, WACS_FSM(value) == 6, 2000);
	if (ret)
		return keys;
	writel(1, WACS2_VLDCLR);
	if (!(value & BIT(2)))
		keys |= BIT(1);
	return keys;
}

bool mt6797_wait_boot_key(void)
{
	ulong started, now, pressed_at = 0;
	unsigned int keys, previous = 0, shown = 0;

	/* Preserve LK's pinmux, pulls and PMIC setup. Only enable the stock scanner. */
	writew(0x400, KP_DEBOUNCE);
	writew(1, KP_EN);
	mdelay(20);
	started = get_timer(0);
	puts("Press VOL+ or VOL- to enter U-Boot fastboot\n");
	do {
		now = get_timer(started);
		if (shown != 5 - now / 1000) {
			shown = 5 - now / 1000;
			printf("Booting Linux in %u seconds...\n", shown);
		}
		keys = mt6797_volume_keys();
		if (keys && keys == previous) {
			if (now - pressed_at >= 30) {
				puts("Volume key: staying in U-Boot fastboot\n");
				x620_log("MT6797_VOLUME_KEY_FASTBOOT\n");
				return true;
			}
		} else {
			previous = keys;
			pressed_at = now;
		}
		schedule();
		mdelay(10);
	} while (get_timer(started) < 5000);
	return false;
}
