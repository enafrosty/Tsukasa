#ifndef TSUKASA_LWIP_ARCH_CC_H
#define TSUKASA_LWIP_ARCH_CC_H

#include <stddef.h>
#include <stdint.h>

#include "../../../include/kprintf.h"
#include "../../../drv/pit.h"

typedef uint8_t u8_t;
typedef int8_t s8_t;
typedef uint16_t u16_t;
typedef int16_t s16_t;
typedef uint32_t u32_t;
typedef int32_t s32_t;
typedef uintptr_t mem_ptr_t;
typedef uint64_t u64_t;
typedef int64_t s64_t;
typedef uint64_t sys_prot_t;

#define LWIP_ERR_T int

#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "u"

#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_FIELD(x) x

#define LWIP_PLATFORM_DIAG(x) do { kprintf x; } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { kprintf("[lwip][assert] %s\n", x); } while (0)

static inline u32_t lwip_port_rand(void)
{
    return (u32_t)pit_ticks();
}

#define LWIP_RAND() lwip_port_rand()

uint32_t sys_now(void);
sys_prot_t sys_arch_protect(void);
void sys_arch_unprotect(sys_prot_t pval);

#endif /* TSUKASA_LWIP_ARCH_CC_H */
