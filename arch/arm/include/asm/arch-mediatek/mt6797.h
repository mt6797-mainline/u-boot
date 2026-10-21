/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __MT6797_H
#define __MT6797_H
struct pt_regs;
void mt6797_record_exception(const char *kind, struct pt_regs *regs);
#endif
