#ifndef ROS_DRIVERS_TIMERS_HPET_H
#define ROS_DRIVERS_TIMERS_HPET_H

#include <acpi.h>

struct Atable *acpihpet(uint8_t *p, int len);

#endif /* ROS_DRIVERS_TIMERS_HPET_H */
