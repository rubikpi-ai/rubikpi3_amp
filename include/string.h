#ifndef __STRING__
#define __STRING__

#include <type.h>

size_t strlen(const char *);
void *memset(void *s, int c, size_t count);
void *memcpy(void *dest, const void *src, size_t count);

#endif
