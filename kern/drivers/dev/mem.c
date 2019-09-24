/* Copyright (c) 2016 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * #mem, memory diagnostics (arenas and slabs)
 */

#include <ns.h>
#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <syscall.h>
#include <sys/queue.h>

struct dev mem_devtab;

static char *devname(void)
{
	return mem_devtab.name;
}

enum {
	Qdir,
	Qarena_stats,
	Qslab_stats,
	Qfree,
	Qkmemstat,
	Qslab_trace,
};

static struct dirtab mem_dir[] = {
	{".", {Qdir, 0, QTDIR}, 0, DMDIR | 0555},
	{"arena_stats", {Qarena_stats, 0, QTFILE}, 0, 0444},
	{"slab_stats", {Qslab_stats, 0, QTFILE}, 0, 0444},
	{"free", {Qfree, 0, QTFILE}, 0, 0444},
	{"kmemstat", {Qkmemstat, 0, QTFILE}, 0, 0444},
	{"slab_trace", {Qslab_trace, 0, QTFILE}, 0, 0444},
};

/* Protected by the arenas_and_slabs_lock */
static struct sized_alloc *slab_trace_data;

static struct chan *mem_attach(char *spec)
{
	return devattach(devname(), spec);
}

static struct walkqid *mem_walk(struct chan *c, struct chan *nc, char **name,
				unsigned int nname)
{
	return devwalk(c, nc, name, nname, mem_dir, ARRAY_SIZE(mem_dir),
		       devgen);
}

static size_t mem_stat(struct chan *c, uint8_t *db, size_t n)
{
	return devstat(c, db, n, mem_dir, ARRAY_SIZE(mem_dir), devgen);
}

/* Prints arena's stats to the sza, adjusting the sza's sofar. */
static void fetch_arena_stats(struct arena *arena, struct sized_alloc *sza)
{
	struct btag *bt_i;
	struct rb_node *rb_i;
	struct arena *a_i;
	struct kmem_cache *kc_i;

	size_t nr_allocs = 0;
	size_t nr_imports = 0;
	size_t amt_alloc = 0;
	size_t amt_free = 0;
	size_t amt_imported = 0;
	size_t empty_hash_chain = 0;
	size_t longest_hash_chain = 0;

	sza_printf(sza, "Arena: %s (%p)\n--------------\n", arena->name, arena);
	sza_printf(sza, "\tquantum: %d, qcache_max: %d\n", arena->quantum,
	           arena->qcache_max);
	sza_printf(sza, "\tsource: %s\n",
	           arena->source ? arena->source->name : "none");
	spin_lock_irqsave(&arena->lock);
	for (int i = 0; i < ARENA_NR_FREE_LISTS; i++) {
		int j = 0;

		if (!BSD_LIST_EMPTY(&arena->free_segs[i])) {
			sza_printf(sza, "\tList of [2^%d - 2^%d):\n", i, i + 1);
			BSD_LIST_FOREACH(bt_i, &arena->free_segs[i], misc_link)
				j++;
			sza_printf(sza, "\t\tNr free segs: %d\n", j);
		}
	}
	for (int i = 0; i < arena->hh.nr_hash_lists; i++) {
		int j = 0;

		if (BSD_LIST_EMPTY(&arena->alloc_hash[i]))
			empty_hash_chain++;
		BSD_LIST_FOREACH(bt_i, &arena->alloc_hash[i], misc_link)
			j++;
		longest_hash_chain = MAX(longest_hash_chain, j);
	}
	sza_printf(sza, "\tSegments:\n\t--------------\n");
	for (rb_i = rb_first(&arena->all_segs); rb_i; rb_i = rb_next(rb_i)) {
		bt_i = container_of(rb_i, struct btag, all_link);
		if (bt_i->status == BTAG_SPAN) {
			nr_imports++;
			amt_imported += bt_i->size;
		}
		if (bt_i->status == BTAG_FREE)
			amt_free += bt_i->size;
		if (bt_i->status == BTAG_ALLOC) {
			nr_allocs++;
			amt_alloc += bt_i->size;
		}
	}
	sza_printf(sza, "\tStats:\n\t-----------------\n");
	sza_printf(sza, "\t\tAmt free: %llu (%p)\n", amt_free, amt_free);
	sza_printf(sza, "\t\tAmt alloc: %llu (%p), nr allocs %d\n",
	           amt_alloc, amt_alloc, nr_allocs);
	sza_printf(sza, "\t\tAmt total segs: %llu, amt alloc segs %llu\n",
	           arena->amt_total_segs, arena->amt_alloc_segs);
	sza_printf(sza, "\t\tAmt imported: %llu (%p), nr imports %d\n",
	           amt_imported, amt_imported, nr_imports);
	sza_printf(sza, "\t\tNr hash %d, empty hash: %d, longest hash %d\n",
	           arena->hh.nr_hash_lists, empty_hash_chain,
	           longest_hash_chain);
	spin_unlock_irqsave(&arena->lock);
	sza_printf(sza, "\tImporting Arenas:\n\t-----------------\n");
	TAILQ_FOREACH(a_i, &arena->__importing_arenas, import_link)
		sza_printf(sza, "\t\t%s\n", a_i->name);
	sza_printf(sza, "\tImporting Slabs:\n\t-----------------\n");
	TAILQ_FOREACH(kc_i, &arena->__importing_slabs, import_link)
		sza_printf(sza, "\t\t%s\n", kc_i->name);
}

static struct sized_alloc *build_arena_stats(void)
{
	struct sized_alloc *sza;
	size_t alloc_amt = 0;
	struct arena *a_i;

	qlock(&arenas_and_slabs_lock);
	/* Rough guess about how many chars per arena we'll need. */
	TAILQ_FOREACH(a_i, &all_arenas, next)
		alloc_amt += 1000;
	sza = sized_kzmalloc(alloc_amt, MEM_WAIT);
	TAILQ_FOREACH(a_i, &all_arenas, next)
		fetch_arena_stats(a_i, sza);
	qunlock(&arenas_and_slabs_lock);
	return sza;
}

/* Prints arena's stats to the sza, updating its sofar. */
static void fetch_slab_stats(struct kmem_cache *kc, struct sized_alloc *sza)
{
	struct kmem_slab *s_i;
	struct kmem_bufctl *bc_i;

	size_t nr_unalloc_objs = 0;
	size_t empty_hash_chain = 0;
	size_t longest_hash_chain = 0;

	spin_lock_irqsave(&kc->cache_lock);
	sza_printf(sza, "\nKmem_cache: %s\n---------------------\n", kc->name);
	sza_printf(sza, "Source: %s\n", kc->source->name);
	sza_printf(sza, "Objsize (incl align): %d\n", kc->obj_size);
	sza_printf(sza, "Align: %d\n", kc->align);
	TAILQ_FOREACH(s_i, &kc->empty_slab_list, link) {
		assert(!s_i->num_busy_obj);
		nr_unalloc_objs += s_i->num_total_obj;
	}
	TAILQ_FOREACH(s_i, &kc->partial_slab_list, link)
		nr_unalloc_objs += s_i->num_total_obj - s_i->num_busy_obj;
	sza_printf(sza, "Nr unallocated in slab layer: %lu\n", nr_unalloc_objs);
	sza_printf(sza, "Nr allocated from slab layer: %d\n", kc->nr_cur_alloc);
	for (int i = 0; i < kc->hh.nr_hash_lists; i++) {
		int j = 0;

		if (BSD_LIST_EMPTY(&kc->alloc_hash[i]))
			empty_hash_chain++;
		BSD_LIST_FOREACH(bc_i, &kc->alloc_hash[i], link)
			j++;
		longest_hash_chain = MAX(longest_hash_chain, j);
	}
	sza_printf(sza,
		   "Nr hash %d, empty hash: %d, longest hash %d, loadlim %d\n",
	           kc->hh.nr_hash_lists, empty_hash_chain,
	           longest_hash_chain, kc->hh.load_limit);
	spin_unlock_irqsave(&kc->cache_lock);
	spin_lock_irqsave(&kc->depot.lock);
	sza_printf(sza, "Depot magsize: %d\n", kc->depot.magsize);
	sza_printf(sza, "Nr empty mags: %d\n", kc->depot.nr_empty);
	sza_printf(sza, "Nr non-empty mags: %d\n", kc->depot.nr_not_empty);
	spin_unlock_irqsave(&kc->depot.lock);
}

static struct sized_alloc *build_slab_stats(void)
{
	struct sized_alloc *sza;
	size_t alloc_amt = 0;
	struct kmem_cache *kc_i;

	qlock(&arenas_and_slabs_lock);
	TAILQ_FOREACH(kc_i, &all_kmem_caches, all_kmc_link)
		alloc_amt += 500;
	sza = sized_kzmalloc(alloc_amt, MEM_WAIT);
	TAILQ_FOREACH(kc_i, &all_kmem_caches, all_kmc_link)
		fetch_slab_stats(kc_i, sza);
	qunlock(&arenas_and_slabs_lock);
	return sza;
}

static struct sized_alloc *build_free(void)
{
	struct arena *a_i;
	struct sized_alloc *sza;
	size_t amt_total = 0;
	size_t amt_alloc = 0;

	sza = sized_kzmalloc(500, MEM_WAIT);
	qlock(&arenas_and_slabs_lock);
	TAILQ_FOREACH(a_i, &all_arenas, next) {
		if (!a_i->is_base)
			continue;
		amt_total += a_i->amt_total_segs;
		amt_alloc += a_i->amt_alloc_segs;
	}
	qunlock(&arenas_and_slabs_lock);
	sza_printf(sza, "Total Memory : %15llu\n", amt_total);
	sza_printf(sza, "Used Memory  : %15llu\n", amt_alloc);
	sza_printf(sza, "Free Memory  : %15llu\n", amt_total - amt_alloc);
	return sza;
}

#define KMEMSTAT_NAME			30
#define KMEMSTAT_OBJSIZE		8
#define KMEMSTAT_TOTAL			15
#define KMEMSTAT_ALLOCED		15
#define KMEMSTAT_NR_ALLOCS		12
#define KMEMSTAT_LINE_LN (8 + KMEMSTAT_NAME + KMEMSTAT_OBJSIZE + KMEMSTAT_TOTAL\
                          + KMEMSTAT_ALLOCED + KMEMSTAT_NR_ALLOCS)

const char kmemstat_fmt[]     = "%-*s: %c :%*llu:%*llu:%*llu:%*llu\n";
const char kmemstat_hdr_fmt[] = "%-*s:Typ:%*s:%*s:%*s:%*s\n";

static void fetch_arena_line(struct arena *arena, struct sized_alloc *sza,
                             int indent)
{
	for (int i = 0; i < indent; i++)
		sza_printf(sza, "    ");
	sza_printf(sza, kmemstat_fmt,
	           KMEMSTAT_NAME - indent * 4, arena->name,
	           'A',
	           KMEMSTAT_OBJSIZE, arena->quantum,
	           KMEMSTAT_TOTAL, arena->amt_total_segs,
	           KMEMSTAT_ALLOCED, arena->amt_alloc_segs,
	           KMEMSTAT_NR_ALLOCS, arena->nr_allocs_ever);
}

static void fetch_slab_line(struct kmem_cache *kc, struct sized_alloc *sza,
                            int indent)
{
	struct kmem_pcpu_cache *pcc;
	struct kmem_slab *s_i;
	size_t nr_unalloc_objs = 0;
	size_t nr_allocs_ever = 0;

	spin_lock_irqsave(&kc->cache_lock);
	TAILQ_FOREACH(s_i, &kc->empty_slab_list, link)
		nr_unalloc_objs += s_i->num_total_obj;
	TAILQ_FOREACH(s_i, &kc->partial_slab_list, link)
		nr_unalloc_objs += s_i->num_total_obj - s_i->num_busy_obj;
	nr_allocs_ever = kc->nr_direct_allocs_ever;
	spin_unlock_irqsave(&kc->cache_lock);
	/* Lockless peak at the pcpu state */
	for (int i = 0; i < kmc_nr_pcpu_caches(); i++) {
		pcc = &kc->pcpu_caches[i];
		nr_allocs_ever += pcc->nr_allocs_ever;
	}

	for (int i = 0; i < indent; i++)
		sza_printf(sza, "    ");
	sza_printf(sza, kmemstat_fmt,
	           KMEMSTAT_NAME - indent * 4, kc->name,
	           'S',
	           KMEMSTAT_OBJSIZE, kc->obj_size,
	           KMEMSTAT_TOTAL, kc->obj_size * (nr_unalloc_objs +
	                                           kc->nr_cur_alloc),
	           KMEMSTAT_ALLOCED, kc->obj_size * kc->nr_cur_alloc,
	           KMEMSTAT_NR_ALLOCS, nr_allocs_ever);
}

static void fetch_arena_and_kids(struct arena *arena, struct sized_alloc *sza,
                                 int indent)
{
	struct arena *a_i;
	struct kmem_cache *kc_i;

	fetch_arena_line(arena, sza, indent);
	TAILQ_FOREACH(a_i, &arena->__importing_arenas, import_link)
		fetch_arena_and_kids(a_i, sza, indent + 1);
	TAILQ_FOREACH(kc_i, &arena->__importing_slabs, import_link)
		fetch_slab_line(kc_i, sza, indent + 1);
}

static struct sized_alloc *build_kmemstat(void)
{
	struct arena *a_i;
	struct kmem_cache *kc_i;
	struct sized_alloc *sza;
	size_t alloc_amt = 100;

	qlock(&arenas_and_slabs_lock);
	TAILQ_FOREACH(a_i, &all_arenas, next)
		alloc_amt += 100;
	TAILQ_FOREACH(kc_i, &all_kmem_caches, all_kmc_link)
		alloc_amt += 100;
	sza = sized_kzmalloc(alloc_amt, MEM_WAIT);
	sza_printf(sza, kmemstat_hdr_fmt,
	           KMEMSTAT_NAME, "Arena/Slab Name",
	           KMEMSTAT_OBJSIZE, "Objsize",
	           KMEMSTAT_TOTAL, "Total Amt",
	           KMEMSTAT_ALLOCED, "Alloc Amt",
	           KMEMSTAT_NR_ALLOCS, "Allocs Ever");
	for (int i = 0; i < KMEMSTAT_LINE_LN; i++)
		sza_printf(sza, "-");
	sza_printf(sza, "\n");
	TAILQ_FOREACH(a_i, &all_arenas, next) {
		if (a_i->source)
			continue;
		fetch_arena_and_kids(a_i, sza, 0);
	}
	qunlock(&arenas_and_slabs_lock);
	return sza;
}

void kmemstat(void)
{
	struct sized_alloc *sza = build_kmemstat();

	printk("%s", sza->buf);
}

static struct chan *mem_open(struct chan *c, int omode)
{
	if (c->qid.type & QTDIR) {
		if (openmode(omode) != O_READ)
			error(EPERM, "Tried opening directory not read-only");
	}
	switch (c->qid.path) {
	case Qarena_stats:
		c->synth_buf = build_arena_stats();
		break;
	case Qslab_stats:
		c->synth_buf = build_slab_stats();
		break;
	case Qfree:
		c->synth_buf = build_free();
		break;
	case Qkmemstat:
		c->synth_buf = build_kmemstat();
		break;
	}
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void mem_close(struct chan *c)
{
	if (!(c->flag & COPEN))
		return;
	switch (c->qid.path) {
	case Qarena_stats:
	case Qslab_stats:
	case Qfree:
	case Qkmemstat:
		kfree(c->synth_buf);
		c->synth_buf = NULL;
		break;
	}
}

static size_t slab_trace_read(struct chan *c, void *ubuf, size_t n,
                              off64_t offset)
{
	size_t ret = 0;

	qlock(&arenas_and_slabs_lock);
	if (slab_trace_data)
		ret = readstr(offset, ubuf, n, slab_trace_data->buf);
	qunlock(&arenas_and_slabs_lock);
	return ret;
}

static size_t mem_read(struct chan *c, void *ubuf, size_t n, off64_t offset)
{
	struct sized_alloc *sza;

	switch (c->qid.path) {
	case Qdir:
		return devdirread(c, ubuf, n, mem_dir, ARRAY_SIZE(mem_dir),
						  devgen);
	case Qarena_stats:
	case Qslab_stats:
	case Qfree:
	case Qkmemstat:
		sza = c->synth_buf;
		return readstr(offset, ubuf, n, sza->buf);
	case Qslab_trace:
		return slab_trace_read(c, ubuf, n, offset);
	default:
		panic("Bad Qid %p!", c->qid.path);
	}
	return -1;
}

/* start, then stop, then print, then read to get the trace */
#define SLAB_TRACE_USAGE "start|stop|print|reset SLAB_NAME"

static void slab_trace_cmd(struct chan *c, struct cmdbuf *cb)
{
	ERRSTACK(1);
	struct sized_alloc *sza, *old_sza;
	struct kmem_cache *kc;

	if (cb->nf < 2)
		error(EFAIL, SLAB_TRACE_USAGE);

	qlock(&arenas_and_slabs_lock);
	if (waserror()) {
		qunlock(&arenas_and_slabs_lock);
		nexterror();
	}
	TAILQ_FOREACH(kc, &all_kmem_caches, all_kmc_link)
		if (!strcmp(kc->name, cb->f[1]))
			break;
	if (!kc)
		error(ENOENT, "No such slab %s", cb->f[1]);
	/* Note that the only time we have a real sza is when printing.
	 * Otherwise, it's NULL.  We still want this to be the chan's sza, since
	 * the reader should get nothing back until they ask for a print. */
	sza = NULL;
	if (!strcmp(cb->f[0], "start")) {
		if (kmem_trace_start(kc))
			error(EFAIL, "Unable to trace slab %s", kc->name);
	} else if (!strcmp(cb->f[0], "stop")) {
		kmem_trace_stop(kc);
	} else if (!strcmp(cb->f[0], "print")) {
		sza = kmem_trace_print(kc);
	} else if (!strcmp(cb->f[0], "reset")) {
		kmem_trace_reset(kc);
	} else {
		error(EFAIL, SLAB_TRACE_USAGE);
	}
	old_sza = slab_trace_data;
	slab_trace_data = sza;
	qunlock(&arenas_and_slabs_lock);
	poperror();
	kfree(old_sza);
}

static size_t mem_write(struct chan *c, void *ubuf, size_t n, off64_t unused)
{
	ERRSTACK(1);
	struct cmdbuf *cb = parsecmd(ubuf, n);

	if (waserror()) {
		kfree(cb);
		nexterror();
	}
	switch (c->qid.path) {
	case Qslab_trace:
		slab_trace_cmd(c, cb);
		break;
	default:
		error(EFAIL, "Unable to write to %s", devname());
	}
	kfree(cb);
	poperror();
	return n;
}

struct dev mem_devtab __devtab = {
	.name = "mem",
	.reset = devreset,
	.init = devinit,
	.shutdown = devshutdown,
	.attach = mem_attach,
	.walk = mem_walk,
	.stat = mem_stat,
	.open = mem_open,
	.create = devcreate,
	.close = mem_close,
	.read = mem_read,
	.bread = devbread,
	.write = mem_write,
	.bwrite = devbwrite,
	.remove = devremove,
	.wstat = devwstat,
	.power = devpower,
	.chaninfo = devchaninfo,
};
