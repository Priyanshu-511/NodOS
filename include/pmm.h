#pragma once
#include <stdint.h>
#include <stddef.h>

static const uint32_t PMM_PAGE_SIZE  = 4096;
static const uint32_t PMM_RAM_SIZE   = 128u * 1024u * 1024u;   // 128 MB
static const uint32_t PMM_PAGE_COUNT = PMM_RAM_SIZE / PMM_PAGE_SIZE; // 65536 pages

void     pmm_init(uint32_t kernel_end);
void*    pmm_alloc();
void     pmm_free(void* page);
uint32_t pmm_free_pages();
uint32_t pmm_used_pages();
uint32_t pmm_get_total_ram_mb();