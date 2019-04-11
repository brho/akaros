/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2003-2004 Eric Biederman
 * Copyright (C) 2005-2010 coresystems GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of
 * the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301 USA
 */

#include <parlib/common.h>
#include <parlib/stdio.h>

#include <stdarg.h>
#include <string.h>
#include <vmm/coreboot_tables.h>

static struct lb_header *lb_table_init(void *addr)
{
	struct lb_header *header;

	/* 16 byte align the address */
	header = (void *)(((unsigned long)addr + 15) & ~15);
	header->signature[0] = 'L';
	header->signature[1] = 'B';
	header->signature[2] = 'I';
	header->signature[3] = 'O';
	header->header_bytes = sizeof(*header);
	header->header_checksum = 0;
	header->table_bytes = 0;
	header->table_checksum = 0;
	header->table_entries = 0;
	return header;
}

static struct lb_record *lb_first_record(struct lb_header *header)
{
	struct lb_record *rec;
	rec = (void *)(((char *)header) + sizeof(*header));
	return rec;
}

static struct lb_record *lb_last_record(struct lb_header *header)
{
	struct lb_record *rec;
	rec = (void *)(((char *)header) + sizeof(*header) + header->table_bytes);
	return rec;
}

struct lb_record *lb_new_record(struct lb_header *header)
{
	struct lb_record *rec;
	rec = lb_last_record(header);
	if (header->table_entries) {
		header->table_bytes += rec->size;
	}
	rec = lb_last_record(header);
	header->table_entries++;
	rec->tag = LB_TAG_UNUSED;
	rec->size = sizeof(*rec);
	return rec;
}

static struct lb_memory *lb_memory(struct lb_header *header)
{
	struct lb_record *rec;
	struct lb_memory *mem;
	rec = lb_new_record(header);
	mem = (struct lb_memory *)rec;
	mem->tag = LB_TAG_MEMORY;
	mem->size = sizeof(*mem);
	return mem;
}

void lb_add_serial(struct lb_serial *new_serial, void *data)
{
	struct lb_header *header = (struct lb_header *)data;
	struct lb_serial *serial;

	serial = (struct lb_serial *)lb_new_record(header);
	serial->tag = LB_TAG_SERIAL;
	serial->size = sizeof(*serial);
	serial->type = new_serial->type;
	serial->baseaddr = new_serial->baseaddr;
	serial->baud = new_serial->baud;
	serial->regwidth = new_serial->regwidth;
}

void lb_add_console(uint16_t consoletype, void *data)
{
	struct lb_header *header = (struct lb_header *)data;
	struct lb_console *console;

	console = (struct lb_console *)lb_new_record(header);
	console->tag = LB_TAG_CONSOLE;
	console->size = sizeof(*console);
	console->type = consoletype;
}

static void lb_framebuffer(struct lb_header *header)
{
#if 0 //CONFIG_FRAMEBUFFER_KEEP_VESA_MODE || CONFIG_MAINBOARD_DO_NATIVE_VGA_INIT
	void fill_lb_framebuffer(struct lb_framebuffer *framebuffer);
	int vbe_mode_info_valid(void);

	// If there isn't any mode info to put in the table, don't ask for it
	// to be filled with junk.
	if (!vbe_mode_info_valid())
		return;
	struct lb_framebuffer *framebuffer;
	framebuffer = (struct lb_framebuffer *)lb_new_record(header);
	fill_lb_framebuffer(framebuffer);
	framebuffer->tag = LB_TAG_FRAMEBUFFER;
	framebuffer->size = sizeof(*framebuffer);
#endif
}

void fill_lb_gpio(struct lb_gpio *gpio, int num,
			 int polarity, const char *name, int value)
{
	memset(gpio, 0, sizeof(*gpio));
	gpio->port = num;
	gpio->polarity = polarity;
	if (value >= 0)
		gpio->value = value;
	strncpy((char *)gpio->name, name, GPIO_MAX_NAME_LENGTH);
}

#if CONFIG_CHROMEOS
static void lb_gpios(struct lb_header *header)
{
	struct lb_gpios *gpios;

	gpios = (struct lb_gpios *)lb_new_record(header);
	gpios->tag = LB_TAG_GPIO;
	gpios->size = sizeof(*gpios);
	gpios->count = 0;
	fill_lb_gpios(gpios);
}

static void lb_vdat(struct lb_header *header)
{
#if CONFIG_HAVE_ACPI_TABLES
	struct lb_range *vdat;

	vdat = (struct lb_range *)lb_new_record(header);
	vdat->tag = LB_TAG_VDAT;
	vdat->size = sizeof(*vdat);
	acpi_get_vdat_info(&vdat->range_start, &vdat->range_size);
#endif
}

static void lb_vbnv(struct lb_header *header)
{
#if CONFIG_PC80_SYSTEM
	struct lb_range *vbnv;

	vbnv = (struct lb_range *)lb_new_record(header);
	vbnv->tag = LB_TAG_VBNV;
	vbnv->size = sizeof(*vbnv);
	vbnv->range_start = CONFIG_VBNV_OFFSET + 14;
	vbnv->range_size = CONFIG_VBNV_SIZE;
#endif
}

#if CONFIG_VBOOT_VERIFY_FIRMWARE
static void lb_vboot_handoff(struct lb_header *header)
{
	void *addr;
	uint32_t size;
	struct lb_range *vbho;

	if (vboot_get_handoff_info(&addr, &size))
		return;

	vbho = (struct lb_range *)lb_new_record(header);
	vbho->tag = LB_TAB_VBOOT_HANDOFF;
	vbho->size = sizeof(*vbho);
	vbho->range_start = (intptr_t)addr;
	vbho->range_size = size;
}
#else
static inline void lb_vboot_handoff(struct lb_header *header) {}
#endif /* CONFIG_VBOOT_VERIFY_FIRMWARE */
#endif /* CONFIG_CHROMEOS */

static void lb_board_id(struct lb_header *header)
{
#if 0 // CONFIG_BOARD_ID_AUTO || CONFIG_BOARD_ID_MANUAL
	struct lb_board_id  *bid;

	bid = (struct lb_board_id *)lb_new_record(header);

	bid->tag = LB_TAG_BOARD_ID;
	bid->size = sizeof(*bid);
	bid->board_id = board_id();
#endif
}

static void lb_strings(struct lb_header *header)
{
	static const struct {
		uint32_t tag;
		const char *string;
	} strings[] = {
		// we may want this at some point.
	};
	unsigned int i;
	for(i = 0; i < sizeof(strings)/sizeof(strings[0]); i++) {
		struct lb_string *rec;
		size_t len;
		rec = (struct lb_string *)lb_new_record(header);
		len = strlen(strings[i].string);
		rec->tag = strings[i].tag;
		rec->size = (sizeof(*rec) + len + 1 + 3) & ~3;
		memcpy(rec->string, strings[i].string, len+1);
	}

}

static void lb_record_version_timestamp(struct lb_header *header)
{
	struct lb_timestamp *rec;
	rec = (struct lb_timestamp *)lb_new_record(header);
	rec->tag = LB_TAG_VERSION_TIMESTAMP;
	rec->size = sizeof(*rec);
	// TODO
	//rec->timestamp = coreboot_version_timestamp;
}

void __attribute__((weak)) lb_board(struct lb_header *header) { /* NOOP */ }

static struct lb_forward *lb_forward(struct lb_header *header, struct lb_header *next_header)
{
	struct lb_record *rec;
	struct lb_forward *forward;
	rec = lb_new_record(header);
	forward = (struct lb_forward *)rec;
	forward->tag = LB_TAG_FORWARD;
	forward->size = sizeof(*forward);
	forward->forward = (uint64_t)(unsigned long)next_header;
	return forward;
}

/* enough with the ip dependency already. This is a trivial checksum. */
static inline uint16_t cb_checksum(const void *ptr, unsigned len)
{
	uint32_t sum;
	const uint8_t *addr = ptr;

	sum = 0;

	while(len > 0) {
		sum += addr[0]<<8 | addr[1] ;
		len -= 2;
		addr += 2;
	}

	sum = (sum & 0xffff) + (sum >> 16);
	sum = (sum & 0xffff) + (sum >> 16);

	return (sum^0xffff);
}

static void *lb_table_fini(struct lb_header *head)
{
	struct lb_record *rec, *first_rec;
	rec = lb_last_record(head);
	if (head->table_entries) {
		head->table_bytes += rec->size;
	}

	first_rec = lb_first_record(head);
	head->table_checksum = cb_checksum((void *)first_rec, head->table_bytes);
	head->header_checksum = 0;
	head->header_checksum = cb_checksum((void *)head, sizeof(*head));
	printf("Wrote coreboot table at: %p, 0x%x bytes, checksum %x\n",
	       head, head->table_bytes, head->table_checksum);
	printf("header checksum (not worth using in a kernel) 0x%x\n", head->header_checksum);
	return rec + rec->size;
}

void * write_coreboot_table(void *where, void *base, uint64_t len)
{
	struct lb_header *head;
	struct lb_memory *m;
	struct lb_memory_range *mem;

	printf("Writing coreboot table at %p with mem %p len 0x%lx\n", where, base, len);

	head = lb_table_init(where);
	m = lb_memory(head);
	mem = (void *)(&m[1]);
	mem->start = pack_lb64((uint64_t) 0);
	mem->size = pack_lb64((uint64_t) base-1);
	mem->type = LB_MEM_RESERVED;
	m->size += sizeof(*mem);
	mem = (void *)(&mem[1]);
	mem->start = pack_lb64((uint64_t) base);
	mem->size = pack_lb64((uint64_t) len);
	mem->type = LB_MEM_RAM;
	m->size += sizeof(*mem);
	printf("Head is %p, mem is %p, m is %p, m->size is %d, mem->size is %d\n",
		head, mem, m, m->size, mem->size);
	return lb_table_fini(head);
}
