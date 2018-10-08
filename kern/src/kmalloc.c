/* Copyright (c) 2009 The Regents of the University of California.
 * See the COPYRIGHT files at the top of this source tree for full
 * license information.
 *
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 */
#include <ros/common.h>
#include <error.h>
#include <pmap.h>
#include <kmalloc.h>
#include <stdio.h>
#include <slab.h>
#include <assert.h>

#define kmallocdebug(args...)  //printk(args)

//List of physical pages used by kmalloc
static spinlock_t pages_list_lock = SPINLOCK_INITIALIZER;
static page_list_t pages_list;

struct kmem_cache *kmalloc_caches[NUM_KMALLOC_CACHES];

static void __kfree_release(struct kref *kref);

void kmalloc_init(void)
{
	char kc_name[KMC_NAME_SZ];

	/* we want at least a 16 byte alignment of the tag so that the bufs kmalloc
	 * returns are 16 byte aligned.  we used to check the actual size == 16,
	 * since we adjusted the KMALLOC_SMALLEST based on that. */
	static_assert(ALIGNED(sizeof(struct kmalloc_tag), 16));
	/* build caches of common sizes.  this size will later include the tag and
	 * the actual returned buffer. */
	size_t ksize = KMALLOC_SMALLEST;
	for (int i = 0; i < NUM_KMALLOC_CACHES; i++) {
		snprintf(kc_name, KMC_NAME_SZ, "kmalloc_%d", ksize);
		kmalloc_caches[i] = kmem_cache_create(kc_name, ksize, KMALLOC_ALIGNMENT,
		                                      0, NULL, 0, 0, NULL);
		ksize <<= 1;
	}
}

void *kmalloc(size_t size, int flags)
{
	// reserve space for bookkeeping and preserve alignment
	size_t ksize = size + sizeof(struct kmalloc_tag);
	void *buf;
	int cache_id;
	// determine cache to pull from
	if (ksize <= KMALLOC_SMALLEST)
		cache_id = 0;
	else
		cache_id = LOG2_UP(ksize) - LOG2_UP(KMALLOC_SMALLEST);
	// if we don't have a cache to handle it, alloc cont pages
	if (cache_id >= NUM_KMALLOC_CACHES) {
		/* The arena allocator will round up too, but we want to know in advance
		 * so that krealloc can avoid extra allocations. */
		size_t amt_alloc = ROUNDUP(size + sizeof(struct kmalloc_tag), PGSIZE);

		buf = kpages_alloc(amt_alloc, flags);
		if (!buf)
			panic("Kmalloc failed!  Handle me!");
		// fill in the kmalloc tag
		struct kmalloc_tag *tag = buf;
		tag->flags = KMALLOC_TAG_PAGES;
		tag->amt_alloc = amt_alloc;
		tag->canary = KMALLOC_CANARY;
		kref_init(&tag->kref, __kfree_release, 1);
		return buf + sizeof(struct kmalloc_tag);
	}
	// else, alloc from the appropriate cache
	buf = kmem_cache_alloc(kmalloc_caches[cache_id], flags);
	if (!buf)
		panic("Kmalloc failed!  Handle me!");
	// store a pointer to the buffers kmem_cache in it's bookkeeping space
	struct kmalloc_tag *tag = buf;
	tag->flags = KMALLOC_TAG_CACHE;
	tag->my_cache = kmalloc_caches[cache_id];
	tag->canary = KMALLOC_CANARY;
	kref_init(&tag->kref, __kfree_release, 1);
	return buf + sizeof(struct kmalloc_tag);
}

void *kzmalloc(size_t size, int flags)
{
	void *v = kmalloc(size, flags);
	if (!v)
		return v;
	memset(v, 0, size);
	return v;
}

void *kmalloc_align(size_t size, int flags, size_t align)
{
	void *addr, *retaddr;
	int *tag_flags, offset;
	/* alignment requests must be a multiple of long, even though we only need
	 * int in the current code. */
	assert(ALIGNED(align, sizeof(long)));
	/* must fit in the space reserved for the offset amount, which is at most
	 * 'align'. */
	assert(align < (1 << (32 - KMALLOC_ALIGN_SHIFT)));
	assert(IS_PWR2(align));
	addr = kmalloc(size + align, flags);
	if (!addr)
		return 0;
	if (ALIGNED(addr, align))
		return addr;
	retaddr = ROUNDUP(addr, align);
	offset = retaddr - addr;
	assert(offset < align);
	/* we might not have room for a full tag.  we might have only 8 bytes.  but
	 * we'll at least have room for the flags part. */
	tag_flags = (int*)(retaddr - sizeof(int));
	*tag_flags = (offset << KMALLOC_ALIGN_SHIFT) | KMALLOC_TAG_UNALIGN;
	return retaddr;
}

void *kzmalloc_align(size_t size, int flags, size_t align)
{
	void *v = kmalloc_align(size, flags, align);
	if (!v)
		return v;
	memset(v, 0, size);
	return v;
}

static struct kmalloc_tag *__get_km_tag(void *buf)
{
	struct kmalloc_tag *tag = (struct kmalloc_tag*)(buf -
	                                            sizeof(struct kmalloc_tag));
	if (tag->canary != KMALLOC_CANARY){
		printk("__get_km_tag bad canary: %08lx@%p, buf %p, expected %08lx\n",
		       tag->canary, &tag->canary, buf, KMALLOC_CANARY);
		hexdump((void *)(buf - sizeof(struct kmalloc_tag)), 256);
		panic("Bad canary");
	}
	return tag;
}

/* If we kmalloc_aligned, the buf we got back (and are now trying to perform
 * some operation on) might not be the original, underlying, unaligned buf.
 *
 * This returns the underlying, unaligned buf, or 0 if the buf was not realigned
 * in the first place. */
static void *__get_unaligned_orig_buf(void *buf)
{
	int *tag_flags = (int*)(buf - sizeof(int));
	if ((*tag_flags & KMALLOC_FLAG_MASK) == KMALLOC_TAG_UNALIGN)
		return (buf - (*tag_flags >> KMALLOC_ALIGN_SHIFT));
	else
		return 0;
}

void *krealloc(void* buf, size_t size, int flags)
{
	void *nbuf;
	size_t osize = 0;
	struct kmalloc_tag *tag;

	if (buf){
		if (__get_unaligned_orig_buf(buf))
			panic("krealloc of a kmalloc_align not supported");
		tag = __get_km_tag(buf);
		/* whatever we got from either a slab or the page allocator is meant for
		 * both the buf+size as well as the kmalloc tag */
		if ((tag->flags & KMALLOC_FLAG_MASK) == KMALLOC_TAG_CACHE) {
			osize = tag->my_cache->obj_size - sizeof(struct kmalloc_tag);
		} else if ((tag->flags & KMALLOC_FLAG_MASK) == KMALLOC_TAG_PAGES) {
			osize = tag->amt_alloc - sizeof(struct kmalloc_tag);
		} else {
			panic("Probably a bad tag, flags %p\n", tag->flags);
		}
		if (osize >= size)
			return buf;
	}

	nbuf = kmalloc(size, flags);

	/* would be more interesting to user error(...) here. */
	/* but in any event, NEVER destroy buf! */
	if (! nbuf)
		return NULL;

	if (osize)
		memmove(nbuf, buf, osize);

	if (buf)
		kfree(buf);

	return nbuf;
}

/* Grabs a reference on a buffer.  Release with kfree().
 *
 * Note that a krealloc on a buffer with ref > 1 that needs a new, underlying
 * buffer will result in two buffers existing.  In this case, the krealloc is a
 * kmalloc and a kfree, but that kfree does not completely free since the
 * original ref > 1. */
void kmalloc_incref(void *buf)
{
	void *orig_buf = __get_unaligned_orig_buf(buf);
	buf = orig_buf ? orig_buf : buf;
	/* if we want a smaller tag, we can extract the code from kref and manually
	 * set the release method in kfree. */
	kref_get(&__get_km_tag(buf)->kref, 1);
}

int kmalloc_refcnt(void *buf)
{
	void *orig_buf = __get_unaligned_orig_buf(buf);
	buf = orig_buf ? orig_buf : buf;
	return kref_refcnt(&__get_km_tag(buf)->kref);
}

static void __kfree_release(struct kref *kref)
{
	struct kmalloc_tag *tag = container_of(kref, struct kmalloc_tag, kref);
	if ((tag->flags & KMALLOC_FLAG_MASK) == KMALLOC_TAG_CACHE)
		kmem_cache_free(tag->my_cache, tag);
	else if ((tag->flags & KMALLOC_FLAG_MASK) == KMALLOC_TAG_PAGES)
		kpages_free(tag, tag->amt_alloc);
	else
		panic("Bad flag 0x%x in %s", tag->flags, __FUNCTION__);
}

void kfree(void *buf)
{
	void *orig_buf;
	if (buf == NULL)
		return;
	orig_buf = __get_unaligned_orig_buf(buf);
	buf = orig_buf ? orig_buf : buf;
	kref_put(&__get_km_tag(buf)->kref);
}

void kmalloc_canary_check(char *str)
{
	if (!debug_canary)
		return;
	struct kmalloc_tag *tag = (struct kmalloc_tag*)(debug_canary -
	                                                sizeof(struct kmalloc_tag));
	if (tag->canary != KMALLOC_CANARY)
		panic("\t\t KMALLOC CANARY CHECK FAILED %s\n", str);
}

struct sized_alloc *sized_kzmalloc(size_t size, int flags)
{
	struct sized_alloc *sza;

	sza = kzmalloc(sizeof(struct sized_alloc) + size, flags);
	if (!sza)
		return NULL;
	sza->buf = sza + 1;
	sza->size = size;
	return sza;
}

void sza_printf(struct sized_alloc *sza, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	sza->sofar += vsnprintf(sza->buf + sza->sofar, sza->size - sza->sofar,
	                        fmt, ap);
	va_end(ap);
}
