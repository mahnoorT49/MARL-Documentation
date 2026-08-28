/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MM_SLAB_H
#define MM_SLAB_H

#include <linux/reciprocal_div.h>
#include <linux/list_lru.h>
#include <linux/local_lock.h>
#include <linux/random.h>
#include <linux/kobject.h>
#include <linux/sched/mm.h>
#include <linux/memcontrol.h>
#include <linux/kfence.h>
#include <linux/kasan.h>

/*
 * Internal slab definitions
 */

#ifdef CONFIG_64BIT
# ifdef system_has_cmpxchg128
# define system_has_freelist_aba()	system_has_cmpxchg128()
# define try_cmpxchg_freelist		try_cmpxchg128
# endif
typedef u128 freelist_full_t;
#else /* CONFIG_64BIT */
# ifdef system_has_cmpxchg64
# define system_has_freelist_aba()	system_has_cmpxchg64()
# define try_cmpxchg_freelist		try_cmpxchg64
# endif
typedef u64 freelist_full_t;
#endif /* CONFIG_64BIT */

#if defined(system_has_freelist_aba) && !defined(CONFIG_HAVE_ALIGNED_STRUCT_PAGE)
#undef system_has_freelist_aba
#endif

/**
 * Freelist pointer and counter to cmpxchg together, avoids the typical ABA
 * problems with cmpxchg of just a pointer.
 * 
 * 
 * struct Description: This structure combines a freelist pointer and counters into a single atomically-accessible unit. It contains the freelist pointer, and counters for inuse objects, total objects, frozen state, and stride. It is used to enable ABA-free cmpxchg operations on freelists, preventing the typical ABA problem when updating freelist pointers.
 */
struct freelist_counters {
	union {
		struct {
			void *freelist;
			union {
				unsigned long counters;
				struct {
					unsigned inuse:16;
					unsigned objects:15;
					/*
					 * If slab debugging is enabled then the
					 * frozen bit can be reused to indicate
					 * that the slab was corrupted
					 */
					unsigned frozen:1;
#ifdef CONFIG_64BIT
					/*
					 * Some optimizations use free bits in 'counters' field
					 * to save memory. In case ->stride field is not available,
					 * such optimizations are disabled.
					 */
					unsigned int stride;
#endif
				};
			};
		};
#ifdef system_has_freelist_aba
		freelist_full_t freelist_counters;
#endif
	};
};

/** Reuses the bits in struct page 
 * 
 * 
 * struct Description: This structure represents a slab page in the SLUB allocator. It contains flags, pointer to the kmem_cache, list linkage, freelist counters, page type, refcount, and optional object extensions. It overlays with struct page and is used to manage individual slabs (groups of objects) within a kmem_cache.
*/
struct slab {
	memdesc_flags_t flags;

	struct kmem_cache *slab_cache;
	union {
		struct {
			struct list_head slab_list;
			/* Double-word boundary */
			struct freelist_counters;
		};
		struct rcu_head rcu_head;
	};

	unsigned int __page_type;
	atomic_t __page_refcount;
#ifdef CONFIG_SLAB_OBJ_EXT
	unsigned long obj_exts;
#endif
};

#define SLAB_MATCH(pg, sl)						\
	static_assert(offsetof(struct page, pg) == offsetof(struct slab, sl))
SLAB_MATCH(flags, flags);
SLAB_MATCH(compound_head, slab_cache);	/* Ensure bit 0 is clear */
SLAB_MATCH(_refcount, __page_refcount);
#ifdef CONFIG_MEMCG
SLAB_MATCH(memcg_data, obj_exts);
#elif defined(CONFIG_SLAB_OBJ_EXT)
SLAB_MATCH(_unused_slab_obj_exts, obj_exts);
#endif
#undef SLAB_MATCH
static_assert(sizeof(struct slab) <= sizeof(struct page));
#if defined(system_has_freelist_aba)
static_assert(IS_ALIGNED(offsetof(struct slab, freelist), sizeof(struct freelist_counters)));
#endif

/**
 * slab_folio - The folio allocated for a slab
 * @s: The slab.
 *
 * Slabs are allocated as folios that contain the individual objects and are
 * using some fields in the first struct page of the folio - those fields are
 * now accessed by struct slab. It is occasionally necessary to convert back to
 * a folio in order to communicate with the rest of the mm.  Please use this
 * helper function instead of casting yourself, as the implementation may change
 * in the future.
 */
#define slab_folio(s)		(_Generic((s),				\
	const struct slab *:	(const struct folio *)s,		\
	struct slab *:		(struct folio *)s))

/**
 * page_slab - Converts from struct page to its slab.
 * @page: A page which may or may not belong to a slab.
 *
 * Return: The slab which contains this page or NULL if the page does
 * not belong to a slab.  This includes pages returned from large kmalloc.
 * 
 * 
 * Function Description: Converts a struct page pointer to a struct slab pointer. It handles compound pages by following the compound_head pointer and verifies the page type is PGTY_slab. Returns the slab pointer or NULL if the page does not belong to a slab.
 */
static inline struct slab *page_slab(const struct page *page)
{
	unsigned long head;

	head = READ_ONCE(page->compound_head);
	if (head & 1)
		page = (struct page *)(head - 1);
	if (data_race(page->page_type >> 24) != PGTY_slab)
		page = NULL;

	return (struct slab *)page;
}

/**
 * slab_page - The first struct page allocated for a slab
 * @s: The slab.
 *
 * A convenience wrapper for converting slab to the first struct page of the
 * underlying folio, to communicate with code not yet converted to folio or
 * struct slab.
 */
#define slab_page(s) folio_page(slab_folio(s), 0)

/**
 * Function Description: Returns the starting virtual address of a slab's memory. It calls folio_address() on the slab's underlying folio. Used to get the base address of a slab.
 */
static inline void *slab_address(const struct slab *slab)
{
	return folio_address(slab_folio(slab));
}

/**
 * Function Description: Returns the NUMA node ID of a slab. It reads the node information from the slab's flags using memdesc_nid(). Used to determine which node a slab resides on.
 */
static inline int slab_nid(const struct slab *slab)
{
	return memdesc_nid(slab->flags);
}

/**
 * Function Description: Returns the pg_data_t structure for the NUMA node of a slab. It calls NODE_DATA() with the slab's node ID. Used to get the node's page data.
 */
static inline pg_data_t *slab_pgdat(const struct slab *slab)
{
	return NODE_DATA(slab_nid(slab));
}

/**
 * Function Description: Converts a virtual address to a struct slab pointer. It calls virt_to_page() and then page_slab(). Used to find the slab containing a given object address.
 */
static inline struct slab *virt_to_slab(const void *addr)
{
	return page_slab(virt_to_page(addr));
}

/**
 * Function Description: Returns the page order of a slab. It calls folio_order() on the slab's underlying folio. Used to determine the size of the slab in pages.
 */
static inline int slab_order(const struct slab *slab)
{
	return folio_order(slab_folio(slab));
}

/**
 * Function Description: Returns the total size of a slab in bytes. It calculates PAGE_SIZE << slab_order(slab). Used to determine how much memory a slab occupies.
 */
static inline size_t slab_size(const struct slab *slab)
{
	return PAGE_SIZE << slab_order(slab);
}

/**
 * Word size structure that can be atomically updated or read and that
 * contains both the order and the number of objects that a slab of the
 * given order would contain.
 * 
 * 
 * struct Description: This structure stores both the page order and the number of objects that a slab of that order would contain. It is used by the SLUB allocator to determine slab size and object count for different allocation sizes.
 */
struct kmem_cache_order_objects {
	unsigned int x;
};

/**
 * Slab cache management.
 * 
 * 
 * struct Description: This is the main structure representing a slab cache in the SLUB allocator. It contains per-CPU sheaves, flags, min_partial count, object sizes, free pointer offset, sheaf capacity, order and min objects, allocation flags, refcount, constructor, alignment, redzone padding, name, and per-node data. Each cache manages a set of slabs of the same object size.
 */
struct kmem_cache {
	struct slub_percpu_sheaves __percpu *cpu_sheaves;
	/** Used for retrieving partial slabs, etc. */
	slab_flags_t flags;
	unsigned long min_partial;
	unsigned int size;		/** Object size including metadata */
	unsigned int object_size;	/** Object size without metadata */
	struct reciprocal_value reciprocal_size;
	unsigned int offset;		/** Free pointer offset */
	unsigned int sheaf_capacity;
	struct kmem_cache_order_objects oo;

	/** Allocation and freeing of slabs */
	struct kmem_cache_order_objects min;
	gfp_t allocflags;		/** gfp flags to use on each alloc */
	int refcount;			/** Refcount for slab cache destroy */
	void (*ctor)(void *object);	/** Object constructor */
	unsigned int inuse;		/** Offset to metadata */
	unsigned int align;		/** Alignment */
	unsigned int red_left_pad;	/** Left redzone padding size */
	const char *name;		/** Name (only for display!) */
	struct list_head list;		/** List of slab caches */
#ifdef CONFIG_SYSFS
	struct kobject kobj;		/** For sysfs */
#endif
#ifdef CONFIG_SLAB_FREELIST_HARDENED
	unsigned long random;
#endif

#ifdef CONFIG_NUMA
	/**
	 * Defragmentation by allocating from a remote node.
	 */
	unsigned int remote_node_defrag_ratio;
#endif

#ifdef CONFIG_SLAB_FREELIST_RANDOM
	unsigned int *random_seq;
#endif

#ifdef CONFIG_KASAN_GENERIC
	struct kasan_cache kasan_info;
#endif

#ifdef CONFIG_HARDENED_USERCOPY
	unsigned int useroffset;	/** Usercopy region offset */
	unsigned int usersize;		/** Usercopy region size */
#endif

#ifdef CONFIG_SLUB_STATS
	struct kmem_cache_stats __percpu *cpu_stats;
#endif

	struct kmem_cache_node *node[MAX_NUMNODES];
};

/**
 * Every cache has !NULL s->cpu_sheaves but they may point to the
 * bootstrap_sheaf temporarily during init, or permanently for the boot caches
 * and caches with debugging enabled, or all caches with CONFIG_SLUB_TINY. This
 * helper distinguishes whether cache has real non-bootstrap sheaves.
 * 
 * 
 * Function Description: Checks if a kmem_cache uses sheaves (per-CPU caching arrays). Returns true if CONFIG_SLUB_TINY is not set and sheaf_capacity is non-zero. Used to determine if sheaf-based allocation is available.
 */
static inline bool cache_has_sheaves(struct kmem_cache *s)
{
	/* Test CONFIG_SLUB_TINY for code elimination purposes */
	return !IS_ENABLED(CONFIG_SLUB_TINY) && s->sheaf_capacity;
}

#if defined(CONFIG_SYSFS) && !defined(CONFIG_SLUB_TINY)
#define SLAB_SUPPORTS_SYSFS 1
void sysfs_slab_unlink(struct kmem_cache *s);
void sysfs_slab_release(struct kmem_cache *s);
int sysfs_slab_alias(struct kmem_cache *s, const char *name);
#else
static inline void sysfs_slab_unlink(struct kmem_cache *s) { }
static inline void sysfs_slab_release(struct kmem_cache *s) { }
static inline int sysfs_slab_alias(struct kmem_cache *s, const char *name)
							{ return 0; }
#endif

void *fixup_red_left(struct kmem_cache *s, void *p);

/**
 * Function Description: Finds the nearest object start to a given address within a slab. It rounds down to the nearest object boundary and handles redzone padding. Returns the start address of the containing object.
 */
static inline void *nearest_obj(struct kmem_cache *cache,
				const struct slab *slab, void *x)
{
	void *object = x - (x - slab_address(slab)) % cache->size;
	void *last_object = slab_address(slab) +
		(slab->objects - 1) * cache->size;
	void *result = (unlikely(object > last_object)) ? last_object : object;

	result = fixup_red_left(cache, result);
	return result;
}

/** Determine object index from a given position 
 * 
 * 
 * Function Description: Calculates the index of an object within a slab. It subtracts the slab address from the object address and divides by the object size using reciprocal division. This is the internal function for object index calculation.
 */
static inline unsigned int __obj_to_index(const struct kmem_cache *cache,
					  void *addr, const void *obj)
{
	return reciprocal_divide(kasan_reset_tag(obj) - addr,
				 cache->reciprocal_size);
}

/**
 * Function Description: Wrapper function that calculates an object's index within a slab. It handles KFENCE addresses (returning 0) and calls __obj_to_index() for normal objects. Returns the index of the object.
 */
static inline unsigned int obj_to_index(const struct kmem_cache *cache,
					const struct slab *slab, const void *obj)
{
	if (is_kfence_address(obj))
		return 0;
	return __obj_to_index(cache, slab_address(slab), obj);
}

/**
 * Function Description: Returns the number of objects in a slab. It reads the objects field from the slab structure. Used to get the object count.
 */
static inline int objs_per_slab(const struct kmem_cache *cache,
				const struct slab *slab)
{
	return slab->objects;
}

/**
 * State of the slab allocator.
 *
 * This is used to describe the states of the allocator during bootup.
 * Allocators use this to gradually bootstrap themselves. Most allocators
 * have the problem that the structures used for managing slab caches are
 * allocated from slab caches themselves.
 */
enum slab_state {
	DOWN,			/* No slab functionality yet */
	PARTIAL,		/* SLUB: kmem_cache_node available */
	UP,			/* Slab caches usable but not all extras yet */
	FULL			/* Everything is working */
};

extern enum slab_state slab_state;

/** The slab cache mutex protects the management structures during changes */
extern struct mutex slab_mutex;

/** The list of all slab caches on the system */
extern struct list_head slab_caches;

/** The slab cache that manages slab cache information */
extern struct kmem_cache *kmem_cache;

/** A table of kmalloc cache names and sizes */
extern const struct kmalloc_info_struct {
	const char *name[NR_KMALLOC_TYPES];
	unsigned int size;
} kmalloc_info[];

/* Kmalloc array related functions */
void setup_kmalloc_cache_index_table(void);
void create_kmalloc_caches(void);

extern u8 kmalloc_size_index[24];

/**
 * Function Description: Computes an index into the kmalloc size index table. It subtracts 1 from the size and divides by 8. Used for fast kmalloc size lookup.
 */
static inline unsigned int size_index_elem(unsigned int bytes)
{
	return (bytes - 1) / 8;
}

/**
 * Find the kmem_cache structure that serves a given size of
 * allocation
 *
 * This assumes size is larger than zero and not larger than
 * KMALLOC_MAX_CACHE_SIZE and the caller must check that.
 * 
 * 
 * Function Description: Finds the appropriate kmem_cache for a given allocation size. It handles bucket selection, size indexing, and returns the cache from the bucket array. Used internally by kmalloc to find the right cache.
 */
static inline struct kmem_cache *
kmalloc_slab(size_t size, kmem_buckets *b, gfp_t flags, unsigned long caller)
{
	unsigned int index;

	if (!b)
		b = &kmalloc_caches[kmalloc_type(flags, caller)];
	if (size <= 192)
		index = kmalloc_size_index[size_index_elem(size)];
	else
		index = fls(size - 1);

	return (*b)[index];
}

/**
 * Function Description: Fixes GFP flags for kmalloc allocations. It adjusts flags based on the allocation context. Used internally by kmalloc.
 */
gfp_t kmalloc_fix_flags(gfp_t flags);

/** Functions provided by the slab allocators 
 * 
 * 
 * Function Description: Creates a kmem_cache with the given parameters. It validates arguments, allocates the cache structure, and registers it. This is the internal function called by the kmem_cache_create API.
 */
int do_kmem_cache_create(struct kmem_cache *s, const char *name,
			 unsigned int size, struct kmem_cache_args *args,
			 slab_flags_t flags);

void __init kmem_cache_init(void);
extern void create_boot_cache(struct kmem_cache *, const char *name,
			unsigned int size, slab_flags_t flags,
			unsigned int useroffset, unsigned int usersize);

int slab_unmergeable(struct kmem_cache *s);
bool slab_args_unmergeable(struct kmem_cache_args *args, slab_flags_t flags);

slab_flags_t kmem_cache_flags(slab_flags_t flags, const char *name);

/**
 * Function Description: Checks if a kmem_cache is a kmalloc cache (serves general-purpose allocations). Returns true if the SLAB_KMALLOC flag is set. Used to identify kmalloc caches.
 */
static inline bool is_kmalloc_cache(struct kmem_cache *s)
{
	return (s->flags & SLAB_KMALLOC);
}

/**
 * Function Description: Checks if a kmem_cache is a normal kmalloc cache (not DMA, not accounted, not reclaimable). Returns true if it's a kmalloc cache without special flags. Used to identify the normal kmalloc cache type.
 */
static inline bool is_kmalloc_normal(struct kmem_cache *s)
{
	if (!is_kmalloc_cache(s))
		return false;
	return !(s->flags & (SLAB_CACHE_DMA|SLAB_ACCOUNT|SLAB_RECLAIM_ACCOUNT));
}

bool __kfree_rcu_sheaf(struct kmem_cache *s, void *obj);
void flush_all_rcu_sheaves(void);
void flush_rcu_sheaves_on_cache(struct kmem_cache *s);

#define SLAB_CORE_FLAGS (SLAB_HWCACHE_ALIGN | SLAB_CACHE_DMA | \
			 SLAB_CACHE_DMA32 | SLAB_PANIC | \
			 SLAB_TYPESAFE_BY_RCU | SLAB_DEBUG_OBJECTS | \
			 SLAB_NOLEAKTRACE | SLAB_RECLAIM_ACCOUNT | \
			 SLAB_TEMPORARY | SLAB_ACCOUNT | \
			 SLAB_NO_USER_FLAGS | SLAB_KMALLOC | SLAB_NO_MERGE)

#define SLAB_DEBUG_FLAGS (SLAB_RED_ZONE | SLAB_POISON | SLAB_STORE_USER | \
			  SLAB_TRACE | SLAB_CONSISTENCY_CHECKS)

#define SLAB_FLAGS_PERMITTED (SLAB_CORE_FLAGS | SLAB_DEBUG_FLAGS)

bool __kmem_cache_empty(struct kmem_cache *);
int __kmem_cache_shutdown(struct kmem_cache *);
void __kmem_cache_release(struct kmem_cache *);
int __kmem_cache_shrink(struct kmem_cache *);
void slab_kmem_cache_release(struct kmem_cache *);

struct seq_file;
struct file;

/**
 * struct Description: This structure holds statistics for a slab cache, used for reporting through /proc/slabinfo. It contains active objects, total objects, active slabs, total slabs, shared availability, limit, batch count, shared count, objects per slab, and cache order. It is used to export cache statistics to userspace.
 */
struct slabinfo {
	unsigned long active_objs;
	unsigned long num_objs;
	unsigned long active_slabs;
	unsigned long num_slabs;
	unsigned long shared_avail;
	unsigned int limit;
	unsigned int batchcount;
	unsigned int shared;
	unsigned int objects_per_slab;
	unsigned int cache_order;
};

void get_slabinfo(struct kmem_cache *s, struct slabinfo *sinfo);

#ifdef CONFIG_SLUB_DEBUG
#ifdef CONFIG_SLUB_DEBUG_ON
DECLARE_STATIC_KEY_TRUE(slub_debug_enabled);
#else
DECLARE_STATIC_KEY_FALSE(slub_debug_enabled);
#endif
extern void print_tracking(struct kmem_cache *s, void *object);
long validate_slab_cache(struct kmem_cache *s);
static inline bool __slub_debug_enabled(void)
{
	return static_branch_unlikely(&slub_debug_enabled);
}
#else
static inline void print_tracking(struct kmem_cache *s, void *object)
{
}
static inline bool __slub_debug_enabled(void)
{
	return false;
}
#endif

/**
 * Returns true if any of the specified slab_debug flags is enabled for the
 * cache. Use only for flags parsed by setup_slub_debug() as it also enables
 * the static key.
 * 
 * 
 * Function Description: Checks if specific debug flags are enabled for a kmem_cache. It verifies the flags are debug flags and checks the cache's flags. Returns true if the flags are enabled.
 */
static inline bool kmem_cache_debug_flags(struct kmem_cache *s, slab_flags_t flags)
{
	if (IS_ENABLED(CONFIG_SLUB_DEBUG))
		VM_WARN_ON_ONCE(!(flags & SLAB_DEBUG_FLAGS));
	if (__slub_debug_enabled())
		return s->flags & flags;
	return false;
}

#if IS_ENABLED(CONFIG_SLUB_DEBUG) && IS_ENABLED(CONFIG_KUNIT)
bool slab_in_kunit_test(void);
#else
static inline bool slab_in_kunit_test(void) { return false; }
#endif

/**
 * slub is about to manipulate internal object metadata.  This memory lies
 * outside the range of the allocated object, so accessing it would normally
 * be reported by kasan as a bounds error.  metadata_access_enable() is used
 * to tell kasan that these accesses are OK.
 * 
 * 
 * Function Description: Enables access to internal slab metadata by disabling KASAN and KMSAN checking. Called before manipulating metadata that would normally be reported as out-of-bounds.
 */
static inline void metadata_access_enable(void)
{
	kasan_disable_current();
	kmsan_disable_current();
}

/**
 * Function Description: Disables access to internal slab metadata by re-enabling KASAN and KMSAN checking. Called after metadata manipulation is complete.
 */
static inline void metadata_access_disable(void)
{
	kmsan_enable_current();
	kasan_enable_current();
}

#ifdef CONFIG_SLAB_OBJ_EXT

/**
 * slab_obj_exts - get the pointer to the slab object extension vector
 * associated with a slab.
 * @slab: a pointer to the slab struct
 *
 * Returns the address of the object extension vector associated with the slab,
 * or zero if no such vector has been associated yet.
 * Do not dereference the return value directly; use get/put_slab_obj_exts()
 * pair and slab_obj_ext() to access individual elements.
 *
 * Example usage:
 *
 * obj_exts = slab_obj_exts(slab);
 * if (obj_exts) {
 *         get_slab_obj_exts(obj_exts);
 *         obj_ext = slab_obj_ext(slab, obj_exts, obj_to_index(s, slab, obj));
 *         // do something with obj_ext
 *         put_slab_obj_exts(obj_exts);
 * }
 *
 * Note that the get/put semantics does not involve reference counting.
 * Instead, it updates kasan/kmsan depth so that accesses to slabobj_ext
 * won't be reported as access violations.
 * 
 * 
 * Function Description: Returns the object extension vector pointer for a slab. It reads the obj_exts field and masks out flag bits. Used to access per-object metadata like memcg or allocation profiling.
 */
static inline unsigned long slab_obj_exts(struct slab *slab)
{
	unsigned long obj_exts = READ_ONCE(slab->obj_exts);

#ifdef CONFIG_MEMCG
	/*
	 * obj_exts should be either NULL, a valid pointer with
	 * MEMCG_DATA_OBJEXTS bit set or be equal to OBJEXTS_ALLOC_FAIL.
	 */
	VM_BUG_ON_PAGE(obj_exts && !(obj_exts & MEMCG_DATA_OBJEXTS) &&
		       obj_exts != OBJEXTS_ALLOC_FAIL, slab_page(slab));
	VM_BUG_ON_PAGE(obj_exts & MEMCG_DATA_KMEM, slab_page(slab));
#endif

	return obj_exts & ~OBJEXTS_FLAGS_MASK;
}

/**
 * Function Description: Prepares to access slab object extensions by enabling metadata access. It calls metadata_access_enable() to allow KASAN/KMSAN to see the metadata. Called before accessing slab_obj_exts().
 */
static inline void get_slab_obj_exts(unsigned long obj_exts)
{
	VM_WARN_ON_ONCE(!obj_exts);
	metadata_access_enable();
}

/**
 * Function Description: Finishes access to slab object extensions by disabling metadata access. It calls metadata_access_disable() to restore KASAN/KMSAN checking. Called after accessing slab_obj_exts().
 */
static inline void put_slab_obj_exts(unsigned long obj_exts)
{
	metadata_access_disable();
}

#ifdef CONFIG_64BIT

/**
 * Function Description: Sets the stride (element size) for a slab's object extensions. It stores the value in the stride field on 64-bit systems. Used to manage the layout of object extension metadata.
 */
static inline void slab_set_stride(struct slab *slab, unsigned int stride)
{
	slab->stride = stride;
}

/**
 * Function Description: Returns the stride (element size) for a slab's object extensions. It reads the stride field on 64-bit systems or returns sizeof(struct slabobj_ext) on 32-bit. Used to calculate offset into the object extension array.
 */
static inline unsigned int slab_get_stride(struct slab *slab)
{
	return slab->stride;
}
#else

static inline void slab_set_stride(struct slab *slab, unsigned int stride)
{
	VM_WARN_ON_ONCE(stride != sizeof(struct slabobj_ext));
}
static inline unsigned int slab_get_stride(struct slab *slab)
{
	return sizeof(struct slabobj_ext);
}
#endif

/**
 * slab_obj_ext - get the pointer to the slab object extension metadata
 * associated with an object in a slab.
 * @slab: a pointer to the slab struct
 * @obj_exts: a pointer to the object extension vector
 * @index: an index of the object
 *
 * Returns a pointer to the object extension associated with the object.
 * Must be called within a section covered by get/put_slab_obj_exts().
 */
static inline struct slabobj_ext *slab_obj_ext(struct slab *slab,
					       unsigned long obj_exts,
					       unsigned int index)
{
	struct slabobj_ext *obj_ext;

	VM_WARN_ON_ONCE(obj_exts != slab_obj_exts(slab));

	obj_ext = (struct slabobj_ext *)(obj_exts +
					 slab_get_stride(slab) * index);
	return kasan_reset_tag(obj_ext);
}

int alloc_slab_obj_exts(struct slab *slab, struct kmem_cache *s,
                        gfp_t gfp, bool new_slab);

#else /* CONFIG_SLAB_OBJ_EXT */

static inline unsigned long slab_obj_exts(struct slab *slab)
{
	return 0;
}

static inline struct slabobj_ext *slab_obj_ext(struct slab *slab,
					       unsigned long obj_exts,
					       unsigned int index)
{
	return NULL;
}

static inline void slab_set_stride(struct slab *slab, unsigned int stride) { }
static inline unsigned int slab_get_stride(struct slab *slab) { return 0; }


#endif /* CONFIG_SLAB_OBJ_EXT */

/**
 * Function Description: Returns the VM stat index for a kmem_cache. It returns NR_SLAB_RECLAIMABLE_B if the cache is reclaimable, otherwise NR_SLAB_UNRECLAIMABLE_B. Used for memory statistics.
 */
static inline enum node_stat_item cache_vmstat_idx(struct kmem_cache *s)
{
	return (s->flags & SLAB_RECLAIM_ACCOUNT) ?
		NR_SLAB_RECLAIMABLE_B : NR_SLAB_UNRECLAIMABLE_B;
}

#ifdef CONFIG_MEMCG
bool __memcg_slab_post_alloc_hook(struct kmem_cache *s, struct list_lru *lru,
				  gfp_t flags, size_t size, void **p);
void __memcg_slab_free_hook(struct kmem_cache *s, struct slab *slab,
			    void **p, int objects, unsigned long obj_exts);
#endif

void kvfree_rcu_cb(struct rcu_head *head);

/**
 * Function Description: Returns the page order for a large kmalloc allocation. It reads the order from the second page's flags. Used to determine the size of large kmalloc allocations.
 */
static inline unsigned int large_kmalloc_order(const struct page *page)
{
	return page[1].flags.f & 0xff;
}

/**
 * Function Description: Returns the size of a large kmalloc allocation. It calculates PAGE_SIZE << large_kmalloc_order(). Used to determine how much memory was allocated.
 */
static inline size_t large_kmalloc_size(const struct page *page)
{
	return PAGE_SIZE << large_kmalloc_order(page);
}

#ifdef CONFIG_SLUB_DEBUG
void dump_unreclaimable_slab(void);
#else
static inline void dump_unreclaimable_slab(void)
{
}
#endif

void ___cache_free(struct kmem_cache *cache, void *x, unsigned long addr);

#ifdef CONFIG_SLAB_FREELIST_RANDOM
int cache_random_seq_create(struct kmem_cache *cachep, unsigned int count,
			gfp_t gfp);
void cache_random_seq_destroy(struct kmem_cache *cachep);
#else
static inline int cache_random_seq_create(struct kmem_cache *cachep,
					unsigned int count, gfp_t gfp)
{
	return 0;
}
static inline void cache_random_seq_destroy(struct kmem_cache *cachep) { }
#endif /* CONFIG_SLAB_FREELIST_RANDOM */

/**
 * Function Description: Determines if a slab object should be zero-initialized on allocation. It checks init_on_alloc settings, constructor presence, and flags. Returns true if the object should be zeroed.
 */
static inline bool slab_want_init_on_alloc(gfp_t flags, struct kmem_cache *c)
{
	if (static_branch_maybe(CONFIG_INIT_ON_ALLOC_DEFAULT_ON,
				&init_on_alloc)) {
		if (c->ctor)
			return false;
		if (c->flags & (SLAB_TYPESAFE_BY_RCU | SLAB_POISON))
			return flags & __GFP_ZERO;
		return true;
	}
	return flags & __GFP_ZERO;
}

/**
 * Function Description: Determines if a slab object should be zeroed on free. It checks init_on_free settings, constructor presence, and flags. Returns true if the object should be zeroed on free.
 */
static inline bool slab_want_init_on_free(struct kmem_cache *c)
{
	if (static_branch_maybe(CONFIG_INIT_ON_FREE_DEFAULT_ON,
				&init_on_free))
		return !(c->ctor ||
			 (c->flags & (SLAB_TYPESAFE_BY_RCU | SLAB_POISON)));
	return false;
}

#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_SLUB_DEBUG)
void debugfs_slab_release(struct kmem_cache *);
#else
static inline void debugfs_slab_release(struct kmem_cache *s) { }
#endif

#ifdef CONFIG_PRINTK
#define KS_ADDRS_COUNT 16
/**
 * struct Description: This structure holds detailed information about a slab object for debugging and printing. It contains the object pointer, slab pointer, object address, data offset, cache pointer, return address, and call stacks for allocation and free. It is used by printk to display object information. 
 */
struct kmem_obj_info {
	void *kp_ptr;
	struct slab *kp_slab;
	void *kp_objp;
	unsigned long kp_data_offset;
	struct kmem_cache *kp_slab_cache;
	void *kp_ret;
	void *kp_stack[KS_ADDRS_COUNT];
	void *kp_free_stack[KS_ADDRS_COUNT];
};
void __kmem_obj_info(struct kmem_obj_info *kpp, void *object, struct slab *slab);
#endif

void __check_heap_object(const void *ptr, unsigned long n,
			 const struct slab *slab, bool to_user);

void defer_free_barrier(void);

static inline bool slub_debug_orig_size(struct kmem_cache *s)
{
	return (kmem_cache_debug_flags(s, SLAB_STORE_USER) &&
			(s->flags & SLAB_KMALLOC));
}

#ifdef CONFIG_SLUB_DEBUG
void skip_orig_size_check(struct kmem_cache *s, const void *object);
#endif

#endif /* MM_SLAB_H */
