// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2002 Richard Henderson
 * Copyright (C) 2001 Rusty Russell, 2002, 2010 Rusty Russell IBM.
 * Copyright (C) 2023 Luis Chamberlain <mcgrof@kernel.org>
 * Copyright (C) 2024 Mike Rapoport IBM.
 */

#define pr_fmt(fmt) "execmem: " fmt

#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>
#include <linux/execmem.h>
#include <linux/maple_tree.h>
#include <linux/set_memory.h>
#include <linux/moduleloader.h>
#include <linux/text-patching.h>

#include <asm/tlbflush.h>

#include "internal.h"

static struct execmem_info *execmem_info __ro_after_init;
static struct execmem_info default_execmem_info __ro_after_init;

#ifdef CONFIG_MMU

/*
 * execmem_vmalloc - Allocates physically backed virtual memory for
 * executable code or related control data within specific architectural
 * address ranges. It attempts allocation in a primary address range
 * first, automatically falls back to an alternate range if space is
 * exhausted, and handles KASAN shadow memory setup when memory
 * sanitization is enabled.
 */
static void *execmem_vmalloc(struct execmem_range *range, size_t size,
			     pgprot_t pgprot, unsigned long vm_flags)
{
	bool kasan = range->flags & EXECMEM_KASAN_SHADOW;
	gfp_t gfp_flags = GFP_KERNEL | __GFP_NOWARN;
	unsigned int align = range->alignment;
	unsigned long start = range->start;
	unsigned long end = range->end;
	void *p;

	if (kasan)
		vm_flags |= VM_DEFER_KMEMLEAK;

	p = __vmalloc_node_range(size, align, start, end, gfp_flags,
				 pgprot, vm_flags, NUMA_NO_NODE,
				 __builtin_return_address(0));
	if (!p && range->fallback_start) {
		start = range->fallback_start;
		end = range->fallback_end;
		p = __vmalloc_node_range(size, align, start, end, gfp_flags,
					 pgprot, vm_flags, NUMA_NO_NODE,
					 __builtin_return_address(0));
	}

	if (!p) {
		pr_warn_ratelimited("unable to allocate memory\n");
		return NULL;
	}

	if (kasan && (kasan_alloc_module_shadow(p, size, GFP_KERNEL) < 0)) {
		vfree(p);
		return NULL;
	}

	return p;
}


/*
 * execmem_vmap - Reserves a contiguous virtual memory address region
 * (vm_struct) earmarked specifically for module data without immediately
 * allocating or mapping physical pages. It queries for space within the
 * primary module data range and tries a secondary fallback range if the
 * initial allocation fails.
 */
struct vm_struct *execmem_vmap(size_t size)
{
	struct execmem_range *range = &execmem_info->ranges[EXECMEM_MODULE_DATA];
	struct vm_struct *area;

	area = __get_vm_area_node(size, range->alignment, PAGE_SHIFT, VM_ALLOC,
				  range->start, range->end, NUMA_NO_NODE,
				  GFP_KERNEL, __builtin_return_address(0));
	if (!area && range->fallback_start)
		area = __get_vm_area_node(size, range->alignment, PAGE_SHIFT, VM_ALLOC,
					  range->fallback_start, range->fallback_end,
					  NUMA_NO_NODE, GFP_KERNEL, __builtin_return_address(0));

	return area;
}
#else
static void *execmem_vmalloc(struct execmem_range *range, size_t size,
			     pgprot_t pgprot, unsigned long vm_flags)
{
	return vmalloc(size);
}
#endif /* CONFIG_MMU */

#ifdef CONFIG_ARCH_HAS_EXECMEM_ROX
struct execmem_cache {
	struct mutex mutex;
	struct maple_tree busy_areas;
	struct maple_tree free_areas;
	unsigned int pending_free_cnt;	/* protected by mutex */
};

/* delay to schedule asynchronous free if fast path free fails */
#define FREE_DELAY	(msecs_to_jiffies(10))

/* mark entries in busy_areas that should be freed asynchronously */
#define PENDING_FREE_MASK	(1 << (PAGE_SHIFT - 1))

static struct execmem_cache execmem_cache = {
	.mutex = __MUTEX_INITIALIZER(execmem_cache.mutex),
	.busy_areas = MTREE_INIT_EXT(busy_areas, MT_FLAGS_LOCK_EXTERN,
				     execmem_cache.mutex),
	.free_areas = MTREE_INIT_EXT(free_areas, MT_FLAGS_LOCK_EXTERN,
				     execmem_cache.mutex),
};


/*
 * mas_range_len - Calculates the total length of the memory range tracked
 * by a Maple Tree state cursor (ma_state). It computes the span size as
 * last - index + 1 to return how many contiguous slots or indices the
 * current range covers.
 */
static inline unsigned long mas_range_len(struct ma_state *mas)
{
	return mas->last - mas->index + 1;
}


/*
 * execmem_set_direct_map_valid - Enables or disables valid direct-map
 * (kernel 1:1 physical memory mapping) access for the pages within a vm_struct
 * allocation. It iterates through the virtual allocation's pages in order-sized
 * blocks without triggering an immediate TLB flush, and automatically rolls
 * back modified pages to their original state if an error occurs midway.
 */
static int execmem_set_direct_map_valid(struct vm_struct *vm, bool valid)
{
	unsigned int nr = (1 << get_vm_area_page_order(vm));
	unsigned int updated = 0;
	int err = 0;

	for (int i = 0; i < vm->nr_pages; i += nr) {
		err = set_direct_map_valid_noflush(vm->pages[i], nr, valid);
		if (err)
			goto err_restore;
		updated += nr;
	}

	return 0;

err_restore:
	for (int i = 0; i < updated; i += nr)
		set_direct_map_valid_noflush(vm->pages[i], nr, !valid);

	return err;
}


/*
 * execmem_force_rw - Transitions an executable memory buffer into a Read-Write
 * Non-Executable (RW/NX) state so it can be safely modified. It strips
 * execution permissions first (set_memory_nx) before granting write permissions
 * (set_memory_rw) to enforce strict W^X kernel safety policies.
 */
static int execmem_force_rw(void *ptr, size_t size)
{
	unsigned int nr = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned long addr = (unsigned long)ptr;
	int ret;

	ret = set_memory_nx(addr, nr);
	if (ret)
		return ret;

	return set_memory_rw(addr, nr);
}


/*
 * execmem_restore_rox - Restores memory permissions back to ROX after code
 * loading, patching, or updates are complete. It converts the target byte range
 * into page counts and applies set_memory_rox() to lock the memory against
 * write access while allowing execution.
 */
int execmem_restore_rox(void *ptr, size_t size)
{
	unsigned int nr = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned long addr = (unsigned long)ptr;

	return set_memory_rox(addr, nr);
}


/*
 * execmem_cache_clean - Deferred workqueue handler that scans the free-area
 * Maple Tree for large, PMD-aligned memory blocks. When a fully PMD-sized free
 * range is found, it restores direct-map page validity, unlinks the entry from
 * the cache, and returns physical memory to the system via vfree().
 */
static void execmem_cache_clean(struct work_struct *work)
{
	struct maple_tree *free_areas = &execmem_cache.free_areas;
	struct mutex *mutex = &execmem_cache.mutex;
	MA_STATE(mas, free_areas, 0, ULONG_MAX);
	void *area;

	mutex_lock(mutex);
	mas_for_each(&mas, area, ULONG_MAX) {
		size_t size = mas_range_len(&mas);

		if (IS_ALIGNED(size, PMD_SIZE) &&
		    IS_ALIGNED(mas.index, PMD_SIZE)) {
			struct vm_struct *vm = find_vm_area(area);

			execmem_set_direct_map_valid(vm, true);
			mas_store_gfp(&mas, NULL, GFP_KERNEL);
			vfree(area);
		}
	}
	mutex_unlock(mutex);
}


/*
 * execmem_cache_clean - Calculates the total length of the memory range tracked
 * by a Maple Tree state cursor (ma_state). It computes the span size as
 * last - index + 1 to return how many contiguous slots or indices the
 * current range covers.
 */
static DECLARE_WORK(execmem_cache_clean_work, execmem_cache_clean);


/*
 * execmem_cache_add_locked - Inserts a freed memory block into the free_areas
 * Maple Tree while assuming the cache mutex is already held. It checks for
 * adjacent free blocks immediately before and after the incoming block,
 * automatically coalescing contiguous ranges into a single larger block to
 * reduce fragmentation.
 */
static int execmem_cache_add_locked(void *ptr, size_t size, gfp_t gfp_mask)
{
	struct maple_tree *free_areas = &execmem_cache.free_areas;
	unsigned long addr = (unsigned long)ptr;
	MA_STATE(mas, free_areas, addr - 1, addr + 1);
	unsigned long lower, upper;
	void *area = NULL;

	lower = addr;
	upper = addr + size - 1;

	area = mas_walk(&mas);
	if (area && mas.last == addr - 1)
		lower = mas.index;

	area = mas_next(&mas, ULONG_MAX);
	if (area && mas.index == addr + size)
		upper = mas.last;

	mas_set_range(&mas, lower, upper);
	return mas_store_gfp(&mas, (void *)lower, gfp_mask);
}


/*
 * execmem_cache_add - A thread-safe wrapper around execmem_cache_add_locked().
 * It utilizes RAII-style mutex management (guard(mutex)) to automatically
 * acquire and release execmem_cache.mutex while adding memory to the free cache.
 */
static int execmem_cache_add(void *ptr, size_t size, gfp_t gfp_mask)
{
	guard(mutex)(&execmem_cache.mutex);

	return execmem_cache_add_locked(ptr, size, gfp_mask);
}


/*
 * within_range - Validates whether a candidate memory block
 * (starting at mas->index with size size) fits within allowed address
 * boundaries. It checks if the block lies entirely inside either the primary
 * memory region (start to end) or the designated secondary region
 * (fallback_start to fallback_end).
 */
static bool within_range(struct execmem_range *range, struct ma_state *mas,
			 size_t size)
{
	unsigned long addr = mas->index;

	if (addr >= range->start && addr + size < range->end)
		return true;

	if (range->fallback_start &&
	    addr >= range->fallback_start && addr + size < range->fallback_end)
		return true;

	return false;
}


/*
 * __execmem_cache_alloc - Searches the free_areas Maple Tree for an available
 * memory block within the requested range that satisfies the requested size.
 * If found, it updates busy_areas to track the allocation, splits off any
 * remaining unused trailing space back into free_areas, and returns the
 * memory address.
 */
static void *__execmem_cache_alloc(struct execmem_range *range, size_t size)
{
	struct maple_tree *free_areas = &execmem_cache.free_areas;
	struct maple_tree *busy_areas = &execmem_cache.busy_areas;
	MA_STATE(mas_free, free_areas, 0, ULONG_MAX);
	MA_STATE(mas_busy, busy_areas, 0, ULONG_MAX);
	struct mutex *mutex = &execmem_cache.mutex;
	unsigned long addr, last, area_size = 0;
	void *area, *ptr = NULL;
	int err;

	mutex_lock(mutex);
	mas_for_each(&mas_free, area, ULONG_MAX) {
		area_size = mas_range_len(&mas_free);

		if (area_size >= size && within_range(range, &mas_free, size))
			break;
	}

	if (area_size < size)
		goto out_unlock;

	addr = mas_free.index;
	last = mas_free.last;

	/* insert allocated size to busy_areas at range [addr, addr + size) */
	mas_set_range(&mas_busy, addr, addr + size - 1);
	err = mas_store_gfp(&mas_busy, (void *)addr, GFP_KERNEL);
	if (err)
		goto out_unlock;

	mas_store_gfp(&mas_free, NULL, GFP_KERNEL);
	if (area_size > size) {
		void *ptr = (void *)(addr + size);

		/*
		 * re-insert remaining free size to free_areas at range
		 * [addr + size, last]
		 */
		mas_set_range(&mas_free, addr + size, last);
		err = mas_store_gfp(&mas_free, ptr, GFP_KERNEL);
		if (err) {
			mas_store_gfp(&mas_busy, NULL, GFP_KERNEL);
			goto out_unlock;
		}
	}
	ptr = (void *)addr;

out_unlock:
	mutex_unlock(mutex);
	return ptr;
}


/*
 * execmem_cache_populate - Allocates a new underlying memory region
 * (preferably rounded up to a large PMD_SIZE boundary) using execmem_vmalloc()
 * to refill the cache. It poisons the memory with trapping instructions for
 * security, transitions the memory to Read-Only Executable (ROX), and adds it
 * to free_areas.
 */
static int execmem_cache_populate(struct execmem_range *range, size_t size)
{
	unsigned long vm_flags = VM_ALLOW_HUGE_VMAP;
	struct vm_struct *vm;
	size_t alloc_size;
	int err = -ENOMEM;
	void *p;

	alloc_size = round_up(size, PMD_SIZE);
	p = execmem_vmalloc(range, alloc_size, PAGE_KERNEL, vm_flags);
	if (!p) {
		alloc_size = size;
		p = execmem_vmalloc(range, alloc_size, PAGE_KERNEL, vm_flags);
	}

	if (!p)
		return err;

	vm = find_vm_area(p);
	if (!vm)
		goto err_free_mem;

	/* fill memory with instructions that will trap */
	execmem_fill_trapping_insns(p, alloc_size);

	err = set_memory_rox((unsigned long)p, vm->nr_pages);
	if (err)
		goto err_free_mem;

	err = execmem_cache_add(p, alloc_size, GFP_KERNEL);
	if (err)
		goto err_reset_direct_map;

	return 0;

err_reset_direct_map:
	execmem_set_direct_map_valid(vm, true);
err_free_mem:
	vfree(p);
	return err;
}


/*
 * execmem_cache_alloc - It first attempts a fast path allocation via
 * __execmem_cache_alloc(); if the cache lacks available space, it trigger
 * execmem_cache_populate() to pull fresh memory into the pool before retrying
 * the allocation.
 */
static void *execmem_cache_alloc(struct execmem_range *range, size_t size)
{
	void *p;
	int err;

	p = __execmem_cache_alloc(range, size);
	if (p)
		return p;

	err = execmem_cache_populate(range, size);
	if (err)
		return NULL;

	return __execmem_cache_alloc(range, size);
}

static inline bool is_pending_free(void *ptr)
{
	return ((unsigned long)ptr & PENDING_FREE_MASK);
}

static inline void *pending_free_set(void *ptr)
{
	return (void *)((unsigned long)ptr | PENDING_FREE_MASK);
}

static inline void *pending_free_clear(void *ptr)
{
	return (void *)((unsigned long)ptr & ~PENDING_FREE_MASK);
}


/*
 * __execmem_cache_free - Performs low-level cleanup for a single memory region.
 * It temporarily switches the block to Read-Write to overwrite it with trapping
 * instructions for security, restores ROX permissions, transfers the region
 * back to free_areas, and removes it from busy_areas.
 */
static int __execmem_cache_free(struct ma_state *mas, void *ptr, gfp_t gfp_mask)
{
	size_t size = mas_range_len(mas);
	int err;

	err = execmem_force_rw(ptr, size);
	if (err)
		return err;

	execmem_fill_trapping_insns(ptr, size);
	execmem_restore_rox(ptr, size);

	err = execmem_cache_add_locked(ptr, size, gfp_mask);
	if (err)
		return err;

	mas_store_gfp(mas, NULL, gfp_mask);
	return 0;
}

static void execmem_cache_free_slow(struct work_struct *work);
static DECLARE_DELAYED_WORK(execmem_cache_free_work, execmem_cache_free_slow);


/*
 * execmem_cache_free_slow - A delayed work kernel structure and its background
 * handler used for asynchronous cleanup. It periodically scans busy_areas for
 * blocks previously flagged as "pending free," retries __execmem_cache_free(),
 * and either reschedules itself if pending items remain or hands off execution
 * to execmem_cache_clean_work once all items are cleared.
 */
static void execmem_cache_free_slow(struct work_struct *work)
{
	struct maple_tree *busy_areas = &execmem_cache.busy_areas;
	MA_STATE(mas, busy_areas, 0, ULONG_MAX);
	void *area;

	guard(mutex)(&execmem_cache.mutex);

	if (!execmem_cache.pending_free_cnt)
		return;

	mas_for_each(&mas, area, ULONG_MAX) {
		if (!is_pending_free(area))
			continue;

		area = pending_free_clear(area);
		if (__execmem_cache_free(&mas, area, GFP_KERNEL))
			continue;

		execmem_cache.pending_free_cnt--;
	}

	if (execmem_cache.pending_free_cnt)
		schedule_delayed_work(&execmem_cache_free_work, FREE_DELAY);
	else
		schedule_work(&execmem_cache_clean_work);
}


/*
 * execmem_cache_free - The primary entry point to free an executable memory
 * cache. It looks up the target address in busy_areas and attempts an
 * immediate, synchronous cleanup; if the fast path fails (e.g., tree allocation
 * constraints under memory pressure), it flags the block as pending and
 * schedules execmem_cache_free_slow() to resolve it asynchronously.
 */
static bool execmem_cache_free(void *ptr)
{
	struct maple_tree *busy_areas = &execmem_cache.busy_areas;
	unsigned long addr = (unsigned long)ptr;
	MA_STATE(mas, busy_areas, addr, addr);
	void *area;
	int err;

	guard(mutex)(&execmem_cache.mutex);

	area = mas_walk(&mas);
	if (!area)
		return false;

	err = __execmem_cache_free(&mas, area, GFP_KERNEL | __GFP_NORETRY);
	if (err) {
		/*
		 * mas points to exact slot we've got the area from, nothing
		 * else can modify the tree because of the mutex, so there
		 * won't be any allocations in mas_store_gfp() and it will just
		 * change the pointer.
		 */
		area = pending_free_set(area);
		mas_store_gfp(&mas, area, GFP_KERNEL);
		execmem_cache.pending_free_cnt++;
		schedule_delayed_work(&execmem_cache_free_work, FREE_DELAY);
		return true;
	}

	schedule_work(&execmem_cache_clean_work);

	return true;
}

#else /* CONFIG_ARCH_HAS_EXECMEM_ROX */
/*
 * when ROX cache is not used the permissions defined by architectures for
 * execmem ranges that are updated before use (e.g. EXECMEM_MODULE_TEXT) must
 * be writable anyway
 */
static inline int execmem_force_rw(void *ptr, size_t size)
{
	return 0;
}

static void *execmem_cache_alloc(struct execmem_range *range, size_t size)
{
	return NULL;
}

static bool execmem_cache_free(void *ptr)
{
	return false;
}
#endif /* CONFIG_ARCH_HAS_EXECMEM_ROX */


/*
 * execmem_alloc - High-level public allocator for executable memory. It aligns
 * the requested size to page boundaries and either provisions memory from the
 * ROX caching layer via execmem_cache_alloc() or directly allocates standard
 * vmalloc pages with specified page protection rules, stripping KASAN address
 * tags before returning the pointer.
 */
void *execmem_alloc(enum execmem_type type, size_t size)
{
	struct execmem_range *range = &execmem_info->ranges[type];
	bool use_cache = range->flags & EXECMEM_ROX_CACHE;
	unsigned long vm_flags = VM_FLUSH_RESET_PERMS;
	pgprot_t pgprot = range->pgprot;
	void *p = NULL;

	size = PAGE_ALIGN(size);

	if (use_cache)
		p = execmem_cache_alloc(range, size);
	else
		p = execmem_vmalloc(range, size, pgprot, vm_flags);

	return kasan_reset_tag(p);
}


/*
 * execmem_alloc_rw - Wrapper that allocates executable-capable memory and
 * immediately prepares it for writing. It uses __free(execmem) to prevent leaks
 * on failure, calls execmem_alloc(), and then calls execmem_force_rw() to
 * temporarily switch permissions to Read-Write Non-Executable (RW/NX) before
 * handing back the pointer.
 */
void *execmem_alloc_rw(enum execmem_type type, size_t size)
{
	void *p __free(execmem) = execmem_alloc(type, size);
	int err;

	if (!p)
		return NULL;

	err = execmem_force_rw(p, size);
	if (err)
		return NULL;

	return no_free_ptr(p);
}


/*
 * execmem_free - High-level public cleanup function for freeing executable
 * memory regions. It enforces a safety check via WARN_ON(in_interrupt()), then
 * attempts to return the buffer to execmem_cache; if the address was not
 * managed by the cache, it falls back to standard vfree().
 */
void execmem_free(void *ptr)
{
	/*
	 * This memory may be RO, and freeing RO memory in an interrupt is not
	 * supported by vmalloc.
	 */
	WARN_ON(in_interrupt());

	if (!execmem_cache_free(ptr))
		vfree(ptr);
}

bool execmem_is_rox(enum execmem_type type)
{
	return !!(execmem_info->ranges[type].flags & EXECMEM_ROX_CACHE);
}


/*
 * execmem_validate - Validates the configuration parameters for executable
 * memory ranges. It ensures required settings like alignment, address bounds,
 * and page protections are set for the default range, and automatically strips
 * he EXECMEM_ROX_CACHE flag if the host architecture lacks support for ROX caching.
 */
static bool execmem_validate(struct execmem_info *info)
{
	struct execmem_range *r = &info->ranges[EXECMEM_DEFAULT];

	if (!r->alignment || !r->start || !r->end || !pgprot_val(r->pgprot)) {
		pr_crit("Invalid parameters for execmem allocator, module loading will fail");
		return false;
	}

	if (!IS_ENABLED(CONFIG_ARCH_HAS_EXECMEM_ROX)) {
		for (int i = EXECMEM_DEFAULT; i < EXECMEM_TYPE_MAX; i++) {
			r = &info->ranges[i];

			if (r->flags & EXECMEM_ROX_CACHE) {
				pr_warn_once("ROX cache is not supported\n");
				r->flags &= ~EXECMEM_ROX_CACHE;
			}
		}
	}

	return true;
}


/*
 * execmem_init_missing - Populates unconfigured memory range types by
 * inheriting baseline settings from the EXECMEM_DEFAULT region template. It
 * copies over address boundaries, alignments, flags, and fallback limits, while
 * ensuring data-only regions (like EXECMEM_MODULE_DATA) receive non-executable
 * PAGE_KERNEL page protections.
 */
static void execmem_init_missing(struct execmem_info *info)
{
	struct execmem_range *default_range = &info->ranges[EXECMEM_DEFAULT];

	for (int i = EXECMEM_DEFAULT + 1; i < EXECMEM_TYPE_MAX; i++) {
		struct execmem_range *r = &info->ranges[i];

		if (!r->start) {
			if (i == EXECMEM_MODULE_DATA)
				r->pgprot = PAGE_KERNEL;
			else
				r->pgprot = default_range->pgprot;
			r->alignment = default_range->alignment;
			r->start = default_range->start;
			r->end = default_range->end;
			r->flags = default_range->flags;
			r->fallback_start = default_range->fallback_start;
			r->fallback_end = default_range->fallback_end;
		}
	}
}

struct execmem_info * __weak execmem_arch_setup(void)
{
	return NULL;
}


/*
 * __execmem_init - Initialization handler for the execmem subsystem. It
 * requests architecture-specific memory layouts via execmem_arch_setup()
 * (defaulting to standard vmalloc bounds and PAGE_KERNEL_EXEC permissions if
 * none are provided), runs validation, fills missing range defaults, and
 * commits the global execmem_info pointer.
 */
static void __init __execmem_init(void)
{
	struct execmem_info *info = execmem_arch_setup();

	if (!info) {
		info = execmem_info = &default_execmem_info;
		info->ranges[EXECMEM_DEFAULT].start = VMALLOC_START;
		info->ranges[EXECMEM_DEFAULT].end = VMALLOC_END;
		info->ranges[EXECMEM_DEFAULT].pgprot = PAGE_KERNEL_EXEC;
		info->ranges[EXECMEM_DEFAULT].alignment = 1;
	}

	if (!execmem_validate(info))
		return;

	execmem_init_missing(info);

	execmem_info = info;
}

#ifdef CONFIG_ARCH_WANTS_EXECMEM_LATE
static int __init execmem_late_init(void)
{
	__execmem_init();
	return 0;
}
core_initcall(execmem_late_init);
#else
void __init execmem_init(void)
{
	__execmem_init();
}
#endif
