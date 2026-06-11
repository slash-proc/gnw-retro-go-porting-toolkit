/* Internal flash placement, overridable via -Wl,--defsym from the Makefile:
 *   __INTFLASH_ORIGIN: 0x08100000 = bank 2 (default), 0x08000000 = bank 1
 *                      (also the address a bank-swapped bank 2 executes at)
 *   __INTFLASH_SIZE:   256K default
 */
__intflash_origin = DEFINED(__INTFLASH_ORIGIN) ? __INTFLASH_ORIGIN : 0x08100000;
__intflash_size   = DEFINED(__INTFLASH_SIZE)   ? __INTFLASH_SIZE   : 256K;
/* External (OSPI) flash, memory-mapped. __EXTFLASH_SIZE from the Makefile
 * (EXTFLASH_SIZE_MB); 64M default = the typical upgrade chip. */
__extflash_size   = DEFINED(__EXTFLASH_SIZE)   ? __EXTFLASH_SIZE   : 64M;

MEMORY {
    ITCMRAM  (rwx) : ORIGIN = 0x00000000, LENGTH = 64K
    DTCMRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
    AXISRAM  (rwx) : ORIGIN = 0x24000000, LENGTH = 1024K
    AHBRAM   (rwx) : ORIGIN = 0x30000000, LENGTH = 128K
    FLASH    (rx)  : ORIGIN = __intflash_origin, LENGTH = __intflash_size
    EXTFLASH (rx)  : ORIGIN = 0x90000000, LENGTH = __extflash_size
}

/* Framebuffer pool reserved at the bottom of AXISRAM (the firmware allocates
 * nothing else there; the app gets the rest). Sized by __FB_BYTES, passed via
 * -Wl,--defsym from the Makefile:
 *   RGB565 double-buffer (default): 300K (2 x 320x240x2)
 *   LUT8 double-buffer:             160K (2 x 75K surfaces + slack, e.g. the
 *                                   audio DMA ping-pong in audio_sai.c)
 */
__fb_reserve = DEFINED(__FB_BYTES) ? __FB_BYTES : 300K;

SECTIONS {
    . = ORIGIN(FLASH);
    .isr_vector : {
        KEEP(*(.isr_vector))
        KEEP(*(.reset_isr))
    } > FLASH

    /* Versioned firmware ABI table pinned at VTOR+0x400 (the vector table is
     * ~684 bytes, well under 0x400). Apps resolve it by reading VTOR at runtime,
     * so it survives reflash to either bank. See include/gnw_firmware_abi.h. */
    .firmware_abi ORIGIN(FLASH) + 0x400 : {
        KEEP(*(.firmware_abi))
    } > FLASH

    .text : {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    .itcram_text : {
        . = ALIGN(4);
        __itcram_text_start__ = .;
        *(.itcram_text)
        . = ALIGN(4);
        __itcram_text_end__ = .;
    } > ITCMRAM AT > FLASH
    __itcram_text_lma = LOADADDR(.itcram_text);

    /* Firmware mutable state (.data/.bss) and the malloc heap live in AHBRAM
       (D2 SRAM @ 0x30000000), NOT DTCMRAM. An app may repurpose all of DTCMRAM
       for its own data/heap, which would otherwise clobber firmware globals
       the app still calls into at runtime — the malloc free-list, systick_cnt,
       HAL state, etc. AHBRAM is used by neither the firmware nor the app, so
       this state survives the handoff. */
    .data : {
        . = ALIGN(4);
        _data_vma_start = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _data_vma_end = .;
    } > AHBRAM AT > FLASH
    _data_lma = LOADADDR(.data);

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _bss_vma_start = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _bss_vma_end = .;
        end = .;
        _end = .;
        __end__ = .;
    } > AHBRAM

    _heap_start = .;                               /* after .bss, in AHBRAM */
    /* Cap the firmware heap at 48K and leave the rest of AHBRAM to the app
       (apps that want it can claim everything from _heap_end up). */
    _heap_end   = ORIGIN(AHBRAM) + 48K;

    /* Stack stays at the top of DTCMRAM; this value is also handed to the app
       as its initial SP. The 16 bytes reserved above it are the boot-magic
       cells (0x2001FFF0..0x20020000). */
    _stack_top = ORIGIN(DTCMRAM) + LENGTH(DTCMRAM) - 16;

    _frame_buffer = ORIGIN(AXISRAM);
    _frame_buffer_end = _frame_buffer + __fb_reserve;

    /* The firmware places nothing in EXTFLASH; apps XIP from it (blob at
       EXTFLASH_OFFSET, memory-mapped by the firmware). */
    _extflash_start = ORIGIN(EXTFLASH);
    _extflash_end   = ORIGIN(EXTFLASH) + LENGTH(EXTFLASH);
}
