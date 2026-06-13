#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t size);

void *calloc(size_t nmemb, size_t size);

void *realloc(void *ptr, size_t size);

int abs(int j);

int system(const char *command);

int rand(void);
void srand(unsigned int seed);
#define RAND_MAX 0x7fff

/* qsort is provided by the firmware ABI (gnw_firmware_abi_t::qsort); declared here
 * so apps can call the standard name (routed through the ABI in the app's libc). */
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));


void free(void *ptr);

void *memset(void *s, int c, size_t n);

void *memcpy(void *dst, const void *src, size_t n);

void *memmove(void *dst, const void *src, size_t n);

void exit(int err);

#ifdef __cplusplus
}
#endif
