/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VMALLOC_H
#define _LINUX_VMALLOC_H

#include <linux/alloc_tag.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/llist.h>
#include <asm/page.h>		/* pgprot_t */
#include <linux/rbtree.h>
#include <linux/overflow.h>

#include <asm/vmalloc.h>

struct vm_area_struct;		/* vma defining user mapping in mm_types.h */
struct notifier_block;		/* in notifier.h */
struct iov_iter;		/* in uio.h */

/** bits in flags of vmalloc's vm_struct below */
#define VM_IOREMAP		0x00000001	/** ioremap() and friends */
#define VM_ALLOC		0x00000002	/** vmalloc() */
#define VM_MAP			0x00000004	/** vmap()ed pages */
#define VM_USERMAP		0x00000008	/** suitable for remap_vmalloc_range */
#define VM_DMA_COHERENT		0x00000010	/** dma_alloc_coherent */
#define VM_UNINITIALIZED	0x00000020	/** vm_struct is not fully initialized */
#define VM_NO_GUARD		0x00000040      /** ***DANGEROUS*** don't add guard page */
#define VM_KASAN		0x00000080      /** has allocated kasan shadow memory */
#define VM_FLUSH_RESET_PERMS	0x00000100	/** reset direct map and flush TLB on unmap, can't be freed in atomic context */
#define VM_MAP_PUT_PAGES	0x00000200	/** put pages and free array in vfree */
#define VM_ALLOW_HUGE_VMAP	0x00000400      /** Allow for huge pages on archs with HAVE_ARCH_HUGE_VMALLOC */

#if (defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)) && \
	!defined(CONFIG_KASAN_VMALLOC)
#define VM_DEFER_KMEMLEAK	0x00000800	/** defer kmemleak object creation */
#else
#define VM_DEFER_KMEMLEAK	0
#endif
#define VM_SPARSE		0x00001000	/** sparse vm_area. not all pages are present. */

/* bits [20..32] reserved for arch specific ioremap internals */

/**
 * Maximum alignment for ioremap() regions.
 * Can be overridden by arch-specific value.
 */
#ifndef IOREMAP_MAX_ORDER
#define IOREMAP_MAX_ORDER	(7 + PAGE_SHIFT)	/** 128 pages */
#endif

/**
 * struct Description: This structure represents a vmalloc/vmap/vmalloc area. It contains the virtual address, size, flags, page array, number of pages, physical address, caller address, and requested size. It is used to track and manage virtually contiguous memory regions backed by non-contiguous physical pages.
 */
struct vm_struct {
	union {
		struct vm_struct *next;	  /** Early registration of vm_areas. */
		struct llist_node llnode; /** Asynchronous freeing on error paths. */
	};

	void			*addr;
	unsigned long		size;
	unsigned long		flags;
	struct page		**pages;
#ifdef CONFIG_HAVE_ARCH_HUGE_VMALLOC
	unsigned int		page_order;
#endif
	unsigned int		nr_pages;
	phys_addr_t		phys_addr;
	const void		*caller;
	unsigned long		requested_size;
};

/**
 * struct Description: This structure represents a virtual memory area in the vmalloc space. It contains the start and end addresses, rb_tree and list nodes for tracking, and either subtree_max_size (when free) or vm_struct pointer (when busy). It is used internally by the vmalloc subsystem to manage the virtual address space. 
 */
struct vmap_area {
	unsigned long va_start;
	unsigned long va_end;

	struct rb_node rb_node;         /** address sorted rbtree */
	struct list_head list;          /** address sorted list */

	/**
	 * The following two variables can be packed, because
	 * a vmap_area object can be either:
	 *    1) in "free" tree (root is free_vmap_area_root)
	 *    2) or "busy" tree (root is vmap_area_root)
	 */
	union {
		unsigned long subtree_max_size; /** in "free" tree */
		struct vm_struct *vm;           /** in "busy" tree */
	};
	unsigned long flags; /** mark type of vm_map_ram area */
};

/* archs that select HAVE_ARCH_HUGE_VMAP should override one or more of these */
#ifndef arch_vmap_p4d_supported
/**
 * Function Description: Checks if the architecture supports P4D-level huge pages for vmap. Returns true if P4D mappings are supported with the given page protection. This can be overridden by architectures.
 */
static inline bool arch_vmap_p4d_supported(pgprot_t prot)
{
	return false;
}
#endif

#ifndef arch_vmap_pud_supported
/**
 * Function Description: Checks if the architecture supports PUD-level huge pages for vmap. Returns true if PUD mappings are supported with the given page protection. This can be overridden by architectures.
 */
static inline bool arch_vmap_pud_supported(pgprot_t prot)
{
	return false;
}
#endif

#ifndef arch_vmap_pmd_supported
/**
 * Function Description: Checks if the architecture supports PMD-level huge pages for vmap. Returns true if PMD mappings are supported with the given page protection. This can be overridden by architectures.
 */
static inline bool arch_vmap_pmd_supported(pgprot_t prot)
{
	return false;
}
#endif

#ifndef arch_vmap_pte_range_map_size
/**
 * Function Description: Returns the size of a PTE range to map for vmap. This is used for architecture-specific PTE mapping sizes. Returns PAGE_SIZE by default.
 */
static inline unsigned long arch_vmap_pte_range_map_size(unsigned long addr, unsigned long end,
							 u64 pfn, unsigned int max_page_shift)
{
	return PAGE_SIZE;
}
#endif

#ifndef arch_vmap_pte_range_unmap_size
/**
 * Function Description: Returns the size of a PTE range to unmap for vmap. This is used for architecture-specific PTE unmapping sizes. Returns PAGE_SIZE by default.
 */
static inline unsigned long arch_vmap_pte_range_unmap_size(unsigned long addr,
							   pte_t *ptep)
{
	return PAGE_SIZE;
}
#endif

#ifndef arch_vmap_pte_supported_shift
/**
 * Function Description: Returns the supported shift (order) for PTE mappings. This indicates the minimum alignment for PTE mappings. Returns PAGE_SHIFT by default.
 */
static inline int arch_vmap_pte_supported_shift(unsigned long size)
{
	return PAGE_SHIFT;
}
#endif

#ifndef arch_vmap_pgprot_tagged
/**
 * Function Description: Returns a tagged version of the page protection for vmap. This is used for architectures that support tagged memory. Returns the original prot by default.
 */
static inline pgprot_t arch_vmap_pgprot_tagged(pgprot_t prot)
{
	return prot;
}
#endif

/**
 * Highlevel APIs for driver use
 * 
 * 
 * Function Description: Unmaps a RAM region previously mapped with vm_map_ram(). It removes the mapping for the given memory and count of pages. This is used to free RAM mappings.
 */
extern void vm_unmap_ram(const void *mem, unsigned int count);

/**
 * Function Description: Maps a set of pages into a contiguous virtual address space. It takes an array of pages and maps them into RAM space. Returns the virtual address or NULL on failure.
 */
extern void *vm_map_ram(struct page **pages, unsigned int count, int node);

/**
 * Function Description: Forces the unmapping of all pending vmalloc aliases. This flushes any cached mappings that might be pending. Used to ensure all mappings are removed.
 */
extern void vm_unmap_aliases(void);

/**
 * Function Description: Allocates virtually contiguous memory without allocation profiling. It allocates memory using vmalloc with default flags. This is the internal implementation for vmalloc().
 */
extern void *vmalloc_noprof(unsigned long size) __alloc_size(1);
#define vmalloc(...)		alloc_hooks(vmalloc_noprof(__VA_ARGS__))

/**
 * Function Description: Allocates zeroed virtually contiguous memory without allocation profiling. It calls vmalloc() and zeroes the allocated memory. This is the internal implementation for vzalloc().
 */
extern void *vzalloc_noprof(unsigned long size) __alloc_size(1);
#define vzalloc(...)		alloc_hooks(vzalloc_noprof(__VA_ARGS__))

/**
 * Function Description: Allocates virtually contiguous memory suitable for user-space mapping without allocation profiling. It sets VM_USERMAP flag to allow remap_vmalloc_range(). This is the internal implementation for vmalloc_user().
 */
extern void *vmalloc_user_noprof(unsigned long size) __alloc_size(1);
#define vmalloc_user(...)	alloc_hooks(vmalloc_user_noprof(__VA_ARGS__))

/**
 * Function Description: Allocates virtually contiguous memory on a specific NUMA node without allocation profiling. It allocates memory from the specified node. This is the internal implementation for vmalloc_node().
 */
extern void *vmalloc_node_noprof(unsigned long size, int node) __alloc_size(1);
#define vmalloc_node(...)	alloc_hooks(vmalloc_node_noprof(__VA_ARGS__))

/**
 * Function Description: Allocates zeroed virtually contiguous memory on a specific NUMA node without allocation profiling. It allocates zeroed memory from the specified node. This is the internal implementation for vzalloc_node().
 */
extern void *vzalloc_node_noprof(unsigned long size, int node) __alloc_size(1);
#define vzalloc_node(...)	alloc_hooks(vzalloc_node_noprof(__VA_ARGS__))

/**
 * Function Description: Allocates virtually contiguous memory that is addressable by 32-bit devices without allocation profiling. It ensures the memory is in the lower 32-bit address space. This is the internal implementation for vmalloc_32().
 */
extern void *vmalloc_32_noprof(unsigned long size) __alloc_size(1);
#define vmalloc_32(...)		alloc_hooks(vmalloc_32_noprof(__VA_ARGS__))

/**
 * Function Description: Allocates 32-bit addressable virtually contiguous memory suitable for user-space mapping without allocation profiling. This is the internal implementation for vmalloc_32_user().
 */
extern void *vmalloc_32_user_noprof(unsigned long size) __alloc_size(1);
#define vmalloc_32_user(...)	alloc_hooks(vmalloc_32_user_noprof(__VA_ARGS__))

/**
 * Function Description: Allocates virtually contiguous memory with specified GFP flags without allocation profiling. This is the internal implementation for __vmalloc().
 */
extern void *__vmalloc_noprof(unsigned long size, gfp_t gfp_mask) __alloc_size(1);
#define __vmalloc(...)		alloc_hooks(__vmalloc_noprof(__VA_ARGS__))

/**
 * Function Description: Allocates virtually contiguous memory within a specific address range without allocation profiling. It allows specifying start and end addresses, alignment, flags, protection, and node. This is the internal implementation for __vmalloc_node_range().
 */
extern void *__vmalloc_node_range_noprof(unsigned long size, unsigned long align,
			unsigned long start, unsigned long end, gfp_t gfp_mask,
			pgprot_t prot, unsigned long vm_flags, int node,
			const void *caller) __alloc_size(1);
#define __vmalloc_node_range(...)	alloc_hooks(__vmalloc_node_range_noprof(__VA_ARGS__))

/**
 * Function Description: Allocates virtually contiguous memory on a specific node with alignment without allocation profiling. This is the internal implementation for __vmalloc_node().
 */
void *__vmalloc_node_noprof(unsigned long size, unsigned long align, gfp_t gfp_mask,
		int node, const void *caller) __alloc_size(1);
#define __vmalloc_node(...)	alloc_hooks(__vmalloc_node_noprof(__VA_ARGS__))

/**
 * Function Description: Allocates virtually contiguous memory using huge pages on a specific node without allocation profiling. It attempts to use large page mappings for better performance. This is the internal implementation for vmalloc_huge_node().
 */
void *vmalloc_huge_node_noprof(unsigned long size, gfp_t gfp_mask, int node) __alloc_size(1);
#define vmalloc_huge_node(...)	alloc_hooks(vmalloc_huge_node_noprof(__VA_ARGS__))

/**
 * Function Description: Allocates virtually contiguous memory using huge pages. It calls vmalloc_huge_node() with NUMA_NO_NODE. This attempts to use large page mappings for better TLB efficiency.
 */
static inline void *vmalloc_huge(unsigned long size, gfp_t gfp_mask)
{
	return vmalloc_huge_node(size, gfp_mask, NUMA_NO_NODE);
}

/**
 * Function Description: Allocates an array of elements using vmalloc with GFP flags without allocation profiling. It multiplies n * size with overflow checking. This is the internal implementation for __vmalloc_array().
 */
extern void *__vmalloc_array_noprof(size_t n, size_t size, gfp_t flags) __alloc_size(1, 2);
#define __vmalloc_array(...)	alloc_hooks(__vmalloc_array_noprof(__VA_ARGS__))

/**
 * Function Description: Allocates an array of elements using vmalloc without allocation profiling. It multiplies n * size with overflow checking. This is the internal implementation for vmalloc_array().
 */
extern void *vmalloc_array_noprof(size_t n, size_t size) __alloc_size(1, 2);
#define vmalloc_array(...)	alloc_hooks(vmalloc_array_noprof(__VA_ARGS__))

/**
 * Function Description: Allocates zeroed array memory using vmalloc with GFP flags without allocation profiling. It multiplies n * size with overflow checking and zeroes the memory. This is the internal implementation for __vcalloc().
 */
extern void *__vcalloc_noprof(size_t n, size_t size, gfp_t flags) __alloc_size(1, 2);
#define __vcalloc(...)		alloc_hooks(__vcalloc_noprof(__VA_ARGS__))

/**
 * Function Description: Allocates zeroed array memory using vmalloc without allocation profiling. It multiplies n * size with overflow checking and zeroes the memory. This is the internal implementation for vcalloc().
 */
extern void *vcalloc_noprof(size_t n, size_t size) __alloc_size(1, 2);
#define vcalloc(...)		alloc_hooks(vcalloc_noprof(__VA_ARGS__))

/**
 * Function Description: Reallocates memory using vmalloc/vfree with alignment and node affinity without allocation profiling. It handles resizing of vmalloc-allocated memory, preserving contents up to the smaller size. This is the internal implementation for vrealloc with alignment.
 */
void *__must_check vrealloc_node_align_noprof(const void *p, size_t size,
		unsigned long align, gfp_t flags, int nid) __realloc_size(2);
#define vrealloc_node_noprof(_p, _s, _f, _nid)	\
	vrealloc_node_align_noprof(_p, _s, 1, _f, _nid)
#define vrealloc_noprof(_p, _s, _f)		\
	vrealloc_node_align_noprof(_p, _s, 1, _f, NUMA_NO_NODE)
#define vrealloc_node_align(...)		alloc_hooks(vrealloc_node_align_noprof(__VA_ARGS__))
#define vrealloc_node(...)			alloc_hooks(vrealloc_node_noprof(__VA_ARGS__))
#define vrealloc(...)				alloc_hooks(vrealloc_noprof(__VA_ARGS__))

/**
 * Function Description: Frees memory allocated with vmalloc() or related functions. It returns the memory to the vmalloc subsystem. This is the main deallocation function for vmalloc memory.
 */
extern void vfree(const void *addr);

/**
 * Function Description: Frees vmalloc memory in atomic context. This is a safe version of vfree() that can be called from interrupt context. It uses a deferred free mechanism.
 */
extern void vfree_atomic(const void *addr);

/**
 * Function Description: Maps an array of pages into a contiguous virtual address space. It creates a mapping with the specified flags and protection. Returns the virtual address or NULL on failure.
 */
extern void *vmap(struct page **pages, unsigned int count,
			unsigned long flags, pgprot_t prot);

/**
 * Function Description: Maps an array of physical frame numbers into a contiguous virtual address space. It creates a mapping from PFNs with the specified protection. Returns the virtual address or NULL on failure.
 */
void *vmap_pfn(unsigned long *pfns, unsigned int count, pgprot_t prot);

/**
 * Function Description: Unmaps a vmap() mapping and frees the virtual address space. It removes the mapping for the given address. This is the counterpart to vmap().
 */
extern void vunmap(const void *addr);

/**
 * Function Description: Maps a portion of a vmalloc area into a user-space VMA. It creates a page table mapping from the vmalloc area to user space. Returns 0 on success or an error code.
 */
extern int remap_vmalloc_range_partial(struct vm_area_struct *vma,
				       unsigned long uaddr, void *kaddr,
				       unsigned long pgoff, unsigned long size);

/**
 * Function Description: Maps a vmalloc area into a user-space VMA. It maps the entire vmalloc area starting from the given offset. Returns 0 on success or an error code.
 */
extern int remap_vmalloc_range(struct vm_area_struct *vma, void *addr,
							unsigned long pgoff);

/**
 * Function Description: Maps a range of pages into a virtual address range. It creates page table mappings for the specified address range using the given pages and page_shift. Returns 0 on success or an error code.
 */
int vmap_pages_range(unsigned long addr, unsigned long end, pgprot_t prot,
		     struct page **pages, unsigned int page_shift);

/**
 * Lowlevel-APIs (not for driver use!)
 * 
 * 
 * Function Description: Returns the usable size of a vm_struct area. It subtracts the guard page size from the total area size if VM_NO_GUARD is not set. This returns the actual allocatable size.
 */
static inline size_t get_vm_area_size(const struct vm_struct *area)
{
	if (!(area->flags & VM_NO_GUARD))
		/* return actual size without guard page */
		return area->size - PAGE_SIZE;
	else
		return area->size;

}

/**
 * Function Description: Allocates a vm_struct for a virtual memory area of the given size and flags. It reserves a region in the vmalloc address space without allocating physical pages. Returns the vm_struct or NULL.
 */
extern struct vm_struct *get_vm_area(unsigned long size, unsigned long flags);

/**
 * Function Description: Allocates a vm_struct with caller tracking for debugging. It records the caller address for slab leak tracking. Returns the vm_struct or NULL.
 */
extern struct vm_struct *get_vm_area_caller(unsigned long size,
					unsigned long flags, const void *caller);

/**
 * Function Description: Allocates a vm_struct within a specific address range with caller tracking. It allows specifying the address range for the allocation. Returns the vm_struct or NULL.
 */
extern struct vm_struct *__get_vm_area_caller(unsigned long size,
					unsigned long flags,
					unsigned long start, unsigned long end,
					const void *caller);

/**
 * Function Description: Frees a vm_struct area and releases its virtual address space. It removes the area from the vmalloc address space. This is used to free unused areas.
 */
void free_vm_area(struct vm_struct *area);

/**
 * Function Description: Removes a vm_struct area from the vmalloc address space. It finds the area by address, removes it from the tree, and returns the vm_struct. Returns NULL if not found.
 */
extern struct vm_struct *remove_vm_area(const void *addr);

/**
 * Function Description: Finds the vm_struct associated with a given address. It searches the vmalloc area tree for the address. Returns the vm_struct or NULL if not found.
 */
extern struct vm_struct *find_vm_area(const void *addr);

/**
 * Function Description: Finds the vmap_area associated with a given address. It searches the vmap area tree for the address. Returns the vmap_area or NULL if not found.
 */
struct vmap_area *find_vmap_area(unsigned long addr);

/**
 * Function Description: Checks if a vmalloc area is mapped with huge pages. It returns true if the area uses page order > 0. This indicates the area is using large page mappin
 */
static inline bool is_vm_area_hugepages(const void *addr)
{
	/**
	 * 
	 * 
	 * This may not 100% tell if the area is mapped with > PAGE_SIZE
	 * page table entries, if for some reason the architecture indicates
	 * larger sizes are available but decides not to use them, nothing
	 * prevents that. This only indicates the size of the physical page
	 * allocated in the vmalloc layer.
	 */
#ifdef CONFIG_HAVE_ARCH_HUGE_VMALLOC
	return find_vm_area(addr)->page_order > 0;
#else
	return false;
#endif
}

/** 
 * for /proc/kcore 
 * 
 * 
 * Function Description: Reads from vmalloc memory into an iov_iter for /proc/kcore. It copies data from the vmalloc area to the user iterator. Returns the number of bytes read.
 */
long vread_iter(struct iov_iter *iter, const char *addr, size_t count);

/**
 * Internals.  Don't use..
 * 
 * 
 * Function Description: Adds a vm_struct area early during boot. This is used for early vmalloc initialization. Called during kernel initialization.
 */
__init void vm_area_add_early(struct vm_struct *vm);

/**
 * Function Description: Registers a vm_struct area early during boot with alignment. This is used for early vmalloc initialization with specific alignment. Called during kernel initialization.
 */
__init void vm_area_register_early(struct vm_struct *vm, size_t align);

/**
 * Function Description: Registers a notifier for vmap purge events. The notifier is called when vmap areas are purged. Returns 0 on success or an error code.
 */
int register_vmap_purge_notifier(struct notifier_block *nb);

/**
 * Function Description: Unregisters a vmap purge notifier. It removes the notifier from the notification chain. Returns 0 on success or an error code.
 */
int unregister_vmap_purge_notifier(struct notifier_block *nb);

#ifdef CONFIG_MMU
/**
 * Macro Description: Returns the total size of the vmalloc address space. This is the difference between VMALLOC_END and VMALLOC_START. Used for statistics and size calculations.
 */
#define VMALLOC_TOTAL (VMALLOC_END - VMALLOC_START)

/**
 * Function Description: Returns the number of pages currently allocated by vmalloc. This is used for statistics and monitoring. Returns the total page count.
 */
unsigned long vmalloc_nr_pages(void);

/**
 * Function Description: Maps pages into a specific range within a vm_struct area. It adds page mappings to the given address range. Returns 0 on success or an error code.
 */
int vm_area_map_pages(struct vm_struct *area, unsigned long start,
		      unsigned long end, struct page **pages);

/**
 * Function Description: Unmaps pages from a specific range within a vm_struct area. It removes page mappings from the given address range.
 */
void vm_area_unmap_pages(struct vm_struct *area, unsigned long start,
			 unsigned long end);

/**
 * Function Description: Unmaps a range of virtual addresses in the vmalloc space. It tears down page table mappings for the specified range.
 */
void vunmap_range(unsigned long addr, unsigned long end);

/**
 * Function Description: Sets the VM_FLUSH_RESET_PERMS flag on a vm_struct. This flag causes TLB flush and permission reset on unmap. Used for security features.
 */
static inline void set_vm_flush_reset_perms(void *addr)
{
	struct vm_struct *vm = find_vm_area(addr);

	if (vm)
		vm->flags |= VM_FLUSH_RESET_PERMS;
}
#else  /* !CONFIG_MMU */
#define VMALLOC_TOTAL 0UL

static inline unsigned long vmalloc_nr_pages(void) { return 0; }
static inline void set_vm_flush_reset_perms(void *addr) {}
#endif /* CONFIG_MMU */

#if defined(CONFIG_MMU) && defined(CONFIG_SMP)

/**
 * Function Description: Allocates multiple vm_struct areas for per-CPU data. It takes offsets and sizes for each area and creates them. Returns an array of vm_struct pointers.
 */
struct vm_struct **pcpu_get_vm_areas(const unsigned long *offsets,
				     const size_t *sizes, int nr_vms,
				     size_t align);

/**
 * Function Description: Frees multiple vm_struct areas allocated for per-CPU data. It releases all the areas and their virtual address space.
 */
void pcpu_free_vm_areas(struct vm_struct **vms, int nr_vms);
# else
static inline struct vm_struct **
pcpu_get_vm_areas(const unsigned long *offsets,
		const size_t *sizes, int nr_vms,
		size_t align)
{
	return NULL;
}

static inline void pcpu_free_vm_areas(struct vm_struct **vms, int nr_vms) {}
#endif

#if defined(CONFIG_MMU) && defined(CONFIG_PRINTK)
/**
 * Function Description: Dumps information about a vmalloc object for debugging. It checks if the address is in the vmalloc area and prints details. Returns true if the object was found.
 */
bool vmalloc_dump_obj(void *object);
#else
static inline bool vmalloc_dump_obj(void *object) { return false; }
#endif

/**
 * Function Description: Applies the current memory allocation scope to GFP flags. This adds flags based on the current task's memory allocation context. Returns the modified GFP flags.
 */
unsigned int memalloc_apply_gfp_scope(gfp_t gfp_mask);

/**
 * Function Description: Restores the previous memory allocation scope. This reverts the GFP flags to their previous state. Used to clean up after memalloc_apply_gfp_scope().
 */
void memalloc_restore_scope(unsigned int flags);
#endif /* _LINUX_VMALLOC_H */
