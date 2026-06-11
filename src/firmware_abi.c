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

// Firmware functions/data not in a public header.
// stdio VFS (fopen/fclose/fread/fwrite/remove) is declared in <stdio.h> and backed
// by littlefs in lfs_flash.c.
extern void common_ingame_overlay(void);
extern void odroid_system_switch_app(int app);
extern volatile unsigned long systick_cnt;
extern unsigned long get_elapsed_time(void);

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

    .odroid_system_emu_init       = FN(odroid_system_emu_init),
    .odroid_system_switch_app     = odroid_system_switch_app,

    .fopen   = fopen,
    .fclose  = fclose,
    .fread   = fread,
    .fwrite  = fwrite,
    .remove  = remove,

    .odroid_input_read_gamepad = FN(odroid_input_read_gamepad),

    .audio_start_playing     = audio_start_playing,
    .audio_get_active_buffer = audio_get_active_buffer,

    .lcd_set_clut           = lcd_set_clut,
    .lcd_setup_framebuffers = lcd_setup_framebuffers,
    .lcd_get_active_buffer  = lcd_get_active_buffer,
    .lcd_get_inactive_buffer = lcd_get_inactive_buffer,
    .lcd_swap               = lcd_swap,

    .common_ingame_overlay = common_ingame_overlay,

    .HAL_GetTick = FN(get_elapsed_time),

    .strstr  = strstr,
    .tolower = tolower,
    .toupper = toupper,
};
