/* Copyright (C) 1991-2016, the Linux Kernel authors
 *
 * This source code is licensed under the GNU General Public License
 * Version 2. See the file COPYING for more details.
 *
 * Part of this code originates from Linux kernel files:
 *
 * linux/arch/x86/include/asm/asm.h
 *
 * These files are missing copyright headers, but are supposed to be
 * governed by the overall Linux copyright.
 */

#pragma once

#define _ASM_EXTABLE(from, to)             \
    " .pushsection \"__ex_table\",\"a\"\n" \
    " .balign 16\n"                        \
    " .quad (" #from ") - .\n"             \
    " .quad (" #to ") - .\n"               \
    " .popsection\n"

#define ASM_STAC
#define ASM_CLAC