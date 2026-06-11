//
// Flash-backed blob store for the "pretend retro-go" persistence layer.
//
// The firmware already brings up the external OSPI NOR (W25Q/MX-class, 64 MB) in
// memory-mapped XIP mode and links a complete write/erase driver in
// deps/Core/Src/flash.c (OSPI_EraseSync / OSPI_Program / OSPI_*MemoryMappedMode),
// initialised by board_ospi_init() at boot. We reuse it verbatim — the exact
// standard disable-MM -> erase/program -> enable-MM sequence for OSPI writes
// while executing from internal flash.
//
// Layout: a fixed 2 MB region at the TOP of the 64 MB chip (offset 62 MB), well
// clear of the app payload (@ 20 MB by default). No coupling to where the payload
// is flashed. Within it, a small fixed table maps logical names to sector-aligned
// slots; each slot stores a one-page header {magic, length} followed by the data.
// Reads come straight from the XIP mapping (no driver needed); writes go through
// the OSPI driver.
//
// While memory-mapped mode is off, nothing in external flash is reachable — but
// this code and the OSPI driver all execute from internal flash (0x08000000), and
// the data buffer lives in RAM, so the critical section is self-contained and
// safe. The only active interrupt during gameplay is SysTick (internal-flash
// handler), so it is left running to keep the tick counter advancing.
//

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "flash.h"          // OSPI_* driver + stm32h7xx_hal.h (SCB cache ops)
#include "odroid_system.h"

#define EXTFLASH_MMAP_BASE   0x90000000UL
// 64 MB chip: reserve the top 2 MB for the retro-go storage layer (saves +
// settings) — past the app payload (@ 20 MB by default). Tied to the 64 MB
// modded chip; adjust if that changes.
#define PERSIST_REGION_OFF   0x03E00000UL   // 62 MB (flash offset, 0-based)
#define PERSIST_REGION_SIZE  0x00200000UL   // 2 MB
#define PERSIST_HDR_MAGIC    0x56534D44UL   // "DMSV"
#define PERSIST_HDR_BYTES    256U           // header occupies one program page
#define FLASH_SECTOR         4096U

typedef struct {
    const char *name;
    uint32_t    off;   // offset within the reserved region (sector-aligned)
    uint32_t    max;   // max bytes incl. header (multiple of the sector size)
} persist_slot_t;

// 6 app save slots (<prefix>-0.sav..<prefix>-5.sav) + a small settings/SRAM
// blob. 6*0x40000 + 0x10000 = 0x190000 < PERSIST_REGION_SIZE. APP_SAVE_PREFIX
// comes from the consuming project's Makefile (default "app").
#ifndef APP_SAVE_PREFIX
#define APP_SAVE_PREFIX "app"
#endif
static const persist_slot_t k_slots[] = {
    { APP_SAVE_PREFIX "-0.sav", 0x000000, 256u * 1024u },
    { APP_SAVE_PREFIX "-1.sav", 0x040000, 256u * 1024u },
    { APP_SAVE_PREFIX "-2.sav", 0x080000, 256u * 1024u },
    { APP_SAVE_PREFIX "-3.sav", 0x0C0000, 256u * 1024u },
    { APP_SAVE_PREFIX "-4.sav", 0x100000, 256u * 1024u },
    { APP_SAVE_PREFIX "-5.sav", 0x140000, 256u * 1024u },
    { APP_SAVE_PREFIX ".sram",  0x180000,  64u * 1024u },
};

static const persist_slot_t *find_slot(const char *name)
{
    for (unsigned i = 0; i < sizeof(k_slots) / sizeof(k_slots[0]); i++)
        if (strcmp(name, k_slots[i].name) == 0)
            return &k_slots[i];
    return NULL;
}

static inline int dcache_on(void)
{
    return (SCB->CCR & SCB_CCR_DC_Msk) != 0;
}

int rg_blob_write(const char *name, const void *buf, uint32_t len)
{
    const persist_slot_t *s = find_slot(name);
    if (!s) {
        printf("persist: unknown slot '%s'\n", name);
        return -1;
    }
    if (PERSIST_HDR_BYTES + len > s->max) {
        // firmware printf has no 'l'/width support — use plain %u (32-bit values)
        printf("persist: '%s' too big (%u > %u)\n", name,
               (unsigned)(PERSIST_HDR_BYTES + len), (unsigned)s->max);
        return -1;
    }

    uint32_t off       = PERSIST_REGION_OFF + s->off;
    uint32_t erase_len = (PERSIST_HDR_BYTES + len + (FLASH_SECTOR - 1)) & ~(FLASH_SECTOR - 1);
    uint32_t mem       = EXTFLASH_MMAP_BASE + off;

    // One-page header so the reader can recover the stored length.
    uint8_t hdr[PERSIST_HDR_BYTES];
    memset(hdr, 0xFF, sizeof hdr);
    ((uint32_t *)hdr)[0] = PERSIST_HDR_MAGIC;
    ((uint32_t *)hdr)[1] = len;

    int dc = dcache_on();
    if (dc) {
        SCB_CleanDCache_by_Addr((uint32_t *)mem, erase_len);
        SCB_InvalidateDCache_by_Addr((uint32_t *)mem, erase_len);
        SCB_DisableDCache();
    }

    OSPI_DisableMemoryMappedMode();
    OSPI_EraseSync(off, erase_len);
    OSPI_Program(off, hdr, PERSIST_HDR_BYTES);
    if (len)
        OSPI_Program(off + PERSIST_HDR_BYTES, (const uint8_t *)buf, len);
    OSPI_EnableMemoryMappedMode();

    if (dc) {
        SCB_InvalidateDCache_by_Addr((uint32_t *)mem, erase_len);
        SCB_EnableDCache();
    }
    return (int)len;
}

int rg_blob_read(const char *name, void *buf, uint32_t max)
{
    const persist_slot_t *s = find_slot(name);
    if (!s)
        return -1;

    const uint8_t  *p = (const uint8_t *)(EXTFLASH_MMAP_BASE + PERSIST_REGION_OFF + s->off);
    const uint32_t *h = (const uint32_t *)p;
    if (h[0] != PERSIST_HDR_MAGIC)
        return -1;   // erased / never written

    uint32_t len = h[1];
    uint32_t n   = len < max ? len : max;
    memcpy(buf, p + PERSIST_HDR_BYTES, n);
    return (int)len;
}

// Return a direct XIP pointer to a slot's stored bytes (no copy), plus its
// length. NULL if the slot is empty/unwritten. The engine's savegame loader
// reads compressed save data straight from flash through this pointer.
const uint8_t *rg_blob_ptr(const char *name, uint32_t *len_out)
{
    const persist_slot_t *s = find_slot(name);
    if (len_out)
        *len_out = 0;
    if (!s)
        return NULL;

    const uint8_t  *p = (const uint8_t *)(EXTFLASH_MMAP_BASE + PERSIST_REGION_OFF + s->off);
    const uint32_t *h = (const uint32_t *)p;
    if (h[0] != PERSIST_HDR_MAGIC)
        return NULL;

    if (len_out)
        *len_out = h[1];
    return p + PERSIST_HDR_BYTES;
}

// Clear a slot: erase its header sector so it reads as empty.
void rg_blob_erase(const char *name)
{
    const persist_slot_t *s = find_slot(name);
    if (!s)
        return;

    uint32_t off = PERSIST_REGION_OFF + s->off;
    uint32_t mem = EXTFLASH_MMAP_BASE + off;
    int dc = dcache_on();
    if (dc) {
        SCB_CleanDCache_by_Addr((uint32_t *)mem, FLASH_SECTOR);
        SCB_InvalidateDCache_by_Addr((uint32_t *)mem, FLASH_SECTOR);
        SCB_DisableDCache();
    }
    OSPI_DisableMemoryMappedMode();
    OSPI_EraseSync(off, FLASH_SECTOR);
    OSPI_EnableMemoryMappedMode();
    if (dc) {
        SCB_InvalidateDCache_by_Addr((uint32_t *)mem, FLASH_SECTOR);
        SCB_EnableDCache();
    }
}
