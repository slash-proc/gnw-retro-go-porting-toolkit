//
// gnw_firmware_abi.h — versioned firmware ABI for runtime-loaded apps.
//
// EXACT layout mirror of real retro-go's gw_firmware_abi_t
// (Core/Inc/retro-go/gw_firmware_abi.h, v1): the firmware publishes the table
// at a FIXED intflash offset (VTOR + 0x400); an app reads the struct and calls
// THROUGH it instead of linking against firmware symbol addresses. Because the
// layouts match field-for-field (every member is a 4-byte pointer on ARM32),
// an app binary built against this header runs unchanged on real retro-go and
// on this toolkit's test firmware — that is the toolkit's whole purpose.
//
// The canonical header drags in retro-go's include web (odroid_*, FatFs, ...),
// so this twin uses self-contained, loosely-typed fields (opaque void* for
// FILE/DIR/FILINFO/jmp_buf/handler pointers — same offsets, same size).
//
// Backwards-compat rules (same as retro-go): NEVER reorder/remove fields; only
// ADD at the end, bumping the version; apps check version+size at init and may
// ignore newer fields. Field names must match the canonical header. The test
// firmware fills only the slots it implements (rest NULL) — see firmware_abi.c.
//
#ifndef GNW_FIRMWARE_ABI_H
#define GNW_FIRMWARE_ABI_H

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GNW_FIRMWARE_ABI_VERSION 1u           /* == GW_FIRMWARE_ABI_VERSION */
#define GNW_FIRMWARE_ABI_OFFSET  0x400u       /* from VTOR (active vector table base) */
#define GNW_VTOR_ADDRESS         0xE000ED08u  /* ARMv7-M Vector Table Offset Register */

typedef struct gnw_firmware_abi {
    /* Header — every app checks these before using the rest. */
    uint32_t version;
    uint32_t size;

    /* libc: string.h */
    void  *(*memchr)(const void *, int, size_t);
    int    (*memcmp)(const void *, const void *, size_t);
    void  *(*memcpy)(void *, const void *, size_t);
    void  *(*memmem)(const void *, size_t, const void *, size_t);
    void  *(*memmove)(void *, const void *, size_t);
    void  *(*memset)(void *, int, size_t);
    char  *(*strchr)(const char *, int);
    int    (*strcmp)(const char *, const char *);
    int    (*strcoll)(const char *, const char *);
    size_t (*strlen)(const char *);
    int    (*strncmp)(const char *, const char *, size_t);
    char  *(*strncpy)(char *, const char *, size_t);
    char  *(*strpbrk)(const char *, const char *);
    char  *(*strrchr)(const char *, int);
    size_t (*strspn)(const char *, const char *);
    char  *(*strstr)(const char *, const char *);
    char  *(*strerror)(int);

    /* libc: ctype.h */
    int (*isalnum)(int);
    int (*isalpha)(int);
    int (*iscntrl)(int);
    int (*isgraph)(int);
    int (*islower)(int);
    int (*ispunct)(int);
    int (*isspace)(int);
    int (*isupper)(int);
    int (*isxdigit)(int);
    int (*tolower)(int);
    int (*toupper)(int);

    /* libc: stdlib.h */
    void   (*abort)(void);
    void   (*qsort)(void *base, size_t nmemb, size_t size,
                    int (*compar)(const void *, const void *));
    double (*strtod)(const char *nptr, char **endptr);
    long   (*strtol)(const char *nptr, char **endptr, int base);

    /* libc: stdio.h (FILE is opaque across the ABI) */
    void  *(*fopen)(const char *path, const char *mode);
    int    (*fclose)(void *stream);
    size_t (*fread)(void *ptr, size_t size, size_t nmemb, void *stream);
    size_t (*fwrite)(const void *ptr, size_t size, size_t nmemb, void *stream);
    int    (*fseek)(void *stream, long offset, int whence);
    long   (*ftell)(void *stream);
    int    (*feof)(void *stream);
    int    (*ferror)(void *stream);
    int    (*fgetc)(void *stream);
    int    (*fputc)(int c, void *stream);
    void  *(*freopen)(const char *path, const char *mode, void *stream);
    int    (*remove)(const char *path);
    int    (*putchar)(int c);
    int    (*puts)(const char *s);
    int    (*fflush)(void *stream);
    int   *(*__errno)(void);
    int    (*vfprintf)(void *, const char *, va_list);
    int    (*vprintf)(const char *, va_list);
    int    (*vsnprintf)(char *, size_t, const char *, va_list);
    int    (*vsprintf)(char *, const char *, va_list);
    int    (*vfscanf)(void *, const char *, va_list);

    /* libc: time.h / setjmp.h / locale.h / libm (jmp_buf, lconv opaque) */
    long   (*time)(long *);
    int    (*setjmp)(void *env);
    void   (*longjmp)(void *env, int val);
    void  *(*localeconv)(void);
    double (*pow)(double x, double y);

    /* libc: assert */
    void (*__assert_func)(const char *file, int line,
                          const char *func, const char *expr);

    /* libgcc helpers */
    int64_t  (*aeabi_d2lz)(double);
    float    (*aeabi_l2f)(int64_t);
    int64_t  (*ldivmod_quot)(int64_t, int64_t);
    int64_t  (*ldivmod_rem)(int64_t, int64_t);
    int      (*popcountsi2)(unsigned);
    uint64_t (*uldivmod_quot)(uint64_t, uint64_t);
    uint64_t (*uldivmod_rem)(uint64_t, uint64_t);

    /* FatFs (DIR / FILINFO pointers opaque, FRESULT -> int) */
    int (*f_opendir)(void *dp, const char *path);
    int (*f_closedir)(void *dp);
    int (*f_readdir)(void *dp, void *fno);

    /* G&W hardware: LCD */
    void  (*lcd_swap)(void);
    void *(*lcd_get_active_buffer)(void);
    void *(*lcd_get_inactive_buffer)(void);
    void *(*lcd_clear_active_buffer)(void);
    void *(*lcd_clear_inactive_buffer)(void);

    /* G&W hardware: audio */
    void     (*audio_start_playing)(uint16_t length);
    int16_t *(*audio_get_active_buffer)(void);
    void     (*audio_clear_active_buffer)(void);
    void     (*audio_clear_inactive_buffer)(void);

    /* G&W hardware: allocators */
    void  *(*itc_malloc)(size_t size);
    void  *(*itc_calloc)(size_t count, size_t size);
    void   (*itc_init)(void);
    void  *(*ram_malloc)(size_t size);
    size_t (*ram_get_free_size)(void);

    /* G&W hardware: RTC */
    uint8_t (*GW_GetCurrentYear)(void);
    uint8_t (*GW_GetCurrentMonth)(void);
    uint8_t (*GW_GetCurrentDay)(void);
    uint8_t (*GW_GetCurrentHour)(void);
    uint8_t (*GW_GetCurrentMinute)(void);
    uint8_t (*GW_GetCurrentSecond)(void);

    /* G&W hardware: watchdog + HAL */
    void     (*wdog_refresh)(void);
    void     (*HAL_Delay)(uint32_t ms);
    uint32_t (*HAL_GetTick)(void);

    /* retro-go: system (handler args opaque) */
    void (*odroid_system_init)(int app_id, int sample_rate);
    void (*odroid_system_emu_init)(void *load_cb, void *save_cb,
                                   void *screenshot_cb, void *shutdown_cb,
                                   void *sleep_post_wakeup_cb, void *sram_save_cb);
    void (*odroid_system_switch_app)(int app);

    /* retro-go: input / display */
    void (*odroid_input_read_gamepad)(void *out_state);
    int  (*odroid_display_get_scaling_mode)(void);
    void (*odroid_display_set_scaling_mode)(int mode);

    /* retro-go: overlay / SD / settings */
    int      (*odroid_overlay_draw_text)(uint16_t x, uint16_t y, uint16_t width,
                                         const char *text, uint16_t color, uint16_t color_bg);
    uint8_t *(*odroid_overlay_cache_file_in_flash)(const char *file_path,
                                                   uint32_t *file_size_p, int byte_swap);
    int      (*odroid_sdcard_mkdir)(const char *path);
    int32_t  (*odroid_settings_app_int32_get)(const char *key, int32_t default_value);
    void     (*odroid_settings_app_int32_set)(const char *key, int32_t value);

    /* retro-go: common emulator loop (bool -> int, struct ptrs opaque) */
    int     (*common_emu_frame_loop)(void);
    void    (*common_emu_input_loop)(void *joystick, void *game_options, void *repaint);
    void    (*common_emu_input_loop_handle_turbo)(void *joystick);
    uint8_t (*common_emu_sound_get_volume)(void);
    int     (*common_emu_sound_loop_is_muted)(void);
    void    (*common_emu_sound_sync)(int use_nops);
    void    (*common_ingame_overlay)(void);

    /* Missing libc (discovered after v1 initial list) */
    char *(*fgets)(char *, int, void *);
    void  (*free)(void *);
    void *(*realloc)(void *, size_t);
    int   (*ungetc)(int, void *);

    /* Firmware data pointers */
    void      *common_emu_state_ptr;
    void     **ROM_DATA_ptr;
    unsigned  *ROM_DATA_LENGTH_ptr;
    void     **ACTIVE_FILE_ptr;
    uint8_t  **pico8_code_flash_addr_ptr;
    uint32_t  *pico8_code_flash_size_ptr;
    uint32_t  *ram_start_ptr;
    void     **impure_ptr_ptr;
    void      *dtcm_p8ram_start;

    /* =====[ v1 appends ]===== */
    void *(*dtcm_malloc)(size_t size);
    int   (*odroid_system_emu_load_state)(int slot);
    void  (*odroid_audio_mute)(int mute);
    void  (*lcd_setup_framebuffers)(int lcd_mode);
    void  (*lcd_get_bonus_pool)(uint8_t **out_ptr, size_t *out_size);
    void  (*lcd_set_clut)(const uint32_t *clut, uint16_t count);

} gnw_firmware_abi_t;

#ifdef __cplusplus
}
#endif

#endif // GNW_FIRMWARE_ABI_H
