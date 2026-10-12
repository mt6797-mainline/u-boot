// SPDX-License-Identifier: GPL-2.0
#include <command.h>
#include <dm.h>
#include <g_dnl.h>
#include <fastboot.h>
#include <generic-phy.h>
#include <linux/delay.h>
#include <linux/usb/gadget.h>
#include <asm/io.h>
#include "x620.h"

/* USB2 recovery sequence from the MT6797 reference PHY driver:
 * drivers/misc/mediatek/mu3phy/mt6797/mtk-phy-asic.c, usb_phy_recover().
 * PMIC rails and PLL calibration remain as initialized by stock LK.
 * SuperSpeed is disabled while proving device-mode USB2.
 */
static int x620_usb_phy_on(struct phy *phy)
{
	void *base = (void *)dev_read_addr(phy->dev);
	void *u2 = base + 0x800;

	x620_log("X620_USB_PHY_START\n");
	/* INFRA2 SSUSB bus/sys/ref gates, reference clk-mt6797.c. */
	writel(BIT(18) | BIT(24) | BIT(9), (void *)0x100010ac);
	udelay(50);
	clrsetbits_le32(u2 + 0x18, BIT(31) | BIT(23) | 0xf, BIT(20) | 2);
	clrbits_le32(u2 + 0x20, BIT(9) | BIT(8));
	/* Release forced UART, suspend, pull-down and transceiver controls. */
	clrbits_le32(u2 + 0x68, BIT(26) | BIT(23) | BIT(21) | BIT(20) |
		     BIT(19) | BIT(18) | BIT(17) | GENMASK(13, 10) |
		     BIT(7) | BIT(6) | GENMASK(5, 4) | BIT(2));
	clrbits_le32(u2 + 0x6c, BIT(16));
	setbits_le32(u2 + 0x14, BIT(11));
	clrsetbits_le32(u2 + 0x70, GENMASK(19, 16), 9 << 16);
	udelay(800);
	clrsetbits_le32(u2 + 0x6c, BIT(4), BIT(5) | BIT(2));
	x620_log("X620_USB_PHY_READY\n");
	return 0;
}

static const struct phy_ops x620_usb_phy_ops = {
	.power_on = x620_usb_phy_on,
};
static const struct udevice_id x620_usb_phy_ids[] = {
	{ .compatible = "leeco,x620-usb2-phy" },
	{ }
};
U_BOOT_DRIVER(x620_usb2_phy) = {
	.name = "x620_usb2_phy",
	.id = UCLASS_PHY,
	.of_match = x620_usb_phy_ids,
	.ops = &x620_usb_phy_ops,
};

static int do_x620usb(struct cmd_tbl *cmdtp, int flag, int argc,
		      char *const argv[])
{
	struct udevice *udc;
	ulong last = 0, started;
	char line[80];
	int ret;

	x620_log("X620_USB_PROBE\n");
	ret = udc_device_get_by_index(0, &udc);
	if (ret) {
		printf("X620 USB controller probe failed: %d\n", ret);
		x620_log("X620_USB_PROBE_FAILED\n");
		return CMD_RET_FAILURE;
	}
	g_dnl_clear_detach();
	fastboot_init(NULL, 0);
	g_dnl_set_serialnumber("X620-UBOOT-RELAY");
	ret = g_dnl_register("usb_dnl_fastboot");
	if (ret) {
		udc_device_put(udc);
		return CMD_RET_FAILURE;
	}
	x620_log("X620_USB_FASTBOOT_REGISTERED\n");
	puts("X620 U-Boot: USB fastboot download/boot; flash commands disabled\n");
	started = get_timer(0);
	while (!g_dnl_detach() && !ctrlc()) {
		ulong seconds = get_timer(started) / 1000;

		dm_usb_gadget_handle_interrupts(udc);
		if (seconds >= last + 5) {
			last = seconds;
			snprintf(line, sizeof(line), "X620_USB_ALIVE %lu seconds\n", seconds);
			puts(line);
			x620_log(line);
		}
		schedule();
	}
	g_dnl_unregister();
	udc_device_put(udc);
	g_dnl_clear_detach();
	return CMD_RET_SUCCESS;
}
U_BOOT_CMD(x620usb, 1, 0, do_x620usb, "run USB fastboot with relay diagnostics", "");
