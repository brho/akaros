#include <multiboot.h>
#include <arch/frontend.h>
#include <kmalloc.h>
#include <assert.h>

#define debug(...) printk(__VA_ARGS__)

int handle_appserver_packet(const char* p, size_t size)
{

	panic("bad appserver packet!");
}
