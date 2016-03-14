#pragma once

#include <acpi.h>

struct Atable *parsehpet(struct Atable *parent,
                         char *name, uint8_t *p, size_t rawsize);
