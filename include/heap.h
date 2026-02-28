#pragma once
#include <stdint.h>
#include <stddef.h>

void  heap_init(uint32_t start, uint32_t size);
void* kmalloc(size_t size);
void  kfree(void* ptr);
void* krealloc(void* ptr, size_t new_size);

uint32_t heap_used();
uint32_t heap_total();
