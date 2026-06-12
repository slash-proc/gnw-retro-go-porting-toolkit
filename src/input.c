//
// Firmware-owned input. The firmware reads the physical button GPIOs and the
// remote-input mailbox and exposes a single poll, gnw_input_read(), that the
// payload (and any firmware overlay) consume. The payload no longer touches GPIO
// directly — this keeps the firmware as the single owner of input, mirroring how
// retro-go's odroid_input layer feeds its apps, and lets the firmware overlay and
// the game share one input source.
//
// Button bit layout (the payload's existing convention; the remote-input mailbox
// uses the same bits so a debug-probe write is OR'd straight in):
//   0 Up   1 Down  2 Left  3 Right  4 A   5 B
//   6 START 7 TIME  8 SELECT 9 GAME  10 PAUSE  11 POWER
// All buttons are active-low (pressed = GPIO low), configured as input+pull-up by
// the board init that the firmware links.
//

#include <stdint.h>
#include <regs.h>
#include <odroid_input.h>

// Remote-input shadow cell at the top of DTCM (just below the boot-magic
// cells). A debug-probe write of a button bitmask here is OR'd into the
// live state.
#define SRAM_REMOTE_INPUT_ADDR 0x2001FFF4UL

extern void audio_stop(void);
extern void odroid_system_sram_save(void);

// PWR/SCB standby registers (not in regs.h) — relocated verbatim from the app's
// old i_system_gnw.c power_off(). PA0/POWER is WKUP1 and cold-boots on press.
#define PWR_CPUCR   (*(volatile uint32_t *) 0x58024810UL)
#define PWR_WKUPCR  (*(volatile uint32_t *) 0x58024820UL)
#define PWR_WKUPEPR (*(volatile uint32_t *) 0x58024828UL)
#define SCB_SCR     (*(volatile uint32_t *) 0xE000ED10UL)

// Power off: flush the app's SRAM handler, silence the amp, enter Standby (wake on
// POWER). The firmware owns power now (retro-go model); the app no longer does it.
// Does not return.
void power_off(void)
{
    odroid_system_sram_save();              // app flushes its settings (its handler)
    audio_stop();
    GPIOE->BSRR = (1u << (3 + 16));         // speaker amp enable (PE3) low
    PWR_WKUPEPR = (1u << 0) | (1u << 8);    // WKUP1 enabled, active low
    PWR_WKUPCR  = 0x3Fu;                    // clear stale wakeup flags
    PWR_CPUCR  |= (1u << 0) | (1u << 2);    // RETDS_CD | PDDS_SRD: Standby
    SCB_SCR    |= (1u << 2);                // SLEEPDEEP
    __asm__ volatile ("dsb; isb; wfi");
}

uint32_t gnw_input_read(void)
{
    uint32_t c = GPIOC->IDR, d = GPIOD->IDR, a = GPIOA->IDR;
    uint32_t btn = 0;

    if (!(d & (1u << 0)))  btn |= (1u << 0);    // Up     PD0
    if (!(d & (1u << 14))) btn |= (1u << 1);    // Down   PD14
    if (!(d & (1u << 11))) btn |= (1u << 2);    // Left   PD11
    if (!(d & (1u << 15))) btn |= (1u << 3);    // Right  PD15
    if (!(d & (1u << 9)))  btn |= (1u << 4);    // A      PD9
    if (!(d & (1u << 5)))  btn |= (1u << 5);    // B      PD5
    if (!(c & (1u << 11))) btn |= (1u << 6);    // START  PC11
    if (!(c & (1u << 5)))  btn |= (1u << 7);    // TIME   PC5
    if (!(c & (1u << 12))) btn |= (1u << 8);    // SELECT PC12
    if (!(c & (1u << 1)))  btn |= (1u << 9);    // GAME   PC1
    if (!(c & (1u << 13))) btn |= (1u << 10);   // PAUSE  PC13
    if (!(a & (1u << 0)))  btn |= (1u << 11);   // POWER  PA0

    uint32_t remote = *(volatile uint32_t *)SRAM_REMOTE_INPUT_ADDR;
    return btn | remote;
}

// Retro-go input API: map the raw mask into the gamepad struct an app reads as
// js.values[ODROID_INPUT_*]. This is the interface a real retro-go port calls.
void odroid_input_read_gamepad(odroid_gamepad_state_t *out)
{
    uint32_t m = gnw_input_read();

    // POWER + PAUSE -> Standby (firmware owns power). POWER must be released once
    // after boot before it is honored (a wake-from-standby may still hold it).
    static int power_armed;
    if (!(m & (1u << 11))) power_armed = 1;
    else if (power_armed && (m & (1u << 10))) power_off();

    out->values[ODROID_INPUT_UP]     = (m >> 0)  & 1;
    out->values[ODROID_INPUT_DOWN]   = (m >> 1)  & 1;
    out->values[ODROID_INPUT_LEFT]   = (m >> 2)  & 1;
    out->values[ODROID_INPUT_RIGHT]  = (m >> 3)  & 1;
    out->values[ODROID_INPUT_A]      = (m >> 4)  & 1;
    out->values[ODROID_INPUT_B]      = (m >> 5)  & 1;
    /* Slot semantics MUST match real retro-go's odroid_input.c:
     * START<-GAME, SELECT<-TIME, VOLUME<-PAUSE, X<-START, Y<-SELECT. */
    out->values[ODROID_INPUT_X]      = (m >> 6)  & 1;   // physical START
    out->values[ODROID_INPUT_SELECT] = (m >> 7)  & 1;   // physical TIME
    out->values[ODROID_INPUT_Y]      = (m >> 8)  & 1;   // physical SELECT
    out->values[ODROID_INPUT_START]  = (m >> 9)  & 1;   // physical GAME
    out->values[ODROID_INPUT_VOLUME] = (m >> 10) & 1;   // physical PAUSE/SET
    out->values[ODROID_INPUT_POWER]  = (m >> 11) & 1;
    out->bitmask = (uint16_t)m;
    out->values[ODROID_INPUT_VOLUME] = (m >> 10) & 1;   // PAUSE (alias)
}
