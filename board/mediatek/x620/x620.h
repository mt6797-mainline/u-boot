/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __X620_LK_H
#define __X620_LK_H
#include <linux/types.h>
extern u64 x620_entry_state[6];
void x620_log(const char *message);
int x620_get_framebuffer(ulong *base, u32 *size);
int x620_display_sync(void);
#endif
