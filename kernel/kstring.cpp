#include "../include/kstring.h"

size_t k_strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int k_strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int k_strncmp(const char* a, const char* b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char* k_strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++));
    return dst;
}

char* k_strncpy(char* dst, const char* src, size_t n) {
    char* d = dst;
    while (n-- && (*d++ = *src++));
    while (n-- > 0) *d++ = '\0';
    return dst;
}

char* k_strcat(char* dst, const char* src) {
    char* d = dst + k_strlen(dst);
    while ((*d++ = *src++));
    return dst;
}

char* k_strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == 0) ? (char*)s : nullptr;
}

char* k_strstr(const char* hay, const char* needle) {
    size_t nlen = k_strlen(needle);
    if (!nlen) return (char*)hay;
    while (*hay) {
        if (k_strncmp(hay, needle, nlen) == 0) return (char*)hay;
        hay++;
    }
    return nullptr;
}

void* k_memset(void* ptr, int val, size_t n) {
    unsigned char* p = (unsigned char*)ptr;
    while (n--) *p++ = (unsigned char)val;
    return ptr;
}

void* k_memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}

int k_memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* pa = (const unsigned char*)a;
    const unsigned char* pb = (const unsigned char*)b;
    while (n--) {
        if (*pa != *pb) return *pa - *pb;
        pa++; pb++;
    }
    return 0;
}

void k_itoa(int n, char* buf, int base) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[32];
    int i = 0;
    bool neg = false;
    if (n < 0 && base == 10) { neg = true; n = -n; }
    unsigned u = (unsigned)n;
    while (u) {
        int d = u % base;
        tmp[i++] = (d < 10) ? '0' + d : 'a' + d - 10;
        u /= base;
    }
    int j = 0;
    if (neg) buf[j++] = '-';
    while (i--) buf[j++] = tmp[i];
    buf[j] = '\0';
}

void k_utoa(uint32_t n, char* buf, int base) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[32];
    int i = 0;
    while (n) {
        int d = n % base;
        tmp[i++] = (d < 10) ? '0' + d : 'a' + d - 10;
        n /= base;
    }
    int j = 0;
    while (i--) buf[j++] = tmp[i];
    buf[j] = '\0';
}

int k_atoi(const char* s) {
    int result = 0, sign = 1;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9')
        result = result * 10 + (*s++ - '0');
    return sign * result;
}

static const char* _strtok_ptr = nullptr;
char* k_strtok(char* str, const char* delim) {
    if (str) _strtok_ptr = str;
    if (!_strtok_ptr) return nullptr;

    // skip leading delimiters
    while (*_strtok_ptr && k_strchr(delim, *_strtok_ptr)) _strtok_ptr++;
    if (!*_strtok_ptr) { _strtok_ptr = nullptr; return nullptr; }

    char* token = (char*)_strtok_ptr;
    while (*_strtok_ptr && !k_strchr(delim, *_strtok_ptr)) _strtok_ptr++;
    if (*_strtok_ptr) *((char*)_strtok_ptr++) = '\0';
    else _strtok_ptr = nullptr;
    return token;
}

char* k_strncat(char* dst, const char* src, int n) {
    int dlen = k_strlen(dst);
    int i = 0;
    while (src[i] && dlen + i < n - 1) { dst[dlen + i] = src[i]; i++; }
    dst[dlen + i] = '\0';
    return dst;
}
