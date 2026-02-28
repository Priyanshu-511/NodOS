#include "../include/pmm.h"
#include "../include/kstring.h"

// One bit per 4 KB page; 0 = free, 1 = used
static uint32_t bitmap[PMM_PAGE_COUNT / 32];

static void set_bit(uint32_t page) {
    bitmap[page / 32] |=  (1u << (page % 32));
}
static void clr_bit(uint32_t page) {
    bitmap[page / 32] &= ~(1u << (page % 32));
}
static bool tst_bit(uint32_t page) {
    return bitmap[page / 32] & (1u << (page % 32));
}

void pmm_init(uint32_t kernel_end) {
    k_memset(bitmap, 0, sizeof(bitmap));

    // Mark the first 1 MB (BIOS / reserved) as used
    for (uint32_t i = 0; i < 256; i++) set_bit(i);

    // Mark pages occupied by the kernel as used
    uint32_t kpages = (kernel_end + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    for (uint32_t i = 0; i < kpages; i++) set_bit(i);
}

void* pmm_alloc() {
    for (uint32_t i = 0; i < PMM_PAGE_COUNT; i++) {
        if (!tst_bit(i)) {
            set_bit(i);
            return (void*)(i * PMM_PAGE_SIZE);
        }
    }
    return nullptr;   // out of memory
}

void pmm_free(void* page) {
    uint32_t idx = (uint32_t)page / PMM_PAGE_SIZE;
    if (idx < PMM_PAGE_COUNT) clr_bit(idx);
}

uint32_t pmm_free_pages() {
    uint32_t cnt = 0;
    for (uint32_t i = 0; i < PMM_PAGE_COUNT; i++)
        if (!tst_bit(i)) cnt++;
    return cnt;
}

uint32_t pmm_used_pages() {
    return PMM_PAGE_COUNT - pmm_free_pages();
}

uint32_t pmm_get_total_ram_mb() {
    return (PMM_PAGE_COUNT * 4096) / (1024 * 1024);
}