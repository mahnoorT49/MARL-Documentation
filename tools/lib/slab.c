// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <string.h>

#include <urcu/uatomic.h>
#include <linux/slab.h>
#include <malloc.h>
#include <linux/gfp.h>

int kmalloc_nr_allocated;
int kmalloc_verbose;

/**
 *Function Description: Allocates memory of the given size using malloc(). It first checks if memory reclaim is allowed; if not, it returns NULL. After allocation, it increases the global counter to track how many allocations are active. If verbose mode is on, it prints the address of the allocated memory. If the __GFP_ZERO flag is set, it fills the memory with zeros before returning the pointer.
 */
void *kmalloc(size_t size, gfp_t gfp)
{
	void *ret;

	if (!(gfp & __GFP_DIRECT_RECLAIM))
		return NULL;

	ret = malloc(size);
	uatomic_inc(&kmalloc_nr_allocated);
	if (kmalloc_verbose)
		printf("Allocating %p from malloc\n", ret);
	if (gfp & __GFP_ZERO)
		memset(ret, 0, size);
	return ret;
}

/**
 *Function Description: Frees the memory pointed to by the given pointer using free(). It first checks if the pointer is NULL; if so, it does nothing. Before freeing, it decreases the global allocation counter to keep track of active allocations. If verbose mode is enabled, it prints the address of the memory being freed.
 */
void kfree(void *p)
{
	if (!p)
		return;
	uatomic_dec(&kmalloc_nr_allocated);
	if (kmalloc_verbose)
		printf("Freeing %p to malloc\n", p);
	free(p);
}

/**
 *Function Description: Allocates memory for an array of n items, where each item is size bytes, using calloc() (which also zeroes the memory). It first checks if memory reclaim is allowed; if not, it returns NULL. After allocation, it increases the global counter and prints the address if verbose mode is on. If the __GFP_ZERO flag is set, it explicitly zeroes the memory again to be safe, then returns the pointer.
 */
void *kmalloc_array(size_t n, size_t size, gfp_t gfp)
{
	void *ret;

	if (!(gfp & __GFP_DIRECT_RECLAIM))
		return NULL;

	ret = calloc(n, size);
	uatomic_inc(&kmalloc_nr_allocated);
	if (kmalloc_verbose)
		printf("Allocating %p from calloc\n", ret);
	if (gfp & __GFP_ZERO)
		memset(ret, 0, n * size);
	return ret;
}
