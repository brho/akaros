#pragma once

#include <stdint.h>

void exception_table_init(void);
uintptr_t get_fixup_ip(uintptr_t xip);
