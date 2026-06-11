#include <ltdc.h>
#include <mm.h>
#include <ospi.h>
#include <rcc.h>
#include <regs.h>
#include <stdio.h>
#include <string.h>
#include <test.h>
#include "board.h"

extern unsigned long _stack_top, _frame_buffer, _data_vma_start, _data_vma_end,
    _data_lma, _bss_vma_start, _bss_vma_end, _stack_top_on_boot,
    __itcram_text_start__, __itcram_text_end__, __itcram_text_lma;

unsigned long systick_cnt = 0;

// retro-go's millisecond clock: the app reads this instead of the raw counter, so
// the firmware owns the time source (SysTick here, a HAL tick on real retro-go).
unsigned long get_elapsed_time(void) { return systick_cnt; }

static void sram_init(void)
{
    unsigned long *src = &_data_lma;
    unsigned long *dst = &_data_vma_start;
    while (dst < &_data_vma_end)
        *dst++ = *src++;

    dst = &_bss_vma_start;
    while (dst < &_bss_vma_end)
        *dst++ = 0;

    src = &__itcram_text_lma;
    dst = &__itcram_text_start__;
    while (dst < &__itcram_text_end__)
        *dst++ = *src++;
}

// Naked trampoline for the final jump — the standard G&W app-handoff sequence.
// No compiler-generated prologue/epilogue, so MSP/SP stays exactly as we set it.
static void __attribute__((naked)) start_app(void (*const pc)(void), uint32_t sp)
{
    __asm("           \n\
          msr msp, r1 /* load r1 into MSP */\n\
          bx r0       /* branch to the address at r0 */\n\
    ");
}

void SystemInit(void);

__attribute__((section(".reset_isr"))) void reset_isr()
{
    RCC_AHB2ENR |=
        RCC_AHB2ENR_SRAM1EN | RCC_AHB2ENR_SRAM2EN;
    __asm__ volatile("dsb");
    // NOTE: Do NOT set MSP here. The reset vector (isr_vec[0] = &_stack_top)
    // already initializes MSP on reset. Writing MSP from inside this non-naked
    // C function moves SP *after* the compiler prologue has allocated locals
    // (e.g. entry_point) relative to the original SP, orphaning them above the
    // new SP. The next nested call (printf) then overwrites them, corrupting
    // the jump target and hardfaulting. Stack setup must stay in the vector.

    // Enable FPU
    SCB->CPACR |= ((3UL << 10*2)|(3UL << 11*2));
    __asm volatile ("dsb");
    __asm volatile ("isb");

    SystemInit();
    sram_init();

    board_early_init();
    board_ospi_init();

    usart_init(115200);

    // Blank the framebuffer BEFORE the panel lights up: SRAM survives warm
    // resets, so without this the first visible frame is whatever the
    // previous session (the app, flasher utility, ...) left behind, rendered
    // through the boot CLUT. Clean the D-cache too — the LTDC reads RAM
    // directly and would otherwise keep scanning the stale bytes.
    memset((void *) &_frame_buffer, 0, 320 * 240);
    SCB_CleanDCache_by_Addr((uint32_t *) &_frame_buffer, 320 * 240);

    // TODO: Transition to the newer OSPI/LCD drivers
    ltdc_init_lut8((void *) &_frame_buffer);
    lcd_panel_init();   // power on + SPI-init the panel, enable backlight
    
    mm_init();
    test_libc();

    // Mount the retro-go LittleFS partition (read-only bring-up; writes happen
    // later from the persistence layer). g_lfs_status records the result.
    extern int lfs_flash_mount(void);
    lfs_flash_mount();
    
    // "Fake retro-go" launch: find the app image on extflash (wherever it was
    // installed), relocate it to that address, and get its entry point. Falls back
    // to the fixed default location if no relocatable header is found.
    extern uint32_t gnw_load_app(void);
    uint32_t entry_point = gnw_load_app();
    if (!entry_point)
        entry_point = (0x90000000 + EXTFLASH_OFFSET) | 1u;

    printf("Jumping to app at 0x%x\n", entry_point);

    // --- Standard board jump-to-app cleanup ---

    // 1. Keep SysTick RUNNING. A typical bootloader stops it because it hands
    //    off to an app that installs its own vector table and SysTick. Our apps
    //    are different: they share THIS firmware's vector table (VTOR stays
    //    at the firmware base) and systick_handler, and read the millisecond
    //    clock through the ABI (HAL_GetTick). Stopping SysTick would freeze
    //    that clock and hang the app, so we deliberately leave it on.

    // 2. Clear PendSV and pending SysTick
    SCB->ICSR |= SCB_ICSR_PENDSVCLR_Msk | SCB_ICSR_PENDSTCLR_Msk;

    // 3. Disable and clear all NVIC interrupts
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    // 4. Clean caches (flush dirty lines) but keep them enabled —
    //    the app runs XIP from OSPI and needs ICache for performance.
    SCB_CleanDCache();
    SCB_InvalidateICache();

    // 5. Barriers
    __DSB();
    __ISB();

    // 6. Jump via naked trampoline — the app entry is at offset 0 (no vector
    //    table). OR 1 into PC to ensure Thumb mode (required on Cortex-M).
    start_app((void (*const)(void))(entry_point | 1), (uint32_t)&_stack_top);

    printf("[!] PANIC: Unreachable\n");
    while (1)
        ;
}

void hardfault_handler(void)
{
    printf("hardfault\n");
    while (1)
        ;
}


void systick_handler(void)
{
    systick_cnt++;
    HAL_IncTick();
}

#define MSP 0
#define RESET 1
#define HARDFAULT 3
#define SYSTICK 15
#define LTDC_IRQ 104

__attribute__((section(".isr_vector"))) unsigned long isr_vec[] = {
    [MSP](unsigned long) & _stack_top,
    [RESET](unsigned long) reset_isr,
    [HARDFAULT](unsigned long) hardfault_handler,
    [SYSTICK](unsigned long) systick_handler
};
;
