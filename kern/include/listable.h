/* Copyright (c) 2009 The Regents of the University  of California. 
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 * 
 * Kevin Klues <klueska@cs.berkeley.edu>    
 */
 
#ifndef ROS_LISTABLE_H
#define ROS_LISTABLE_H
 
#include <ros/common.h>
#include <sys/queue.h>

#define DECLARE_LISTABLE_ITEM(name, link, item)   \
struct name;                                      \
struct name {                                     \
	LIST_ENTRY(name) link;                        \
	item;                                         \
};                                                \
LIST_HEAD(name##_list, name);                     \
typedef struct name name##_t;                     \
typedef struct name##_list name##_list_t;

#endif //ROS_LISTABLE_H








