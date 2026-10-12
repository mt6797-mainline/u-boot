/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __MT6797_X620_EXCEPTION_H
#define __MT6797_X620_EXCEPTION_H
struct pt_regs;
void x620_record_exception(const char *kind, struct pt_regs *regs);
#endif
