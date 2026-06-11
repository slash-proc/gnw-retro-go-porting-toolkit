//
// gnw_firmware_abi.h — versioned firmware ABI for runtime-loaded apps.
//
// Mirrors retro-go's gw_firmware_abi mechanism (Core/Inc/retro-go/gw_firmware_abi.h):
// the firmware publishes g_firmware_abi at a FIXED intflash offset (VTOR + 0x400);
// an app reads the struct and calls THROUGH it instead of linking against firmware
// symbol addresses. This decouples the app binary from the firmware's code layout —
// the firmware can be rebuilt/refactored without breaking a previously built app, as
// long as GNW_FIRMWARE_ABI_VERSION is unchanged.
//
// Backwards-compat rules (same as retro-go): NEVER reorder/remove fields; only ADD
// at the end, bumping the version; apps check version+size at init and may ignore
// newer fields. This is the contract that replaces the --just-symbols coupling.
//
// (Our surface is smaller than retro-go's full gw_firmware_abi_t — no FatFS, our own
// display + littlefs storage — so this lists the functions a core typically needs.
// It can be widened toward gw_firmware_abi_t for full cross-firmware compatibility.)
//
#ifndef GNW_FIRMWARE_ABI_H
#define GNW_FIRMWARE_ABI_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#define GNW_FIRMWARE_ABI_VERSION 1u
#define GNW_FIRMWARE_ABI_OFFSET  0x400u        // from VTOR (active vector table base)
#define GNW_VTOR_ADDRESS         0xE000ED08u   // ARMv7-M Vector Table Offset Register

typedef struct gnw_firmware_abi {
    uint32_t version;   // == GNW_FIRMWARE_ABI_VERSION
    uint32_t size;      // == sizeof(gnw_firmware_abi_t)

    // --- libc retro-go's ABI provides (the rest the port owns in gnw_libc.c) ---
    void  *(*memcpy)(void *, const void *, size_t);
    void  *(*memset)(void *, int, size_t);
    void  *(*memmove)(void *, const void *, size_t);
    int    (*memcmp)(const void *, const void *, size_t);
    size_t (*strlen)(const char *);
    char  *(*strncpy)(char *, const char *, size_t);
    int    (*strcmp)(const char *, const char *);
    int    (*strncmp)(const char *, const char *, size_t);
    size_t (*strspn)(const char *, const char *);

    // --- retro-go persistence + power --------------------------------------
    void   (*odroid_system_emu_init)(void *load, void *save, void *screenshot,
                                     void *shutdown, void *wakeup, void *sram_save);
    // save/load/get_path/sram_save are firmware-internal: the firmware's overlay
    // menu + standby flow call the handlers the core registered above; a core
    // never calls them. The core "quits" via odroid_system_switch_app (-> launcher
    // on real retro-go; -> Standby here). POWER+PAUSE reaches power_off directly.
    void   (*odroid_system_switch_app)(int app);

    // --- storage: stdio VFS (mirrors retro-go's plugin file API; the firmware
    //     backs it with littlefs, so the app sees files, never the filesystem).
    //     FILE* is opaque (void*) across the ABI boundary. --------------------
    void  *(*fopen)(const char *path, const char *mode);
    int    (*fclose)(void *stream);
    size_t (*fread)(void *ptr, size_t size, size_t nmemb, void *stream);
    size_t (*fwrite)(const void *ptr, size_t size, size_t nmemb, void *stream);
    int    (*remove)(const char *path);

    // --- input (POWER is not exposed; the firmware owns power/standby) ------
    void     (*odroid_input_read_gamepad)(void *out_state);

    // --- audio (firmware owns the SAI/DMA ring; the app refills) ------------
    void     (*audio_start_playing)(uint16_t length);   // start DMA over length*2
    int16_t *(*audio_get_active_buffer)(void);          // the half to refill

    // --- display: LUT8 CLUT + retro-go double-buffer ----------------------
    void   (*lcd_set_clut)(const uint32_t *clut, uint16_t count);
    void   (*lcd_setup_framebuffers)(int lcd_mode);   // LCD_MODE_LUT8
    void  *(*lcd_get_active_buffer)(void);            // the surface to draw into
    void  *(*lcd_get_inactive_buffer)(void);
    void   (*lcd_swap)(void);                         // present, then flip (vblank)

    // --- in-game overlay/menu (retro-go ABI: common_ingame_overlay) --------
    void   (*common_ingame_overlay)(void);       // firmware uses the stored CLUT

    // --- timing (retro-go ABI: HAL_GetTick) --------------------------------
    uint32_t (*HAL_GetTick)(void);               // free-running 1 kHz ms clock

    // --- more retro-go-provided libc ---------------------------------------
    char  *(*strstr)(const char *, const char *);
    int    (*tolower)(int);
    int    (*toupper)(int);
} gnw_firmware_abi_t;

#endif // GNW_FIRMWARE_ABI_H
