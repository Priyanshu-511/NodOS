#pragma once
#include <stdint.h>
#include <stddef.h>

size_t   k_strlen(const char* s);
int      k_strcmp(const char* a, const char* b);
int      k_strncmp(const char* a, const char* b, size_t n);
char*    k_strcpy(char* dst, const char* src);
char*    k_strncpy(char* dst, const char* src, size_t n);
char*    k_strcat(char* dst, const char* src);
char*    k_strncat(char* dst, const char* src, int n);
char*    k_strchr(const char* s, int c);
char*    k_strstr(const char* hay, const char* needle);
void*    k_memset(void* ptr, int val, size_t n);
void*    k_memcpy(void* dst, const void* src, size_t n);
int      k_memcmp(const void* a, const void* b, size_t n);

// Number conversions
void     k_itoa(int n, char* buf, int base);
void     k_utoa(uint32_t n, char* buf, int base);
int      k_atoi(const char* s);

// Simple tokenizer (modifies string in place)
char*    k_strtok(char* str, const char* delim);
