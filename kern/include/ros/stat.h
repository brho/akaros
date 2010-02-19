#ifndef _ROS_STAT_H
#define _ROS_STAT_H

// This matches the stat structure used by the RAMP appserver.
#include <stdint.h>

struct ros_stat
{
   int16_t st_dev;
  uint32_t st_ino;
  uint16_t st_mode;
  uint16_t st_nlink;
  uint16_t st_uid;
  uint16_t st_gid;
   int16_t st_rdev;
   int32_t st_size;
   int32_t st_atime;
   int32_t st_spare1;
   int32_t st_mtime;
   int32_t st_spare2;
   int32_t st_ctime;
   int32_t st_spare3;
   int32_t st_blksize;
   int32_t st_blocks;
   int32_t st_spare4[2];
};

#endif
