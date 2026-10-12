// SPDX-License-Identifier: GPL-2.0+
#include <command.h>
#include <env.h>
#include <fastboot.h>
#include <stdio.h>
#include "x620.h"

static bool boot_requested;

void x620_fastboot_prepare(void)
{
	boot_requested = false;
}

/* Called in the gadget completion callback; loading must wait for teardown. */
void fastboot_boot(void)
{
	boot_requested = true;
	x620_log("X620_FASTBOOT_BOOT_QUEUED\n");
}

bool x620_fastboot_run(void)
{
	const char *command;
	int ret;

	if (!boot_requested)
		return false;
	boot_requested = false;
	command = env_get("fastboot_bootcmd");
	x620_log("X620_LINUX_BOOT_BEGIN\n");
	ret = run_command(command ? command : "bootm 0x48000000", 0);
	printf("X620 Linux boot returned (%d); returning to USB fastboot\n", ret);
	x620_log("X620_LINUX_BOOT_RETURNED\n");
	/* Loading failure must not invoke stock LK again through an auto-reset. */
	return true;
}
