/* 
 * Copyright 2022, Capgemini Engineering
 * 
 * SPDX-License-Identifier: BSD-2-Clause
 *
 */

#include <io_dma.h>
#include <linux/dma-direction.h>

#define MAX_DMA_ALLOCS 256

struct dma_allocation_t {
    /* Base data for all DMA allocations */
    bool in_use;
    bool is_mapping;
    void *public_vaddr; /* The vaddr used outside of this file */
    void *mapped_vaddr; /* The vaddr that is mapped to the paddr */
    void *paddr;
    size_t size;
    /* Additional data relevant only to DMA mappings */
    enum dma_data_direction mapping_dir;
};

static struct dma_allocation_t dma_alloc[MAX_DMA_ALLOCS];

static ps_dma_man_t *sel4_dma_manager = NULL;


static int next_free_allocation_index(void)
{
    for (int x = 0; x < MAX_DMA_ALLOCS; x++)
        if (!dma_alloc[x].in_use) return x;
    return -1;
}

static int find_allocation_index_by_public_vaddr(void *addr)
{
    for (int x = 0; x < MAX_DMA_ALLOCS; x++) {
        if (dma_alloc[x].in_use &&
            dma_alloc[x].public_vaddr == addr &&
            dma_alloc[x].size == 0)
            return x;
        if (dma_alloc[x].in_use &&
            dma_alloc[x].public_vaddr <= addr &&
            dma_alloc[x].public_vaddr + dma_alloc[x].size > addr)
            return x;
    }
    return -1;
}

static int find_allocation_index_by_mapped_vaddr(void *addr)
{
    for (int x = 0; x < MAX_DMA_ALLOCS; x++) {
        if (dma_alloc[x].in_use &&
            dma_alloc[x].mapped_vaddr == addr &&
            dma_alloc[x].size == 0)
            return x;
        if (dma_alloc[x].in_use &&
            dma_alloc[x].mapped_vaddr <= addr &&
            dma_alloc[x].mapped_vaddr + dma_alloc[x].size > addr)
            return x;
    }
    return -1;
}

static int find_allocation_index_by_paddr(void *addr)
{
    for (int x = 0; x < MAX_DMA_ALLOCS; x++) {
        if (dma_alloc[x].in_use &&
            dma_alloc[x].paddr == addr &&
            dma_alloc[x].size == 0)
            return x;
        if (dma_alloc[x].in_use &&
            dma_alloc[x].paddr <= addr &&
            dma_alloc[x].paddr + dma_alloc[x].size > addr)
            return x;
    }
    return -1;
}

static void clear_allocation(int alloc_index)
{
    dma_alloc[alloc_index].in_use = false;
    dma_alloc[alloc_index].is_mapping = false;
    dma_alloc[alloc_index].public_vaddr = NULL;
    dma_alloc[alloc_index].mapped_vaddr = NULL;
    dma_alloc[alloc_index].paddr = 0;
    dma_alloc[alloc_index].size = 0;
    dma_alloc[alloc_index].mapping_dir = DMA_NONE;
}

void *sel4_dma_phys_to_virt(void *paddr)
{
    assert(sel4_dma_manager != NULL);

    // Find the allocation containing this address.
    int alloc_index = find_allocation_index_by_paddr(paddr);
    if (alloc_index >= 0)
        return dma_alloc[alloc_index].public_vaddr +
            (paddr - dma_alloc[alloc_index].paddr);

    UBOOT_LOGE("Unable to determine virtual address from physical %p", paddr);
    /* This is a fatal error. Not being able to determine an address
        * indicates that we are attempting to communicate with a
        * device via memory that has not been mapped into the physical
        * address space. This implies that additional data needs to be
        * DMA allocated. */
    assert(false);
}


void *sel4_dma_virt_to_phys(void *vaddr)
{
    assert(sel4_dma_manager != NULL);

    // Find the allocation containing this address.
    int alloc_index = find_allocation_index_by_public_vaddr(vaddr);
    if (alloc_index >= 0)
        return dma_alloc[alloc_index].paddr +
            (vaddr - dma_alloc[alloc_index].public_vaddr);

    UBOOT_LOGE("Unable to determine physical address from virtual %p", vaddr);
    /* This is a fatal error. Not being able to determine an address
        * indicates that we are attempting to communicate with a
        * device via memory that has not been mapped into the physical
        * address space. This implies that additional data needs to be
        * DMA allocated. */
    assert(false);
}

bool sel4_dma_is_mapped(void *vaddr)
{
    assert(sel4_dma_manager != NULL);

    return (find_allocation_index_by_public_vaddr(vaddr) >= 0);
}

void sel4_dma_flush_range(void *start, void *stop)
{
    assert(sel4_dma_manager != NULL);

    /* Only support cases where both the start and end address are addresses
     * we have previously allocated and mapped, and are in the same allocation */
    int alloc_index = find_allocation_index_by_public_vaddr(start);
    if (alloc_index < 0) {
        UBOOT_LOGD("Flushed start address is not DMA allocated: %p", start);
        return;
    }

    /* If this is mapped in the 'to device' direction then we need to start by
     * copying the mapped virtual data to the DMA-backed area before flushing */
    if (dma_alloc[alloc_index].is_mapping &&
        dma_alloc[alloc_index].mapping_dir == DMA_TO_DEVICE)
        memcpy(
            dma_alloc[alloc_index].mapped_vaddr,
            dma_alloc[alloc_index].public_vaddr,
            dma_alloc[alloc_index].size);

    /* Determine how much data to flush */
    size_t flush_size;
    if (stop > start)
        flush_size = (size_t) (stop - start);
    else if (stop == start)
        /* Default to flushing one cache line */
        flush_size = (size_t) CONFIG_SYS_CACHELINE_SIZE;
    else
        return;

    /* Determine the address to flush from */
    void *flush_start = dma_alloc[alloc_index].mapped_vaddr +
            ((void*) start - dma_alloc[alloc_index].public_vaddr);

    /* Perform the flush */
    sel4_dma_manager->dma_cache_op_fn(
        flush_start,
        flush_size,
        DMA_CACHE_OP_CLEAN);

    /* If this is mapped in the 'from device' direction then we need to finish
     * by copying the mapped virtual data to the DMA-backed area */
    if (dma_alloc[alloc_index].is_mapping &&
        dma_alloc[alloc_index].mapping_dir == DMA_FROM_DEVICE)
        memcpy(
            dma_alloc[alloc_index].public_vaddr,
            dma_alloc[alloc_index].mapped_vaddr,
            dma_alloc[alloc_index].size);
}

void sel4_dma_invalidate_range(void *start, void *stop)
{
    assert(sel4_dma_manager != NULL);

    /* Only support cases where both the start and end address are addresses
     * we have previously allocated and mapped, and are in the same allocation */
    int alloc_index = find_allocation_index_by_public_vaddr(start);
    if (alloc_index < 0) {
        UBOOT_LOGD("Flushed start address is not DMA allocated: %p", start);
        return;
    }

    /* Determine how much data to invalidate */
    size_t inval_size;
    if (stop > start)
        inval_size = (size_t) (stop - start);
    else if (stop == start)
        /* Default to flushing one cache line */
        inval_size = (size_t) CONFIG_SYS_CACHELINE_SIZE;
    else
        return;

    /* Determine the address to invalidate from */
    void *inval_start = dma_alloc[alloc_index].mapped_vaddr +
            ((void*) start - dma_alloc[alloc_index].public_vaddr);

    sel4_dma_manager->dma_cache_op_fn(
        inval_start,
        inval_size,
        DMA_CACHE_OP_INVALIDATE);

    /* If this is mapped in then we need to finish by copying the mapped
     * (i.e. invalidated) virtual data to the DMA-backed area */
    if (dma_alloc[alloc_index].is_mapping)
        memcpy(
            dma_alloc[alloc_index].public_vaddr,
            dma_alloc[alloc_index].mapped_vaddr,
            dma_alloc[alloc_index].size);
}

void sel4_dma_free(void *vaddr)
{
    assert(sel4_dma_manager != NULL);

    // Find the previous allocation.
    int alloc_index = find_allocation_index_by_public_vaddr(vaddr);
    if (alloc_index < 0) {
        UBOOT_LOGE("Call to free DMA allocation not in bookkeeping");
        return;
    }

    UBOOT_LOGD("vaddr = %p, alloc_index = %i", vaddr, alloc_index);

    sel4_dma_manager->dma_free_fn(
        dma_alloc[alloc_index].mapped_vaddr,
        dma_alloc[alloc_index].size);

    // Allocation cleared. Update bookkeeping.
    clear_allocation(alloc_index);
}

void* sel4_dma_memalign(size_t align, size_t size)
{
    assert(sel4_dma_manager != NULL);

    int alloc_index = next_free_allocation_index();
    if (alloc_index < 0) {
        UBOOT_LOGE("No free DMA allocation slots, unable to allocate");
        return NULL;
    }

    void* mapped_vaddr = sel4_dma_manager->dma_alloc_fn(
        size,
        align,
        true,
        PS_MEM_NORMAL);
   
    if (mapped_vaddr == NULL) {
        UBOOT_LOGE("DMA allocation returned null pointer");
        return NULL;
    }

    void *paddr = (void*) sel4_dma_manager->dma_pin_fn(
        mapped_vaddr,
        size);
    if (paddr == NULL) {
        UBOOT_LOGE("DMA pin return null pointer");
        // Clean up before returning.
        sel4_dma_manager->dma_free_fn(
            mapped_vaddr,
            size);
        return NULL;
    }
    UBOOT_LOGD(
        "size = 0x%x, align = 0x%x, vaddr = %p, paddr = %p, alloc_index = %i",
        size, align, mapped_vaddr, paddr, alloc_index);

    // Memory allocated and pinned. Update bookkeeping.
    dma_alloc[alloc_index].in_use = true;
    dma_alloc[alloc_index].mapped_vaddr = mapped_vaddr;
    dma_alloc[alloc_index].public_vaddr = mapped_vaddr;
    dma_alloc[alloc_index].paddr = paddr;
    dma_alloc[alloc_index].size = size;
    // Not a mapping.
    dma_alloc[alloc_index].is_mapping = false;
    dma_alloc[alloc_index].mapping_dir = DMA_NONE;

    return mapped_vaddr;
}

void* sel4_dma_malloc(size_t size)
{
    /* Default to alignment on cacheline boundaries */
    return sel4_dma_memalign(CONFIG_SYS_CACHELINE_SIZE, size);
}

void sel4_dma_initialise(ps_dma_man_t *dma_manager)
{
    sel4_dma_manager = dma_manager;

    for (int x = 0; x < MAX_DMA_ALLOCS; x++)
        clear_allocation(x);
}

void sel4_dma_shutdown(void)
{
    // Deallocate any currently allocated DMA.
    for (int x = 0; x < MAX_DMA_ALLOCS; x++)
        if (dma_alloc[x].in_use)
            sel4_dma_free(dma_alloc[x].public_vaddr);

    // Clear the pointer to the DMA routines.
    sel4_dma_manager = NULL;
}

/* Routines to support an implementation of the linux 'DMA mapping' API */

void *sel4_dma_map_single(void* public_vaddr, size_t size, enum dma_data_direction dir)
{
    /* Only handle the DMA_TO_DEVICE and DMA_FROM_DEVICE directions */
    if (dir != DMA_TO_DEVICE && dir != DMA_FROM_DEVICE) {
        UBOOT_LOGE("Unhandled DMA mapping direction: %i", dir);
        return NULL;
    }

    /* Start by creating a DMA allocation */
    void* mapped_vaddr = sel4_dma_malloc(size);
    if (mapped_vaddr == NULL)
        return NULL;

    /* Now find the index we just allocated to and update the book-keeping
     * with mapping information */
    int alloc_index = find_allocation_index_by_mapped_vaddr(mapped_vaddr);
    assert(alloc_index >= 0);

    dma_alloc[alloc_index].is_mapping = true;
    dma_alloc[alloc_index].public_vaddr = public_vaddr;
    dma_alloc[alloc_index].mapping_dir = dir;

    /* Flush the cache to make sure all buffers are aligned */
    sel4_dma_flush_range(public_vaddr, public_vaddr + size);

    return (void*) dma_alloc[alloc_index].paddr;
}

void sel4_dma_unmap_single(void* paddr)
{
    /* Find the allocation index to be cleared */
    int alloc_index = find_allocation_index_by_paddr(paddr);
    if (alloc_index < 0) {
        UBOOT_LOGE("Call to clear DMA mapping not in bookkeeping");
        return;
    }

    if (!dma_alloc[alloc_index].is_mapping) {
        UBOOT_LOGE("Call to clear DMA mapping not in bookkeeping");
        return;
    }

    void* public_vaddr = dma_alloc[alloc_index].public_vaddr;
    size_t size = dma_alloc[alloc_index].size;

    /* Flush the cache to make sure all buffers are aligned */
    sel4_dma_flush_range(public_vaddr, public_vaddr + size);

    /* Now free the DAM allocation (which also clears the mapping) */
    sel4_dma_free(public_vaddr);
}

/* Map data cache requests on to DMA requests. Note that U-Boot code that is
 * requesting the data cache to be flushed or invalidated is expecting those
 * addresses to be DMA mapped. */

void invalidate_dcache_range(unsigned long start, unsigned long stop)
{
    sel4_dma_invalidate_range((void*) start, (void *) stop);
}

void flush_dcache_range(unsigned long start, unsigned long stop)
{
	sel4_dma_flush_range((void*) start, (void *) stop);
}