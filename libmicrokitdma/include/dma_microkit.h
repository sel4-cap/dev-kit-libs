/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <io_dma.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/util.h>
#include <sel4/sel4.h>
#include <utils/attribute.h>

/* Add memory to the dma allocator. This function must be called before using any
 * of the functions below. Pass in the pool to allocate from, the size of this
 * pool in bytes, the page size of the associated mappings and the caching.
 */
int microkit_dma_init(
    void *dma_pool,
    size_t dma_pool_sz,
    size_t page_size,
    bool cached)
NONNULL(1) WARN_UNUSED_RESULT;

/**
 * Allocate memory to be used for DMA.
 *
 * @param size Size in bytes to allocate
 * @param align Alignment constraint in bytes (0 == none)
 *
 * @return Virtual address of allocation or NULL on failure
 */
void *microkit_dma_alloc(
    size_t size,
    unsigned int align,
    bool cached)
ALLOC_SIZE(1) ALLOC_ALIGN(2) MALLOC WARN_UNUSED_RESULT;

/**
 * Free previously allocated DMA memory.
 *
 * @param ptr Virtual address that was allocated (passing NULL is treated as a
 *    no-op)
 * @param size Size that was given in the allocation request
 */
void microkit_dma_free(
    void *ptr,
    size_t size);

/* Return the physical address of a pointer into a DMA buffer. Returns NULL if
 * you pass a pointer into memory that is not part of a DMA buffer. Behaviour
 * is undefined if you pass a pointer into memory that is part of a DMA buffer,
 * but not one currently allocated to you by microkit_dma_alloc_page.
 */
uintptr_t microkit_dma_get_paddr(
    void *ptr);


/* Initialise a DMA manager */
int microkit_dma_manager(
    ps_dma_man_t *man)
NONNULL_ALL WARN_UNUSED_RESULT;

/* Debug functionality for profiling DMA heap usage. This information is
 * returned from a call to `microkit_dma_stats`. Note that this functionality is
 * only available when NDEBUG is not defined.
 */
typedef struct {

    /* The total size of the heap in bytes. */
    size_t heap_size;

    /* The low water mark of available bytes the heap has ever reached. */
    size_t minimum_heap_size;

    /* The current live (allocated) heap space in bytes. Note that the
     * currently available bytes in the heap can be calculated as
     * `heap_size - current_outstanding`
     */
    size_t current_outstanding;

    /* The number of defragmentation attempts that have been performed. Note
     * that no information is provided as to which of these defragmentation
     * operations did useful work.
     */
    uint64_t defragmentations;

    /* Number of coalescing operations that were performed during
     * defragmentations.
     */
    uint64_t coalesces;

    /* Total number of allocation requests (succeeded or failed) that have been
     * performed.
     */
    uint64_t total_allocations;

    /* Number of allocations that initially failed, but then succeeded on
     * retrying after defragmenting the heap.
     */
    uint64_t succeeded_allocations_on_defrag;

    /* Number of failed allocations. This is separated into those that failed
     * because the heap was exhausted and for some other reason. The total
     * failures is calculable by summing them. The succeeded allocations are
     * available by subtracting their sum from `total_allocations`.
     */
    uint64_t failed_allocations_out_of_memory;
    uint64_t failed_allocations_other;

    /* Average allocation request (succeeded or failed) in bytes. */
    size_t average_allocation;

    /* Minimum allocation request (succeeded or failed) in bytes. */
    size_t minimum_allocation;

    /* Maximum allocation request (succeeded or failed) in bytes. */
    size_t maximum_allocation;

    /* Maximum alignment constraint (succeeded or failed) in bytes. */
    int maximum_alignment;

    /* Minimum alignment constraint (succeeded or failed) in bytes. */
    int minimum_alignment;

} microkit_dma_stats_t;

/* Retrieve the above statistics for the current DMA heap. This function is
 * only provided when NDEBUG is not defined. The caller should not modify or
 * free the returned value that may be a static resource.
 */
const microkit_dma_stats_t *microkit_dma_stats(void) RETURNS_NONNULL;

/*
 * This struct describes the information about a frame in a component's DMA pool.
 */
struct dma_frame {
    seL4_CPtr cap;
    size_t size;
    uintptr_t vaddr;
    uintptr_t paddr;
    bool cached;
};
typedef struct dma_frame dma_frame_t;

struct dma_pool {
    uintptr_t start_vaddr;
    uintptr_t end_vaddr;
    size_t frame_size;
    size_t pool_size;
    size_t num_frames;
    /* This is an array of the dma_frame_t structs that represent the frames of
     * a DMA pool, num_frames will determine how many entries are in the
     * array */
    dma_frame_t **dma_frames;
};
typedef struct dma_pool dma_pool_t;