#ifndef ROS_INC_REALTEK_H
#define ROS_INC_REALTEK_H

#include <arch/types.h>
#include <trap.h>

void init_nic(void);
void nic_interrupt_handler(trapframe_t *tf, void* data);
int scan_pci(void);
void read_mac(void);
void setup_interrupts(void);
void setup_rx_descriptors(void);
void configure_nic(void);
void poll_rx_descriptors(void);
void nic_handle_rx_packet(void);


#endif /* !ROS_INC_REALTEK_H */
