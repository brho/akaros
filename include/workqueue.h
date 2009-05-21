#ifndef ROS_KERN_WORKQUEUE_H
#define ROS_KERN_WORKQUEUE_H
#ifndef ROS_KERNEL
# error "This is an ROS kernel header; user programs should not #include it"
#endif

// Once we have a real kmalloc, we can make this dynamic.  Want a list.
typedef void (*func_t)(void* data);
typedef struct work {
	func_t func;
	void* data;
} work_t;

void process_workqueue(void);

#endif /* ROS_KERN_WORKQUEUE_H */
