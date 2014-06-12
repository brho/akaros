#ifndef ROS_KERN_KSTACK_H
#define ROS_KERN_KSTACK_H

#ifdef CONFIG_LARGE_KSTACKS
#define KSTKSHIFT	(PGSHIFT + 1)		/* KSTKSIZE == 2 * PGSIZE */
#else
#define KSTKSHIFT	(PGSHIFT)			/* KSTKSIZE == PGSIZE */
#endif

#define KSTKSIZE	(1 << KSTKSHIFT)	/* size of a static kernel stack */

#endif /* ROS_KERN_KSTACK_H */
