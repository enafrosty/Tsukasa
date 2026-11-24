/*
 * Freestanding kernel configuration for lwext4 on Project Tsukasa.
 */

#ifndef GENERATED_EXT4_CONFIG_H_
#define GENERATED_EXT4_CONFIG_H_

#include <stddef.h>

#define CONFIG_USE_DEFAULT_CFG        0
#define CONFIG_EXT_FEATURE_SET_LVL    4
#define CONFIG_EXTENTS_ENABLE         1
#define CONFIG_JOURNALING_ENABLE      1
#define CONFIG_XATTR_ENABLE           0
#define CONFIG_DEBUG_PRINTF           0
#define CONFIG_DEBUG_ASSERT           0
#define CONFIG_HAVE_OWN_ASSERT        1
#define CONFIG_HAVE_OWN_OFLAGS        0
#define CONFIG_HAVE_OWN_ERRNO         0
#define CONFIG_BLOCK_DEV_ENABLE_STATS 0
#define CONFIG_BLOCK_DEV_CACHE_SIZE   16
#define CONFIG_EXT4_MAX_BLOCKDEV_NAME 32
#define CONFIG_EXT4_BLOCKDEVS_COUNT   8
#define CONFIG_EXT4_MAX_MP_NAME       64
#define CONFIG_EXT4_MOUNTPOINTS_COUNT 8
#define CONFIG_UNALIGNED_ACCESS       1
#define CONFIG_USE_USER_MALLOC        1

void *ext4_user_malloc(size_t size);
void *ext4_user_calloc(size_t nmemb, size_t size);
void *ext4_user_realloc(void *ptr, size_t size);
void ext4_user_free(void *ptr);

#endif /* GENERATED_EXT4_CONFIG_H_ */
