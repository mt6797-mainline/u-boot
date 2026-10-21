// SPDX-License-Identifier: GPL-2.0+
/* Independent relay and Android kernel slots inside the physical boot GPT entry. */
#include <blk.h>
#include <cpu_func.h>
#include <clk.h>
#include <clk-uclass.h>
#include <command.h>
#include <dm.h>
#include <fastboot.h>
#include <fb_mmc.h>
#include <malloc.h>
#include <memalign.h>
#include <mmc.h>
#include <part.h>
#include <time.h>
#include <asm/io.h>
#include <asm/cache.h>
#include <asm/unaligned.h>
#include <linux/libfdt.h>
#include <linux/iopoll.h>
#include <linux/sizes.h>
#include "x620.h"

#define RELAY_BYTES	SZ_1M
#define BOOT_BYTES	SZ_16M
#define PAYLOAD_ADDR	0x48000000UL
#define IO_BYTES	SZ_256K
#define MSDC_HCLK_UPDATE	BIT(12)
#define MSDC_SOURCE_UPDATE BIT(13)

static int mt6797_msdc_clock_update(u32 mask)
{
	u32 value;

	writel(mask, (void *)0x10000004);
	return readl_poll_timeout((void *)0x10000004, value, !(value & mask), 20000);
}

static int mt6797_msdc_clock_probe(struct udevice *dev)
{
	void *top = (void *)0x10000000;

	/* Factory TOP muxes require UPDATE even when selector readback has changed. */
	writel(BIT(23), top + 0x74);
	if (mt6797_msdc_clock_update(MSDC_SOURCE_UPDATE))
		return -ETIMEDOUT;
	writel(GENMASK(9, 8) | GENMASK(19, 16), top + 0x78);
	return mt6797_msdc_clock_update(MSDC_HCLK_UPDATE | MSDC_SOURCE_UPDATE);
}

static int mt6797_msdc_clock_enable(struct clk *clk)
{
	if (clk->id > 1)
		return -EINVAL;
	if (clk->id == 0) {
		writel(BIT(23), (void *)0x10000078);
		return mt6797_msdc_clock_update(MSDC_SOURCE_UPDATE);
	} else {
		writel(BIT(2), (void *)0x1000108c);
	}
	return 0;
}

static ulong mt6797_msdc_clock_rate(struct clk *clk)
{
	return 26000000;
}

static const struct clk_ops mt6797_msdc_clock_ops = {
	.enable = mt6797_msdc_clock_enable,
	.get_rate = mt6797_msdc_clock_rate,
};

static const struct udevice_id mt6797_msdc_clock_ids[] = {
	{ .compatible = "mediatek,mt6797-msdc-retained-clocks" },
	{ }
};

U_BOOT_DRIVER(mt6797_msdc_clocks) = {
	.name = "mt6797_msdc_clocks",
	.id = UCLASS_CLK,
	.of_match = mt6797_msdc_clock_ids,
	.probe = mt6797_msdc_clock_probe,
	.ops = &mt6797_msdc_clock_ops,
};

int board_fastboot_mmc_part(const char *name, struct blk_desc **desc,
			    struct disk_partition *part)
{
	int ret;

	/* Never expose physical boot, GPT, or firmware through alternate names. */
	if (strcmp(name, "boot") && strcmp(name, "uboot"))
		return -ENOENT;
	*desc = blk_get_dev("mmc", 0);
	if (!*desc)
		return -ENODEV;
	ret = part_get_info_by_name(*desc, "boot", part);
	if (ret < 0)
		return ret;
	if (part->blksz != 512 || part->size != BOOT_BYTES / 512)
		return -EINVAL;
	if (!strcmp(name, "boot")) {
		part->start += RELAY_BYTES / 512;
		part->size -= RELAY_BYTES / 512;
	} else {
		part->size = RELAY_BYTES / 512;
	}
	strlcpy((char *)part->name, name, sizeof(part->name));
	return ret;
}

/* Parse only the v0 relay and v2 Linux layouts that this port actually boots. */
static int mt6797_image_size(const void *data, u32 available, bool relay, u32 *size)
{
	const u8 *p = data;
	u32 ks, rs, ds = 0, version;
	u64 length;

	if (available < 2048 || memcmp(p, "ANDROID!", 8) ||
	    get_unaligned_le32(p + 36) != 2048 || get_unaligned_le32(p + 24))
		return -EINVAL;
	version = get_unaligned_le32(p + 40);
	ks = get_unaligned_le32(p + 8);
	rs = get_unaligned_le32(p + 16);
	if (!ks || !rs)
		return -EINVAL;
	if (relay) {
		if (version || get_unaligned_le32(p + 12) != 0x40080000 ||
		    get_unaligned_le32(p + 20) != 0x45000000 ||
		    get_unaligned_le32(p + 32) != 0x44000000 ||
		    memcmp(p + 48, "MT6797-RELAY\0", 13))
			return -EINVAL;
	} else {
		if (version != 2 || get_unaligned_le32(p + 12) != 0x40200000 ||
		    get_unaligned_le32(p + 20) != 0x42800000 ||
		    get_unaligned_le64(p + 1652) != 0x47000000 ||
		    get_unaligned_le32(p + 1644) != 1660 ||
		    get_unaligned_le32(p + 1632))
			return -EINVAL;
		ds = get_unaligned_le32(p + 1648);
		if (ds < sizeof(struct fdt_header) || ds > SZ_1M)
			return -EINVAL;
	}
	length = 2048 + ALIGN((u64)ks, 2048) + ALIGN((u64)rs, 2048) +
		 ALIGN((u64)ds, 2048);
	if (length > available)
		return -EFBIG;
	*size = length;
	return 0;
}

int board_fastboot_mmc_write(const char *name, void *buffer, u32 bytes, char *response)
{
	struct disk_partition part;
	struct blk_desc *desc;
	u8 *check, *p = buffer;
	u32 length, offset, chunk;
	bool relay = !strcmp(name, "uboot");
	int ret;

	ret = board_fastboot_mmc_part(name, &desc, &part);
	if (ret < 0) {
		fastboot_fail("only logical boot and uboot are supported", response);
		return 0;
	}
	if (bytes > part.size * 512 ||
	    mt6797_image_size(buffer, bytes, relay, &length) || length != bytes) {
		fastboot_fail("invalid image or logical partition too small", response);
		return 0;
	}
	check = memalign(ARCH_DMA_MINALIGN, IO_BYTES);
	if (!check) {
		fastboot_fail("cannot allocate readback buffer", response);
		return 0;
	}
	/* Invalidate the header, write/verify the body, then commit the header last. */
	memset(check, 0, 512);
	if (blk_dwrite(desc, part.start, 1, check) != 1)
		goto io_error;
	for (offset = 512; offset < bytes; offset += chunk) {
		chunk = min_t(u32, bytes - offset, IO_BYTES);
		if (blk_dwrite(desc, part.start + offset / 512, chunk / 512,
			       p + offset) != chunk / 512 ||
		    blk_dread(desc, part.start + offset / 512, chunk / 512,
			      check) != chunk / 512 || memcmp(check, p + offset, chunk))
			goto io_error;
		schedule();
	}
	if (blk_dwrite(desc, part.start, 1, p) != 1 ||
	    blk_dread(desc, part.start, 1, check) != 1 || memcmp(check, p, 512))
		goto io_error;
	free(check);
	printf("MT6797: flashed %s, %u bytes, readback verified\n", name, bytes);
	fastboot_okay(NULL, response);
	return 0;
io_error:
	free(check);
	fastboot_fail("eMMC write/readback failed; reflash before restarting", response);
	return 0;
}

int board_fastboot_mmc_erase(const char *name, char *response)
{
	ALLOC_CACHE_ALIGN_BUFFER(u8, zero, 512);
	struct disk_partition part;
	struct blk_desc *desc;

	/* Erase-group rounding must never cross back into the relay prefix. */
	if (strcmp(name, "boot") || board_fastboot_mmc_part(name, &desc, &part) < 0) {
		fastboot_fail("only logical boot erase is supported", response);
		return 0;
	}
	memset(zero, 0, 512);
	if (blk_dwrite(desc, part.start, 1, zero) != 1)
		fastboot_fail("boot header invalidation failed", response);
	else
		fastboot_okay(NULL, response);
	return 0;
}

static int do_mt6797boot(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	struct disk_partition part;
	struct blk_desc *desc;
	void *buffer = (void *)PAYLOAD_ADDR;
	u32 bytes;
	int ret;

	ret = board_fastboot_mmc_part("boot", &desc, &part);
	if (ret < 0)
		goto fail;
	if (blk_dread(desc, part.start, 4, buffer) != 4 ||
	    mt6797_image_size(buffer, part.size * 512, false, &bytes))
		goto fail;
	printf("MT6797: loading persistent boot (%u bytes)\n", bytes);
	mt6797_relay_log("MT6797_PERSISTENT_BOOT_READ\n");
	if (blk_dread(desc, part.start, bytes / 512, buffer) != bytes / 512)
		goto fail;
	ret = run_command("bootm 0x48000000", 0);
	printf("MT6797: persistent Linux returned (%d)\n", ret);
fail:
	puts("MT6797: persistent boot unavailable; entering USB fastboot\n");
	return CMD_RET_FAILURE;
}

U_BOOT_CMD(mt6797boot, 1, 0, do_mt6797boot,
	   "boot the Android kernel slot behind the U-Boot relay", "");
