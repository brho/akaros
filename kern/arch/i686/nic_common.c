/** @file
 * @brief Common Nic Variables
 *
 * See Info below 
 *
 * @author Paul Pearce <pearce@eecs.berkeley.edu>
 *
 */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <arch/nic_common.h>
#include <kmalloc.h>
#include <stdio.h>

// Global send_frame function pointer
// Means we can only have one network card per system right now...
int (*send_frame)(const char *data, size_t len);

// Global variables for managing ethernet packets over a nic
// Again, since these are global for all network cards we are 
// limited to only one for now
unsigned char device_mac[6];
uint8_t eth_up = 0; 
uint32_t num_packet_buffers = 0;
char* packet_buffers[MAX_PACKET_BUFFERS];
uint32_t packet_buffers_sizes[MAX_PACKET_BUFFERS];
uint32_t packet_buffers_head = 0;
uint32_t packet_buffers_tail = 0;
spinlock_t packet_buffers_lock = SPINLOCK_INITIALIZER;

