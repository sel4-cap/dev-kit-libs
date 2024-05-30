/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Microkit DMA functionality. Note that parts of this interoperate with
 * generated code to provide complete functionality.
 */

#include <assert.h>
#include <limits.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dma_microkit.h>
#include <error.h>
#include <utils/util.h>
#include <sel4/sel4.h>
#include <uboot_print.h>

/* Check consistency of bookkeeping structures */
#define DEBUG_DMA

extern uintptr_t dma_base;
extern uintptr_t dma_cp_paddr;


/* NOT THREAD SAFE. The code could be made thread safe relatively easily by
 * operating atomically on the free list.
 */

/* We store the free list as a linked-list. If 'head' is NULL that implies we
 * have exhausted our allocation pool.
 */
static void *head;

/* This is a helper function to query the name of the current instance */
extern const char *get_instance_name(void);

/* A node in the free list. Note that the free list is stored as a linked-list
 * of such nodes *within* the DMA pages themselves. This struct is deliberately
 * arranged to be tightly packed (the non-word sized member at the end) so that
 * it consumes as little size as possible. The size of this struct determines
 * the minimum region we can track, and we'd like to be as permissive as
 * possible. Ordinarily this would be achievable in a straightforward way with
 * `__attribute__((packed, aligned(1)))`, but unaligned accesses to uncached
 * memory (which these will live in) are UNPREDICTABLE on some of our platforms
 * like ARMv7.
 */
typedef struct {

    /* This struct also conceptually has the following member. However, it is
     * not directly necessary because the nodes are stored in-place. The
     * virtual address of a region is available as the pointer to the node
     * itself.
     *
     *  void *vaddr;
     */

    /* Flag indicating whether or not this region is cached. */
    bool cached;

    /* The size in bytes of this region. */
    size_t size;

    /* The next node in the list. */
    void *next;

    /* The upper bits of the physical address of this region. We don't need to
     * store the lower bits (the offset into the physical frame) because we can
     * reconstruct these from the offset into the page, obtainable as described
     * above. See `extract_paddr` below.
     */
uintptr_t paddr_upper:
    sizeof(uintptr_t) * 8 - PAGE_BITS_4K;

} region_t;

static void save_paddr(
    region_t *r,
    uintptr_t paddr)
{
    assert(r != NULL);
    r->paddr_upper = paddr >> PAGE_BITS_4K;
}

static uintptr_t PURE try_extract_paddr(
    region_t *r)
{
    assert(r != NULL);
    uintptr_t paddr = r->paddr_upper;
    if (paddr != 0) {
        uintptr_t offset = (uintptr_t)r & MASK(PAGE_BITS_4K);
        paddr = (paddr << PAGE_BITS_4K) | offset;
    }
    return paddr;
}

/* Getting the physical address of a region requires a system call. Since this
 * information is requested quite often, we we try to minimize the number of
 * calls and remember the physical address. For the new region carved out of an
 * existing regions we can calculate its physical address from the cached value
 * and don't need an explicit system call either. */
static void calculate_paddr_for_new_region(
    region_t *r,
    region_t *p,
    size_t offset)
{
    assert(p != NULL);
    assert(r != NULL);
    assert(offset <= p->size);
    uintptr_t base_paddr = try_extract_paddr(p);
    save_paddr(r, base_paddr ? base_paddr + offset : 0);
}


static uintptr_t extract_paddr(
    region_t *r)
{
    uintptr_t paddr = try_extract_paddr(r);
    if (paddr == 0) {
        /* We've never looked up the physical address of this region. Look it
         * up and cache it now.
         */
        paddr = microkit_dma_get_paddr(r);
        assert(paddr != 0);
        save_paddr(r, paddr);
        paddr = try_extract_paddr(r);
    }
    assert(paddr != 0);
    return paddr;
}

/* Various helpers for dealing with the above data structure layout. */
static void prepend_node(
    region_t *node)
{
    assert(node != NULL);
    node->next = head;
    head = node;
}

static void remove_node(
    region_t *previous,
    region_t *node)
{
    assert(node != NULL);
    if (previous == NULL) {
        head = node->next;
    } else {
        previous->next = node->next;
    }
}

static void replace_node(
    region_t *previous,
    region_t *old,
    region_t *new)
{
    assert(old != NULL);
    assert(new != NULL);
    new->next = old->next;
    if (previous == NULL) {
        head = new;
    } else {
        previous->next = new;
    }
}

static void shrink_node(
    region_t *node,
    size_t by)
{
    assert(node != NULL);
    assert(by > 0 && node->size > by);
    node->size -= by;
}

static void grow_node(
    region_t *node,
    size_t by)
{
    assert(node != NULL);
    assert(by > 0);
    node->size += by;
}

#ifdef DEBUG_DMA

/* Check certain assumptions hold on the free list. This function is intended
 * to be a no-op when NDEBUG is defined.
 */
static void check_consistency(void)
{
    if (head == NULL) {
        /* Empty free list. */
        return;
    }

    /* Validate that there are no cycles in the free list using Brent's
     * algorithm.
     */
    region_t *tortoise = head, *hare = tortoise->next;
    for (int power = 1, lambda = 1; hare != NULL; lambda++) {
        assert(tortoise != hare && "cycle in free list");
        if (power == lambda) {
            tortoise = hare;
            power *= 2;
            lambda = 0;
        }
        hare = hare->next;
    }

    /* Validate invariants on individual regions. */
    for (region_t *r = head; r != NULL; r = r->next) {

        assert(r != NULL && "a region includes NULL");

        assert(extract_paddr(r) != 0 && "a region includes physical frame 0");

        assert(r->size > 0 && "a region has size 0");

        assert(r->size >= sizeof(region_t) && "a region has an invalid size");

        assert(UINTPTR_MAX - (uintptr_t)r >= r->size &&
               "a region overflows in virtual address space");

        assert(UINTPTR_MAX - extract_paddr(r) >= r->size &&
               "a region overflows in physical address space");
    }

    /* Ensure no regions overlap. */
    for (region_t *r = head; r != NULL; r = r->next) {
        for (region_t *p = head; p != r; p = p->next) {

            uintptr_t r_vaddr UNUSED = (uintptr_t)r,
                              p_vaddr UNUSED = (uintptr_t)p,
                                      r_paddr UNUSED = extract_paddr(r),
                                              p_paddr UNUSED = extract_paddr(p);

            assert(!((r_vaddr >= p_vaddr && r_vaddr < p_vaddr + p->size) ||
                     (p_vaddr >= r_vaddr && p_vaddr < r_vaddr + r->size)) &&
                   "two regions overlap in virtual address space");

            assert(!((r_paddr >= p_paddr && r_paddr < p_paddr + p->size) ||
                     (p_paddr >= r_paddr && p_paddr < r_paddr + r->size)) &&
                   "two regions overlap in physical address space");
        }
    }
}
#else
#define check_consistency()
#endif

#ifdef NDEBUG
#define STATS(arg) do { } while (0)
#else
/* Statistics functionality. */

#define STATS(arg) do { arg; } while (0)

static microkit_dma_stats_t stats;

static size_t total_allocation_bytes;

const microkit_dma_stats_t *microkit_dma_stats(void)
{
    if (stats.total_allocations > 0) {
        stats.average_allocation = total_allocation_bytes / stats.total_allocations;
    } else {
        stats.average_allocation = 0;
    }
    return (const microkit_dma_stats_t *)&stats;
}
#endif

/* Defragment the free list. Can safely be called at any time. The complexity
 * of this function is at least O(n^2).
 *
 * Over time the free list can evolve to contain separate chunks that are
 * actually contiguous, both physically and virtually. This fragmentation can
 * result in unnecessary allocation failures, so this function is provided to
 * coalesce such chunks. For example, the free list may end up like:
 *
 *  +---------------+    +---------------+    +---------------+
 *  | vaddr: 0x4000 |    | vaddr: 0x7000 |    | vaddr: 0x2000 |
 *  | size : 0x1000 |    | size : 0x2000 |    | size : 0x2000 |
 *  | next : -------|--->| next : -------|--->| next : NULL   |
 *  | paddr: 0x6000 |    │ paddr: 0x8000 |    | paddr: 0x4000 |
 *  +---------------+    +---------------+    +---------------+
 *
 * after defragmentation, the free list will look like:
 *
 *  +---------------+    +---------------+
 *  | vaddr: 0x2000 |    | vaddr: 0x7000 |
 *  | size : 0x3000 |    | size : 0x2000 |
 *  | next : -------|--->| next : NULL   |
 *  | paddr: 0x4000 |    | paddr: 0x8000 |
 *  +---------------+    +---------------+
 */
static void defrag(void)
{
    assert(head != NULL &&
           "attempted defragmentation of DMA free list before initialisation");

    check_consistency();

    STATS(stats.defragmentations++);

    /* For each region in the free list... */
    for (region_t *pprev = NULL, *p = head; p != NULL; pprev = p, p = p->next) {

        uintptr_t p_vstart = (uintptr_t)p,           /* start virtual address */
                  p_vend   = (uintptr_t)p + p->size, /* end virtual address */
                  p_pstart = extract_paddr(p),       /* start physical address */
                  p_pend   = p_pstart + p->size;     /* end physical address */
        int p_cached = p->cached;

        /* For each region *before* this one... */
        for (region_t *qprev = NULL, *q = head; q != p; qprev = q, q = q->next) {

            uintptr_t q_vstart = (uintptr_t)q,
                      q_vend   = (uintptr_t)q + q->size,
                      q_pstart = extract_paddr(q),
                      q_pend   = q_pstart + q->size;
            int q_cached = q->cached;

            /* We could not have entered this loop if 'p' was the head of the
             * free list.
             */
            assert(pprev != NULL);

            if (p_vstart == q_vend && p_pstart == q_pend && p_cached == q_cached) {
                /* 'p' immediately follows the region 'q'. Coalesce 'p' into
                 * 'q'.
                 */
                grow_node(q, p->size);
                remove_node(pprev, p);
                STATS(stats.coalesces++);
                /* Bump the outer scan back to the node we just modified
                 * (accounting for the fact that the next thing we will do is
                 * increment 'p' as we go round the loop). The reason for this
                 * is that our changes may have made further coalescing
                 * possible between the node we modified and where 'p' is
                 * currently pointing.
                 */
                if (qprev == NULL) {
                    /* We just coalesced 'p' into the free list head; reset the
                     * scan. Note that we'll end up skipping the head as we go
                     * round the loop, but that's fine because the body of the
                     * outer loop does nothing for the first iteration.
                     */
                    p = head;
                } else {
                    p = qprev;
                }
                break;
            }

            if (p_vend == q_vstart && p_pend == q_pstart && p_cached == q_cached) {
                /* 'p' immediately precedes the region 'q'. Coalesce 'q' into
                 * 'p'.
                 */
                grow_node(p, q->size);
                remove_node(qprev, q);
                STATS(stats.coalesces++);

                /* Similar to above, we bump the outer scan back so we
                 * reconsider 'p' again the next time around the loop. Now that
                 * we've expanded 'p' there may be further coalescing we can
                 * do.
                 */
                p = pprev;
                break;
            }
        }
    }

    check_consistency();
}

static void free_region(
    void *ptr,
    size_t size,
    bool cached)
{
    /* Although we've already checked the address, do another quick sanity check */
    assert(ptr != NULL);

    /* If the user allocated a region that was too small, we would have rounded
     * up the size during allocation.
     */
    if (size < sizeof(region_t)) {
        size = sizeof(region_t);
    }

    /* The 'size' of all allocated chunk should be aligned to the bookkeeping
     * struct, so bump it to the actual size we have allocated.
     */
    if (size % __alignof__(region_t) != 0) {
        size = ROUND_UP(size, __alignof__(region_t));
    }

    /* We should have never allocated memory that is insufficiently aligned to
     * host bookkeeping data now that it has been returned to us.
     */
    assert((uintptr_t)ptr % alignof(region_t) == 0);

    STATS(({
        if (size >= stats.current_outstanding)
        {
            stats.current_outstanding = 0;
        } else
        {
            stats.current_outstanding -= size;
        }
    }));

    region_t *p = ptr;
    p->paddr_upper = 0;
    p->size = size;
    p->cached = cached;
    prepend_node(p);

    check_consistency();
}

/* Initialise DMA */
int microkit_dma_init(
    void *dma_pool,
    size_t dma_pool_sz,
    size_t page_size,
    bool cached)
{

    /* The caller should have passed us a valid DMA pool. */
    if (page_size != 0 && (page_size <= sizeof(region_t) ||
                           (uintptr_t)dma_pool % page_size != 0))  {
        return -1;
    }

    /* Bail out if the caller gave us an insufficiently aligned pool. */
    if (dma_pool == NULL || (uintptr_t)dma_pool % alignof(region_t) != 0) {
        return -1;
    }

    /* We're going to store bookkeeping in the DMA pages, that we expect to be
     * power-of-2-sized, so the bookkeeping struct better be
     * power-of-2-aligned. Your compiler should always guarantee this.
     */
    assert(IS_POWER_OF_2(alignof(region_t)) &&
                  "region_t is not power-of-2-aligned");

    /* The page size the caller has given us should be a power of 2 and at least
     * the alignment of `region_t`.
     */
    if (page_size != 0 && (!IS_POWER_OF_2(page_size) ||
                           page_size < alignof(region_t))) {
        return -1;
    }

    STATS(stats.heap_size = dma_pool_sz);
    STATS(stats.minimum_heap_size = dma_pool_sz);
    STATS(stats.minimum_allocation = SIZE_MAX);
    STATS(stats.minimum_alignment = INT_MAX);

    /* Split dma pool into regions */
    for (void *base = dma_pool; base < dma_pool + dma_pool_sz;
            base += page_size) {
            assert((uintptr_t)base % alignof(region_t) == 0 &&
                   "we misaligned the DMA pool base address during "
                   "initialisation");
            free_region(base, page_size, cached);
        }

    check_consistency();

    return 0;
}


/* Get physical address from virtual address, dma_cp_paddr is provided in the system file */
uintptr_t microkit_dma_get_paddr(
    void *ptr)
{
    int offset = (uint64_t)ptr - (int)dma_base;
    return (uintptr_t*)(dma_cp_paddr + offset);
}

/* Allocate a DMA region from a free region. */
static void *try_alloc_from_free_region(
    size_t size,
    unsigned int align,
    region_t *prev,
    region_t *p)
{
    /* Our caller should have rounded 'size' up. */
    assert(size >= sizeof(region_t));

    /* The caller should have ensured 'size' is aligned to the bookkeeping
     * struct, so that the bookkeeping we may have to write for the remainder
     * chunk of a region is aligned.
     */
    assert(size % alignof(region_t) == 0);

    /* The caller should have ensured that the alignment requirements are
     * sufficient that any chunk we ourselves allocate, can later host
     * bookkeeping in its initial bytes when it is freed.
     */
    assert(align >= alignof(region_t));

    uintptr_t p_end = (uintptr_t)p + p->size;

    /* Each region starts with a metadata header of sizeof(region_t) bytes. We
     * start scanning from the end, so we can leave this header in place if
     * parts of the block can be used to fulfill the allocation request.
     */
    for (uintptr_t q = ROUND_DOWN(p_end - size, align);
         (q == (uintptr_t)p) || (q >= (uintptr_t)p + sizeof(region_t));
         q -= align) {
        

        uintptr_t q_end = (uintptr_t)q + size;

        /* Check if this is a suitable chunk, it must be big enough to satisfy
         * the callers memory needs and leave enough room to turn the cut off
         * suffix into a new chunk.
         */
        uintptr_t new_chunk_size = p_end - q_end;
        if ((0 != new_chunk_size) && (new_chunk_size < sizeof(region_t))) {
            continue;
        }

        /* Found something that satisfies the caller's requirements and
         * leaves us enough room to turn the cut off suffix into a new
         * chunk. There are four possible cases here... */
        if ((uintptr_t)p == q) {
            if (p->size == size) {
                /* 1. We're giving them the whole chunk; we can just remove
                 * this node.
                 */
                remove_node(prev, p);
            } else {
                /* 2. We're giving them the start of the chunk. We need to
                 * extract the end as a new node.
                 */
                region_t *r = (region_t *)((uintptr_t)p + size);
                r->size = p->size - size;
                calculate_paddr_for_new_region(r, p, size);
                replace_node(prev, p, r);
            }
        } else if (0 == new_chunk_size) {
            /* 3. We're giving them the end of the chunk. We need to shrink the
             * existing node.
             */
            shrink_node(p, size);
        } else {
            /* 4. We're giving them the middle of a chunk. We need to shrink the
             * existing node and extract the end as a new node.
             */
            size_t new_p_size = q - (uintptr_t)p;

            region_t *r = (region_t *)(q + size);
            size_t offset = new_p_size + size;
            r->size = p->size - offset;
            calculate_paddr_for_new_region(r, p, offset);
            p->size = new_p_size;
            prepend_node(r);
        }

        return (void *)q;
    }

    /* Region can't be used. */
    return NULL;
}

/* Allocate a DMA region from a block in the list of free regions */
static void *try_alloc_from_free_list(
    size_t size,
    unsigned int align,
    bool cached)
{
    /* For each region in the free list... */
    for (region_t *prev = NULL, *p = head; p != NULL; prev = p, p = p->next) {

        /* Check if region can satisfy the allocation request. */
        if ((p->size < size) || (p->cached != cached)) {
            continue;
        }

        /* Try to allocate a DMA region within this region. */
        void *q = try_alloc_from_free_region(size, align, prev, p);
        if (NULL != q) {
            return q;
        }
    }

    /* No satisfying region found. */
    return NULL;
}

void *microkit_dma_alloc(
    size_t size,
    unsigned int align,
    bool cached)
{

    STATS(({
        stats.total_allocations++;
        if (size < stats.minimum_allocation)
        {
            stats.minimum_allocation = size;
        }
        if (size > stats.maximum_allocation)
        {
            stats.maximum_allocation = size;
        }
        if (align < stats.minimum_alignment)
        {
            stats.minimum_alignment = align;
        }
        if (align > stats.maximum_alignment)
        {
            stats.maximum_alignment = align;
        }
        total_allocation_bytes += size;
    }));

    if (head == NULL) {
        /* Nothing in the free list. */
        UBOOT_LOGE("DMA pool empty, can't alloc block of size %zu (align=%u, cached=%u)",
                size, align, cached);
        STATS(stats.failed_allocations_out_of_memory++);
        return NULL;
    }

    if (align == 0) {
        /* No alignment requirements. */
        align = 1;
    }

    if (align < alignof(region_t)) {
        /* Allocating something with a weaker alignment constraint than our
         * bookkeeping data may lead to us giving out a chunk of memory that is
         * not sufficiently aligned to host bookkeeping data when it is
         * returned to us. Bump it up in this case.
         */
        align = alignof(region_t);
    }

    if (size < sizeof(region_t)) {
        /* We need to bump up smaller allocations because they may be freed at
         * a point when they cannot be conjoined with another chunk in the heap
         * and therefore need to become host to region_t metadata.
         */
        size = sizeof(region_t);
    }

    if (size % alignof(region_t) != 0) {
        /* We need to ensure that 'size' is aligned to the bookkeeping
         * struct, so that the remainder chunk of a region is aligned.
         */
        size = ROUND_UP(size, alignof(region_t));
    }

    void *p = try_alloc_from_free_list(size, align, cached);
    if ((p == NULL) && (size > sizeof(region_t))) {
        /* We failed to allocate a matching region, but we may be able to
         * satisfy this allocation by defragmenting the free list and
         * re-attempting.
         */
        UBOOT_LOGI("re-try allocation after defragmentation of free list");
        defrag();
        p = try_alloc_from_free_list(size, align, cached);
        if (p != NULL) {
            STATS(stats.succeeded_allocations_on_defrag++);
        }
    }

    check_consistency();

    if (p == NULL) {
        STATS(stats.failed_allocations_other++);
    } else {
        STATS(({
            stats.current_outstanding += size;
            if (stats.heap_size - stats.current_outstanding < stats.minimum_heap_size)
            {
                stats.minimum_heap_size = stats.heap_size - stats.current_outstanding;
            }
        }));
    }

    return p;
}

void microkit_dma_free(
    void *ptr,
    size_t size)
{
    // Cached is set to true in the system file 
    bool cached = 1;

    /* Call the common function to free the DMA memory */
    free_region(ptr, size, cached);
}

/* The remaining functions are to comply with the ps_io_ops-related interface
 * from libplatsupport. Note that many of the operations are no-ops, because
 * our case is somewhat constrained.
 */

static void *dma_alloc(
    size_t size,
    int align,
    int cached,
    ps_mem_flags_t flags UNUSED)
{
    return microkit_dma_alloc(size, align, cached);
}

static void dma_free(
    void *addr,
    size_t size)
{
    microkit_dma_free(addr, size);
}

/* All Microkit DMA pages are pinned for the duration of execution, so this is
 * effectively a no-op.
 */
static uintptr_t dma_pin(
    void *addr,
    size_t size UNUSED)
{
    return microkit_dma_get_paddr(addr);
}

/* As above, all pages are pinned so this is also a no-op. */
static void dma_unpin(
    void *addr UNUSED,
    size_t size UNUSED)
{
    /* empty */
}

static void dma_cache_op(
    void *addr UNUSED,
    size_t size UNUSED,
    dma_cache_op_t op UNUSED)
{
    /* x86 DMA is usually cache coherent and doesn't need maintenance ops */
#ifdef CONFIG_ARCH_ARM
    switch (op) {
    case DMA_CACHE_OP_CLEAN:
        // seL4_ARM_Page_Clean_Data(frame_cap, frame_start_offset, frame_start_offset + size);
        seL4_ARM_VSpace_CleanInvalidate_Data(3, addr, addr + size);
        break;
    case DMA_CACHE_OP_INVALIDATE:
        //seL4_ARM_Page_Invalidate_Data(frame_cap, frame_start_offset, frame_start_offset + size);
        seL4_ARM_VSpace_Invalidate_Data(3, addr, addr + size);
        break;
    case DMA_CACHE_OP_CLEAN_INVALIDATE:
        // seL4_ARM_Page_CleanInvalidate_Data(frame_cap, frame_start_offset, frame_start_offset + size);
        seL4_ARM_VSpace_CleanInvalidate_Data(3, addr, addr + size);
        break;
    default:
        UBOOT_LOGF("Invalid cache_op %d", op);
        return;
    }
#endif
}

/* Initialise DMA manager */
int microkit_dma_manager(
    ps_dma_man_t *man)
{
    if (man == NULL) {
        UBOOT_LOGE("man is NULL");
        return -1;
    }
    man->dma_alloc_fn = dma_alloc;
    man->dma_free_fn = dma_free;
    man->dma_pin_fn = dma_pin;
    man->dma_unpin_fn = dma_unpin;
    man->dma_cache_op_fn = dma_cache_op;
    return 0;
}
