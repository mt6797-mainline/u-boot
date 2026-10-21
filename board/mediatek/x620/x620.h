/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __MT6797_X620_H
#define __MT6797_X620_H
#include <linux/types.h>
extern u64 mt6797_lk_entry_state[6];
void mt6797_relay_log(const char *message);
int x620_get_framebuffer(ulong *base, u32 *size);
int x620_display_sync(void);
void mt6797_fastboot_prepare(void);
bool mt6797_fastboot_run(void);
bool mt6797_wait_boot_key(void);
#endif
