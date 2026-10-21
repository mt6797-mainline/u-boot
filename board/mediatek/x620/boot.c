// SPDX-License-Identifier: GPL-2.0+
#include <command.h>
#include <env.h>
#include <fastboot.h>
#include <g_dnl.h>
#include <stdio.h>
#include "x620.h"

static bool boot_requested;
static bool reset_requested;

void mt6797_fastboot_prepare(void)
{
	boot_requested = false;
	reset_requested = false;
}

/* Reboot after leaving the completion callback and shutting down USB DMA. */
void fastboot_reboot(void)
{
	reset_requested = true;
	mt6797_relay_log("MT6797_FASTBOOT_RESET_QUEUED\n");
	g_dnl_trigger_detach();
}

/* Called in the gadget completion callback; loading must wait for teardown. */
void fastboot_boot(void)
{
	boot_requested = true;
	mt6797_relay_log("X620_FASTBOOT_BOOT_QUEUED\n");
}

bool mt6797_fastboot_run(void)
{
	const char *command;
	int ret;

	if (reset_requested) {
		reset_requested = false;
		run_command("reset", 0);
		puts("MT6797: reset returned; staying in USB fastboot\n");
		return true;
	}

	if (!boot_requested)
		return false;
	boot_requested = false;
	command = env_get("fastboot_bootcmd");
	mt6797_relay_log("X620_LINUX_BOOT_BEGIN\n");
	ret = run_command(command ? command : "bootm 0x48000000", 0);
	printf("X620 Linux boot returned (%d); returning to USB fastboot\n", ret);
	mt6797_relay_log("X620_LINUX_BOOT_RETURNED\n");
	/* Loading failure must not invoke stock LK again through an auto-reset. */
	return true;
}
