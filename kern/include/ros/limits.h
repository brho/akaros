#ifndef ROS_INC_LIMITS_H
#define ROS_INC_LIMITS_H

/* Keep this 255 to stay in sync with glibc (expects d_name[256]) */
#define MAX_FILENAME_SZ 255
/* POSIX / glibc name: */
#define NAME_MAX MAX_FILENAME_SZ

#define PATH_MAX 4096 /* includes null-termination */

#endif /* ROS_INC_LIMITS_H */
