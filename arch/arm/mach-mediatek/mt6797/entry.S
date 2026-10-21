/* SPDX-License-Identifier: GPL-2.0+ */
#include <linux/linkage.h>
	.section .data
	.balign 8
	.global mt6797_lk_entry_state
mt6797_lk_entry_state:
	.quad 0, 0, 0, 0, 0, 0
	.text

/* Called before any stack or C runtime; retain all four LK arguments. */
ENTRY(save_boot_params)
	adrp x9, mt6797_lk_entry_state
	add x9, x9, :lo12:mt6797_lk_entry_state
	stp x0, x1, [x9]
	stp x2, x3, [x9, #16]
	mrs x10, CurrentEL
	str x10, [x9, #32]
	cmp x10, #0xc
	b.eq 3f
	cmp x10, #0x8
	b.eq 2f
	mrs x11, sctlr_el1
	b 4f
2:	mrs x11, sctlr_el2
	b 4f
3:	mrs x11, sctlr_el3
4:	str x11, [x9, #40]
	/* The stock Linux entry contract has MMU disabled. */
	tbnz x11, #0, 9f
	/* Vendor persistent RAM console: signature/start/size + byte data. */
	movz x12, #0xf000
	movk x12, #0x444b, lsl #16
	movz w13, #0x4244
	movk w13, #0x4347, lsl #16
	str w13, [x12]
	mov w13, #17
	str w13, [x12, #4]
	str w13, [x12, #8]
	adr x14, .Lentry_message
	add x15, x12, #12
5:	ldrb w16, [x14], #1
	strb w16, [x15], #1
	subs w13, w13, #1
	b.ne 5b
	dc cvac, x12
	dsb sy
	/* Disable AP RGU WDT immediately, before driver model and relocation. */
	movz x12, #0x7000
	movk x12, #0x1000, lsl #16
	ldr w13, [x12]
	bic w13, w13, #1
	mov w14, #0x22000000
	orr w13, w13, w14
	str w13, [x12]
	dsb sy
9:	b save_boot_params_ret
.Lentry_message:
	.ascii "X620_UBOOT_ENTRY\n"
ENDPROC(save_boot_params)
