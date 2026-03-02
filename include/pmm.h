#pragma once
#include <stdint.h>
#include <stddef.h>

// Physical Memory Manager (4KB pages, fixed RAM size)

static const uint32_t PMM_PAGE_SIZE  = 4096;
static const uint32_t PMM_RAM_SIZE   = 256u * 1024u * 1024u;   // 256 MB
static const uint32_t PMM_PAGE_COUNT = PMM_RAM_SIZE / PMM_PAGE_SIZE;

void     pmm_init(uint32_t kernel_end); // init after kernel
void*    pmm_alloc();                   // allocate 1 page
void     pmm_free(void* page);          // free page
uint32_t pmm_free_pages();              // free page count
uint32_t pmm_used_pages();              // used page count
uint32_t pmm_get_total_ram_mb();        // total RAM in MB