//
// sdcard.c — SD-card bring-up (SPI1 / Tim's mod) for the toolkit firmware.
//
// Hardware ported faithfully from retro-go-sd (Core/Src/gw_sdcard.c SPI1 path,
// Core/Src/main.c MX_SPI1_Init, Core/Src/stm32h7xx_hal_msp.c SPI1 MspInit); the
// retro-go error-screen / overlay UI is intentionally omitted (the test firmware
// needs the mount, not the GUI). Soft-SPI (Yota9) fallback follows in 1.2b.
//
// Compiled only when SD_CARD==1 (the retro-go storage lever).
//
#include "stm32h7xx_hal.h"
#include "main.h"
#include "board.h"
#include "sdcard.h"
#include "ff.h"

SPI_HandleTypeDef hspi1;

static FATFS       s_fatfs;
static bool        s_mounted = false;
static sd_backend_t s_backend = SD_BACKEND_NONE;

sd_backend_t sdcard_backend(void) { return s_backend; }
bool         sdcard_mounted(void) { return s_mounted; }

// --- SPI1 peripheral (faithful: retro-go main.c MX_SPI1_Init) ---------------
void MX_SPI1_Init(void)
{
    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_MASTER;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;
    hspi1.Init.NSS               = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
    hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial     = 0;
    hspi1.Init.NSSPMode          = SPI_NSS_PULSE_DISABLE;
    hspi1.Init.NSSPolarity       = SPI_NSS_POLARITY_LOW;
    hspi1.Init.FifoThreshold     = SPI_FIFO_THRESHOLD_01DATA;
    hspi1.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    hspi1.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    hspi1.Init.MasterSSIdleness        = SPI_MASTER_SS_IDLENESS_00CYCLE;
    hspi1.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    hspi1.Init.MasterReceiverAutoSusp  = SPI_MASTER_RX_AUTOSUSP_DISABLE;
    hspi1.Init.MasterKeepIOState       = SPI_MASTER_KEEP_IO_STATE_DISABLE;
    hspi1.Init.IOSwap                  = SPI_IO_SWAP_DISABLE;
    if (HAL_SPI_Init(&hspi1) != HAL_OK)
        Error_Handler();
}

// SPI1 pin/clock bring-up (faithful: retro-go stm32h7xx_hal_msp.c SPI1 branch).
// PD7 = MOSI, PB3 = SCK, PB4 = MISO (AF5). Called by HAL_SPI_Init.
void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if (hspi->Instance == SPI1) {
        __HAL_RCC_SPI1_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();

        GPIO_InitStruct.Pin       = GPIO_PIN_7;                 /* PD7 MOSI */
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull      = GPIO_NOPULL;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
        HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

        GPIO_InitStruct.Pin       = GPIO_PIN_3;                 /* PB3 SCK */
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

        GPIO_InitStruct.Pin       = GPIO_PIN_4;                 /* PB4 MISO */
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    }
}

// --- SPI1 SD power/CS + init (faithful: retro-go gw_sdcard.c sdcard_init_spi1) -
static void sdcard_init_spi1(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    HAL_GPIO_WritePin(SD_VCC_GPIO_Port, SD_VCC_Pin, GPIO_PIN_RESET);  /* VCC off */
    HAL_GPIO_WritePin(SD_CS_GPIO_Port,  SD_CS_Pin,  GPIO_PIN_SET);    /* CS high */

    GPIO_InitStruct.Pin   = SD_VCC_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SD_VCC_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin   = SD_CS_Pin;
    HAL_GPIO_Init(SD_CS_GPIO_Port, &GPIO_InitStruct);

    HAL_Delay(5);                                                     /* VCC reset */
    HAL_GPIO_WritePin(SD_VCC_GPIO_Port, SD_VCC_Pin, GPIO_PIN_SET);    /* VCC on */

    MX_SPI1_Init();
    HAL_SPI_MspInit(&hspi1);
}

static void sdcard_deinit_spi1(void)
{
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
    uint8_t dummy = 0xFF;                       /* release MISO before shutdown */
    HAL_SPI_Transmit(&hspi1, &dummy, 1, 100);
    HAL_SPI_DeInit(&hspi1);
    HAL_GPIO_WritePin(SD_VCC_GPIO_Port, SD_VCC_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SD_CS_GPIO_Port,  SD_CS_Pin,  GPIO_PIN_RESET);
}

// --- public probe/mount -----------------------------------------------------
void sdcard_init(void)
{
    // Try SPI1 (Tim's mod) first.
    sdcard_init_spi1();
    s_backend = SD_BACKEND_SPI1;
    if (f_mount(&s_fatfs, "", 1) == FR_OK) {
        s_mounted = true;
        return;
    }
    sdcard_deinit_spi1();

    // TODO(1.2b): soft-SPI (Yota9) fallback via board_ospi_suspend/resume.

    s_backend = SD_BACKEND_NONE;
    s_mounted = false;
}

void sdcard_deinit(void)
{
    if (s_mounted) {
        f_unmount("");
        s_mounted = false;
    }
    if (s_backend == SD_BACKEND_SPI1)
        sdcard_deinit_spi1();
    s_backend = SD_BACKEND_NONE;
}
