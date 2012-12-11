#include <slab.h>
#include <stdio.h>

static void test_single_cache(int iters, size_t size, int align, int flags,
                              void (*ctor)(void *, size_t),
                              void (*dtor)(void *, size_t))
{
	struct kmem_cache *test_cache;
	void *objects[iters];
	test_cache = kmem_cache_create("test_cache", size, align, flags, ctor, dtor);
	printf("Testing Kmem Cache:\n");
	print_kmem_cache(test_cache);
	for (int i = 0; i < iters; i++) {
		objects[i] = kmem_cache_alloc(test_cache, 0);
		printf("Buffer %d addr = %p\n", i, objects[i]);
	}
	for (int i = 0; i < iters; i++) {
		kmem_cache_free(test_cache, objects[i]);
	}
	kmem_cache_destroy(test_cache);
	printf("\n\n\n\n");
}

void a_ctor(void *buf, size_t size)
{
	printf("constructin tests\n");
}
void a_dtor(void *buf, size_t size)
{
	printf("destructin tests\n");
}

int main(void)
{
	test_single_cache(10, 128, 512, 0, 0, 0);
	test_single_cache(10, 128, 4, 0, a_ctor, a_dtor);
	test_single_cache(10, 1024, 16, 0, 0, 0);
}
