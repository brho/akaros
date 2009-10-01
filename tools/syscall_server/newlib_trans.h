#ifndef NEWLIB_TRANS_H
#define NEWLIB_TRANS_H

#include <stdint.h>

// For translating the stat structure
typedef struct newlib_stat {
	int16_t st_dev;
	uint16_t st_ino;
	uint32_t st_mode;
	uint16_t st_nlink;
	uint16_t st_uid;
	uint16_t st_gid;
	int16_t st_rdev;
	int32_t st_size;
	int32_t st_atim;
	int32_t st_spare1;
	int32_t st_mtim;
	int32_t st_spare2;
	int32_t st_ctim;
	int32_t st_spare3;
	int32_t st_blksize;
	int32_t st_blocks;
	int32_t st_spare4[2];
} newlib_stat_t;

// For translating the open flags
#define NEWLIB_O_RDONLY    0x0000 
#define NEWLIB_O_WRONLY    0x0001
#define NEWLIB_O_RDWR      0x0002
#define NEWLIB_O_APPEND    0x0008
#define NEWLIB_O_CREAT     0x0200
#define NEWLIB_O_TRUNC     0x0400
#define NEWLIB_O_EXCL      0x0800

// For translating the open modes
#define NEWLIB_S_IRWXU     \
        (NEWLIB_S_IRUSR | NEWLIB_S_IWUSR | NEWLIBS_IXUSR)
#define     NEWLIB_S_IRUSR 0000400 /* read permission, owner */
#define     NEWLIB_S_IWUSR 0000200 /* write permission, owner */
#define     NEWLIB_S_IXUSR 0000100/* execute/search permission, owner */
#define NEWLIB_S_IRWXG     \
        (NEWLIB_S_IRGRP | NEWLIB_S_IWGRP | NEWLIB_S_IXGRP)
#define     NEWLIB_S_IRGRP 0000040 /* read permission, group */
#define     NEWLIB_S_IWGRP 0000020 /* write permission, grougroup */
#define     NEWLIB_S_IXGRP 0000010/* execute/search permission, group */
#define NEWLIB_S_IRWXO     \
        (NEWLIB_S_IROTH | NEWLIB_S_IWOTH | NEWLIB_S_IXOTH)
#define     NEWLIB_S_IROTH 0000004 /* read permission, other */
#define     NEWLIB_S_IWOTH 0000002 /* write permission, other */
#define     NEWLIB_S_IXOTH 0000001/* execute/search permission, other */

// For translating lseek's whence
# define    NEWLIB_SEEK_SET    0
# define    NEWLIB_SEEK_CUR    1
# define    NEWLIB_SEEK_END    2

#endif //NEWLIB_TRANS_H
