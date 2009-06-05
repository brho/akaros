/*-
 * Copyright (c) 1982, 1986, 1989, 1993
 *      The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)stat.h      8.12 (Berkeley) 6/16/95
 * $FreeBSD: src/sys/sys/stat.h,v 1.46 2009/03/14 19:11:08 das Exp $
 */

#ifndef _SYS_STAT_H_
#define _SYS_STAT_H_

#include <_ansi.h>
#include <time.h>
#include <sys/types.h>
#include <sys/cdefs.h>
#include <sys/_types.h>

#define	S_IFMT		_IFMT
#define _S_IFMT		_IFMT

/*
 * Standard type definitions.
 */
typedef __uint32_t      __blksize_t;    /* file block size */
typedef __int64_t       __blkcnt_t;     /* file block count */
typedef __int32_t       __clockid_t;    /* clock_gettime()... */
typedef __uint32_t      __fflags_t;     /* file flags */
typedef __uint64_t      __fsblkcnt_t;
typedef __uint64_t      __fsfilcnt_t;
//typedef __uint32_t      __gid_t;
typedef __int64_t       __id_t;         /* can hold a gid_t, pid_t, or uid_t */
typedef __uint32_t      __ino_t;        /* inode number */
typedef long            __key_t;        /* IPC key (for Sys V IPC) */
typedef __int32_t       __lwpid_t;      /* Thread ID (a.k.a. LWP) */
typedef __uint16_t      __mode_t;       /* permissions */
typedef int             __accmode_t;    /* access permissions */
typedef int             __nl_item;
typedef __uint16_t      __nlink_t;      /* link count */
//typedef __int64_t       __off_t;        /* file offset */
//typedef __int32_t       __pid_t;        /* process [group] */
typedef __int64_t       __rlim_t;       /* resource limit - intentionally */
                                        /* signed, because of legacy code */
                                        /* that uses -1 for RLIM_INFINITY */
typedef __uint8_t       __sa_family_t;
typedef __uint32_t      __socklen_t;
typedef long            __suseconds_t;  /* microseconds (signed) */
typedef struct __timer  *__timer_t;     /* timer_gettime()... */
typedef struct __mq     *__mqd_t;       /* mq_open()... */
//typedef __uint32_t      __uid_t;
typedef unsigned int    __useconds_t;   /* microseconds (unsigned) */
typedef int             __cpuwhich_t;   /* which parameter for cpuset. */
typedef int             __cpulevel_t;   /* level parameter for cpuset. */
typedef int             __cpusetid_t;   /* cpuset identifier. */

/*
 * Unusual type definitions.
 */
/*
 * rune_t is declared to be an ``int'' instead of the more natural
 * ``unsigned long'' or ``long''.  Two things are happening here.  It is not
 * unsigned so that EOF (-1) can be naturally assigned to it and used.  Also,
 * it looks like 10646 will be a 31 bit standard.  This means that if your
 * ints cannot hold 32 bits, you will be in trouble.  The reason an int was
 * chosen over a long is that the is*() and to*() routines take ints (says
 * ANSI C), but they use __ct_rune_t instead of int.
 *
 * NOTE: rune_t is not covered by ANSI nor other standards, and should not
 * be instantiated outside of lib/libc/locale.  Use wchar_t.  wchar_t and
 * rune_t must be the same type.  Also, wint_t must be no narrower than
 * wchar_t, and should be able to hold all members of the largest
 * character set plus one extra value (WEOF), and must be at least 16 bits.
 */
typedef int             __ct_rune_t;    /* arg type for ctype funcs */
typedef __ct_rune_t     __rune_t;       /* rune_t (see above) */
typedef __ct_rune_t     __wchar_t;      /* wchar_t (see above) */
typedef __ct_rune_t     __wint_t;       /* wint_t (see above) */

//typedef __uint32_t      __dev_t;        /* device number */

typedef __uint32_t      __fixpt_t;      /* fixed point number */

/*
 * mbstate_t is an opaque object to keep conversion state during multibyte
 * stream conversions.
 */
typedef union {
        char            __mbstate8[128];
        __int64_t       _mbstateL;      /* for alignment */
} __mbstate_t;

#ifndef _BLKSIZE_T_DECLARED
typedef __blksize_t     blksize_t;
#define _BLKSIZE_T_DECLARED
#endif

#ifndef _BLKCNT_T_DECLARED
typedef __blkcnt_t      blkcnt_t;
#define _BLKCNT_T_DECLARED
#endif

#ifndef _DEV_T_DECLARED
//typedef __dev_t         dev_t;
#define _DEV_T_DECLARED
#endif

#ifndef _FFLAGS_T_DECLARED
typedef __fflags_t      fflags_t;
#define _FFLAGS_T_DECLARED
#endif

#ifndef _GID_T_DECLARED
//typedef __gid_t         gid_t;
#define _GID_T_DECLARED
#endif

#ifndef _INO_T_DECLARED
//typedef __ino_t         ino_t;
#define _INO_T_DECLARED
#endif

#ifndef _MODE_T_DECLARED
//typedef __mode_t        mode_t;
#define _MODE_T_DECLARED
#endif

#ifndef _NLINK_T_DECLARED
//typedef __nlink_t       nlink_t;
#define _NLINK_T_DECLARED
#endif

#ifndef _OFF_T_DECLARED
//typedef __off_t         off_t;
#define _OFF_T_DECLARED
#endif

#ifndef _TIME_T_DECLARED
//typedef __time_t        time_t;
typedef time_t			__time_t;
#define _TIME_T_DECLARED
#endif

#ifndef _UID_T_DECLARED
//typedef __uid_t         uid_t;
#define _UID_T_DECLARED
#endif

#if !defined(_KERNEL) && __BSD_VISIBLE
/*
 * XXX we need this for struct timespec.  We get miscellaneous namespace
 * pollution with it.
 */
#include <sys/time.h>
#endif

#if !__BSD_VISIBLE
struct __timespec {
	__time_t tv_sec;        /* seconds */
	long    tv_nsec;        /* and nanoseconds */
};
#endif

#if __BSD_VISIBLE
struct ostat {
        __uint16_t st_dev;              /* inode's device */
        ino_t     st_ino;               /* inode's number */
        mode_t    st_mode;              /* inode protection mode */
        nlink_t   st_nlink;             /* number of hard links */
        __uint16_t st_uid;              /* user ID of the file's owner */
        __uint16_t st_gid;              /* group ID of the file's group */
        __uint16_t st_rdev;             /* device type */
        __int32_t st_size;              /* file size, in bytes */
        struct  timespec st_atimespec;  /* time of last access */
        struct  timespec st_mtimespec;  /* time of last data modification */
        struct  timespec st_ctimespec;  /* time of last file status change */
        __int32_t st_blksize;           /* optimal blocksize for I/O */
        __int32_t st_blocks;            /* blocks allocated for file */
        fflags_t  st_flags;             /* user defined flags for file */
        __uint32_t st_gen;              /* file generation number */
};
#endif /* __BSD_VISIBLE */

struct stat {
 __int32_t st_dev;
 __int32_t st_ino;
 __int16_t st_mode;
 __int16_t st_nlink;
 __int32_t st_uid;
 __int32_t st_gid;
 __int32_t st_rdev;

 struct timespec st_atimespec;
 struct timespec st_mtimespec;
 struct timespec st_ctimespec;

 __int64_t st_size;
 blkcnt_t st_blocks;
 blksize_t st_blksize;
 __uint32_t st_flags;
 __uint32_t st_gen;
 __int32_t st_lspare;
 __int64_t st_qspare[2];
};

#if 0
struct stat {
        __dev_t   st_dev;               /* inode's device */
        ino_t     st_ino;               /* inode's number */
        mode_t    st_mode;              /* inode protection mode */
        nlink_t   st_nlink;             /* number of hard links */
        uid_t     st_uid;               /* user ID of the file's owner */
        gid_t     st_gid;               /* group ID of the file's group */
        __dev_t   st_rdev;              /* device type */
#if __BSD_VISIBLE
        struct  timespec st_atimespec;  /* time of last access */
        struct  timespec st_mtimespec;  /* time of last data modification */
        struct  timespec st_ctimespec;  /* time of last file status change */
#else
        time_t    st_atime;             /* time of last access */
        long      __st_atimensec;       /* nsec of last access */
        time_t    st_mtime;             /* time of last data modification */
        long      __st_mtimensec;       /* nsec of last data modification */
        time_t    st_ctime;             /* time of last file status change */
        long      __st_ctimensec;       /* nsec of last file status change */
#endif
        off_t     st_size;              /* file size, in bytes */
        blkcnt_t st_blocks;             /* blocks allocated for file */
        blksize_t st_blksize;           /* optimal blocksize for I/O */
        fflags_t  st_flags;             /* user defined flags for file */
        __uint32_t st_gen;              /* file generation number */
        __int32_t st_lspare;
#if __BSD_VISIBLE
        struct timespec st_birthtimespec; /* time of file creation */
        /*
         * Explicitly pad st_birthtimespec to 16 bytes so that the size of
         * struct stat is backwards compatible.  We use bitfields instead
         * of an array of chars so that this doesn't require a C99 compiler
         * to compile if the size of the padding is 0.  We use 2 bitfields
         * to cover up to 64 bits on 32-bit machines.  We assume that
         * CHAR_BIT is 8...
         */
        unsigned int :(8 / 2) * (16 - (int)sizeof(struct timespec));
        unsigned int :(8 / 2) * (16 - (int)sizeof(struct timespec));
#else
        time_t    st_birthtime;         /* time of file creation */
        long      st_birthtimensec;     /* nsec of file creation */
        unsigned int :(8 / 2) * (16 - (int)sizeof(struct __timespec));
        unsigned int :(8 / 2) * (16 - (int)sizeof(struct __timespec));
#endif
};
#endif


#if __BSD_VISIBLE
struct nstat {
        __dev_t   st_dev;               /* inode's device */
        ino_t     st_ino;               /* inode's number */
        __uint32_t st_mode;             /* inode protection mode */
        __uint32_t st_nlink;            /* number of hard links */
        uid_t     st_uid;               /* user ID of the file's owner */
        gid_t     st_gid;               /* group ID of the file's group */
        __dev_t   st_rdev;              /* device type */
        struct  timespec st_atimespec;  /* time of last access */
        struct  timespec st_mtimespec;  /* time of last data modification */
        struct  timespec st_ctimespec;  /* time of last file status change */
        off_t     st_size;              /* file size, in bytes */
        blkcnt_t st_blocks;             /* blocks allocated for file */
        blksize_t st_blksize;           /* optimal blocksize for I/O */
        fflags_t  st_flags;             /* user defined flags for file */
        __uint32_t st_gen;              /* file generation number */
        struct timespec st_birthtimespec; /* time of file creation */
        /*
         * See above about the following padding.
         */
        unsigned int :(8 / 2) * (16 - (int)sizeof(struct timespec));
        unsigned int :(8 / 2) * (16 - (int)sizeof(struct timespec));
};
#endif

#if __BSD_VISIBLE
#define st_atime st_atimespec.tv_sec
#define st_mtime st_mtimespec.tv_sec
#define st_ctime st_ctimespec.tv_sec
#define st_birthtime st_birthtimespec.tv_sec
#endif

#define     _IFMT   0170000 /* type of file */
#define     _IFDIR  0040000 /* directory */
#define     _IFCHR  0020000 /* character special */
#define     _IFBLK  0060000 /* block special */
#define     _IFREG  0100000 /* regular */
#define     _IFLNK  0120000 /* symbolic link */
#define     _IFSOCK 0140000 /* socket */
#define     _IFIFO  0010000 /* fifo */

#define S_IFMT      _IFMT
#define S_IFDIR     _IFDIR
#define S_IFCHR     _IFCHR
#define S_IFBLK     _IFBLK
#define S_IFREG     _IFREG
#define S_IFLNK     _IFLNK
#define S_IFSOCK    _IFSOCK
#define S_IFIFO     _IFIFO

#define S_ISUID 0004000                 /* set user id on execution */
#define S_ISGID 0002000                 /* set group id on execution */
#if __BSD_VISIBLE
#define S_ISTXT 0001000                 /* sticky bit */
#endif

#define S_IRWXU 0000700                 /* RWX mask for owner */
#define S_IRUSR 0000400                 /* R for owner */
#define S_IWUSR 0000200                 /* W for owner */
#define S_IXUSR 0000100                 /* X for owner */

#if __BSD_VISIBLE
#define S_IREAD         S_IRUSR
#define S_IWRITE        S_IWUSR
#define S_IEXEC         S_IXUSR
#endif

#define S_IRWXG 0000070                 /* RWX mask for group */
#define S_IRGRP 0000040                 /* R for group */
#define S_IWGRP 0000020                 /* W for group */
#define S_IXGRP 0000010                 /* X for group */

#define S_IRWXO 0000007                 /* RWX mask for other */
#define S_IROTH 0000004                 /* R for other */
#define S_IWOTH 0000002                 /* W for other */
#define S_IXOTH 0000001                 /* X for other */

#if __XSI_VISIBLE
#define S_IFMT   0170000                /* type of file mask */
#define S_IFIFO  0010000                /* named pipe (fifo) */
#define S_IFCHR  0020000                /* character special */
#define S_IFDIR  0040000                /* directory */
#define S_IFBLK  0060000                /* block special */
#define S_IFREG  0100000                /* regular */
#define S_IFLNK  0120000                /* symbolic link */
#define S_IFSOCK 0140000                /* socket */
#define S_ISVTX  0001000                /* save swapped text even after use */
#endif
#if __BSD_VISIBLE
#define S_IFWHT  0160000                /* whiteout */
#endif

#define S_ISDIR(m)      (((m) & 0170000) == 0040000)    /* directory */
#define S_ISCHR(m)      (((m) & 0170000) == 0020000)    /* char special */
#define S_ISBLK(m)      (((m) & 0170000) == 0060000)    /* block special */
#define S_ISREG(m)      (((m) & 0170000) == 0100000)    /* regular file */
#define S_ISFIFO(m)     (((m) & 0170000) == 0010000)    /* fifo or socket */
#if __POSIX_VISIBLE >= 200112
#define S_ISLNK(m)      (((m) & 0170000) == 0120000)    /* symbolic link */
#define S_ISSOCK(m)     (((m) & 0170000) == 0140000)    /* socket */
#endif
#if __BSD_VISIBLE
#define S_ISWHT(m)      (((m) & 0170000) == 0160000)    /* whiteout */
#endif

#if __BSD_VISIBLE
#define ACCESSPERMS     (S_IRWXU|S_IRWXG|S_IRWXO)       /* 0777 */
                                                        /* 7777 */
#define ALLPERMS        (S_ISUID|S_ISGID|S_ISTXT|S_IRWXU|S_IRWXG|S_IRWXO)
                                                        /* 0666 */
#define DEFFILEMODE     (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH)

#define S_BLKSIZE       512             /* block size used in the stat struct */

/*
 * Definitions of flags stored in file flags word.
 *
 * Super-user and owner changeable flags.
 */
#define UF_SETTABLE     0x0000ffff      /* mask of owner changeable flags */
#define UF_NODUMP       0x00000001      /* do not dump file */
#define UF_IMMUTABLE    0x00000002      /* file may not be changed */
#define UF_APPEND       0x00000004      /* writes to file may only append */
#define UF_OPAQUE       0x00000008      /* directory is opaque wrt. union */
#define UF_NOUNLINK     0x00000010      /* file may not be removed or renamed */
/*
 * Super-user changeable flags.
 */
#define SF_SETTABLE     0xffff0000      /* mask of superuser changeable flags */
#define SF_ARCHIVED     0x00010000      /* file is archived */
#define SF_IMMUTABLE    0x00020000      /* file may not be changed */
#define SF_APPEND       0x00040000      /* writes to file may only append */
#define SF_NOUNLINK     0x00100000      /* file may not be removed or renamed */
#define SF_SNAPSHOT     0x00200000      /* snapshot inode */

#ifdef _KERNEL
/*
 * Shorthand abbreviations of above.
 */
#define OPAQUE          (UF_OPAQUE)
#define APPEND          (UF_APPEND | SF_APPEND)
#define IMMUTABLE       (UF_IMMUTABLE | SF_IMMUTABLE)
#define NOUNLINK        (UF_NOUNLINK | SF_NOUNLINK)
#endif

#endif /* __BSD_VISIBLE */

#ifndef _KERNEL
__BEGIN_DECLS
#if __BSD_VISIBLE
int     chflags(const char *, unsigned long);
#endif
int     chmod(const char *, mode_t);
#if __BSD_VISIBLE
int     fchflags(int, unsigned long);
#endif
#if __POSIX_VISIBLE >= 200112
int     fchmod(int, mode_t);
#endif
#if __POSIX_VISIBLE >= 200809
int     fchmodat(int, const char *, mode_t, int);
#endif
int     fstat(int, struct stat *);
#if __BSD_VISIBLE
int     lchflags(const char *, int);
int     lchmod(const char *, mode_t);
#endif
#if __POSIX_VISIBLE >= 200112
int     lstat(const char * __restrict, struct stat * __restrict);
#endif
int     mkdir(const char *, mode_t);
int     mkfifo(const char *, mode_t);
#if !defined(_MKNOD_DECLARED) && __XSI_VISIBLE
int     mknod(const char *, mode_t, dev_t);
#define _MKNOD_DECLARED
#endif
int     stat(const char * __restrict, struct stat * __restrict);
mode_t  umask(mode_t);
#if __BSD_VISIBLE || __POSIX_VISIBLE >= 200809
int     fstatat(int, const char *, struct stat *, int);
int     mkdirat(int, const char *, mode_t);
int     mkfifoat(int, const char *, mode_t);
#endif
#if __BSD_VISIBLE || __XSI_VISIBLE >= 700
int     mknodat(int, const char *, mode_t, dev_t);
#endif
__END_DECLS
#endif /* !_KERNEL */

#endif /* !_SYS_STAT_H_ */

