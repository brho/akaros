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
int packet_waiting;
int packet_buffer_size;
char *CT(MAX_FRAME_SIZE - PACKET_HEADER_SIZE) packet_buffer;
char *CT(MAX_FRAME_SIZE) packet_buffer_orig;
int packet_buffer_pos = 0;

char* (*packet_wrap)(const char *CT(len) data, size_t len);
int (*send_frame)(const char *CT(len) data, size_t len);
