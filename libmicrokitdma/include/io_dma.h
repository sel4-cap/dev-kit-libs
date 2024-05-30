#include <microkit.h>

/**
 * Memory usage hints. These indicate how memory is expected to be used
 * allowing for better memory attributes or caching to be done.
 * For example, memory that is only written to would be best mapped
 * write combining if the architecture supports it.
 */
typedef enum ps_mem_flags {
    PS_MEM_NORMAL, /* No hints, consider 'normal' memory */
    PS_MEM_HR,     /* Host typically reads */
    PS_MEM_HW      /* Host typically writes */
} ps_mem_flags_t;

typedef enum dma_cache_op {
    DMA_CACHE_OP_CLEAN,
    DMA_CACHE_OP_INVALIDATE,
    DMA_CACHE_OP_CLEAN_INVALIDATE
} dma_cache_op_t;

/**
 * Allocate a dma memory buffer. Must be contiguous in physical and virtual address,
 * but my cross page boundaries. It is also guaranteed that this memory region can
 * be pinned
 *
 * @param cookie Cookie for the dma manager
 * @param size Size in bytes of the dma memory region
 * @param align Alignment in bytes of the dma region
 * @param cached Whether the region should be mapped cached or not
 * @param flags Memory access flags
 *
 * @return NULL on failure, otherwise virtual address of allocation
 */
typedef void *(*ps_dma_alloc_fn_t)(
    size_t size,
    int align,
    int cached,
    ps_mem_flags_t flags);

/**
 * Free a previously allocated dma memory buffer
 *
 * @param cookie Cookie for the dma manager
 * @param addr Virtual address of the memory buffer as given by the dma_alloc function
 * @param size Original size of the allocated buffer
 */
typedef void (*ps_dma_free_fn_t)(
    void *addr,
    size_t size);

/**
 * Pin a piece of memory. This ensures it is resident and has a translation until
 * it is unpinned. You should not pin a memory range that overlaps with another
 * pinned range. If pinning is successful memory is guaranteed to be contiguous
 * in physical memory.
 *
 * @param cookie Cookie for the dma manager
 * @param addr Address of the memory to pin
 * @param size Range of memory to pin
 *
 * @return 0 if memory could not be pinned, otherwise physical address
 */
typedef uintptr_t (*ps_dma_pin_fn_t)(
    void *addr,
    size_t size);

/**
 * Unpin a piece of memory. You should only unpin the exact same range
 * that was pinned, do not partially unpin a range or unpin memory that
 * was never pinned.
 *
 * @param cookie Cookie for the dma manager
 * @param addr Address of the memory to unpin
 * @param size Range of the memory to unpin
 */
typedef void (*ps_dma_unpin_fn_t)(
    void *addr,
    size_t size);

/**
 * Perform a cache operation on a dma memory region. Pinned and unpinned
 * memory can have cache operations performed on it
 *
 * @param cookie Cookie for the dma manager
 * @param addr Start address to perform the cache operation on
 * @param size Size of the range to perform the cache operation on
 * @param op Cache operation to perform
 */
typedef void (*ps_dma_cache_op_fn_t)(
    void *addr,
    size_t size,
    dma_cache_op_t op);

typedef struct ps_dma_man {
    ps_dma_alloc_fn_t dma_alloc_fn;
    ps_dma_free_fn_t dma_free_fn;
    ps_dma_pin_fn_t dma_pin_fn;
    ps_dma_unpin_fn_t dma_unpin_fn;
    ps_dma_cache_op_fn_t dma_cache_op_fn;
} ps_dma_man_t;


