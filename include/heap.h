#pragma once
#include <stdint.h>
#include <stddef.h>

// Kernel heap allocator API

void  heap_init(uint32_t start, uint32_t size); // init heap region
void* kmalloc(size_t size);                     // allocate memory
void  kfree(void* ptr);                         // free memory
void* krealloc(void* ptr, size_t new_size);     // resize block

uint32_t heap_used();   // used bytes
uint32_t heap_total();  // total heap size