#ifndef _ROS_STAT_H
#define _ROS_STAT_H

struct newlib_stat
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

static void __convert_stat(const struct newlib_stat* nst, struct stat* st)
{
  #define __stat_copy_field(field) \
    st->st_##field = (typeof(st->st_##field))nst->st_##field
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
