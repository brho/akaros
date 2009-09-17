#ifndef ROS_INC_NE2K_H
#define ROS_INC_NE2K_H

#include <ros/common.h>
#include <trap.h>
#include <pmap.h>

#define ne2k_debug(...)  cprintf(__VA_ARGS__)  
#define ne2k_interrupt_debug(...) //cprintf(__VA_ARGS__)  
#define ne2k_frame_debug(...)  cprintf(__VA_ARGS__)  

#define NIC_IRQ_CPU			5

// Macro for formatting PCI Configuration Address queries
#define MK_CONFIG_ADDR(BUS, DEV, FUNC, REG) (unsigned long)( (BUS << 16) | (DEV << 11) | \
                                                             (FUNC << 8) | REG  | \
                                                             ((uint32_t)0x80000000))
#define NE2K_VENDOR_ID 0x10EC
#define NE2K_DEV_ID 0x8029

void ne2k_init();
int ne2k_scan_pci();
void ne2k_configure_nic();
void ne2k_setup_interrupts();
void ne2k_interrupt_handler(trapframe_t *tf, void* data);



#endif /* !ROS_INC_NE2K_H */
