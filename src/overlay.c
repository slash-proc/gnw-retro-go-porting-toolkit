//
// Firmware-owned in-game overlay menu (retro-go style), drawn in LUT8 directly
// into the shared framebuffer over the frozen game frame.
//
// The payload yields to gnw_overlay_run() when it sees the trigger (GAME+PAUSE);
// this blocks the game loop, draws a menu, and dispatches actions through the
// retro-go persistence callbacks (odroid_system_emu_*), then returns so the game
// resumes and redraws. The app passes its current CLUT so we pick text/box colors
// from the live palette (no CLUT writes, so no glitch when the game resumes).
//
// Not yet faithful in one respect: it blocks rather than letting the app keep
// pumping audio each frame, so sound holds its last buffer while the menu is open
// (addressed in the persistence-polish task). Kept simple and self-contained.
//

#include <stdint.h>

#include <odroid_input.h>
#include <odroid_system.h>
#include <audio_sai.h>
#include "font8x8.h"

extern void HAL_Delay(uint32_t ms);
extern void *lcd_get_inactive_buffer(void);   // the surface the LTDC is scanning
extern const uint32_t *lcd_get_clut(void);    // the app's live palette

#define FB_W      320
#define FB_H      240

// The overlay renders into the live (scanned) framebuffer so it composites over
// the frozen game frame. With double-buffering that's lcd_get_inactive_buffer()
// (the just-presented surface), captured when the menu opens (the overlay blocks,
// so no swap moves it while open). Set in gnw_overlay_run.
static volatile uint8_t *g_fb;

// Boot-menu return via the SRAM magic cells.
#define SRAM_MAGIC_ADDR    0x2001FFF8UL
#define SRAM_MAGIC_TARGET  0x2001FFFCUL
#define BOOT_MAGIC_FORCE   0x424F4F54UL  // "BOOT"

static uint8_t g_box, g_text;   // palette indices chosen from the live CLUT

static void pick_colors(const uint32_t *clut)
{
    // Darkest entry -> box, brightest -> text. clut entries are 0x00RRGGBB.
    int lo = 0x7fffffff, hi = -1;
    g_box = 0; g_text = 255;
    if (!clut) return;
    for (int i = 0; i < 256; i++) {
        uint32_t c = clut[i];
        int lum = (int)(c & 0xff) + (int)((c >> 8) & 0xff) + (int)((c >> 16) & 0xff);
        if (lum < lo) { lo = lum; g_box = (uint8_t)i; }
        if (lum > hi) { hi = lum; g_text = (uint8_t)i; }
    }
}

static void fill_rect(int x, int y, int w, int h, uint8_t idx)
{
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= FB_H) continue;
        volatile uint8_t *row = g_fb + yy * FB_W;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx >= 0 && xx < FB_W) row[xx] = idx;
        }
    }
}

// 8x8 glyph scaled by `s`. Lowercase folds to uppercase.
static void draw_char(int x, int y, char ch, uint8_t idx, int s)
{
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if ((unsigned char)ch < FONT8X8_FIRST || (unsigned char)ch > FONT8X8_LAST) ch = ' ';
    const uint8_t *g = font8x8[(unsigned char)ch - FONT8X8_FIRST];
    for (int ry = 0; ry < 8; ry++) {
        uint8_t bits = g[ry];
        for (int rx = 0; rx < 8; rx++) {
            if (bits & (0x80 >> rx))
                fill_rect(x + rx * s, y + ry * s, s, s, idx);
        }
    }
}

static int str_w(const char *s, int scale) { int n = 0; while (s[n]) n++; return n * 8 * scale; }

static void draw_str(int x, int y, const char *s, uint8_t idx, int scale)
{
    for (int i = 0; s[i]; i++)
        draw_char(x + i * 8 * scale, y, s[i], idx, scale);
}

static const char *const MENU_ITEMS[] = {
    "RESUME GAME",
    "SAVE GAME",
    "LOAD GAME",
    "OVERCLOCK",         // value item: Left/Right cycles the CPU clock
    "QUIT TO LAUNCHER",
};
#define N_ITEMS ((int)(sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0])))
#define OC_ITEM   3      // index of the OVERCLOCK row above

// Firmware CPU overclock (board.c). Only PLL1 changes; OSPI is on HSI so XIP is safe.
extern void board_set_overclock(int level);
extern int  board_get_overclock(void);
extern int  board_overclock_levels(void);
static const char *const OC_LABELS[] = { "< 280 MHz >", "< 312 MHz >", "< 353 MHz >" };

static void draw_menu(int sel, const char *status)
{
    const int bw = 240, bh = 150;
    const int bx = (FB_W - bw) / 2, by = (FB_H - bh) / 2;

    fill_rect(bx, by, bw, bh, g_box);
    // 2px border in text color.
    fill_rect(bx, by, bw, 2, g_text);
    fill_rect(bx, by + bh - 2, bw, 2, g_text);
    fill_rect(bx, by, 2, bh, g_text);
    fill_rect(bx + bw - 2, by, 2, bh, g_text);

    draw_str(bx + (bw - str_w("RETRO-GO", 2)) / 2, by + 10, "RETRO-GO", g_text, 2);

    int iy = by + 42;
    for (int i = 0; i < N_ITEMS; i++) {
        if (i == sel)
            draw_str(bx + 14, iy, ">", g_text, 1);
        draw_str(bx + 28, iy, MENU_ITEMS[i], g_text, 1);
        if (i == OC_ITEM) {   // append the current "< NNN MHz >" value, right side
            const char *v = OC_LABELS[board_get_overclock()];
            draw_str(bx + bw - 14 - str_w(v, 1), iy, v, g_text, 1);
        }
        iy += 16;
    }
    if (status)
        draw_str(bx + 14, by + bh - 16, status, g_text, 1);
}

static void wait_release(void)
{
    // Let go of the opening GAME+PAUSE combo before reading menu input.
    while (gnw_input_read() & ((1u << 9) | (1u << 10)))
        HAL_Delay(10);
    HAL_Delay(60);
}

#define N_SLOTS 3

static void draw_slot_menu(const char *title, int sel)
{
    const int bw = 240, bh = 150;
    const int bx = (FB_W - bw) / 2, by = (FB_H - bh) / 2;

    fill_rect(bx, by, bw, bh, g_box);
    fill_rect(bx, by, bw, 2, g_text);
    fill_rect(bx, by + bh - 2, bw, 2, g_text);
    fill_rect(bx, by, 2, bh, g_text);
    fill_rect(bx + bw - 2, by, 2, bh, g_text);

    draw_str(bx + (bw - str_w(title, 2)) / 2, by + 10, title, g_text, 2);

    int iy = by + 42;
    for (int i = 0; i < N_SLOTS; i++) {
        char item[8] = { 'S', 'L', 'O', 'T', ' ', (char)('1' + i), 0 };
        if (i == sel)
            draw_str(bx + 14, iy, ">", g_text, 1);
        draw_str(bx + 28, iy, item, g_text, 1);
        iy += 16;
    }
    draw_str(bx + 14, by + bh - 16, "B: BACK", g_text, 1);
}

// Second-level slot select for save/load. Returns 0..N_SLOTS-1 or -1 (back).
static int pick_slot(const char *title)
{
    int sel = 0;
    uint32_t prev = gnw_input_read();
    draw_slot_menu(title, sel);

    for (;;) {
        HAL_Delay(30);
        uint32_t cur = gnw_input_read();
        uint32_t edge = cur & ~prev;
        prev = cur;

        if (edge & (1u << 0)) { sel = (sel + N_SLOTS - 1) % N_SLOTS; draw_slot_menu(title, sel); }  // Up
        if (edge & (1u << 1)) { sel = (sel + 1) % N_SLOTS;           draw_slot_menu(title, sel); }  // Down
        if (edge & ((1u << 5) | (1u << 10)))   // B or PAUSE = back
            return -1;
        if (edge & (1u << 4))                  // A = select
            return sel;
    }
}

// Returns after the user resumes or an action is dispatched. `clut` is the app's
// live palette (256 x 0x00RRGGBB) used to pick legible colors.
void common_ingame_overlay(void)
{
    g_fb = (volatile uint8_t *)lcd_get_inactive_buffer();   // the visible frozen frame
    pick_colors(lcd_get_clut());
    audio_silence();        // zero the DMA buffer so the held tone goes quiet
    wait_release();

    int sel = 0;
    uint32_t prev = gnw_input_read();
    draw_menu(sel, 0);

    for (;;) {
        HAL_Delay(30);
        uint32_t cur = gnw_input_read();
        uint32_t edge = cur & ~prev;   // newly pressed
        prev = cur;

        if (edge & (1u << 0)) { sel = (sel + N_ITEMS - 1) % N_ITEMS; draw_menu(sel, 0); }  // Up
        if (edge & (1u << 1)) { sel = (sel + 1) % N_ITEMS;          draw_menu(sel, 0); }   // Down

        // Left/Right adjusts the OVERCLOCK value in place (applied immediately).
        if ((edge & ((1u << 2) | (1u << 3))) && sel == OC_ITEM) {
            int n = board_overclock_levels();
            int d = (edge & (1u << 3)) ? 1 : (n - 1);   // Right=+1, Left=-1
            board_set_overclock((board_get_overclock() + d) % n);
            draw_menu(sel, 0);
        }

        // B or PAUSE closes (resume).
        if (edge & ((1u << 5) | (1u << 10)))
            goto done;

        if (edge & (1u << 4)) {   // A = select
            switch (sel) {
            case 0:   // Resume
                goto done;
            case 1: { // Save -> slot picker (the app defers the work; runs after we return)
                int slot = pick_slot("SAVE");
                if (slot < 0) { wait_release(); prev = gnw_input_read(); draw_menu(sel, 0); continue; }
                odroid_system_emu_save_state(slot);
                goto done;
            }
            case 2: { // Load -> slot picker
                int slot = pick_slot("LOAD");
                if (slot < 0) { wait_release(); prev = gnw_input_read(); draw_menu(sel, 0); continue; }
                odroid_system_emu_load_state(slot);
                goto done;
            }
            case OC_ITEM: {  // Overclock: A cycles forward (same as Right), stay in menu
                int n = board_overclock_levels();
                board_set_overclock((board_get_overclock() + 1) % n);
                draw_menu(sel, 0);
                continue;
            }
            case 4:   // Quit to launcher (boot menu) — resets, never returns.
                odroid_system_sram_save();   // flush settings before we leave
                *(volatile uint32_t *)SRAM_MAGIC_ADDR   = BOOT_MAGIC_FORCE;
                *(volatile uint32_t *)SRAM_MAGIC_TARGET = 0x08000000;
                __asm volatile ("dsb");
                *(volatile uint32_t *)0xE000ED0C = (0x5FAu << 16) | (1u << 2); // SYSRESETREQ
                while (1) {}
            }
        }
    }
done:
    return;
}

// retro-go ABI parity: the standard in-game loop. Real retro-go implements the
// PAUSE menu + macros (save/load, volume, brightness) and the bare-POWER
// save+sleep here; this test firmware maps the essentials onto its own
// blocking overlay so an app exercising the retro-go convention behaves
// equivalently: PAUSE/SET tap-release -> overlay menu, POWER -> power off.
#include "odroid_input.h"
void common_emu_input_loop(odroid_gamepad_state_t *js, void *game_options, void (*repaint)(void))
{
    (void)game_options; (void)repaint;
    static int pause_down;
    if (js->values[ODROID_INPUT_POWER]) {
        extern void power_off(void);
        power_off();                       // does not return
    }
    if (js->values[ODROID_INPUT_VOLUME]) {
        pause_down = 1;
        js->values[ODROID_INPUT_VOLUME] = 0;   // consume, like retro-go
    } else if (pause_down) {
        pause_down = 0;
        common_ingame_overlay();           // blocking save/load/quit menu
    }
}
