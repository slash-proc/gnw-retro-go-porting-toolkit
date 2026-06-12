//
// firmware_abi.c — populate + publish the versioned firmware ABI table.
//
// The struct is placed in .firmware_abi (pinned to intflash + 0x400 by the linker)
// and marked `used` so --gc-sections keeps it even though the firmware never
// references it; apps reach it via the fixed VTOR+0x400 address, not by symbol name.
// See include/gnw_firmware_abi.h for the contract.
//
#include "gnw_firmware_abi.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <mm.h>

#include "odroid_system.h"
#include "odroid_input.h"
#include "audio_sai.h"
#include "ltdc.h"
#if SD_CARD == 1
#include "ff.h"   // FatFs dir/mkdir for the SD-backed ABI slots
#endif

// Firmware functions/data not in a public header.
// stdio VFS (fopen/fclose/fread/fwrite/remove) is declared in <stdio.h> and backed
// by littlefs in lfs_flash.c.
extern void common_ingame_overlay(void);
extern void odroid_system_switch_app(int app);
/* loader.c: stand-in for retro-go's odroid_overlay_cache_file_in_flash
 * (fixed-slot map of the staged WHD; memory-mapped flash IS the cache). */
extern uint8_t *gnw_storage_map_file(const char *path, uint32_t *size_p, int byte_swap);
/* No watchdog on the test firmware — but real retro-go runs a WWDG and apps
 * refresh it through this slot, so it must never be NULL. */
static void wdog_refresh_noop(void) {}
extern void odroid_system_init(int app_id, int sample_rate);
extern void common_emu_input_loop();
extern volatile unsigned long systick_cnt;
extern unsigned long get_elapsed_time(void);

#if SD_CARD == 1
// feof/ferror live in vfs_fatfs.c but aren't in the toolkit's minimal <stdio.h>.
int feof(FILE *stream);
int ferror(FILE *stream);
// FatFs directory + mkdir bridged to the ABI's opaque-pointer slots. The app
// allocates DIR/FILINFO-sized buffers (it includes ff.h too) and passes them in.
static int abi_f_opendir(void *dp, const char *path) { return f_opendir((DIR *)dp, path); }
static int abi_f_readdir(void *dp, void *fno)        { return f_readdir((DIR *)dp, (FILINFO *)fno); }
static int abi_f_closedir(void *dp)                  { return f_closedir((DIR *)dp); }
static int abi_sdcard_mkdir(const char *path)        { return f_mkdir(path); }
// Round-robin OSPI cache: stage an SD file into external flash, return its XIP
// pointer (gw_flash_alloc.c). No progress UI on the test firmware.
#include "gw_flash_alloc.h"
static uint8_t *abi_cache_file(const char *path, uint32_t *size_p, int byte_swap)
{
    return store_file_in_flash(path, size_p, byte_swap, 0);
}
#endif

// Firmware signatures don't all match the ABI's generic prototypes exactly (printf
// returns void here, size_t is unsigned long, odroid_system_emu_init takes typed
// handlers) — all ABI-compatible on ARM AAPCS, so silence the type-pedantry for the
// table's intentional bridging.
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
#define FN(p) (p)

__attribute__((section(".firmware_abi"), used))
const gnw_firmware_abi_t g_firmware_abi = {
    .version = GNW_FIRMWARE_ABI_VERSION,
    .size    = sizeof(gnw_firmware_abi_t),

    .memcpy   = memcpy,
    .memset   = memset,
    .memmove  = memmove,
    .memcmp   = memcmp,
    .strlen   = strlen,
    .strncpy  = strncpy,
    .strcmp   = strcmp,
    .strncmp  = strncmp,
    .strspn   = strspn,

    .odroid_system_init           = FN(odroid_system_init),
    .odroid_system_emu_init       = FN(odroid_system_emu_init),
    .odroid_system_switch_app     = odroid_system_switch_app,

    .fopen   = fopen,
    .fclose  = fclose,
    .fread   = fread,
    .fwrite  = fwrite,
    .remove  = remove,
#if SD_CARD == 1
    // SD-backed file/dir ops (FatFs VFS in vfs_fatfs.c + FatFs dir API).
    .fseek    = fseek,
    .ftell    = ftell,
    .feof     = feof,
    .ferror   = ferror,
    .f_opendir  = abi_f_opendir,
    .f_readdir  = abi_f_readdir,
    .f_closedir = abi_f_closedir,
    .odroid_sdcard_mkdir = abi_sdcard_mkdir,
#endif

    .odroid_input_read_gamepad = FN(odroid_input_read_gamepad),

#if SD_CARD == 1
    .odroid_overlay_cache_file_in_flash = abi_cache_file,      // SD -> round-robin OSPI cache
#else
    .odroid_overlay_cache_file_in_flash = gnw_storage_map_file, // fixed-slot WHD stand-in
#endif

    .wdog_refresh = wdog_refresh_noop,

    .audio_start_playing     = audio_start_playing,
    .audio_get_active_buffer = audio_get_active_buffer,

    .lcd_set_clut           = lcd_set_clut,
    .lcd_setup_framebuffers = lcd_setup_framebuffers,
    .lcd_get_active_buffer  = lcd_get_active_buffer,
    .lcd_get_inactive_buffer = lcd_get_inactive_buffer,
    .lcd_swap               = lcd_swap,

    .common_ingame_overlay = common_ingame_overlay,
    .common_emu_input_loop = FN(common_emu_input_loop),

    .HAL_GetTick = FN(get_elapsed_time),

    .strstr  = strstr,
    .tolower = tolower,
    .toupper = toupper,
};
