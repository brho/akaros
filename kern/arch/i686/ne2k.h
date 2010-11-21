#ifndef ROS_INC_NE2K_H
#define ROS_INC_NE2K_H

#include <ros/common.h>
#include <trap.h>
#include <pmap.h>
#include <arch/pci.h>
#include <arch/nic_common.h>

#define ne2k_debug(...)  //cprintf(__VA_ARGS__)  
#define ne2k_interrupt_debug(...) //cprintf(__VA_ARGS__)  
#define ne2k_frame_debug(...) //cprintf(__VA_ARGS__)  

#define NIC_IRQ_CPU			5

#define NE2K_VENDOR_ID 0x10EC
#define NE2K_DEV_ID 0x8029

void ne2k_init();
int ne2k_scan_pci();
void ne2k_configure_nic();
void ne2k_setup_interrupts();
void ne2k_interrupt_handler(trapframe_t *tf, void* data);
void ne2k_mem_alloc();
void ne2k_read_mac();
void ne2k_test_interrupts();
void ne2k_handle_rx_packet();
int ne2k_send_frame(const char *CT(len) data, size_t len);
char *CT(PACKET_HEADER_SIZE + len) ne2k_packet_wrap(const char *CT(len) data, size_t len);

#endif /* !ROS_INC_NE2K_H */
