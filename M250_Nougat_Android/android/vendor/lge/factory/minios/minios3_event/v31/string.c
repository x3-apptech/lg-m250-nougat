#include "string.h"

void *revcpy(void *dst, const void *src, size_t len)
{
    char *srcp = (char *)src;
    char *dstp = (char *)dst;
    srcp += len-1;
    dstp += len-1;
    while ( len-- ) {
        *dstp-- = *srcp--;
    }
    return dst;
}
