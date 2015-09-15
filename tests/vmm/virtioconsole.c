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

int *mmap_blob;
void *stack;
volatile int shared = 0;
int mcp = 1;
#define V(x, t) (*((volatile t*)(x)))
// NOTE: p is both our virtual and guest physical.
void *p;
int debug = 0;
struct virtqueue *head, *consin, *consout;
pthread_t *my_threads;
void **my_retvals;
int nr_threads = 2;
	char *line, *consline, *outline;
	struct scatterlist iov[32];
	unsigned int inlen, outlen, conslen;
	/* unlike Linux, this shared struct is for both host and guest. */
//	struct virtqueue *constoguest = 
//		vring_new_virtqueue(0, 512, 8192, 0, inpages, NULL, NULL, "test");
volatile int gaveit = 0, gotitback = 0;
struct virtqueue *guesttocons;
	struct scatterlist out[] = { {NULL, sizeof(outline)}, };
	struct scatterlist in[] = { {NULL, sizeof(line)}, };
	int iter = 1;
volatile int done = 0;



static void *fail(void *arg)
{
       	uint16_t head = 0;

	int i, j, ret;
	for(i = 0; i < 8;) {
		/* guest: make a line available to host */
		if ((gotitback == gaveit) && (!virtqueue_add_inbuf_avail(guesttocons, in, 1, line, 0))) {
			gaveit++;
		}

		/* guest code. Get all your buffers back */
		char *cp = NULL;
		while ((cp = virtqueue_get_buf_used(guesttocons, &conslen))) {
			if (cp != line) {
				gotitback++;
				continue;
			}
			//fprintf(stderr, "guest: from host: %s\n", cp);
			/* guest: push some buffers into the channel for the host to use */
			/* can't use sprintf here ... */
			outline[0] = '0' + i;
			for(j = 0; line[j] != 0; j++)
				outline[j+1] = line[j];
			j++;
			outline[j] = 0;
			
//				sprintf(outline, "guest: outline %d:%s:\n", iter, line);
			ret = virtqueue_add_outbuf_avail(guesttocons, out, 1, outline, 0);
			i++;
		}
	}

	done = 1;
	__asm__ __volatile__("vmcall");
	__asm__ __volatile__("mov $0xdeadbeef, %rbx; mov 5, %rax\n");
}

unsigned long long *p512, *p1, *p2m;

void *talk_thread(void *arg)
{
	fprintf(stderr, "talk thread ..\n");
	uint16_t head;
	int i;
	int num;
	for(num = 0; num < 8;) {
		/* host: use any buffers we should have been sent. */
		head = wait_for_vq_desc(guesttocons, iov, &outlen, &inlen);
		if (debug)
		printf("vq desc head %d, gaveit %d gotitback %d\n", head, gaveit, gotitback);
		for(i = 0; debug && i < outlen + inlen; i++)
			printf("v[%d/%d] v %p len %d\n", i, outlen + inlen, iov[i].v, iov[i].length);
		/* host: if we got an output buffer, just output it. */
		for(i = 0; i < outlen; i++) {
			num++;
			printf("Host:%s:\n", (char *)iov[i].v);
		}
		
		if (debug) printf("outlen is %d; inlen is %d\n", outlen, inlen);
		/* host: fill in the writeable buffers. */
		for (i = outlen; i < outlen + inlen; i++) {
			/* host: read a line. */
			memset(consline, 0, 128);
			if (1) {
				if (fgets(consline, 4096-256, stdin) == NULL) {
					exit(0);
				} 
				if (debug) printf("GOT A LINE:%s:\n", consline);
			} else {
				sprintf(consline, "hi there. %d\n", i);
			}
			memmove(iov[i].v, consline, strlen(consline)+ 1);
			iov[i].length = strlen(consline) + 1;
		}
		
		/* host: now ack that we used them all. */
		add_used(guesttocons, head, outlen+inlen);
	}
	fprintf(stderr, "All done\n");
	return NULL;
}

int main(int argc, char **argv)
{
	int nr_gpcs = 1;
	int fd = open("#cons/sysctl", O_RDWR), ret;
	void * x;
	static char cmd[512];
	debug = argc > 1;
	if (fd < 0) {
		perror("#cons/sysctl");
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

	void *outpages;
	outpages = mmap((int*)4096, 1048576, PROT_READ | PROT_WRITE,
	                 MAP_ANONYMOUS, -1, 0);
	if (outpages == MAP_FAILED) {
		perror("Unable to mmap");
		exit(1);
	}
	line = outpages;
	outline = outpages + 128;
	consline = outpages + 256;
	outpages += 4096;
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

	pthread_mcp_init();	/* gives us one vcore */
	vcore_request(nr_threads - 1);	/* ghetto incremental interface */
	for (int i = 0; i < nr_threads; i++) {
		x = __procinfo.vcoremap;
		fprintf(stderr, "%p\n", __procinfo.vcoremap);
		fprintf(stderr, "Vcore %d mapped to pcore %d\n", i,
			   __procinfo.vcoremap[i].pcoreid);
	}

	guesttocons = vring_new_virtqueue(0, 512, 8192, 0, outpages, NULL, NULL, "test");
	fprintf(stderr, "guesttocons is %p\n", guesttocons);
	out[0].v = outline;
	in[0].v = line;
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
	ret = posix_memalign((void **)&p512, 4096, 3 * 4096);
	if (ret) {
		perror("ptp alloc");
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
	showscatterlist(in, 1);
	showscatterlist(out, 1);
	showvq(guesttocons);
	//showdesc(guesttocons, 0);
	if (debug)
		fprintf(stderr, "Writing command :%s:\n", cmd);
	ret = write(fd, cmd, strlen(cmd));
	if (ret != strlen(cmd)) {
		perror(cmd);
	}
	sprintf(cmd, "V 0 0 0");
	while (! done) {
		if (debug)
			fprintf(stderr, "RESUME\n");
		ret = write(fd, cmd, strlen(cmd));
		if (ret != strlen(cmd)) {
			perror(cmd);
		}
	}

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
