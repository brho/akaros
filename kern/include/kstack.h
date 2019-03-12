#pragma once

#define KSTKSHIFT	(PGSHIFT + 1)	/* KSTKSIZE == 2 * PGSIZE */

#define KSTKSIZE	(1 << KSTKSHIFT) /* size of a static kernel stack */
