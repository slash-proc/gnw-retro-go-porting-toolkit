/*-----------------------------------------------------------------------*/
/* FatFs disk I/O glue for the porting toolkit.                           */
/*                                                                        */
/* Dispatches the FatFs disk_* calls to the active SD backend selected at */
/* mount time (sdcard.c). SPI1 (Tim's mod) today; soft-SPI (Yota9) is     */
/* added in 1.2b. Single physical drive (the SD card); pdrv is ignored.   */
/*-----------------------------------------------------------------------*/

#include "ff.h"
#include "diskio.h"
#include "user_diskio_spi.h"     /* USER_SPI_* (SPI1 backend) */
#include "user_diskio_softspi.h" /* USER_SOFTSPI_* (soft-SPI / Yota9 backend) */
#include "sdcard.h"              /* sdcard_backend() */
#include "board.h"               /* board_rtc_get_fattime() */

DSTATUS disk_status(BYTE pdrv)
{
    switch (sdcard_backend()) {
    case SD_BACKEND_SPI1:    return USER_SPI_status(pdrv);
    case SD_BACKEND_SOFTSPI: return USER_SOFTSPI_status(pdrv);
    default:                 return STA_NOINIT;
    }
}

DSTATUS disk_initialize(BYTE pdrv)
{
    switch (sdcard_backend()) {
    case SD_BACKEND_SPI1:    return USER_SPI_initialize(pdrv);
    case SD_BACKEND_SOFTSPI: return USER_SOFTSPI_initialize(pdrv);
    default:                 return STA_NOINIT;
    }
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    switch (sdcard_backend()) {
    case SD_BACKEND_SPI1:    return USER_SPI_read(pdrv, buff, sector, count);
    case SD_BACKEND_SOFTSPI: return USER_SOFTSPI_read(pdrv, buff, sector, count);
    default:                 return RES_NOTRDY;
    }
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    switch (sdcard_backend()) {
    case SD_BACKEND_SPI1:    return USER_SPI_write(pdrv, buff, sector, count);
    case SD_BACKEND_SOFTSPI: return USER_SOFTSPI_write(pdrv, buff, sector, count);
    default:                 return RES_NOTRDY;
    }
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    switch (sdcard_backend()) {
    case SD_BACKEND_SPI1:    return USER_SPI_ioctl(pdrv, cmd, buff);
    case SD_BACKEND_SOFTSPI: return USER_SOFTSPI_ioctl(pdrv, cmd, buff);
    default:                 return RES_NOTRDY;
    }
}

/* FatFs timestamp (FF_FS_NORTC == 0) — sourced from the board RTC. */
DWORD get_fattime(void)
{
    return board_rtc_get_fattime();
}
