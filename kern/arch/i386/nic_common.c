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

uint8_t eth_up = 0; 

// Hacky stuff for syscall hack. Go away.
uint32_t packet_buffer_count = 0;
char* packet_buffer[PACKET_BUFFER_SIZE];
uint32_t packet_buffer_sizes[PACKET_BUFFER_SIZE];
uint32_t packet_buffer_head = 0;
uint32_t packet_buffer_tail = 0;
spinlock_t packet_buffer_lock = SPINLOCK_INITIALIZER;


char* (*packet_wrap)(const char *CT(len) data, size_t len);
int (*send_frame)(const char *CT(len) data, size_t len);
