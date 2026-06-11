//
// "Fake retro-go" loader: find the app image on external flash, relocate it to
// wherever it was installed, and hand back its entry point.
//
// The app is a relocatable XIP payload: linked at a fixed base, carrying a
// header at offset 0 and a table of the absolute-pointer sites that must be
// fixed up. This loader scans extflash for the header magic (the
// install-address discovery retro-go does via its metadata table), then
// rewrites each reloc site by (install_addr - link_base) so the code runs XIP
// from its actual address. Patching is sector-batched and guarded by a
// "relocated to X" marker so it runs once per install.
//

#include <stdint.h>
#include <string.h>

#include "flash.h"   // OSPI_* + stm32h7xx_hal.h (SCB)

#define EXTFLASH_BASE   0x90000000UL
#define RELOC_MAGIC     0x31525844UL   // "DXR1" — the relocatable-blob header magic
#define SECTOR          4096U

// Header layout (8 x u32 at image offset 0):
//   [0] magic [1] link_base [2] entry_offset [3] image_size
//   [4] reloc_offset [5] reloc_count [6] relocated_to (marker) [7] pad
enum { H_MAGIC, H_LINKBASE, H_ENTRY, H_IMGSIZE, H_RELOCOFF, H_RELOCCNT, H_MARKER };

static inline int dcache_on(void) { return (SCB->CCR & SCB_CCR_DC_Msk) != 0; }

static void patch_sector(uint32_t flash_off, const uint8_t *data) {
    uint32_t mem = EXTFLASH_BASE + flash_off;
    int dc = dcache_on();
    if (dc) {
        SCB_CleanDCache_by_Addr((uint32_t *)mem, SECTOR);
        SCB_InvalidateDCache_by_Addr((uint32_t *)mem, SECTOR);
        SCB_DisableDCache();
    }
    OSPI_DisableMemoryMappedMode();
    OSPI_EraseSync(flash_off, SECTOR);
    OSPI_Program(flash_off, data, SECTOR);
    OSPI_EnableMemoryMappedMode();
    if (dc) {
        SCB_InvalidateDCache_by_Addr((uint32_t *)mem, SECTOR);
        SCB_EnableDCache();
    }
}

static uint8_t s_sector[SECTOR];

// Apply the relocation table at install address X (delta = X - link_base != 0).
static void relocate(uint32_t X, int32_t delta, const uint32_t *hdr) {
    const uint32_t *table = (const uint32_t *)(X + hdr[H_RELOCOFF]);
    uint32_t count = hdr[H_RELOCCNT];

    uint32_t i = 0;
    while (i < count) {
        uint32_t sec_base = table[i] & ~(SECTOR - 1);   // image offset, sector-aligned
        uint32_t sec_addr = X + sec_base;               // XIP address (read with MM on)
        memcpy(s_sector, (const void *)sec_addr, SECTOR);
        while (i < count && table[i] >= sec_base && table[i] < sec_base + SECTOR) {
            uint32_t in_sec = table[i] - sec_base;
            *(uint32_t *)(s_sector + in_sec) += (uint32_t)delta;
            i++;
        }
        if (sec_base == 0)
            ((uint32_t *)s_sector)[H_MARKER] = X;        // mark relocated-to-X in the header
        patch_sector(sec_addr - EXTFLASH_BASE, s_sector);
    }
}

// Scan extflash from EXTFLASH_OFFSET (after the LFS partition) for the app image,
// relocate it if needed, and return its thumb entry address (0 if not found).
uint32_t gnw_load_app(void) {
    uint32_t scan_end = EXTFLASH_BASE + OSPI_GetSize();
    uint32_t X = 0;
    for (uint32_t a = EXTFLASH_BASE + EXTFLASH_OFFSET; a < scan_end; a += SECTOR) {
        const uint32_t *p = (const uint32_t *)a;
        if (p[H_MAGIC] == RELOC_MAGIC &&
            p[H_LINKBASE] >= EXTFLASH_BASE && p[H_LINKBASE] < scan_end &&
            p[H_RELOCCNT] < 100000u) {
            X = a;
            break;
        }
    }
    if (!X)
        return 0;

    const uint32_t *hdr = (const uint32_t *)X;
    int32_t delta = (int32_t)(X - hdr[H_LINKBASE]);
    if (delta != 0 && hdr[H_MARKER] != X)
        relocate(X, delta, hdr);

    return (X + hdr[H_ENTRY]) | 1u;
}
