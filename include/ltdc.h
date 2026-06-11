#ifndef LTDC_H
#define LTDC_H

#include <stdint.h>

void ltdc_init_lut8(void *frame_buffer);
void lcd_set_clut(const uint32_t *clut, uint16_t count);
const uint32_t *lcd_get_clut(void);   // last CLUT set (for the firmware overlay)

// retro-go double-buffer (the core draws into active, swaps; tear-free at vblank).
typedef enum { LCD_MODE_RGB565 = 0, LCD_MODE_LUT8 = 1 } lcd_mode_t;
void  lcd_setup_framebuffers(int lcd_mode);
void *lcd_get_active_buffer(void);
void *lcd_get_inactive_buffer(void);
void  lcd_swap(void);

// Power on and SPI-initialize the physical LCD panel, then enable the
// backlight. Call after ltdc_init_lut8() (LTDC must already be scanning valid
// data by the time the panel's SPI init sequence completes).
void lcd_panel_init(void);

#endif
