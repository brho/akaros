#pragma once

#ifdef CONFIG_ACPI
extern void acpi_reboot(void);
#else
static inline void acpi_reboot(void) { }
#endif
