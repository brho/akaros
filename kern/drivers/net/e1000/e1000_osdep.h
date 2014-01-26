/*******************************************************************************

  Intel PRO/1000 Linux driver
  Copyright(c) 1999 - 2008 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

/* glue for the OS-dependent part of e1000
 * includes register access macros
 */

#ifndef _E1000_OSDEP_H_
#define _E1000_OSDEP_H_

#define __le16     uint16_t
#define __le32     uint32_t
#define __le64     uint64_t

#define __iomem

#define ETH_FCS_LEN 4

#define usec_delay(x) udelay(x)
#define msec_delay(x) mdelay(x)
#define msec_delay_irq(x) mdelay(x)

#define PCI_COMMAND_REGISTER   PCI_COMMAND
#define CMD_MEM_WRT_INVALIDATE PCI_COMMAND_INVALIDATE
#define ETH_ADDR_LEN           ETH_ALEN

#define DEBUGFUNC(F) DBG(F "\n")

#define DEBUGOUT(S)             DBG(S)
#define DEBUGOUT1(S, A...)      DBG(S, A)

#define DEBUGOUT2 DEBUGOUT1
#define DEBUGOUT3 DEBUGOUT2
#define DEBUGOUT7 DEBUGOUT3

#define E1000_REGISTER(a, reg) (((a)->mac.type >= e1000_82543) \
                               ? reg                           \
                               : e1000_translate_register_82542(reg))

#define E1000_WRITE_REG(a, reg, value) \
    outl((int)((a)->hw_addr + E1000_REGISTER(a, reg)), (value))

#define E1000_READ_REG(a, reg) (inl((a)->hw_addr + E1000_REGISTER(a, reg)))

#define E1000_WRITE_REG_ARRAY(a, reg, offset, value) \
    outl((int)((a)->hw_addr + E1000_REGISTER(a, reg) + ((offset) << 2)), (value))

#define E1000_READ_REG_ARRAY(a, reg, offset) (inl((a)->hw_addr + E1000_REGISTER(a, reg) + ((offset) << 2)))

#define E1000_READ_REG_ARRAY_DWORD E1000_READ_REG_ARRAY
#define E1000_WRITE_REG_ARRAY_DWORD E1000_WRITE_REG_ARRAY

#define E1000_WRITE_REG_ARRAY_WORD(a, reg, offset, value) (outw((value), ((a)->hw_addr + E1000_REGISTER(a, reg) + ((offset) << 1))))

#define E1000_READ_REG_ARRAY_WORD(a, reg, offset) (inw((a)->hw_addr + E1000_REGISTER(a, reg) + ((offset) << 1)))

#define E1000_WRITE_REG_ARRAY_BYTE(a, reg, offset, value) (outb((value), ((a)->hw_addr + E1000_REGISTER(a, reg) + (offset))))

#define E1000_READ_REG_ARRAY_BYTE(a, reg, offset) (inb((a)->hw_addr + E1000_REGISTER(a, reg) + (offset)))

#define E1000_WRITE_REG_IO(a, reg, offset) do { \
    outl(reg, ((a)->io_base));                  \
    outl(offset, ((a)->io_base + 4));      } while(0)

#define E1000_WRITE_FLUSH(a) E1000_READ_REG(a, E1000_STATUS)

#define E1000_WRITE_FLASH_REG(a, reg, value) (outl((value), ((a)->flash_address + reg)))

#define E1000_WRITE_FLASH_REG16(a, reg, value) (outw((value), ((a)->flash_address + reg)))

#define E1000_READ_FLASH_REG(a, reg) (inl((a)->flash_address + reg))

#define E1000_READ_FLASH_REG16(a, reg) (inw((a)->flash_address + reg))

#endif /* _E1000_OSDEP_H_ */
