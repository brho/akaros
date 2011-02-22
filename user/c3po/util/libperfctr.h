/* $Id: libperfctr.h,v 1.1 2003/03/22 07:41:06 zf Exp $
 * Library interface to Linux x86 Performance-Monitoring Counters.
 *
 * Copyright (C) 1999-2003  Mikael Pettersson
 */

#ifndef __LIB_PERFCTR_H
#define __LIB_PERFCTR_H

#define CONFIG_KPERFCTR
#include <linux/perfctr.h>

/*
 * Operations on the process' own virtual-mode perfctrs.
 */

struct vperfctr;	/* opaque */

struct vperfctr *vperfctr_open(void);
int vperfctr_info(const struct vperfctr*, struct perfctr_info*);
unsigned long long vperfctr_read_tsc(const struct vperfctr*);
unsigned long long vperfctr_read_pmc(const struct vperfctr*, unsigned);
void vperfctr_read_ctrs(const struct vperfctr*, struct perfctr_sum_ctrs*);
int vperfctr_read_state(const struct vperfctr*, struct perfctr_sum_ctrs*,
			struct vperfctr_control*);
int vperfctr_control(const struct vperfctr*, struct vperfctr_control*);
int vperfctr_stop(const struct vperfctr*);
int vperfctr_is_running(const struct vperfctr*);
int vperfctr_iresume(const struct vperfctr*);
int vperfctr_unlink(const struct vperfctr*);
void vperfctr_close(struct vperfctr*);

/*
 * Operations on other processes' virtual-mode perfctrs.
 * (Preliminary, subject to change.)
 */

struct rvperfctr;	/* opaque */

struct rvperfctr *rvperfctr_open(int pid);
int rvperfctr_pid(const struct rvperfctr*);
int rvperfctr_info(const struct rvperfctr*, struct perfctr_info*);
void rvperfctr_read_ctrs(const struct rvperfctr*, struct perfctr_sum_ctrs*);
int rvperfctr_read_state(const struct rvperfctr*, struct perfctr_sum_ctrs*,
			 struct vperfctr_control*);
int rvperfctr_control(const struct rvperfctr*, struct vperfctr_control*);
int rvperfctr_stop(const struct rvperfctr*);
int rvperfctr_unlink(const struct rvperfctr*);
void rvperfctr_close(struct rvperfctr*);

/*
 * Operations on global-mode perfctrs.
 */

struct gperfctr;	/* opaque */

struct gperfctr *gperfctr_open(void);
void gperfctr_close(struct gperfctr*);
int gperfctr_control(const struct gperfctr*, struct gperfctr_control*);
int gperfctr_read(const struct gperfctr*, struct gperfctr_state*);
int gperfctr_stop(const struct gperfctr*);
int gperfctr_info(const struct gperfctr*, struct perfctr_info*);

/*
 * Descriptions of the events available for different processor types.
 */

enum perfctr_unit_mask_type {
    perfctr_um_type_fixed,	/* one fixed (required) value */
    perfctr_um_type_exclusive,	/* exactly one of N values */
    perfctr_um_type_bitmask,	/* bitwise 'or' of N power-of-2 values */
};

struct perfctr_unit_mask_value {
    unsigned int value;
    const char *description;	/* [NAME:]text */
};

struct perfctr_unit_mask {
    unsigned int default_value;
    enum perfctr_unit_mask_type type:16;
    unsigned short nvalues;
    struct perfctr_unit_mask_value values[1/*nvalues*/];
};

struct perfctr_event {
    unsigned int evntsel;
    unsigned int counters_set; /* P4 force this to be CPU-specific */
    const struct perfctr_unit_mask *unit_mask;
    const char *name;
    const char *description;
};

struct perfctr_event_set {
    unsigned int cpu_type;
    const char *event_prefix;
    const struct perfctr_event_set *include;
    unsigned int nevents;
    const struct perfctr_event *events;
};

const struct perfctr_event_set *perfctr_cpu_event_set(unsigned int cpu_type);

/*
 * Miscellaneous operations.
 */

/* this checks the ABI between library and kernel -- it can also
   be used by applications operating on raw file descriptors */
int _perfctr_abi_check_fd(int fd, unsigned int user_abi_version);
static __inline__ int perfctr_abi_check_fd(int fd)
{
    return _perfctr_abi_check_fd(fd, PERFCTR_ABI_VERSION);
}

int perfctr_info(int fd, struct perfctr_info *info);
unsigned int perfctr_info_nrctrs(const struct perfctr_info*);
const char *perfctr_info_cpu_name(const struct perfctr_info*);
void perfctr_info_print(const struct perfctr_info*);
void perfctr_cpu_control_print(const struct perfctr_cpu_control*);

#endif /* __LIB_PERFCTR_H */
