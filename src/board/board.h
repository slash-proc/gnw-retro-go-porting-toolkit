#ifndef BOARD_H
#define BOARD_H

#include "main.h"
#include <stdbool.h>

#define BOOT_DELAY_CYCLES 2000000
#define STABILIZATION_DELAY_CYCLES 100000

/* Bare-metal GPIO config (replaces HAL_GPIO_Init). Args take the HAL
 * GPIO_MODE_x / GPIO_PULLx / GPIO_SPEED_x / GPIO_AFx constants. */
void board_gpio_init(GPIO_TypeDef *g, uint32_t pins, uint32_t mode,
                     uint32_t pull, uint32_t speed, uint32_t af);
void board_early_init(void);
void board_clocks_init(void);
/* Runtime CPU overclock (set from the in-game overlay). Only PLL1 changes; OSPI is
 * on HSI so XIP stays in spec. Level 0=280, 1=312, 2=353 MHz (retro-go's table). */
void     board_set_overclock(int level);
int      board_get_overclock(void);
int      board_overclock_levels(void);
uint32_t board_overclock_hz(int level);
void board_gpios_init(void);
void board_lcd_gpios_init(void);
bool board_ospi_init(void);
uint32_t board_ospi_get_size(void);
/* OSPI flash <-> SD SoftSPI pin handoff (Yota9 mod shares the flash pins).
 * Strictly paired: suspend before bit-banging SD, resume after. */
void board_ospi_suspend(void);
void board_ospi_resume(void);
void board_adc_init(void);
void board_rtc_init(void);
uint32_t board_rtc_get_fattime(void);

bool board_check_button(GPIO_TypeDef *port, uint16_t pin);
uint32_t board_get_battery_raw(void);
uint32_t board_get_battery_millivolts(void);
void board_battery_update(int *out_percent, bool *out_plugged);
void board_system_reset(void);
bool board_is_charging(void);
bool board_is_power_good(void);
#endif // BOARD_H
