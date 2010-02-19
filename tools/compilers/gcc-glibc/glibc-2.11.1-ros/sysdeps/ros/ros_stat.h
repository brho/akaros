#ifndef _ROS_CONVERT_STAT_H
#define _ROS_CONVERT_STAT_H

#include <ros/stat.h>

#define __stat_copy_field(field) \
  st->st_##field = (typeof(st->st_##field))nst->st_##field

static void __convert_stat(const struct ros_stat* nst, struct stat* st)
{
  __stat_copy_field(dev);
  __stat_copy_field(ino);
  __stat_copy_field(mode);
  __stat_copy_field(nlink);
  __stat_copy_field(uid);
  __stat_copy_field(gid);
  __stat_copy_field(size);
  __stat_copy_field(atime);
  __stat_copy_field(mtime);
  __stat_copy_field(ctime);
}

static void __convert_stat64(const struct stat* nst, struct stat64* st)
{
  __stat_copy_field(dev);
  __stat_copy_field(ino);
  __stat_copy_field(mode);
  __stat_copy_field(nlink);
  __stat_copy_field(uid);
  __stat_copy_field(gid);
  __stat_copy_field(size);
  __stat_copy_field(atime);
  __stat_copy_field(mtime);
  __stat_copy_field(ctime);
}

#endif
