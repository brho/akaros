/* This exists so we can conveniently call for sys/types.h in headers shared by
 * both the kernel and userspace.  This is the kernel's version. */

#pragma once

#include <ros/common.h>
#include <arch/types.h>

#define PAGE_ALIGN(addr) ALIGN(addr, PAGE_SIZE)

typedef uint64_t __dev_t;
typedef uint64_t __ino64_t;
typedef uint32_t __mode_t;
typedef uint64_t __nlink_t;
typedef uint32_t __uid_t;
typedef uint32_t __gid_t;
typedef int64_t __off64_t;
typedef __off64_t off64_t;
typedef int64_t __blksize_t;
typedef int64_t __blkcnt64_t;

typedef uint64_t __le64;
typedef uint64_t __be64;
typedef uint32_t __le32;
typedef uint32_t __be32;
typedef uint16_t __le16;
typedef uint16_t __be16;

#define KiB	1024u
#define MiB	1048576u
#define GiB	1073741824u
#define TiB	1099511627776ull
#define PiB	1125899906842624ull
#define EiB	1152921504606846976ull
