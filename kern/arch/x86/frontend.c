#include <multiboot.h>
#include <arch/frontend.h>
#include <net/nic_common.h>
#include <kmalloc.h>

#define debug(...) printk(__VA_ARGS__)

int handle_appserver_packet(const char* p, size_t size)
{
#warning "need to rewrite this for the new ip stack"
	panic("bad appserver packet!");
}
