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

#endif // SDCARD_H
