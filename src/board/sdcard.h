#ifndef SDCARD_H
#define SDCARD_H
//
// sdcard.h — SD-card filesystem bring-up for the toolkit firmware.
//
// Mirrors retro-go-sd's SD probe: try the SPI1 mod (Tim's, now standard) first,
// then the soft-SPI-over-flash-pins mod (Yota9). Compiled only when SD_CARD==1
// (the retro-go storage lever: FAT-on-SD vs LFS-on-flash). Generic — no
// project-specific paths or behavior live here.
//
#include <stdbool.h>

typedef enum {
    SD_BACKEND_NONE = 0,
    SD_BACKEND_SPI1,      // hardware SPI1 (Tim's mod): PD7/PB3/PB4, CS PB9, VCC PA15
    SD_BACKEND_SOFTSPI,   // bit-bang over shared OSPI flash pins (Yota9 mod) — 1.2b
} sd_backend_t;

// Probe + mount the SD card (SPI1 first, soft-SPI next). Sets the active backend.
void sdcard_init(void);
// Unmount and power down the SD interface.
void sdcard_deinit(void);
// True once a filesystem is mounted.
bool sdcard_mounted(void);
// Active backend (SD_BACKEND_NONE until a successful mount). Used by diskio.c.
sd_backend_t sdcard_backend(void);

// SPI1 peripheral config (defines hspi1). Public so the SD driver can reach it.
void MX_SPI1_Init(void);

// --- SD bring-up self-test (SWD-readable) -----------------------------------
// A debugger can read g_sd_probe after boot to confirm the SD stack mounted and
// can list the root, without a UART (magic == 0x5DCAFE01 once populated).
#include <stdint.h>
#define SD_PROBE_MAX_ENTRIES 8
#define SD_PROBE_NAME_LEN    64
typedef struct {
    uint32_t magic;        // 0x5DCAFE01 once populated
    uint32_t mounted;      // sdcard_mounted()
    uint32_t backend;      // sd_backend_t (0 none / 1 SPI1 / 2 soft-SPI)
    uint32_t count;        // number of root entries captured
    char     names[SD_PROBE_MAX_ENTRIES][SD_PROBE_NAME_LEN];
} sd_probe_t;
extern volatile sd_probe_t g_sd_probe;
void sdcard_selftest(void);   // list "/" into g_sd_probe

// OSPI<->soft-SPI pin handoff for the Yota9 mod (shared flash pins). ToOspi=1
// restores memory-mapped flash; ToOspi=0 suspends it and drives the flash pins
// as GPIO for bit-banging. Called by the soft-SPI SD driver around each op.
void switch_ospi_gpio(uint8_t ToOspi);

#endif // SDCARD_H
