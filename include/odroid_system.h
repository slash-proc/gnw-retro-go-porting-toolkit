//
// "Pretend retro-go" persistence API.
//
// Our firmware plays the role of retro-go for the app payload: it owns the
// OSPI flash hardware and exposes this API, which the payload (the "app") calls
// to register save/load callbacks and to read/write its persistent data. The
// payload never touches OSPI itself.
//
// This subset mirrors examples/game-and-watch-retro-go-sd's odroid_system.h so a
// future real-retro-go port stays source-compatible. For the flash-only build,
// the file/blob layer (rg_blob_*) is backed by a fixed reserved region of the
// external flash (see src/persist.c), not a filesystem.
//

#ifndef ODROID_SYSTEM_H
#define ODROID_SYSTEM_H

#include <stdint.h>
#include <stdbool.h>

// Save/load callbacks an app registers via odroid_system_emu_init().
typedef bool (*state_handler_t)(const char *filename);
typedef void *(*screenshot_handler_t)(void);
typedef void (*shutdown_handler_t)(void);
typedef void (*sleep_post_wakeup_handler_t)(void);
typedef void (*sram_save_handler_t)(void);

// Logical path kinds. Values match retro-go's enum spacing closely enough for a
// stub; odroid_system_get_path() turns these into logical blob names.
typedef enum {
    ODROID_PATH_SAVE_STATE_1 = 1,
    ODROID_PATH_SAVE_STATE_2 = 2,
    ODROID_PATH_SAVE_STATE_3 = 3,
    ODROID_PATH_SAVE_SRAM    = 10,
} emu_path_type_t;

// Register the app's persistence callbacks (any may be NULL).
void odroid_system_emu_init(state_handler_t load_cb, state_handler_t save_cb,
                            screenshot_handler_t screenshot_cb,
                            shutdown_handler_t shutdown_cb,
                            sleep_post_wakeup_handler_t sleep_post_wakeup_cb,
                            sram_save_handler_t sram_save_cb);

// Resolve a logical save path (e.g. "<prefix>.sram", "<prefix>-1.sav"; see
// APP_SAVE_PREFIX). The returned pointer is valid until the next call.
const char *odroid_system_get_path(emu_path_type_t type, const char *rom_path);

// Drive the registered save/load-state callbacks for a 0-based slot. The
// framework owns path construction; the app's callback just does the I/O.
bool odroid_system_emu_save_state(int slot);
bool odroid_system_emu_load_state(int slot);

// Invoke the registered SRAM-save callback (real retro-go calls this on
// app-switch / sleep; here it is driven explicitly by the app).
void odroid_system_sram_save(void);

// Flash-backed blob store — the flash-only stand-in for retro-go's fopen/fwrite
// filesystem. Names are logical (see the slot table in src/persist.c).
//   rg_blob_write: returns bytes stored, or -1 on error (unknown name / too big).
//   rg_blob_read:  returns the stored length (may exceed `max`, indicating
//                  truncation), or -1 if the slot is empty/unwritten.
int rg_blob_write(const char *name, const void *buf, uint32_t len);
int rg_blob_read(const char *name, void *buf, uint32_t max);
// Direct XIP pointer + length for a stored slot (NULL if empty) — lets a reader
// consume the bytes in place without copying. Clear a slot with rg_blob_erase.
const uint8_t *rg_blob_ptr(const char *name, uint32_t *len_out);
void rg_blob_erase(const char *name);

#endif // ODROID_SYSTEM_H
