/*
 * tlsf.c - Two-Level Segregated Fit allocator.
 *
 * Adapted to be fully freestanding (no libc, no OS headers).
 * O(1) allocation and deallocation.
 *
 * Design:
 *   Blocks are tagged with a header containing size + free/prev-free bits.
 *   A two-level bitmap (FL = floor(log2(size)), SL = next 4 bits) indexes
 *   free lists so any suitable block is found in O(1) bit operations.
 *
 *   FL range: 4..27 (16 bytes .. 128 MiB)
 *   SL count:  16 (4 SL bits)
 */

#include "tlsf.h"
#include <stdint.h>
#include <stddef.h>

/* ---- Configuration ---------------------------------------------------- */

#define FL_INDEX_MAX   28
#define SL_INDEX_BITS   4
#define SL_INDEX_COUNT (1 << SL_INDEX_BITS)
#define FL_INDEX_COUNT (FL_INDEX_MAX - SL_INDEX_BITS + 1)

#define BLOCK_ALIGN  sizeof(void *)
#define BLOCK_MIN    (sizeof(block_t))

/* ---- Block structure -------------------------------------------------- */

/* Status bits stored in the low bits of block.size. */
#define BLOCK_FREE    1u
#define BLOCK_PREV_FREE 2u
#define BLOCK_OVERHEAD  sizeof(uint32_t)   /* just size field at block start */

typedef struct block_s {
    /* Size field: upper bits = payload size (multiple of BLOCK_ALIGN).
     * Bit 0: 1 = free, 0 = used.
     * Bit 1: 1 = previous physical block is free.
     */
    uint32_t size;

    /* Intrusive free-list links (only valid when free). */
    struct block_s *prev_free;
    struct block_s *next_free;

    /* Immediately followed by the user payload (or the next block header). */
} block_t;

/* Physical footer: stored at the end of every FREE block.
 * Points back to the block header so the next block can coalesce. */
typedef struct {
    block_t *header;
} footer_t;

/* ---- Helper macros ----------------------------------------------------- */

#define SZ_MASK   (~3u)

static inline uint32_t block_size(const block_t *b)   { return b->size & SZ_MASK; }
static inline int  block_is_free(const block_t *b)    { return (b->size & BLOCK_FREE) ? 1 : 0; }
static inline int  block_prev_free(const block_t *b)  { return (b->size & BLOCK_PREV_FREE) ? 1 : 0; }

static inline void block_set_size(block_t *b, uint32_t sz)
{
    b->size = (b->size & ~SZ_MASK) | (sz & SZ_MASK);
}
static inline void block_set_free(block_t *b, int f)
{
    b->size = f ? (b->size | BLOCK_FREE) : (b->size & ~BLOCK_FREE);
}
static inline void block_set_prev_free(block_t *b, int f)
{
    b->size = f ? (b->size | BLOCK_PREV_FREE) : (b->size & ~BLOCK_PREV_FREE);
}

/* Header overhead to subtract when reporting usable size. */
#define HDR_SIZE  offsetof(block_t, prev_free)

static inline void *block_to_ptr(block_t *b)  { return (char *)b + HDR_SIZE; }
static inline block_t *ptr_to_block(void *p)  { return (block_t *)((char *)p - HDR_SIZE); }

/* Next/previous block in the physical pool (not free list). */
static inline block_t *block_phys_next(block_t *b)
{
    return (block_t *)((char *)b + HDR_SIZE + block_size(b));
}
static inline block_t *block_phys_prev(block_t *b)
{
    /* Footer of previous block sits just before this header. */
    footer_t *f = (footer_t *)((char *)b - sizeof(footer_t));
    return f->header;
}
static inline footer_t *block_footer(block_t *b)
{
    return (footer_t *)((char *)b + HDR_SIZE + block_size(b) - sizeof(footer_t));
}
static inline void block_write_footer(block_t *b)
{
    block_footer(b)->header = b;
}

/* ---- Find-first-bit helpers ------------------------------------------- */

static inline int fls(uint32_t v)   /* floor(log2(v)); v must be != 0 */
{
    int n;
    __asm__ ("bsrl %1, %0" : "=r"(n) : "r"(v));
    return n;
}
static inline int ffs(uint32_t v)   /* index of lowest set bit */
{
    int n;
    __asm__ ("bsfl %1, %0" : "=r"(n) : "r"(v));
    return n;
}

/* ---- Control structure (stored inside the pool memory) ---------------- */

typedef struct {
    block_t  *free_lists[FL_INDEX_COUNT][SL_INDEX_COUNT];
    uint32_t  fl_bitmap;
    uint32_t  sl_bitmap[FL_INDEX_COUNT];

    /* Sentinel block — a dummy zero-size "used" block at the very end. */
    block_t   sentinel;
} control_t;

/* The public tlsf_t is just the control_t. */
struct tlsf_s {
    control_t ctrl;
};

/* ---- Mapping ---------------------------------------------------------- */

static void mapping_insert(uint32_t size, int *fl, int *sl)
{
    if (size < (1u << SL_INDEX_BITS)) {
        *fl = 0;
        *sl = (int)size;
    } else {
        *fl = fls(size);
        *sl = (int)((size >> (*fl - SL_INDEX_BITS)) & (SL_INDEX_COUNT - 1));
        /* Round up: use next SL bucket to guarantee the block is large enough. */
    }
}

static void mapping_search(uint32_t size, int *fl, int *sl)
{
    /* Round up size to next bucket boundary before searching. */
    if (size >= (1u << SL_INDEX_BITS)) {
        uint32_t round = (1u << (fls(size) - SL_INDEX_BITS)) - 1u;
        size += round;
    }
    mapping_insert(size, fl, sl);
}

/* ---- Free list management --------------------------------------------- */

static inline void fl_sl_set(control_t *c, int fl, int sl)
{
    c->sl_bitmap[fl] |= (1u << sl);
    c->fl_bitmap     |= (1u << fl);
}
static inline void fl_sl_clear(control_t *c, int fl, int sl)
{
    c->sl_bitmap[fl] &= ~(1u << sl);
    if (!c->sl_bitmap[fl])
        c->fl_bitmap &= ~(1u << fl);
}

static void insert_block(control_t *c, block_t *b)
{
    int fl, sl;
    mapping_insert(block_size(b), &fl, &sl);
    b->next_free = c->free_lists[fl][sl];
    b->prev_free = NULL;
    if (c->free_lists[fl][sl])
        c->free_lists[fl][sl]->prev_free = b;
    c->free_lists[fl][sl] = b;
    fl_sl_set(c, fl, sl);
}

static void remove_block(control_t *c, block_t *b)
{
    int fl, sl;
    mapping_insert(block_size(b), &fl, &sl);
    if (b->prev_free) b->prev_free->next_free = b->next_free;
    else              c->free_lists[fl][sl]   = b->next_free;
    if (b->next_free) b->next_free->prev_free = b->prev_free;
    if (!c->free_lists[fl][sl])
        fl_sl_clear(c, fl, sl);
}

/* Find a free block >= size. Returns NULL if not found. */
static block_t *find_suitable(control_t *c, uint32_t size)
{
    int fl, sl;
    mapping_search(size, &fl, &sl);

    uint32_t sl_map = c->sl_bitmap[fl] & (~0u << sl);
    if (!sl_map) {
        uint32_t fl_map = c->fl_bitmap & (~0u << (fl + 1));
        if (!fl_map) return NULL;
        fl = ffs(fl_map);
        sl_map = c->sl_bitmap[fl];
    }
    sl = ffs(sl_map);
    return c->free_lists[fl][sl];
}

/* ---- Merge / split ----------------------------------------------------- */

static block_t *coalesce_prev(control_t *c, block_t *b)
{
    if (block_prev_free(b)) {
        block_t *prev = block_phys_prev(b);
        remove_block(c, prev);
        block_set_size(prev, block_size(prev) + HDR_SIZE + block_size(b));
        return prev;
    }
    return b;
}

static block_t *coalesce_next(control_t *c, block_t *b)
{
    block_t *next = block_phys_next(b);
    if (block_is_free(next)) {
        remove_block(c, next);
        block_set_size(b, block_size(b) + HDR_SIZE + block_size(next));
    }
    return b;
}

static block_t *split_block(block_t *b, uint32_t size)
{
    uint32_t remain = block_size(b) - size - HDR_SIZE;
    if (remain < (uint32_t)BLOCK_MIN) return NULL;

    block_t *rest = (block_t *)((char *)b + HDR_SIZE + size);
    rest->size = remain;
    block_set_free(rest, 0);
    block_set_prev_free(rest, 0);
    block_set_size(b, size);
    return rest;
}

/* ---- Pool bootstrap --------------------------------------------------- */

static void pool_add(control_t *c, void *mem, size_t size)
{
    /* Minimum: one free block + sentinel. */
    if (size < (HDR_SIZE + BLOCK_MIN + HDR_SIZE + sizeof(footer_t) + 4))
        return;

    block_t *b = (block_t *)mem;
    uint32_t bsz = (uint32_t)(size - HDR_SIZE - HDR_SIZE - sizeof(footer_t));
    bsz &= SZ_MASK;

    b->size = 0;
    block_set_size(b, bsz);
    block_set_free(b, 1);
    block_set_prev_free(b, 0);
    block_write_footer(b);

    /* Sentinel immediately after. */
    block_t *sent = block_phys_next(b);
    sent->size = 0;   /* size=0, not free */
    block_set_prev_free(sent, 1);

    insert_block(c, b);
}

/* ---- Public API ------------------------------------------------------- */

tlsf_t *tlsf_create(void *mem, size_t size)
{
    if (!mem || size < sizeof(control_t) + 64u) return NULL;

    tlsf_t *t = (tlsf_t *)mem;
    control_t *c = &t->ctrl;

    /* Zero bitmaps and free lists. */
    c->fl_bitmap = 0;
    for (int f = 0; f < FL_INDEX_COUNT; f++) {
        c->sl_bitmap[f] = 0;
        for (int s = 0; s < SL_INDEX_COUNT; s++)
            c->free_lists[f][s] = NULL;
    }

    /* The remaining memory after the control structure becomes the pool. */
    void *pool = (char *)mem + sizeof(control_t);
    size_t pool_size = size - sizeof(control_t);
    pool_add(c, pool, pool_size);

    return t;
}

void tlsf_add_pool(tlsf_t *t, void *mem, size_t size)
{
    if (!t || !mem) return;
    pool_add(&t->ctrl, mem, size);
}

void *tlsf_malloc(tlsf_t *t, size_t size)
{
    if (!t || size == 0) return NULL;

    /* Align and clamp. */
    size = (size + BLOCK_ALIGN - 1) & ~(BLOCK_ALIGN - 1);
    if (size < BLOCK_MIN) size = BLOCK_MIN;

    control_t *c = &t->ctrl;
    block_t *b = find_suitable(c, (uint32_t)size);
    if (!b) return NULL;

    remove_block(c, b);
    block_set_free(b, 0);

    /* Split if there is enough remainder. */
    block_t *rest = split_block(b, (uint32_t)size);
    if (rest) {
        block_set_free(rest, 1);
        block_write_footer(rest);
        block_t *next = block_phys_next(rest);
        block_set_prev_free(next, 1);
        insert_block(c, rest);
    } else {
        block_t *next = block_phys_next(b);
        block_set_prev_free(next, 0);
    }

    return block_to_ptr(b);
}

void tlsf_free(tlsf_t *t, void *ptr)
{
    if (!t || !ptr) return;
    control_t *c = &t->ctrl;
    block_t *b = ptr_to_block(ptr);

    block_set_free(b, 1);
    block_write_footer(b);

    b = coalesce_prev(c, b);
    b = coalesce_next(c, b);

    /* Mark next physical block's prev_free bit. */
    block_t *next = block_phys_next(b);
    block_set_prev_free(next, 1);
    block_write_footer(b);

    insert_block(c, b);
}

void *tlsf_calloc(tlsf_t *t, size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *p = tlsf_malloc(t, total);
    if (p) {
        uint8_t *b = (uint8_t *)p;
        for (size_t i = 0; i < total; i++) b[i] = 0;
    }
    return p;
}

void *tlsf_realloc(tlsf_t *t, void *ptr, size_t size)
{
    if (!ptr) return tlsf_malloc(t, size);
    if (size == 0) { tlsf_free(t, ptr); return NULL; }

    void *np = tlsf_malloc(t, size);
    if (!np) return NULL;

    block_t *b = ptr_to_block(ptr);
    uint32_t old_size = block_size(b);
    size_t copy = (size_t)old_size < size ? (size_t)old_size : size;
    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)np;
    for (size_t i = 0; i < copy; i++) dst[i] = src[i];

    tlsf_free(t, ptr);
    return np;
}
