// SPDX-License-Identifier: GPL-2.0+
#include <dm.h>
#include <video.h>
#include <asm/io.h>
#include "x620.h"

/* Minimal refresh of the retained LK OVL0 -> DSI0 command-mode path. */
int x620_display_sync(void)
{
	static bool busy_reported;

	/*
	 * udelay() runs schedule(), whose video_idle callback re-enters here.
	 * Polling a stuck DSI inside this callback multiplies the timeout and
	 * can stall console output before the USB command is reached.
	 * Keep pending framebuffer changes and retry on the next video sync.
	 */
	if (readl((void *)0x1401c00c) & BIT(31)) {
		if (!busy_reported) {
			mt6797_relay_log("X620_VIDEO_BUSY_SKIP\n");
			busy_reported = true;
		}
		return 0;
	}
	if (busy_reported) {
		mt6797_relay_log("X620_VIDEO_BUSY_CLEARED\n");
		busy_reported = false;
	}
	dsb();
	writel(1, (void *)0x1401f020); /* LK owns display mutex 0. */
	clrbits_le32((void *)0x1401c000, BIT(0));
	setbits_le32((void *)0x1401c000, BIT(0));
	return 0;
}

static int x620_video_probe(struct udevice *dev)
{
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);
	struct video_priv *priv = dev_get_uclass_priv(dev);
	u32 layers;
	int ret;

	ret = x620_get_framebuffer(&plat->base, &plat->size);
	if (ret)
		return ret;
	/* Check inherited state before touching the display path. */
	layers = readl((void *)0x1400b02c);
	if (!(readl((void *)0x1400b00c) & BIT(0)) || !(layers & BIT(0)) ||
	    readl((void *)0x1400bf40) != plat->base ||
	    (readl((void *)0x1400b044) & 0xffff) != 4352 ||
	    (readl((void *)0x1401c014) & 3)) {
		mt6797_relay_log("X620_VIDEO_UNEXPECTED_LK_STATE\n");
		return -ENODEV;
	}
	/* Remove the stock opaque logo layer above layer 0. */
	writel(BIT(0), (void *)0x1400b02c);
	/*
	 * U-Boot's XRGB8888 text colors have a zero upper byte. LK leaves
	 * ARGB blending enabled, making those glyphs transparent. Treat this
	 * layer as opaque while preserving the retained pixel format/pitch.
	 */
	clrbits_le32((void *)0x1400b030, BIT(8));
	priv->xsize = 1080;
	priv->ysize = 1920;
	priv->line_length = 4352;
	priv->bpix = VIDEO_BPP32;
	priv->format = VIDEO_X8R8G8B8;
	priv->white_on_black = true;
	/* The framebuffer has a dedicated noncacheable ARM64 mapping. */
	video_set_flush_dcache(dev, false);
	mt6797_relay_log("X620_VIDEO_READY\n");
	return 0;
}

static int x620_video_sync(struct udevice *dev)
{
	return x620_display_sync();
}

static const struct video_ops x620_video_ops = {
	.video_sync = x620_video_sync,
};
static const struct udevice_id x620_video_ids[] = {
	{ .compatible = "leeco,x620-lk-video" },
	{ }
};
U_BOOT_DRIVER(x620_lk_video) = {
	.name = "x620_lk_video",
	.id = UCLASS_VIDEO,
	.of_match = x620_video_ids,
	.probe = x620_video_probe,
	.ops = &x620_video_ops,
};
