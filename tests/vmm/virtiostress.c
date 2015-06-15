#include <stdio.h> 
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <parlib/arch/arch.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <ros/syscall.h>
#include <sys/mman.h>
#include <vmm/virtio.h>


struct u16_pool *id;
int *mmap_blob;
void *stack;
volatile int shared = 0;
int mcp = 1;
#define V(x, t) (*((volatile t*)(x)))
// NOTE: p is both our virtual and guest physical.
void *p;
int debug = 0;
struct virtqueue *head, *consin, *consout;
struct page {uint8_t d[4096];};
void *ringpages;
struct page *datapages;
pthread_t *my_threads;
void **my_retvals;
int nr_threads = 2;
	char *line, *consline, *outline;
	struct scatterlist iov[512];
	unsigned int inlen, outlen, conslen;
	/* unlike Linux, this shared struct is for both host and guest. */
//	struct virtqueue *constoguest = 
//		vring_new_virtqueue(0, 512, 8192, 0, inpages, NULL, NULL, "test");
struct virtqueue *guesttocons;
struct scatterlist io[512];
int iter = 1;
volatile int done = 0;
volatile int cnt;
volatile int failed, done, added, badput, badget, bogus;
volatile struct page *usedhead;
volatile int fstate;
volatile int failiter;

#define MAX_U16_POOL_SZ (1 << 16)

/* IDS is the stack of 16 bit integers we give out.  TOS is the top of stack -
 * it is the index of the next slot that can be popped, if there are any.  It's
 * a u32 so it can be greater than a u16.
 *
 * All free slots in ids will be below the TOS, ranging from indexes [0, TOS),
 * where if TOS == 0, then there are no free slots to push.
 *
 * We can hand out u16s in the range [0, 65535].
 *
 * The check array is used instead of a bitfield because these architectures
 * suck at those. */

struct u16_pool {
	uint32_t tos;
	uint16_t *ids;
	uint8_t *check;
	int size;
};

struct u16_pool *create_u16_pool(unsigned int num)
{
	struct u16_pool *id;
	int tot = sizeof(*id) + sizeof(uint16_t) * num + num;
	/* We could have size be a u16, but this might catch bugs where users
	 * tried to ask for more than 2^16 and had it succeed. */
	if (num > MAX_U16_POOL_SZ)
		return NULL;
	/* ids and check are alloced and aligned right after the id struct */
	id = mmap((int*)4096, tot, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
	if (id == MAP_FAILED) {
		perror("create_u16_pool: Unable to mmap");
		exit(1);
	}

	id->size = num;
	id->ids = (void *)&id[1];
	id->check = (void *)&id->ids[id->size];
	for (int i = 0; i < id->size; i++) {
		id->ids[i] = i;
		// fe rhymes with "free"
		id->check[i] = 0xfe;
	}
	id->tos = 0;
	return id;
}

/* Returns an unused u16, or -1 on failure (pool full or corruption).
 *
 * The invariant is that the stackpointer (TOS) will always point to the next
 * slot that can be popped, if there are any.  All free slots will be below the
 * TOS, ranging from indexes [0, TOS), where if TOS == 0, then there are no free
 * slots to push.  The last valid slot is when TOS == size - 1. */
int get_u16(struct u16_pool *id)
{
	uint16_t v;

	if (id->tos == id->size) {
		return -1;
	}
	v = id->ids[id->tos++];
	/* v is ours, we can freely read and write its check field */
	if (id->check[v] != 0xfe) {
		badget++;
		return -1;
	}
	id->check[v] = 0x5a;
	return v;
}

void put_u16(struct u16_pool *id, int v)
{
	/* we could check for if v is in range before dereferencing. */
	if (id->check[v] != 0x5a) {
		badput;
		return;
	}
	id->check[v] = 0xfe;
	id->ids[--id->tos] = v;
}

static void *fail(void *arg)
{
       	uint16_t head = 0;

	int i, j, v, ret;

	for(i = 0; i < 4096;) {
		/* get all the free ones you can, and add them all */
		fstate = 1;
		for(cnt = 0, v = get_u16(id); v >= 0; v = get_u16(id), cnt++) {
			failiter++;
			added++;
			/* 1:1 mapping of iovs to data pages for now */
			io[cnt].v = &datapages[v];
		}

		fstate++;
		if (! cnt)
			continue;
		if (virtqueue_add_inbuf_avail(guesttocons, io, cnt, datapages, 0)) {
			failed = 1;
			break;
		}
		fstate++;

		while (cnt > 0) {
			if ((usedhead = virtqueue_get_buf_used(guesttocons, &conslen))) {
				if (usedhead != datapages) { failed = 1; goto verybad;}
				for(j = 0; j < conslen; j++) {		
					int idx = (struct page *)io[j].v - usedhead;
					put_u16(id, idx);
					cnt--;
				}
			} else bogus++;
		}
		i++;
		fstate++;
	}

	done = 1;
verybad:
	__asm__ __volatile__("vmcall");
}

unsigned long long *p512, *p1, *p2m;

void *talk_thread(void *arg)
{
	fprintf(stderr, "talk thread ..\n");
	uint16_t head;
	int i;
	int num;
	int tot = 0;
	for(;;) {
		/* host: use any buffers we should have been sent. */
		head = wait_for_vq_desc(guesttocons, iov, &outlen, &inlen);
		if (debug)
			printf("vq desc head %d\n", head);
		if ((outlen == 0) && (inlen == 0)) // EOF
			break;
		tot += outlen + inlen;
		for(i = 0; debug && i < outlen + inlen; i++)
			printf("v[%d/%d] v %p len %d\n", i, outlen + inlen, iov[i].v, iov[i].length);

		if (debug) printf("outlen is %d; inlen is %d\n", outlen, inlen);
		if (0)
		{ printf("BEFORE ADD USED cnt %d conslen %d badget %d badput %d usedhead %p bogus %d\n", cnt, conslen, badget, badput, usedhead, bogus); showvq(guesttocons); getchar();}
		/* host: now ack that we used them all. */
		add_used(guesttocons, head, outlen+inlen);
		if (0)
		{ printf("cnt %d conslen %d badget %d badput %d usedhead %p bogus %d\n", cnt, conslen, badget, badput, usedhead, bogus); showvq(guesttocons); getchar();} 
		if (debug)
			printf("LOOP fstate %d \n", fstate);
	}
	fprintf(stderr, "All done, tot %d, failed %d, failiter %d\n", tot, failed, failiter);
	return NULL;
}

int main(int argc, char **argv)
{
	int nr_gpcs = 1;
	int fd = open("#c/sysctl", O_RDWR), ret;
	void * x;
	static char cmd[512];
	debug = argc > 1;
	if (fd < 0) {
		perror("#c/sysctl");
		exit(1);
	}

	if (ros_syscall(SYS_setup_vmm, nr_gpcs, 0, 0, 0, 0, 0) != nr_gpcs) {
		perror("Guest pcore setup failed");
		exit(1);
	}
	/* blob that is faulted in from the EPT first.  we need this to be in low
	 * memory (not above the normal mmap_break), so the EPT can look it up.
	 * Note that we won't get 4096.  The min is 1MB now, and ld is there. */
	mmap_blob = mmap((int*)4096, PGSIZE, PROT_READ | PROT_WRITE,
	                 MAP_ANONYMOUS, -1, 0);
	if (mmap_blob == MAP_FAILED) {
		perror("Unable to mmap");
		exit(1);
	}


	p512 = mmap((int*)4096, (3 + 256 + 512)*4096, PROT_READ | PROT_WRITE,
	                 MAP_ANONYMOUS, -1, 0);
	if (ringpages == MAP_FAILED) {
		perror("Unable to mmap");
		exit(1);
	}
	ringpages = p512 + 3*4096;
	datapages = ringpages + 256*4096;
	
	stack = mmap((int*)4096, 8192, PROT_READ | PROT_WRITE,
	                 MAP_ANONYMOUS, -1, 0);
	if (stack == MAP_FAILED) {
		perror("Unable to mmap");
		exit(1);
	}

	my_threads = calloc(sizeof(pthread_t) , nr_threads);
	my_retvals = calloc(sizeof(void *) , nr_threads);
	if (!(my_retvals && my_threads))
		perror("Init threads/malloc");

	pthread_lib_init();	/* gives us one vcore */
	vcore_request(nr_threads - 1);	/* ghetto incremental interface */
	for (int i = 0; i < nr_threads; i++) {
		x = __procinfo.vcoremap;
		fprintf(stderr, "%p\n", __procinfo.vcoremap);
		fprintf(stderr, "Vcore %d mapped to pcore %d\n", i,
			   __procinfo.vcoremap[i].pcoreid);
	}
	id = create_u16_pool(512); //1<<16);

	guesttocons = vring_new_virtqueue(0, 512, 8192, 0, ringpages, NULL, NULL, "test");
	fprintf(stderr, "guesttocons is %p\n", guesttocons);

	if (mcp) {
		if (pthread_create(&my_threads[0], NULL, &talk_thread, NULL))
			perror("pth_create failed");
//      if (pthread_create(&my_threads[1], NULL, &fail, NULL))
//          perror("pth_create failed");
	}
	fprintf(stderr, "threads started\n");
	
	ret = syscall(33, 1);
	if (ret < 0) {
		perror("vm setup");
		exit(1);
	}

	p1 = &p512[512];
	p2m = &p512[1024];
	p512[0] = (unsigned long long)p1 | 7;
	p1[0] = /*0x87; */ (unsigned long long)p2m | 7;
	p2m[0] = 0x87;
	p2m[1] = 0x200000 | 0x87;
	p2m[2] = 0x400000 | 0x87;
	p2m[3] = 0x600000 | 0x87;
	
	fprintf(stderr, "p512 %p p512[0] is 0x%lx p1 %p p1[0] is 0x%x\n", p512, p512[0], p1,
	       p1[0]);
	sprintf(cmd, "V 0x%x 0x%x 0x%x", (unsigned long long)fail,
		(unsigned long long)stack+8192, (unsigned long long)p512);
	showvq(guesttocons);
	//showdesc(guesttocons, 0);
	if (debug)
		fprintf(stderr, "Writing command :%s:\n", cmd);
	ret = write(fd, cmd, strlen(cmd));
	if (ret != strlen(cmd)) {
		perror(cmd);
	}
	sprintf(cmd, "V 0 0 0");
	while (! done && ! failed) {
		if (debug)
			fprintf(stderr, "RESUME\n");
		ret = write(fd, cmd, strlen(cmd));
		if (ret != strlen(cmd)) {
			perror(cmd);
		}
	}

	printf("VM DONE, done %d failed %d\n", done, failed);
	virtqueue_close(guesttocons);
	if (debug)
		fprintf(stderr, "shared is %d\n", shared);

	for (int i = 0; i < nr_threads - 1; i++) {
		int ret;
		if (pthread_join(my_threads[i], &my_retvals[i]))
			perror("pth_join failed");
		fprintf(stderr, "%d %d\n", i, ret);
	}
	
	return 0;
}
