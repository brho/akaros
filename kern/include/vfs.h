#pragma once

#include <ros/common.h>
#include <ros/limits.h>
#include <sys/queue.h>
#include <sys/uio.h>
#include <bitmask.h>
#include <kref.h>
#include <time.h>
#include <radix.h>
#include <hashtable.h>
#include <pagemap.h>
#include <fdtap.h>

/* Per-process structs */
#define NR_OPEN_FILES_DEFAULT 32
#define NR_FILE_DESC_DEFAULT 32

/* Bitmask for file descriptors, big for when we exceed the initial small.  We
 * could just use the fd_array to check for openness instead of the bitmask,
 * but eventually we might want to use the bitmasks for other things (like
 * which files are close_on_exec. */

typedef struct fd_set {
    uint8_t fds_bits[BYTES_FOR_BITMASK(NR_FILE_DESC_MAX)];
} fd_set;


struct small_fd_set {
    uint8_t fds_bits[BYTES_FOR_BITMASK(NR_FILE_DESC_DEFAULT)];
};

/* Helper macros to manage fd_sets */
#define FD_SET(n, p)  ((p)->fds_bits[(n)/8] |=  (1 << ((n) & 7)))
#define FD_CLR(n, p)  ((p)->fds_bits[(n)/8] &= ~(1 << ((n) & 7)))
#define FD_ISSET(n,p) ((p)->fds_bits[(n)/8] &   (1 << ((n) & 7)))
#define FD_ZERO(p)    memset((void*)(p),0,sizeof(*(p)))

/* Describes an open file.  We need this, since the FD flags are supposed to be
 * per file descriptor, not per file (like the file status flags). */
struct chan;	/* from 9ns */
struct file_desc {
	struct file					*fd_file;
	struct chan					*fd_chan;
	unsigned int				fd_flags;
	struct fd_tap				*fd_tap;
};

/* All open files for a process */
struct fd_table {
	spinlock_t					lock;
	bool						closed;
	int							max_files;		/* max files ptd to by fd */
	int							max_fdset;		/* max of the current fd_set */
	int							hint_min_fd;	/* <= min available fd */
	struct file_desc			*fd;			/* initially pts to fd_array */
	struct fd_set				*open_fds;		/* init, pts to open_fds_init */
	struct small_fd_set			open_fds_init;
	struct file_desc			fd_array[NR_OPEN_FILES_DEFAULT];
};

ssize_t kread_file(struct file_or_chan *file, void *buf, size_t sz);
void *kread_whole_file(struct file_or_chan *file);

/* Process-related File management functions */
void *lookup_fd(struct fd_table *fdt, int fd, bool incref, bool vfs);
int insert_obj_fdt(struct fd_table *fdt, void *obj, int low_fd, int fd_flags,
                   bool must_use_low, bool vfs);
bool close_fd(struct fd_table *fdt, int fd);
void close_fdt(struct fd_table *open_files, bool cloexec);
void clone_fdt(struct fd_table *src, struct fd_table *dst);
