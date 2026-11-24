#ifndef _LWEXT4_COMPAT_INTTYPES_H
#define _LWEXT4_COMPAT_INTTYPES_H

#include <stdint.h>

#ifndef PRIu8
#define PRIu8  "u"
#endif
#ifndef PRIu16
#define PRIu16 "u"
#endif
#ifndef PRIu32
#define PRIu32 "u"
#endif
#ifndef PRIu64
#define PRIu64 "llu"
#endif
#ifndef PRId32
#define PRId32 "d"
#endif
#ifndef PRId64
#define PRId64 "lld"
#endif
#ifndef PRIx32
#define PRIx32 "x"
#endif
#ifndef PRIx64
#define PRIx64 "llx"
#endif

#endif /* _LWEXT4_COMPAT_INTTYPES_H */
