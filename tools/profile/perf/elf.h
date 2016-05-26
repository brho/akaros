#pragma once

#include <stddef.h>

#define BUILD_ID_SIZE   20

int filename__read_build_id(const char *filename, void *bf, size_t size);
void symbol__elf_init(void);
