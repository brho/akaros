#include <arena.h>
#include <slab.h>
#include <ktest.h>
#include <dma.h>
#include <pmap.h>

KTEST_SUITE("ARENA")

static bool test_nextfit(void)
{
	struct arena *a;
	void *o1, *o2, *o3;

	a = arena_create(__func__, (void*)1, 30, 1, NULL, NULL, NULL, 0,
			 MEM_WAIT);
	o1 = arena_alloc(a, 1, MEM_WAIT | ARENA_NEXTFIT);
	o2 = arena_alloc(a, 1, MEM_WAIT | ARENA_NEXTFIT);
	/* If we didn't NEXTFIT, the allocator would likely give us '1' back */
	arena_free(a, o1, 1);
	o3 = arena_alloc(a, 1, MEM_WAIT | ARENA_NEXTFIT);
	KT_ASSERT(o3 == o2 + 1);
	arena_free(a, o2, 1);
	arena_free(a, o3, 1);
	arena_destroy(a);

	return true;
}

static bool test_bestfit(void)
{
	struct arena *a;
	void *o1;

	a = arena_create(__func__, NULL, 0, 1, NULL, NULL, NULL, 0, MEM_WAIT);
	/* Each span will be an independent chunk in the allocator.  Their base
	 * values don't matter; they just identify the spans.
	 *
	 * BESTFIT for 65 should be 67.  INSTANTFIT should be 128.  The (128-1)
	 * objects around 67 are to make sure we check all objects on the 2^6
	 * list. */
	arena_add(a, (void*)1000, 64, MEM_WAIT);
	arena_add(a, (void*)3000, 128 - 1, MEM_WAIT);
	arena_add(a, (void*)2000, 67, MEM_WAIT);
	arena_add(a, (void*)4000, 128 - 1, MEM_WAIT);
	arena_add(a, (void*)5000, 128, MEM_WAIT);
	o1 = arena_alloc(a, 65, MEM_WAIT | ARENA_BESTFIT);
	KT_ASSERT(o1 == (void*)2000);
	arena_free(a, o1, 65);
	arena_destroy(a);

	return true;
}

static bool test_instantfit(void)
{
	struct arena *a;
	void *o1;

	a = arena_create(__func__, NULL, 0, 1, NULL, NULL, NULL, 0, MEM_WAIT);
	arena_add(a, (void*)1000, 64, MEM_WAIT);
	arena_add(a, (void*)2000, 67, MEM_WAIT);
	arena_add(a, (void*)3000, 64, MEM_WAIT);
	arena_add(a, (void*)4000, 128, MEM_WAIT);
	o1 = arena_alloc(a, 65, MEM_WAIT | ARENA_INSTANTFIT);
	KT_ASSERT(o1 == (void*)4000);
	arena_free(a, o1, 65);
	arena_destroy(a);

	return true;
}

static bool test_quantum_align(void)
{
	struct arena *a;
	void *o1, *o2;

	a = arena_create(__func__, NULL, 0, 32, NULL, NULL, NULL, 0, MEM_WAIT);
	/* this should give us one object only: */
	arena_add(a, (void*)(4096 + 1), 64, MEM_WAIT);
	/* 1 gets rounded up to quantum, so we're really asking for 32 */
	o1 = arena_alloc(a, 1, MEM_WAIT);
	KT_ASSERT(o1 == ROUNDUP((void*)(4096 + 1), a->quantum));
	/* Should be nothing quantum-sized left */
	o2 = arena_alloc(a, 1, MEM_ATOMIC);
	KT_ASSERT(o2 == NULL);
	arena_free(a, o1, 1);
	arena_destroy(a);

	return true;
}

static bool test_odd_quantum(void)
{
	struct arena *a;
	void *o1, *o2;

	a = arena_create(__func__, NULL, 0, 7, NULL, NULL, NULL, 0, MEM_WAIT);
	arena_add(a, (void*)7, 49, MEM_WAIT);
	o1 = arena_alloc(a, 7, MEM_WAIT);
	KT_ASSERT(o1 == (void*)7);
	o2 = arena_alloc(a, 7, MEM_WAIT);
	KT_ASSERT(o2 == (void*)14);
	arena_free(a, o1, 7);
	arena_free(a, o2, 7);

	/* In older arena code, this would fragment such that it could hand out
	 * non-quantum-aligned objects. */
	o1 = arena_xalloc(a, 7, 4, 0, 0, NULL, NULL, MEM_WAIT);
	o2 = arena_alloc(a, 7, MEM_WAIT);
	KT_ASSERT(!((uintptr_t)o2 % 7));
	arena_xfree(a, o1, 7);
	arena_free(a, o2, 7);
	arena_destroy(a);

	return true;
}

/* The nocross-fallback hops over the first nocross boundary in a segment try,
 * in the hopes that the rest of the segment can satisfy the constraints. */
static bool test_nocross_fallback(void)
{
	struct arena *a;
	void *o1;

	a = arena_create(__func__, NULL, 0, 3, NULL, NULL, NULL, 0, MEM_WAIT);
	arena_add(a, (void*)3, 20, MEM_WAIT);
	o1 = arena_xalloc(a, 3, 1, 0, 4, NULL, NULL, MEM_WAIT);
	/* 6 would be wrong.  We hopped over 4, but then didn't check that
	 * segment either (crosses 8). */
	KT_ASSERT(o1 == (void*)9);
	arena_xfree(a, o1, 3);
	arena_destroy(a);

	return true;
}

static bool test_xalloc_from_freelist(void)
{
	struct arena *a;
	void *o1;

	a = arena_create(__func__, NULL, 0, 1, NULL, NULL, NULL, 0, MEM_WAIT);
	/* one object on the order 3 list: size [8, 15].  it also starts at 15,
	 * which will satisfy align=8 phase=7. */
	arena_add(a, (void*)15, 15, MEM_WAIT);
	/* adding phase + ALIGN(align) would have us look on the order 4 list,
	 * which is what older code did. */
	o1 = arena_xalloc(a, 15, 8, 7, 0, NULL, NULL,
			  MEM_ATOMIC | ARENA_BESTFIT);
	KT_ASSERT(o1 == (void*)15);
	arena_xfree(a, o1, 15);
	arena_destroy(a);

	return true;
}

/* Right now, instantfit failures do *not* fall back to bestfit.  If we ever do
 * that, we can turn on this test.  {,x}alloc with a source will fallback to
 * bestfit *after* it went to the source. */
static bool test_alloc_instantfit_fallback(void)
{
	struct arena *a;
	void *o1;

	a = arena_create(__func__, NULL, 0, 1, NULL, NULL, NULL, 0, MEM_WAIT);
	/* one object on the order 3 list: size [8, 15], at 1. */
	arena_add(a, (void*)1, 15, MEM_WAIT);
	o1 = arena_alloc(a, 15, MEM_ATOMIC);
	KT_ASSERT(o1 == (void*)1);
	arena_free(a, o1, 15);
	o1 = arena_xalloc(a, 15, 1, 0, 0, NULL, NULL, MEM_ATOMIC);
	KT_ASSERT(o1 == (void*)1);
	arena_xfree(a, o1, 15);
	arena_destroy(a);

	return true;
}

static bool test_qcache(void)
{
	struct arena *a;
	void *o1, *o2, *o3, *o4;

	/* 3 qcaches */
	a = arena_create(__func__, NULL, 0, 1, NULL, NULL, NULL, 3, MEM_WAIT);
	arena_add(a, (void*)1, 10000, MEM_WAIT);
	/* Alloc from each qc, plus the arena. */
	o1 = arena_alloc(a, 1, MEM_WAIT);
	o2 = arena_alloc(a, 2, MEM_WAIT);
	o3 = arena_alloc(a, 3, MEM_WAIT);
	o4 = arena_alloc(a, 4, MEM_WAIT);

	arena_free(a, o1, 1);
	arena_free(a, o2, 2);
	arena_free(a, o3, 3);
	arena_free(a, o4, 4);
	arena_destroy(a);

	return true;
}

static bool test_qc_odd_quantum(void)
{
	struct arena *a;
	void *o[4];

	/* 3 qcaches, non-power-of-two quantum.  This checks the slab guarantee
	 * that if slab objects (qcaches) are a multiple of source->quantum,
	 * then all allocations are multiples of quantum. */
	a = arena_create(__func__, NULL, 0, 7, NULL, NULL, NULL, 21, MEM_WAIT);
	arena_add(a, (void*)7, 10000, MEM_WAIT);
	/* Alloc from each qc, plus the arena, ensure quantum alignment. */
	for (int i = 1; i < 4; i++) {
		size_t amt = 7 * i;

		/* Get a few before checking them all */
		for (int j = 0; j < ARRAY_SIZE(o); j++)
			o[j] = arena_alloc(a, amt, MEM_WAIT);
		for (int j = 0; j < ARRAY_SIZE(o); j++)
			KT_ASSERT(!((uintptr_t)o[j] % 7));
		for (int j = 0; j < ARRAY_SIZE(o); j++)
			arena_free(a, o[j], amt);
	}
	arena_destroy(a);

	return true;
}

/* slab code had an issue with align > PGSIZE.  QCs are quantum aligned, so
 * quantum > PGSIZE with a QC caused trouble. */
static bool test_qc_large_quantum(void)
{
	struct arena *a;
	void *o1;

	a = arena_create(__func__, NULL, 0, 8192, NULL, NULL, NULL, 8192,
			 MEM_WAIT);
	arena_add(a, (void*)8192, 8192 * 4, MEM_WAIT);
	o1 = arena_alloc(a, 8192, MEM_WAIT);
	arena_free(a, o1, 8192);
	arena_destroy(a);

	return true;
}

/* Just examples of stuff you can do. */
static void *tiaf(struct arena *a, size_t amt, int flags)
{
	void *obj = arena_alloc(a, amt, flags);

	return (void*)((uintptr_t)obj << 15);
}

static void tiff(struct arena *a, void *obj, size_t amt)
{
	arena_free(a, (void*)((uintptr_t)obj >> 15), amt);
}

static bool test_import(void)
{
	struct arena *a, *s;
	void *o1, *o2;

	s = arena_create("test_import-source", NULL, 0, 4096, NULL, NULL, NULL,
			 0, MEM_WAIT);
	arena_add(s, (void*)4096, 4096 * 4, MEM_WAIT);
	a = arena_create("test_import-actual", NULL, 0, 1, tiaf, tiff, s, 2,
			 MEM_WAIT);

	o1 = arena_alloc(a, 1, MEM_WAIT);
	o2 = arena_alloc(a, 2, MEM_WAIT);
	/* Make sure our handlers run.  The source gives 'a' addresses around
	 * 4096, which the import funcs translate to above 1 << 15. */
	KT_ASSERT((uintptr_t)o1 >= (1 << 15));
	KT_ASSERT((uintptr_t)o2 >= (1 << 15));

	arena_free(a, o1, 1);
	arena_free(a, o2, 2);
	arena_destroy(a);
	arena_destroy(s);

	return true;
}

static bool test_import_slab(void)
{
	struct arena *s;
	struct kmem_cache *kc;
	void *o[3];

	s = arena_create(__func__, NULL, 0, 7, NULL, NULL, NULL,
			 0, MEM_WAIT);
	/* We need to have a sizable amount here, since the KCs will pull a lot
	 * of resources when growing.  7000 isn't enough. */
	arena_add(s, (void*)7, 70000, MEM_WAIT);

	/* Quantum-preserving guarantee */
	kc = kmem_cache_create("test_import_slab-QP", 14, 1, KMC_NOTOUCH, s,
			       NULL, NULL, NULL);
	for (int i = 0; i < ARRAY_SIZE(o); i++)
		o[i] = kmem_cache_alloc(kc, MEM_WAIT);
	for (int i = 0; i < ARRAY_SIZE(o); i++)
		KT_ASSERT(!((uintptr_t)o[i] % 7));
	for (int i = 0; i < ARRAY_SIZE(o); i++)
		kmem_cache_free(kc, o[i]);
	kmem_cache_destroy(kc);


	/* Listen to slab's alignment guarantee */
	kc = kmem_cache_create("test_import_slab-AG", 1, 16, KMC_NOTOUCH, NULL,
			       NULL, NULL, NULL);
	for (int i = 0; i < ARRAY_SIZE(o); i++)
		o[i] = kmem_cache_alloc(kc, MEM_WAIT);
	for (int i = 0; i < ARRAY_SIZE(o); i++)
		KT_ASSERT(ALIGNED(o[i], 16));
	for (int i = 0; i < ARRAY_SIZE(o); i++)
		kmem_cache_free(kc, o[i]);
	kmem_cache_destroy(kc);


	arena_destroy(s);

	return true;
}

/* Arena import code wasn't grabbing enough, such that when we aligned the
 * source object to a's np2sb (which happened to be a power of 2), we had
 * nothing left to actually put in the arena.
 *
 * Additionally, arena's weren't freeing the segment back to their sources. */
static bool test_import_alignment(void)
{
	struct arena *s, *a;
	void *o1;

	s = arena_create("test_import_alignment-s", NULL, 0, 1,
			 NULL, NULL, NULL, 0, MEM_WAIT);
	arena_add(s, (void*)1, 1000, MEM_WAIT);
	a = arena_create("test_import_alignment-a", NULL, 0, 16,
			 arena_alloc, arena_free, s,
			 0, MEM_WAIT);
	o1 = arena_alloc(a, 16, MEM_WAIT);
	KT_ASSERT(o1);
	arena_free(a, o1, 16);
	arena_destroy(a);
	arena_destroy(s);

	return true;
}

static bool test_xalloc(void)
{
	struct arena *a;
	void *o1, *o2, *o3, *o4;

	a = arena_create(__func__, NULL, 0, 3, NULL, NULL, NULL, 0, MEM_WAIT);
	arena_add(a, (void*)3, 4096, MEM_WAIT);

	/* align 16, phase 6 */
	o1 = arena_xalloc(a, 3, 16, 6, 0, NULL, NULL, MEM_WAIT);
	KT_ASSERT(ALIGNED((uintptr_t)o1 - 6, 16));
	KT_ASSERT(!((uintptr_t)o1 % 3));

	/* nocross 16 */
	o2 = arena_xalloc(a, 15, 1, 0, 16, NULL, NULL, MEM_WAIT);
	KT_ASSERT(!((uintptr_t)o2 % 3));
	KT_ASSERT(ROUNDUP(o2 + 1, 16) >= o2 + 15);

	/* min 81, max 252.  should be available. */
	o3 = arena_xalloc(a, 3, 1, 0, 0, (void*)81, (void*)252, MEM_WAIT);
	KT_ASSERT(!((uintptr_t)o3 % 3));
	KT_ASSERT(81 <= (uintptr_t)o3 && (uintptr_t)o3 < 252);

	/* older xalloc code could hand out non-free segments! */
	o4 = arena_xalloc(a, 3, 1, 0, 0, (void*)81, (void*)252, MEM_WAIT);
	KT_ASSERT(!((uintptr_t)o4 % 3));
	KT_ASSERT(81 <= (uintptr_t)o4 && (uintptr_t)o4 < 252);
	KT_ASSERT(o4 != o3);

	arena_xfree(a, o1, 3);
	arena_xfree(a, o2, 15);
	arena_xfree(a, o3, 3);
	arena_xfree(a, o4, 3);
	arena_destroy(a);

	return true;
}

static bool test_xalloc_minmax(void)
{
	struct arena *a;
	void *o1, *o2;

	a = arena_create(__func__, NULL, 0, 1, NULL, NULL, NULL, 0, MEM_WAIT);
	arena_add(a, (void*)1, 100, MEM_WAIT);
	o1 = arena_xalloc(a, 20, 1, 0, 0, (void*)10, (void*)30, MEM_ATOMIC);
	KT_ASSERT((uintptr_t)o1 == 10);
	o2 = arena_xalloc(a, 20, 1, 0, 0, (void*)30, (void*)50, MEM_ATOMIC);
	KT_ASSERT((uintptr_t)o2 == 30);
	arena_xfree(a, o1, 20);
	arena_xfree(a, o2, 20);
	arena_destroy(a);

	return true;
}

/* Note we don't use qcaches - they throw off the measurements, since all qcache
 * objects (free or not) are counted as allocated from the arena's perspective.
 */
static bool test_accounting(void)
{
	struct arena *a;
	void *o1, *o2;

	a = arena_create(__func__, NULL, 0, 1, NULL, NULL, NULL, 0, MEM_WAIT);
	arena_add(a, (void*)1, 100, MEM_WAIT);
	KT_ASSERT(arena_amt_free(a) == 100);
	KT_ASSERT(arena_amt_total(a) == 100);

	/* Ensuring some fragmentation */
	o1 = arena_xalloc(a, 15, 1, 0, 0, (void*)10, (void*)40, MEM_WAIT);
	o2 = arena_xalloc(a, 15, 1, 0, 0, (void*)50, (void*)90, MEM_WAIT);

	KT_ASSERT(arena_amt_free(a) == 70);
	KT_ASSERT(arena_amt_total(a) == 100);

	arena_free(a, o1, 15);
	arena_free(a, o2, 15);
	arena_destroy(a);

	return true;
}

static void *tssaf(struct arena *a, size_t amt, int flags)
{
	static uintptr_t store = PGSIZE;
	void *ret;

	ret = (void*)store;
	store += ROUNDUP(amt, a->quantum);

	return ret;
}

static void tssff(struct arena *a, void *obj, size_t amt)
{
}

static bool test_self_source(void)
{
	struct arena *s, *a;
	void *o1, *o2;

	s = arena_create(__func__, NULL, 0, PGSIZE, tssaf, tssff,
			 ARENA_SELF_SOURCE, 0, MEM_WAIT);
	o1 = arena_alloc(s, 1, MEM_WAIT);
	o2 = arena_alloc(s, 1, MEM_WAIT);
	KT_ASSERT(o1 != o2);
	arena_free(s, o1, 1);
	arena_free(s, o2, 1);

	a = arena_create("test_self_source-import", NULL, 0, 1,
			 arena_alloc, arena_free, s, 0, MEM_WAIT);
	o1 = arena_alloc(a, 1, MEM_WAIT);
	o2 = arena_alloc(a, 1, MEM_WAIT);
	KT_ASSERT(o1 != o2);
	arena_free(a, o1, 1);
	arena_free(a, o2, 1);

	arena_destroy(a);
	arena_destroy(s);

	return true;
}

static bool test_dma_pool(void)
{
	struct dma_pool *dp;
	#define NR_LOOPS 10
	void *va[NR_LOOPS];
	dma_addr_t da[NR_LOOPS];

	dp = dma_pool_create(__func__, NULL, 33, 16, 64);
	for (int i = 0; i < NR_LOOPS; i++) {
		va[i] = dma_pool_alloc(dp, MEM_WAIT, &da[i]);
		KT_ASSERT(ALIGNED(va[i], 16));
		KT_ASSERT(ROUNDUP(va[i] + 1, 64) >= va[i] + 33);
		KT_ASSERT(PADDR(va[i]) == da[i]);
	}
	for (int i = 0; i < NR_LOOPS; i++)
		dma_pool_free(dp, va[i], da[i]);
	dma_pool_destroy(dp);

	return true;
}

static struct ktest ktests[] = {
	KTEST_REG(nextfit,		CONFIG_KTEST_ARENA),
	KTEST_REG(bestfit,		CONFIG_KTEST_ARENA),
	KTEST_REG(instantfit,		CONFIG_KTEST_ARENA),
	KTEST_REG(quantum_align,	CONFIG_KTEST_ARENA),
	KTEST_REG(odd_quantum,		CONFIG_KTEST_ARENA),
	KTEST_REG(nocross_fallback,	CONFIG_KTEST_ARENA),
	KTEST_REG(xalloc_from_freelist,	CONFIG_KTEST_ARENA),
	KTEST_REG(qcache,		CONFIG_KTEST_ARENA),
	KTEST_REG(qc_odd_quantum,	CONFIG_KTEST_ARENA),
	KTEST_REG(qc_large_quantum,	CONFIG_KTEST_ARENA),
	KTEST_REG(import,		CONFIG_KTEST_ARENA),
	KTEST_REG(import_slab,		CONFIG_KTEST_ARENA),
	KTEST_REG(import_alignment,	CONFIG_KTEST_ARENA),
	KTEST_REG(xalloc,		CONFIG_KTEST_ARENA),
	KTEST_REG(xalloc_minmax,	CONFIG_KTEST_ARENA),
	KTEST_REG(accounting,		CONFIG_KTEST_ARENA),
	KTEST_REG(self_source,		CONFIG_KTEST_ARENA),
	KTEST_REG(dma_pool,		CONFIG_KTEST_ARENA),
};

static int num_ktests = sizeof(ktests) / sizeof(struct ktest);

static void __init register_arena_ktests(void)
{
        REGISTER_KTESTS(ktests, num_ktests);
}
init_func_1(register_arena_ktests);
