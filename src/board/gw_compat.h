#ifndef GW_COMPAT_H
#define GW_COMPAT_H
//
// gw_compat.h — small shims so retro-go's gw_flash_alloc.c builds against the
// toolkit. Bridges the few retro-go-only symbols (gw_linker.h / config.h /
// gw_malloc.h / gw_ofw.h) to toolkit equivalents.
//
#include <stdint.h>
#include "flash.h"   // OSPI_* (OSPI_GetSize, OSPI_EraseSync, OSPI_Program, ...)

// retro-go's gw_flash.h spells the size getter OSPI_GetFlashSize; toolkit: OSPI_GetSize.
#define OSPI_GetFlashSize OSPI_GetSize

// External-flash base + reserved-bottom region, provided as linker symbols
// (--defsym in the Makefile): &__EXTFLASH_BASE__ == 0x90000000,
// &__EXTFLASH_OFFSET__ == the bytes reserved for the app blob at the bottom.
extern char __EXTFLASH_BASE__[];
extern char __EXTFLASH_OFFSET__[];

// No "original firmware" on the test firmware, so nothing of its own to reserve.
static inline uint32_t get_ofw_extflash_size(void) { return 0; }

// Where the flash-cache metadata file lives on the storage (SD root).
#define ODROID_BASE_PATH_SAVES ""

#endif // GW_COMPAT_H
