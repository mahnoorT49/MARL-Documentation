/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DMA_MAPPING_H
#define _LINUX_DMA_MAPPING_H

#include <linux/device.h>
#include <linux/err.h>
#include <linux/dma-direction.h>
#include <linux/scatterlist.h>
#include <linux/bug.h>
#include <linux/cache.h>

/*
 * List of possible attributes associated with a DMA mapping. The semantics
 * of each attribute should be defined in Documentation/core-api/dma-attributes.rst.
 */

/**
 * DMA_ATTR_WEAK_ORDERING: Specifies that reads and writes to the mapping
 * may be weakly ordered, that is that reads and writes may pass each other.
 */
#define DMA_ATTR_WEAK_ORDERING		(1UL << 1)
/**
 * DMA_ATTR_WRITE_COMBINE: Specifies that writes to the mapping may be
 * buffered to improve performance.
 */
#define DMA_ATTR_WRITE_COMBINE		(1UL << 2)
/**
 * DMA_ATTR_NO_KERNEL_MAPPING: Lets the platform to avoid creating a kernel
 * virtual mapping for the allocated buffer.
 */
#define DMA_ATTR_NO_KERNEL_MAPPING	(1UL << 4)
/**
 * DMA_ATTR_SKIP_CPU_SYNC: Allows platform code to skip synchronization of
 * the CPU cache for the given buffer assuming that it has been already
 * transferred to 'device' domain.
 */
#define DMA_ATTR_SKIP_CPU_SYNC		(1UL << 5)
/**
 * DMA_ATTR_FORCE_CONTIGUOUS: Forces contiguous allocation of the buffer
 * in physical memory.
 */
#define DMA_ATTR_FORCE_CONTIGUOUS	(1UL << 6)
/**
 * DMA_ATTR_ALLOC_SINGLE_PAGES: This is a hint to the DMA-mapping subsystem
 * that it's probably not worth the time to try to allocate memory to in a way
 * that gives better TLB efficiency.
 */
#define DMA_ATTR_ALLOC_SINGLE_PAGES	(1UL << 7)
/**
 * DMA_ATTR_NO_WARN: This tells the DMA-mapping subsystem to suppress
 * allocation failure reports (similarly to __GFP_NOWARN).
 */
#define DMA_ATTR_NO_WARN	(1UL << 8)

/**
 * DMA_ATTR_PRIVILEGED: used to indicate that the buffer is fully
 * accessible at an elevated privilege level (and ideally inaccessible or
 * at least read-only at lesser-privileged levels).
 */
#define DMA_ATTR_PRIVILEGED		(1UL << 9)

/**
 * DMA_ATTR_MMIO - Indicates memory-mapped I/O (MMIO) region for DMA mapping
 *
 * This attribute indicates the physical address is not normal system
 * memory. It may not be used with kmap*()/phys_to_virt()/phys_to_page()
 * functions, it may not be cacheable, and access using CPU load/store
 * instructions may not be allowed.
 *
 * Usually this will be used to describe MMIO addresses, or other non-cacheable
 * register addresses. When DMA mapping this sort of address we call
 * the operation Peer to Peer as a one device is DMA'ing to another device.
 * For PCI devices the p2pdma APIs must be used to determine if DMA_ATTR_MMIO
 * is appropriate.
 *
 * For architectures that require cache flushing for DMA coherence
 * DMA_ATTR_MMIO will not perform any cache flushing. The address
 * provided must never be mapped cacheable into the CPU.
 */
#define DMA_ATTR_MMIO		(1UL << 10)

/**
 * DMA_ATTR_DEBUGGING_IGNORE_CACHELINES: Indicates the CPU cache line can be
 * overlapped. All mappings sharing a cacheline must have this attribute for
 * this to be considered safe.
 */
#define DMA_ATTR_DEBUGGING_IGNORE_CACHELINES	(1UL << 11)

/**
 * DMA_ATTR_REQUIRE_COHERENT: Indicates that DMA coherency is required.
 * All mappings that carry this attribute can't work with SWIOTLB and cache
 * flushing.
 */
#define DMA_ATTR_REQUIRE_COHERENT	(1UL << 12)

/**
 * A dma_addr_t can hold any valid DMA or bus address for the platform.  It can
 * be given to a device to use as a DMA source or target.  It is specific to a
 * given device and there may be a translation between the CPU physical address
 * space and the bus address space.
 *
 * DMA_MAPPING_ERROR is the magic error code if a mapping failed.  It should not
 * be used directly in drivers, but checked for using dma_mapping_error()
 * instead.
 */
#define DMA_MAPPING_ERROR		(~(dma_addr_t)0)

#define DMA_BIT_MASK(n)	GENMASK_ULL((n) - 1, 0)

/**
 * struct Description: This structure holds the state of an IOVA (I/O Virtual Address) mapping. It contains the DMA address and the size of the mapping, with the high bit used to indicate whether SWIOTLB was used. It is used to track and manage IOVA mappings for DMA operations.
 */
struct dma_iova_state {
	dma_addr_t addr;
	u64 __size;
};

/**
 * Use the high bit to mark if we used swiotlb for one or more ranges.
 */
#define DMA_IOVA_USE_SWIOTLB		(1ULL << 63)

/**
 * Function Description: Returns the size of an IOVA mapping. It extracts the size from the state structure by masking out the SWIOTLB flag bit. This helps determine how much memory is mapped for DMA operations.
 */
static inline size_t dma_iova_size(struct dma_iova_state *state)
{
	/* Casting is needed for 32-bits systems */
	return (size_t)(state->__size & ~DMA_IOVA_USE_SWIOTLB);
}

#ifdef CONFIG_DMA_API_DEBUG
void debug_dma_mapping_error(struct device *dev, dma_addr_t dma_addr);
void debug_dma_map_single(struct device *dev, const void *addr,
		unsigned long len);
#else

/**
 * Function Description: This is a debug function that logs when a DMA mapping error occurs. It helps developers track and debug DMA mapping failures. When debugging is disabled, this function does nothing.
 */
static inline void debug_dma_mapping_error(struct device *dev,
		dma_addr_t dma_addr)
{
}

/**
 * Description: This is a debug function that logs when a single DMA mapping is created. It helps developers track DMA mapping activities. When debugging is disabled, this function does nothing. 
 */
static inline void debug_dma_map_single(struct device *dev, const void *addr,
		unsigned long len)
{
}
#endif /* CONFIG_DMA_API_DEBUG */

#ifdef CONFIG_HAS_DMA
/**
 * Function Description: Checks if a DMA mapping operation failed. It compares the returned DMA address with the error value and returns an error code if the mapping failed. This should be called after every DMA mapping to check for errors.
 */
static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	debug_dma_mapping_error(dev, dma_addr);

	if (unlikely(dma_addr == DMA_MAPPING_ERROR))
		return -ENOMEM;
	return 0;
}

dma_addr_t dma_map_page_attrs(struct device *dev, struct page *page,
		size_t offset, size_t size, enum dma_data_direction dir,
		unsigned long attrs);
void dma_unmap_page_attrs(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir, unsigned long attrs);
dma_addr_t dma_map_phys(struct device *dev, phys_addr_t phys, size_t size,
		enum dma_data_direction dir, unsigned long attrs);
void dma_unmap_phys(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir, unsigned long attrs);
unsigned int dma_map_sg_attrs(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction dir, unsigned long attrs);
void dma_unmap_sg_attrs(struct device *dev, struct scatterlist *sg,
				      int nents, enum dma_data_direction dir,
				      unsigned long attrs);
int dma_map_sgtable(struct device *dev, struct sg_table *sgt,
		enum dma_data_direction dir, unsigned long attrs);
dma_addr_t dma_map_resource(struct device *dev, phys_addr_t phys_addr,
		size_t size, enum dma_data_direction dir, unsigned long attrs);
void dma_unmap_resource(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir, unsigned long attrs);
void *dma_alloc_attrs(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t flag, unsigned long attrs);
void dma_free_attrs(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_handle, unsigned long attrs);
void *dmam_alloc_attrs(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs);
void dmam_free_coherent(struct device *dev, size_t size, void *vaddr,
		dma_addr_t dma_handle);
int dma_get_sgtable_attrs(struct device *dev, struct sg_table *sgt,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);
int dma_mmap_attrs(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);
bool dma_can_mmap(struct device *dev);
bool dma_pci_p2pdma_supported(struct device *dev);
int dma_set_mask(struct device *dev, u64 mask);
int dma_set_coherent_mask(struct device *dev, u64 mask);
u64 dma_get_required_mask(struct device *dev);
bool dma_addressing_limited(struct device *dev);
size_t dma_max_mapping_size(struct device *dev);
size_t dma_opt_mapping_size(struct device *dev);
unsigned long dma_get_merge_boundary(struct device *dev);
struct sg_table *dma_alloc_noncontiguous(struct device *dev, size_t size,
		enum dma_data_direction dir, gfp_t gfp, unsigned long attrs);
void dma_free_noncontiguous(struct device *dev, size_t size,
		struct sg_table *sgt, enum dma_data_direction dir);
void *dma_vmap_noncontiguous(struct device *dev, size_t size,
		struct sg_table *sgt);
void dma_vunmap_noncontiguous(struct device *dev, void *vaddr);
int dma_mmap_noncontiguous(struct device *dev, struct vm_area_struct *vma,
		size_t size, struct sg_table *sgt);
#else /** CONFIG_HAS_DMA */
static inline dma_addr_t dma_map_page_attrs(struct device *dev,
		struct page *page, size_t offset, size_t size,
		enum dma_data_direction dir, unsigned long attrs)
{
	return DMA_MAPPING_ERROR;
}
static inline void dma_unmap_page_attrs(struct device *dev, dma_addr_t addr,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
}
static inline dma_addr_t dma_map_phys(struct device *dev, phys_addr_t phys,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	return DMA_MAPPING_ERROR;
}
static inline void dma_unmap_phys(struct device *dev, dma_addr_t addr,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
}
static inline unsigned int dma_map_sg_attrs(struct device *dev,
		struct scatterlist *sg, int nents, enum dma_data_direction dir,
		unsigned long attrs)
{
	return 0;
}
static inline void dma_unmap_sg_attrs(struct device *dev,
		struct scatterlist *sg, int nents, enum dma_data_direction dir,
		unsigned long attrs)
{
}
static inline int dma_map_sgtable(struct device *dev, struct sg_table *sgt,
		enum dma_data_direction dir, unsigned long attrs)
{
	return -EOPNOTSUPP;
}
static inline dma_addr_t dma_map_resource(struct device *dev,
		phys_addr_t phys_addr, size_t size, enum dma_data_direction dir,
		unsigned long attrs)
{
	return DMA_MAPPING_ERROR;
}
static inline void dma_unmap_resource(struct device *dev, dma_addr_t addr,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
}
static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return -ENOMEM;
}
static inline void *dma_alloc_attrs(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t flag, unsigned long attrs)
{
	return NULL;
}
static inline void dma_free_attrs(struct device *dev, size_t size,
		void *cpu_addr, dma_addr_t dma_handle, unsigned long attrs)
{
}
static inline void *dmam_alloc_attrs(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs)
{
	return NULL;
}
static inline void dmam_free_coherent(struct device *dev, size_t size,
		void *vaddr, dma_addr_t dma_handle)
{
}
static inline int dma_get_sgtable_attrs(struct device *dev,
		struct sg_table *sgt, void *cpu_addr, dma_addr_t dma_addr,
		size_t size, unsigned long attrs)
{
	return -ENXIO;
}
static inline int dma_mmap_attrs(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs)
{
	return -ENXIO;
}
static inline bool dma_can_mmap(struct device *dev)
{
	return false;
}
static inline bool dma_pci_p2pdma_supported(struct device *dev)
{
	return false;
}
static inline int dma_set_mask(struct device *dev, u64 mask)
{
	return -EIO;
}
static inline int dma_set_coherent_mask(struct device *dev, u64 mask)
{
	return -EIO;
}
static inline u64 dma_get_required_mask(struct device *dev)
{
	return 0;
}
static inline bool dma_addressing_limited(struct device *dev)
{
	return false;
}
static inline size_t dma_max_mapping_size(struct device *dev)
{
	return 0;
}
static inline size_t dma_opt_mapping_size(struct device *dev)
{
	return 0;
}
static inline unsigned long dma_get_merge_boundary(struct device *dev)
{
	return 0;
}
static inline struct sg_table *dma_alloc_noncontiguous(struct device *dev,
		size_t size, enum dma_data_direction dir, gfp_t gfp,
		unsigned long attrs)
{
	return NULL;
}
static inline void dma_free_noncontiguous(struct device *dev, size_t size,
		struct sg_table *sgt, enum dma_data_direction dir)
{
}
static inline void *dma_vmap_noncontiguous(struct device *dev, size_t size,
		struct sg_table *sgt)
{
	return NULL;
}
static inline void dma_vunmap_noncontiguous(struct device *dev, void *vaddr)
{
}
static inline int dma_mmap_noncontiguous(struct device *dev,
		struct vm_area_struct *vma, size_t size, struct sg_table *sgt)
{
	return -EINVAL;
}
#endif /** CONFIG_HAS_DMA */

#ifdef CONFIG_IOMMU_DMA
/**
 * dma_use_iova - check if the IOVA API is used for this state
 * @state: IOVA state
 *
 * Return %true if the DMA transfers uses the dma_iova_*() calls or %false if
 * they can't be used.
 * 
 * 
 * Function Description: Checks if the IOVA API is being used for a given DMA state. It returns true if the state has a non-zero size, indicating that IOVA mapping is active. This helps determine which DMA path is being used.
 */
static inline bool dma_use_iova(struct dma_iova_state *state)
{
	return state->__size != 0;
}

bool dma_iova_try_alloc(struct device *dev, struct dma_iova_state *state,
		phys_addr_t phys, size_t size);
void dma_iova_free(struct device *dev, struct dma_iova_state *state);
void dma_iova_destroy(struct device *dev, struct dma_iova_state *state,
		size_t mapped_len, enum dma_data_direction dir,
		unsigned long attrs);
int dma_iova_sync(struct device *dev, struct dma_iova_state *state,
		size_t offset, size_t size);
int dma_iova_link(struct device *dev, struct dma_iova_state *state,
		phys_addr_t phys, size_t offset, size_t size,
		enum dma_data_direction dir, unsigned long attrs);
void dma_iova_unlink(struct device *dev, struct dma_iova_state *state,
		size_t offset, size_t size, enum dma_data_direction dir,
		unsigned long attrs);
#else /** CONFIG_IOMMU_DMA */
static inline bool dma_use_iova(struct dma_iova_state *state)
{
	return false;
}
static inline bool dma_iova_try_alloc(struct device *dev,
		struct dma_iova_state *state, phys_addr_t phys, size_t size)
{
	return false;
}
static inline void dma_iova_free(struct device *dev,
		struct dma_iova_state *state)
{
}
static inline void dma_iova_destroy(struct device *dev,
		struct dma_iova_state *state, size_t mapped_len,
		enum dma_data_direction dir, unsigned long attrs)
{
}
static inline int dma_iova_sync(struct device *dev,
		struct dma_iova_state *state, size_t offset, size_t size)
{
	return -EOPNOTSUPP;
}
static inline int dma_iova_link(struct device *dev,
		struct dma_iova_state *state, phys_addr_t phys, size_t offset,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	return -EOPNOTSUPP;
}
static inline void dma_iova_unlink(struct device *dev,
		struct dma_iova_state *state, size_t offset, size_t size,
		enum dma_data_direction dir, unsigned long attrs)
{
}
#endif /** CONFIG_IOMMU_DMA */

#if defined(CONFIG_HAS_DMA) && defined(CONFIG_DMA_NEED_SYNC)
void __dma_sync_single_for_cpu(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir);
void __dma_sync_single_for_device(struct device *dev, dma_addr_t addr,
		size_t size, enum dma_data_direction dir);
void __dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
		int nelems, enum dma_data_direction dir);
void __dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
		int nelems, enum dma_data_direction dir);
bool __dma_need_sync(struct device *dev, dma_addr_t dma_addr);

/**
 * Function Description: Checks if a device needs DMA synchronization operations. It returns true if the device requires sync operations or if debugging is enabled. This helps determine whether to perform sync operations.
 */
static inline bool dma_dev_need_sync(const struct device *dev)
{
	/** Always call DMA sync operations when debugging is enabled */
	return !dev->dma_skip_sync || IS_ENABLED(CONFIG_DMA_API_DEBUG);
}

/**
 * Function Description: Synchronizes a single DMA buffer from the device to the CPU. This ensures the CPU can safely access the buffer after DMA completion. It only performs sync if the device needs it.
 */
static inline void dma_sync_single_for_cpu(struct device *dev, dma_addr_t addr,
		size_t size, enum dma_data_direction dir)
{
	if (dma_dev_need_sync(dev))
		__dma_sync_single_for_cpu(dev, addr, size, dir);
}

/**
 * Function Description: Synchronizes a single DMA buffer from the CPU to the device. This ensures the device can safely access the buffer before starting DMA. It only performs sync if the device needs it.
 */
static inline void dma_sync_single_for_device(struct device *dev,
		dma_addr_t addr, size_t size, enum dma_data_direction dir)
{
	if (dma_dev_need_sync(dev))
		__dma_sync_single_for_device(dev, addr, size, dir);
}

/**
 * Function Description: Synchronizes a scatterlist from the device to the CPU. This ensures the CPU can safely access all buffers after DMA completion. It only performs sync if the device needs it.
 */
static inline void dma_sync_sg_for_cpu(struct device *dev,
		struct scatterlist *sg, int nelems, enum dma_data_direction dir)
{
	if (dma_dev_need_sync(dev))
		__dma_sync_sg_for_cpu(dev, sg, nelems, dir);
}

/**
 * Function Description: Synchronizes a scatterlist from the CPU to the device. This ensures the device can safely access all buffers before starting DMA. It only performs sync if the device needs it.
 */
static inline void dma_sync_sg_for_device(struct device *dev,
		struct scatterlist *sg, int nelems, enum dma_data_direction dir)
{
	if (dma_dev_need_sync(dev))
		__dma_sync_sg_for_device(dev, sg, nelems, dir);
}

/**
 * Function Description: Checks if a specific DMA buffer needs synchronization. It combines device-level and buffer-level checks to determine if sync is required. Returns true if sync is needed, false otherwise.
 */
static inline bool dma_need_sync(struct device *dev, dma_addr_t dma_addr)
{
	return dma_dev_need_sync(dev) ? __dma_need_sync(dev, dma_addr) : false;
}
bool dma_need_unmap(struct device *dev);
#else /** !CONFIG_HAS_DMA || !CONFIG_DMA_NEED_SYNC */
static inline bool dma_dev_need_sync(const struct device *dev)
{
	return false;
}
static inline void dma_sync_single_for_cpu(struct device *dev, dma_addr_t addr,
		size_t size, enum dma_data_direction dir)
{
}
static inline void dma_sync_single_for_device(struct device *dev,
		dma_addr_t addr, size_t size, enum dma_data_direction dir)
{
}
static inline void dma_sync_sg_for_cpu(struct device *dev,
		struct scatterlist *sg, int nelems, enum dma_data_direction dir)
{
}
static inline void dma_sync_sg_for_device(struct device *dev,
		struct scatterlist *sg, int nelems, enum dma_data_direction dir)
{
}
static inline bool dma_need_sync(struct device *dev, dma_addr_t dma_addr)
{
	return false;
}
static inline bool dma_need_unmap(struct device *dev)
{
	return false;
}
#endif /** !CONFIG_HAS_DMA || !CONFIG_DMA_NEED_SYNC */

struct page *dma_alloc_pages(struct device *dev, size_t size,
		dma_addr_t *dma_handle, enum dma_data_direction dir, gfp_t gfp);
void dma_free_pages(struct device *dev, size_t size, struct page *page,
		dma_addr_t dma_handle, enum dma_data_direction dir);
int dma_mmap_pages(struct device *dev, struct vm_area_struct *vma,
		size_t size, struct page *page);

/**
 * Function Description: Allocates non-coherent memory for DMA operations. It allocates pages and returns the virtual address. This memory may require cache synchronization for proper DMA operation.
 */
static inline void *dma_alloc_noncoherent(struct device *dev, size_t size,
		dma_addr_t *dma_handle, enum dma_data_direction dir, gfp_t gfp)
{
	struct page *page = dma_alloc_pages(dev, size, dma_handle, dir, gfp);
	return page ? page_address(page) : NULL;
}

/**
 * Function Description: Frees non-coherent DMA memory. It releases the pages back to the system. This is the cleanup function for dma_alloc_noncoherent().
 */
static inline void dma_free_noncoherent(struct device *dev, size_t size,
		void *vaddr, dma_addr_t dma_handle, enum dma_data_direction dir)
{
	dma_free_pages(dev, size, virt_to_page(vaddr), dma_handle, dir);
}

/**
 * Function Description: Maps a single memory buffer for DMA operations. It checks that the buffer is not vmalloc memory, then maps the underlying page. This is the main function for creating single DMA mappings.
 */
static inline dma_addr_t dma_map_single_attrs(struct device *dev, void *ptr,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	/** DMA must never operate on areas that might be remapped. */
	if (dev_WARN_ONCE(dev, is_vmalloc_addr(ptr),
			  "rejecting DMA map of vmalloc memory\n"))
		return DMA_MAPPING_ERROR;
	debug_dma_map_single(dev, ptr, size);
	return dma_map_page_attrs(dev, virt_to_page(ptr), offset_in_page(ptr),
			size, dir, attrs);
}

/**
 * Function Description: Unmaps a single memory buffer that was mapped for DMA. It calls the page unmap function to remove the DMA mapping. This is the cleanup function for dma_map_single_attrs().
 */
static inline void dma_unmap_single_attrs(struct device *dev, dma_addr_t addr,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	return dma_unmap_page_attrs(dev, addr, size, dir, attrs);
}

/**
 * Function Description: Synchronizes a range within a single DMA buffer from the device to the CPU. It calculates the full address and calls the single sync function. This is useful for syncing only part of a buffer.
 */
static inline void dma_sync_single_range_for_cpu(struct device *dev,
		dma_addr_t addr, unsigned long offset, size_t size,
		enum dma_data_direction dir)
{
	return dma_sync_single_for_cpu(dev, addr + offset, size, dir);
}

/**
 * Function Description: Synchronizes a range within a single DMA buffer from the CPU to the device. It calculates the full address and calls the single sync function. This is useful for syncing only part of a buffer.
 */
static inline void dma_sync_single_range_for_device(struct device *dev,
		dma_addr_t addr, unsigned long offset, size_t size,
		enum dma_data_direction dir)
{
	return dma_sync_single_for_device(dev, addr + offset, size, dir);
}

/**
 * dma_unmap_sgtable - Unmap the given buffer for DMA
 * @dev:	The device for which to perform the DMA operation
 * @sgt:	The sg_table object describing the buffer
 * @dir:	DMA direction
 * @attrs:	Optional DMA attributes for the unmap operation
 *
 * Unmaps a buffer described by a scatterlist stored in the given sg_table
 * object for the @dir DMA operation by the @dev device. After this function
 * the ownership of the buffer is transferred back to the CPU domain.
 * 
 * 
 * Function Description: Unmaps a scatterlist table that was mapped for DMA. It calls the scatterlist unmap function on the original number of entries. This is the cleanup function for dma_map_sgtable().
 */
static inline void dma_unmap_sgtable(struct device *dev, struct sg_table *sgt,
		enum dma_data_direction dir, unsigned long attrs)
{
	dma_unmap_sg_attrs(dev, sgt->sgl, sgt->orig_nents, dir, attrs);
}

/**
 * dma_sync_sgtable_for_cpu - Synchronize the given buffer for CPU access
 * @dev:	The device for which to perform the DMA operation
 * @sgt:	The sg_table object describing the buffer
 * @dir:	DMA direction
 *
 * Performs the needed cache synchronization and moves the ownership of the
 * buffer back to the CPU domain, so it is safe to perform any access to it
 * by the CPU. Before doing any further DMA operations, one has to transfer
 * the ownership of the buffer back to the DMA domain by calling the
 * dma_sync_sgtable_for_device().
 * 
 * 
 * Function Description: Synchronizes a scatterlist table from the device to the CPU. It calls the scatterlist sync function on all entries. This ensures the CPU can safely access all buffers.
 */
static inline void dma_sync_sgtable_for_cpu(struct device *dev,
		struct sg_table *sgt, enum dma_data_direction dir)
{
	dma_sync_sg_for_cpu(dev, sgt->sgl, sgt->orig_nents, dir);
}

/**
 * dma_sync_sgtable_for_device - Synchronize the given buffer for DMA
 * @dev:	The device for which to perform the DMA operation
 * @sgt:	The sg_table object describing the buffer
 * @dir:	DMA direction
 *
 * Performs the needed cache synchronization and moves the ownership of the
 * buffer back to the DMA domain, so it is safe to perform the DMA operation.
 * Once finished, one has to call dma_sync_sgtable_for_cpu() or
 * dma_unmap_sgtable().
 * 
 * 
 * Function Description: Synchronizes a scatterlist table from the CPU to the device. It calls the scatterlist sync function on all entries. This ensures the device can safely access all buffers.
 */
static inline void dma_sync_sgtable_for_device(struct device *dev,
		struct sg_table *sgt, enum dma_data_direction dir)
{
	dma_sync_sg_for_device(dev, sgt->sgl, sgt->orig_nents, dir);
}

#define dma_map_single(d, a, s, r) dma_map_single_attrs(d, a, s, r, 0)
#define dma_unmap_single(d, a, s, r) dma_unmap_single_attrs(d, a, s, r, 0)
#define dma_map_sg(d, s, n, r) dma_map_sg_attrs(d, s, n, r, 0)
#define dma_unmap_sg(d, s, n, r) dma_unmap_sg_attrs(d, s, n, r, 0)
#define dma_map_page(d, p, o, s, r) dma_map_page_attrs(d, p, o, s, r, 0)
#define dma_unmap_page(d, a, s, r) dma_unmap_page_attrs(d, a, s, r, 0)
#define dma_get_sgtable(d, t, v, h, s) dma_get_sgtable_attrs(d, t, v, h, s, 0)
#define dma_mmap_coherent(d, v, c, h, s) dma_mmap_attrs(d, v, c, h, s, 0)

/**
 * Function Description: Checks if a given physical address and size are suitable for coherent DMA. It verifies that the memory region is within the device's DMA capabilities. Returns true if the memory is suitable, false otherwise.
 */
bool dma_coherent_ok(struct device *dev, phys_addr_t phys, size_t size);

/**
 * Function Description: Allocates coherent DMA memory. This memory does not require cache synchronization and is always consistent between CPU and device. This is the preferred method for DMA allocations.
 */
static inline void *dma_alloc_coherent(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp)
{
	return dma_alloc_attrs(dev, size, dma_handle, gfp,
			(gfp & __GFP_NOWARN) ? DMA_ATTR_NO_WARN : 0);
}

/**
 * Function Description: Frees coherent DMA memory that was allocated with dma_alloc_coherent(). It releases the memory back to the system. This is the cleanup function for coherent allocations.
 */
static inline void dma_free_coherent(struct device *dev, size_t size,
		void *cpu_addr, dma_addr_t dma_handle)
{
	return dma_free_attrs(dev, size, cpu_addr, dma_handle, 0);
}

/**
 * Function Description: Returns the DMA mask of a device. If the device has a valid DMA mask, it returns that value; otherwise, it returns a default 32-bit mask. This is used to determine the device's addressing capabilities.
 */
static inline u64 dma_get_mask(struct device *dev)
{
	if (dev->dma_mask && *dev->dma_mask)
		return *dev->dma_mask;
	return DMA_BIT_MASK(32);
}

/**
 * Set both the DMA mask and the coherent DMA mask to the same thing.
 * Note that we don't check the return value from dma_set_coherent_mask()
 * as the DMA API guarantees that the coherent DMA mask can be set to
 * the same or smaller than the streaming DMA mask.
 * 
 * 
 * Function Description: Sets both the streaming and coherent DMA masks to the same value. It first sets the streaming mask, then the coherent mask. This is a convenience function for setting both masks at once.
 */
static inline int dma_set_mask_and_coherent(struct device *dev, u64 mask)
{
	int rc = dma_set_mask(dev, mask);
	if (rc == 0)
		dma_set_coherent_mask(dev, mask);
	return rc;
}

/**
 * Similar to the above, except it deals with the case where the device
 * does not have dev->dma_mask appropriately setup.
 * 
 * 
 * Function Description: Forces both the streaming and coherent DMA masks to the same value, even if the device doesn't have a DMA mask set up. It sets the dma_mask pointer to point to coherent_dma_mask before setting masks.
 */
static inline int dma_coerce_mask_and_coherent(struct device *dev, u64 mask)
{
	dev->dma_mask = &dev->coherent_dma_mask;
	return dma_set_mask_and_coherent(dev, mask);
}

/**
 * Function Description: Returns the maximum segment size for DMA operations on a device. This is the largest contiguous memory region the device can handle. Returns a default value if not set.
 */
static inline unsigned int dma_get_max_seg_size(struct device *dev)
{
	if (dev->dma_parms && dev->dma_parms->max_segment_size)
		return dev->dma_parms->max_segment_size;
	return SZ_64K;
}

/**
 * Function Description: Sets the maximum segment size for DMA operations on a device. This tells the DMA API the largest contiguous region the device can handle. It stores the value in the device's DMA parameters.
 */
static inline void dma_set_max_seg_size(struct device *dev, unsigned int size)
{
	if (WARN_ON_ONCE(!dev->dma_parms))
		return;
	dev->dma_parms->max_segment_size = size;
}

/**
 * Function Description: Returns the segment boundary mask for a device. This defines the boundary that DMA segments cannot cross. Returns ULONG_MAX if not set.
 */
static inline unsigned long dma_get_seg_boundary(struct device *dev)
{
	if (dev->dma_parms && dev->dma_parms->segment_boundary_mask)
		return dev->dma_parms->segment_boundary_mask;
	return ULONG_MAX;
}

/**
 * dma_get_seg_boundary_nr_pages - return the segment boundary in "page" units
 * @dev: device to guery the boundary for
 * @page_shift: ilog() of the IOMMU page size
 *
 * Return the segment boundary in IOMMU page units (which may be different from
 * the CPU page size) for the passed in device.
 *
 * If @dev is NULL a boundary of U32_MAX is assumed, this case is just for
 * non-DMA API callers.
 * 
 * 
 * Function Description: Returns the segment boundary in page units for a device. It converts the boundary mask into number of pages based on the page shift. This is used by IOMMU drivers for page alignment.
 */
static inline unsigned long dma_get_seg_boundary_nr_pages(struct device *dev,
		unsigned int page_shift)
{
	if (!dev)
		return (U32_MAX >> page_shift) + 1;
	return (dma_get_seg_boundary(dev) >> page_shift) + 1;
}

/**
 * Function Description: Sets the segment boundary mask for a device. This defines the boundary that DMA segments cannot cross. It stores the value in the device's DMA parameters.
 */
static inline void dma_set_seg_boundary(struct device *dev, unsigned long mask)
{
	if (WARN_ON_ONCE(!dev->dma_parms))
		return;
	dev->dma_parms->segment_boundary_mask = mask;
}

/**
 * Function Description: Returns the minimum alignment mask for DMA operations on a device. This defines the alignment requirements for DMA buffers. Returns 0 if not set.
 */
static inline unsigned int dma_get_min_align_mask(struct device *dev)
{
	if (dev->dma_parms)
		return dev->dma_parms->min_align_mask;
	return 0;
}

/**
 * Function Description: Sets the minimum alignment mask for DMA operations on a device. This defines the alignment requirements for DMA buffers. It stores the value in the device's DMA parameters.
 */
static inline void dma_set_min_align_mask(struct device *dev,
		unsigned int min_align_mask)
{
	if (WARN_ON_ONCE(!dev->dma_parms))
		return;
	dev->dma_parms->min_align_mask = min_align_mask;
}

#ifndef dma_get_cache_alignment
/**
 * Function Description: Returns the cache alignment requirement for the architecture. This is the minimum alignment needed for cache-coherent DMA. Returns the architecture's DMA alignment value or 1.
 */
static inline int dma_get_cache_alignment(void)
{
#ifdef ARCH_HAS_DMA_MINALIGN
	return ARCH_DMA_MINALIGN;
#endif
	return 1;
}
#endif

#ifdef ARCH_HAS_DMA_MINALIGN
#define ____dma_from_device_aligned __aligned(ARCH_DMA_MINALIGN)
#else
#define ____dma_from_device_aligned
#endif
/** Mark start of DMA buffer */
#define __dma_from_device_group_begin(GROUP)			\
	__cacheline_group_begin(GROUP) ____dma_from_device_aligned
/** Mark end of DMA buffer */
#define __dma_from_device_group_end(GROUP)			\
	__cacheline_group_end(GROUP) ____dma_from_device_aligned

/**
 * Function Description: Allocates coherent DMA memory that is automatically managed by the device. The memory will be automatically freed when the driver is removed. This is the managed version of dma_alloc_coherent().
 */
static inline void *dmam_alloc_coherent(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp)
{
	return dmam_alloc_attrs(dev, size, dma_handle, gfp,
			(gfp & __GFP_NOWARN) ? DMA_ATTR_NO_WARN : 0);
}

/**
 * Function Description: Allocates DMA memory with write-combining attribute. This can improve performance for certain workloads by allowing write buffering. It adds the WRITE_COMBINE attribute to the allocation.
 */
static inline void *dma_alloc_wc(struct device *dev, size_t size,
				 dma_addr_t *dma_addr, gfp_t gfp)
{
	unsigned long attrs = DMA_ATTR_WRITE_COMBINE;

	if (gfp & __GFP_NOWARN)
		attrs |= DMA_ATTR_NO_WARN;

	return dma_alloc_attrs(dev, size, dma_addr, gfp, attrs);
}

/**
 * Function Description: Frees DMA memory that was allocated with dma_alloc_wc(). It calls the free function with the WRITE_COMBINE attribute. This is the cleanup function for write-combining allocations.
 */
static inline void dma_free_wc(struct device *dev, size_t size,
			       void *cpu_addr, dma_addr_t dma_addr)
{
	return dma_free_attrs(dev, size, cpu_addr, dma_addr,
			      DMA_ATTR_WRITE_COMBINE);
}

/**
 * Function Description: Maps write-combining DMA memory into user space. It creates a user-space mapping with the WRITE_COMBINE attribute. This allows user-space to access write-combining DMA memory.
 */
static inline int dma_mmap_wc(struct device *dev,
			      struct vm_area_struct *vma,
			      void *cpu_addr, dma_addr_t dma_addr,
			      size_t size)
{
	return dma_mmap_attrs(dev, vma, cpu_addr, dma_addr, size,
			      DMA_ATTR_WRITE_COMBINE);
}

#ifdef CONFIG_NEED_DMA_MAP_STATE
#define DEFINE_DMA_UNMAP_ADDR(ADDR_NAME)        dma_addr_t ADDR_NAME
#define DEFINE_DMA_UNMAP_LEN(LEN_NAME)          __u32 LEN_NAME
#define dma_unmap_addr(PTR, ADDR_NAME)           ((PTR)->ADDR_NAME)
#define dma_unmap_addr_set(PTR, ADDR_NAME, VAL)  (((PTR)->ADDR_NAME) = (VAL))
#define dma_unmap_len(PTR, LEN_NAME)             ((PTR)->LEN_NAME)
#define dma_unmap_len_set(PTR, LEN_NAME, VAL)    (((PTR)->LEN_NAME) = (VAL))
#else
#define DEFINE_DMA_UNMAP_ADDR(ADDR_NAME)
#define DEFINE_DMA_UNMAP_LEN(LEN_NAME)
#define dma_unmap_addr(PTR, ADDR_NAME)           \
	({ typeof(PTR) __p __maybe_unused = PTR; 0; })
#define dma_unmap_addr_set(PTR, ADDR_NAME, VAL)  \
	do { typeof(PTR) __p __maybe_unused = PTR; } while (0)
#define dma_unmap_len(PTR, LEN_NAME)             \
	({ typeof(PTR) __p __maybe_unused = PTR; 0; })
#define dma_unmap_len_set(PTR, LEN_NAME, VAL)    \
	do { typeof(PTR) __p __maybe_unused = PTR; } while (0)
#endif

#endif /** _LINUX_DMA_MAPPING_H */
