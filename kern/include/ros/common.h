#ifndef ROS_COMMON_H
#define ROS_COMMON_H

#define FOR_CIRC_BUFFER(next, size, var) \
	for (int _var = 0, var = (next); _var < (size); _var++, var = (var + 1) % (size))

#endif /* ROS_COMMON_H */
