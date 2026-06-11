#pragma once
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

int atoi(const char *s);

double atof(const char *ptr);

unsigned long strlen(const char *s);

char *strdup(const char *s);

char *strcpy(char *dest, const char *src);

int strcmp(const char *s1, const char *s2);

int strncmp(const char *s1, const char *s2, unsigned long size);

int strcasecmp(const char *s1, const char *s2);

unsigned long strlen(const char *s);

char *strdup(const char *s);

char *strchr(const char *s, int c);

char *strrchr(const char *s, int c);

int memcmp(const void *s1, const void *s2, size_t n);

char *strncpy(char *dst, const char *src, size_t n);

/* implemented in src/libc.c; declared for app use */
char *strstr(const char *haystack, const char *needle);
int strncasecmp(const char *s1, const char *s2, size_t n);
void *memset(void *s, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
size_t strcspn(const char *s, const char *reject);
size_t strspn(const char *s, const char *accept);

int toupper(int c);

int tolower(int c);

int isspace(int c);

#ifdef __cplusplus
}
#endif
