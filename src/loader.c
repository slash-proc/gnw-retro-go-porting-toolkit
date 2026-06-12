//
// "Fake retro-go" loader: launch the GWHB RAM-overlay app, exactly the way
// real retro-go's homebrew menu does.
//
// The app image starts with the 'GWHB' magic word; the contract (shared with
// retro-go's rg_emulators.c) is: copy the image to __RAM_EMU_START__
// (0x2404B000) and jump to offset 4. Stage 1 inside the image unpacks its own
// segments (ITCM/DTCM/AXISRAM) — nothing executes from external flash and
// nothing is relocated.
//
// Test-firmware staging (gnwmanager, see Makefile.common `flash`):
//   EXTFLASH_OFFSET                  the GWHB image
//   EXTFLASH_OFFSET + WHD_SLOT_OFFSET  the WHD data file
// The app finds the WHD through the ABI (gnw_storage_map_file below), which
// stands in for retro-go's odroid_overlay_cache_file_in_flash.
//

#include <stdint.h>
#include <string.h>

#include "flash.h"   // OSPI_* + stm32h7xx_hal.h (SCB)

#define EXTFLASH_BASE   0x90000000UL
#define GWHB_MAGIC      0x42485747UL   // 'GWHB'
#define RAM_EMU_START   0x2404B000UL   // retro-go __RAM_EMU_START__ (both variants)
#define RAM_EMU_LENGTH  (724 * 1024UL)

#ifndef WHD_SLOT_OFFSET
#define WHD_SLOT_OFFSET (768 * 1024UL)
#endif

// Stand-in for retro-go's odroid_overlay_cache_file_in_flash (ABI slot): the
// test firmware has no /roms tree, so map known names to the fixed staging
// slot. Memory-mapped flash is the "cache" — return the XIP pointer directly.
uint8_t *gnw_storage_map_file(const char *path, uint32_t *size_p, int byte_swap)
{
    (void)byte_swap;
    const char *dot = path ? strrchr(path, '.') : 0;
    if (dot && !strcmp(dot, ".whd")) {
        uint32_t addr = EXTFLASH_BASE + EXTFLASH_OFFSET + WHD_SLOT_OFFSET;
        if (size_p)
            *size_p = OSPI_GetSize() - (EXTFLASH_OFFSET + WHD_SLOT_OFFSET);
        return (uint8_t *)addr;
    }
    if (size_p)
        *size_p = 0;
    return 0;
}

// Copy the GWHB image from extflash to RAM_EMU and return its thumb entry
// (0 if no image present).
uint32_t gnw_load_app(void)
{
    const uint32_t *img = (const uint32_t *)(EXTFLASH_BASE + EXTFLASH_OFFSET);
    if (img[0] != GWHB_MAGIC)
        return 0;

    memcpy((void *)RAM_EMU_START, img, RAM_EMU_LENGTH);

    // Make the copied stage-1 visible to instruction fetch (stage 1 handles
    // its own maintenance for the segments it copies further).
    SCB_CleanDCache_by_Addr((uint32_t *)RAM_EMU_START, RAM_EMU_LENGTH);
    SCB_InvalidateICache();

    return (RAM_EMU_START + 4) | 1u;
}
