#include "../include/heap.h"
#include "../include/kstring.h"

struct Block {
    uint32_t size;
    bool     free;
    Block*   next;
};

static Block*   heap_start = nullptr;
static uint32_t heap_base  = 0;
static uint32_t heap_sz    = 0;

void heap_init(uint32_t start, uint32_t size) {
    heap_base  = start;
    heap_sz    = size;
    heap_start = (Block*)start;
    heap_start->size = size - sizeof(Block);
    heap_start->free = true;
    heap_start->next = nullptr;
}

void* kmalloc(size_t size) {
    if (!size) return nullptr;
    size = (size + 7) & ~7u;   // 8-byte align

    Block* cur = heap_start;
    while (cur) {
        if (cur->free && cur->size >= size) {
            // Split if there's enough room for a new block header + min data
            if (cur->size >= size + sizeof(Block) + 8) {
                Block* next = (Block*)((uint8_t*)cur + sizeof(Block) + size);
                next->size  = cur->size - size - sizeof(Block);
                next->free  = true;
                next->next  = cur->next;
                cur->next   = next;
                cur->size   = size;
            }
            cur->free = false;
            return (uint8_t*)cur + sizeof(Block);
        }
        cur = cur->next;
    }
    return nullptr;   // out of heap
}

void kfree(void* ptr) {
    if (!ptr) return;
    Block* blk = (Block*)((uint8_t*)ptr - sizeof(Block));
    blk->free = true;

    // Merge with next block if also free
    while (blk->next && blk->next->free) {
        blk->size += sizeof(Block) + blk->next->size;
        blk->next  = blk->next->next;
    }
}

void* krealloc(void* ptr, size_t new_size) {
    if (!ptr)     return kmalloc(new_size);
    if (!new_size){ kfree(ptr); return nullptr; }
    Block* blk = (Block*)((uint8_t*)ptr - sizeof(Block));
    if (blk->size >= new_size) return ptr;
    void* np = kmalloc(new_size);
    if (np) { k_memcpy(np, ptr, blk->size); kfree(ptr); }
    return np;
}

uint32_t heap_used() {
    uint32_t used = 0;
    Block* cur = heap_start;
    while (cur) {
        if (!cur->free) used += cur->size + sizeof(Block);
        cur = cur->next;
    }
    return used;
}

uint32_t heap_total() { return heap_sz; }
