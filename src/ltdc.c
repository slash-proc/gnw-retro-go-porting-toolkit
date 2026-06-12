#include <regs.h>
#include <stdio.h>
#include "stm32h7xx_hal.h"
#include "board.h"   // board_gpio_init()

LTDC_HandleTypeDef hltdc;

void ltdc_init_lut8(void *frame_buffer) {
    hltdc.Instance = LTDC;
    hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
    hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
    hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
    hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IIPC;
    hltdc.Init.HorizontalSync = 9;
    hltdc.Init.VerticalSync = 1;
    hltdc.Init.AccumulatedHBP = 60;
    hltdc.Init.AccumulatedVBP = 7;
    hltdc.Init.AccumulatedActiveW = 380;
    hltdc.Init.AccumulatedActiveH = 247;
    hltdc.Init.TotalWidth = 392;
    hltdc.Init.TotalHeigh = 255;
    hltdc.Init.Backcolor.Blue = 0;
    hltdc.Init.Backcolor.Green = 0;
    hltdc.Init.Backcolor.Red = 0;
    
    if (HAL_LTDC_Init(&hltdc) != HAL_OK) {
        printf("LTDC Init failed\n");
    }

    LTDC_LayerCfgTypeDef pLayerCfg = {0};
    pLayerCfg.WindowX0 = 0;
    pLayerCfg.WindowX1 = 320;
    pLayerCfg.WindowY0 = 0;
    pLayerCfg.WindowY1 = 240;
    pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_L8;
    pLayerCfg.Alpha = 255;
    pLayerCfg.Alpha0 = 0;
    pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
    pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
    pLayerCfg.FBStartAdress = (uint32_t)frame_buffer;
    pLayerCfg.ImageWidth = 320;
    pLayerCfg.ImageHeight = 240;
    pLayerCfg.Backcolor.Blue = 0;
    pLayerCfg.Backcolor.Green = 0;
    pLayerCfg.Backcolor.Red = 0;

    if (HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, 0) != HAL_OK) {
        printf("LTDC ConfigLayer failed\n");
    }

    // Program the reload
    HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_IMMEDIATE);
}

// The app's live CLUT pointer, stashed for the firmware overlay (which renders in
// LUT8 and needs legible palette indices). Points at the app's static palette.
static const uint32_t *s_clut;
const uint32_t *lcd_get_clut(void) { return s_clut; }

void lcd_set_clut(const uint32_t *clut, uint16_t count) {
    s_clut = clut;
    if (HAL_LTDC_ConfigCLUT(&hltdc, (uint32_t *)clut, count, 0) != HAL_OK) {
        printf("LTDC ConfigCLUT failed\n");
    }
    if (HAL_LTDC_EnableCLUT(&hltdc, 0) != HAL_OK) {
        printf("LTDC EnableCLUT failed\n");
    }
    HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_IMMEDIATE);
}

// --- Double-buffer (retro-go model) -----------------------------------------
// Two LUT8 surfaces carved from the RAM_UC pool. The core draws into
// lcd_get_active_buffer() and calls lcd_swap(); the LTDC framebuffer address
// reloads at vblank (tear-free). The pool is LUT8-sized (160K = 2x 75K), so
// lcd_setup_framebuffers takes a mode for ABI parity but only LUT8 fits.
#include <string.h>
extern uint8_t _frame_buffer[];     // RAM_UC pool base

// MPU map, MIRRORING REAL RETRO-GO (apps depend on this exact ownership):
//   region 0: AHBRAM 128K @0x30000000 uncached/non-bufferable (audio DMA head;
//             apps re-carve 0x30004000+ cacheable via their own region 7)
//   regions 3..6: LCD framebuffer footprint @0x24000000 uncached so the LTDC
//             sees CPU writes immediately (154K LUT8 = 128+16+8+2)
// Regions 1/2 (null guard, stack redzone) are retro-go niceties we skip;
// region 7 is reserved for the app. lcd_setup_framebuffers re-asserts the
// LCD coverage exactly like retro-go's mpu_set_lcd_pool_uncached_range.
static void mpu_region(int n, uint32_t base, uint32_t size_log2, int cacheable)
{
    volatile uint32_t *MPU_RNR  = (volatile uint32_t *)0xE000ED98;
    volatile uint32_t *MPU_RBAR = (volatile uint32_t *)0xE000ED9C;
    volatile uint32_t *MPU_RASR = (volatile uint32_t *)0xE000EDA0;
    uint32_t rasr = (0x3u << 24) | (1u << 19) | ((size_log2 - 1u) << 1) | 1u;
    if (cacheable)
        rasr |= (1u << 17) | (1u << 16);   // C+B: Normal WBWA
    *MPU_RNR = (uint32_t)n; *MPU_RBAR = base; *MPU_RASR = rasr;
}

void gnw_mpu_init(void)
{
    volatile uint32_t *MPU_CTRL = (volatile uint32_t *)0xE000ED94;
    __asm__ volatile ("dmb");
    *MPU_CTRL = 0;                                   // disable while updating
    mpu_region(0, 0x30000000, 17, 0);                // AHB 128K uncached
    // LUT8 LCD pool: 154K = 128 + 16 + 8 + 2
    mpu_region(3, 0x24000000, 17, 0);                // 128K
    mpu_region(4, 0x24020000, 14, 0);                // 16K
    mpu_region(5, 0x24024000, 13, 0);                // 8K
    mpu_region(6, 0x24026000, 11, 0);                // 2K
    *MPU_CTRL = (1u << 2) | (1u << 1) | 1u;          // PRIVDEFENA|HFNMIENA|ENABLE
    __asm__ volatile ("dsb; isb");
}

#define LCD_FB_W 320
#define LCD_FB_H 240

static uint8_t *s_lcd_fb[2];
static int      s_lcd_active;       // the buffer the core draws into

void lcd_setup_framebuffers(int lcd_mode) {
    (void)lcd_mode;                                  // pool is LUT8-sized
    const uint32_t sz = LCD_FB_W * LCD_FB_H;         // 1 byte/px (L8)
    s_lcd_fb[0]  = _frame_buffer;
    s_lcd_fb[1]  = _frame_buffer + sz;
    s_lcd_active = 0;
    memset(s_lcd_fb[0], 0, 2u * sz);                 // both buffers -> black
    HAL_LTDC_SetPixelFormat(&hltdc, LTDC_PIXEL_FORMAT_L8, 0);
    HAL_LTDC_SetAddress(&hltdc, (uint32_t)s_lcd_fb[0], 0);
    HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING);
    gnw_mpu_init();   // re-assert LCD/AHB coverage, retro-go style
}

void *lcd_get_active_buffer(void)   { return s_lcd_fb[s_lcd_active]; }
void *lcd_get_inactive_buffer(void) { return s_lcd_fb[s_lcd_active ^ 1]; }

void lcd_swap(void) {
    // Present the just-drawn (active) buffer at the next vblank, then flip so the
    // next frame draws into the other surface.
    HAL_LTDC_SetAddress(&hltdc, (uint32_t)s_lcd_fb[s_lcd_active], 0);
    HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING);
    s_lcd_active ^= 1;
}

// --- Physical LCD panel bring-up (G&W LCD init sequence) ---
//
// Pin map (G&W): SPI2 SCK=PB13, MOSI=PB15 (AF5); LCD CS=PB12; Reset=PD8;
// power rails 3V3=PD4 (active high), 1V8=PD1 (active low); backlight=PA4/5/6.
// board_early_init() already set these as outputs (CS high, reset low,
// backlight off) and enabled the GPIO clocks.

static void lcd_spi_tx(const uint8_t cmd[2])
{
    GPIOB->BSRR = (uint32_t)GPIO_PIN_12 << 16;   // CS low (assert)
    HAL_Delay(2);

    SPI2->CR1 &= ~SPI_CR1_SPE;
    SPI2->CR2 = 2;
    SPI2->CR1 |= SPI_CR1_SPE;
    SPI2->CR1 |= SPI_CR1_CSTART;

    for (int i = 0; i < 2; i++) {
        while (!(SPI2->SR & SPI_SR_TXP))
            ;
        *((volatile uint8_t *) &SPI2->TXDR) = cmd[i];
    }
    while (!(SPI2->SR & SPI_SR_EOT))
        ;
    SPI2->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC;

    HAL_Delay(2);
    GPIOB->BSRR = GPIO_PIN_12;   // CS high (idle)
    HAL_Delay(2);
}

void lcd_panel_init(void)
{
    // SPI2 setup (SCK=PB13, MOSI=PB15, AF5). CS/reset/power/backlight GPIOs
    // are already configured by board_early_init().
    __HAL_RCC_SPI2_CLK_ENABLE();
    board_gpio_init(GPIOB, GPIO_PIN_13 | GPIO_PIN_15,
                    GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW, GPIO_AF5_SPI2);

    __HAL_RCC_SPI2_FORCE_RESET();
    __HAL_RCC_SPI2_RELEASE_RESET();
    SPI2->CFG1 = (7 << SPI_CFG1_DSIZE_Pos) | (4 << SPI_CFG1_MBR_Pos);
    SPI2->CFG2 = SPI_CFG2_MASTER | SPI_CFG2_SSM | SPI_CFG2_SSOE;
    SPI2->CR1 = SPI_CR1_SSI;
    SPI2->CR2 = 2;
    SPI2->CR1 |= SPI_CR1_SPE;

    GPIOB->BSRR = GPIO_PIN_12;   // CS high (idle)
    GPIOD->BSRR = GPIO_PIN_8;    // reset deasserted (high)

    // Power on: 3V3 first, then 1V8, then let the rails settle.
    GPIOD->BSRR = GPIO_PIN_4;                   // 3V3 on  (active high)
    HAL_Delay(2);
    GPIOD->BSRR = (uint32_t)GPIO_PIN_1 << 16;   // 1V8 on  (active low)
    HAL_Delay(50);

    // Reset pulse: high, assert low, release high.
    GPIOD->BSRR = GPIO_PIN_8;                    // high
    HAL_Delay(1);
    GPIOD->BSRR = (uint32_t)GPIO_PIN_8 << 16;    // low (assert)
    HAL_Delay(20);
    GPIOD->BSRR = GPIO_PIN_8;                    // high (release)
    HAL_Delay(50);

    // Panel init command sequence (2 bytes each).
    lcd_spi_tx((const uint8_t[]){0x08, 0x80});
    lcd_spi_tx((const uint8_t[]){0x6E, 0x80});
    lcd_spi_tx((const uint8_t[]){0x80, 0x80});
    lcd_spi_tx((const uint8_t[]){0x68, 0x00});
    lcd_spi_tx((const uint8_t[]){0xD0, 0x00});
    lcd_spi_tx((const uint8_t[]){0x1B, 0x00});
    lcd_spi_tx((const uint8_t[]){0xE0, 0x00});
    lcd_spi_tx((const uint8_t[]){0x6A, 0x80});
    lcd_spi_tx((const uint8_t[]){0x80, 0x00});
    lcd_spi_tx((const uint8_t[]){0x14, 0x80});
    HAL_Delay(50);

    // Backlight on.
    GPIOA->BSRR = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
}

