#include <regs.h>
#include <stdio.h>

// Direct OctoSPI register base
#define OCTOSPI1 ((struct ospi_regs *) 0x52005000UL)
struct ospi_regs {
    volatile unsigned long CR;
    volatile unsigned long DCR1;
    volatile unsigned long DCR2;
    volatile unsigned long DCR3;
    volatile unsigned long DCR4;
    volatile unsigned long SR;
    volatile unsigned long FCR;
    volatile unsigned long DLR;
    volatile unsigned long CCR;
    volatile unsigned long AR;
    volatile unsigned long ABR;
    volatile unsigned long DR;
    volatile unsigned long PSMKR;
    volatile unsigned long PSMAR;
    volatile unsigned long PIR;
    volatile unsigned long LPTR;
    volatile unsigned long IR;
    volatile unsigned long WCCR;
    volatile unsigned long WWCR;
    volatile unsigned long WPCCR;
    volatile unsigned long WABR;
    volatile unsigned long WTCR;
};

#define OCTOSPI_CR_EN (1UL << 0)
#define OCTOSPI_CR_FMODE (3UL << 28)
#define OCTOSPI_DCR1_DEVSIZE_Pos 16
#define OCTOSPI_DCR1_CSHT_Pos 8
#define OCTOSPI_CCR_IMODE_Pos 0
#define OCTOSPI_CCR_ADMODE_Pos 4
#define OCTOSPI_CCR_DMODE_Pos 8

void ospi_init(void) {
    // 1. Enable Clocks
    RCC_AHB4ENR |= RCC_AHB4ENR_GPIOEEN | RCC_AHB4ENR_GPIOAEN | RCC_AHB4ENR_GPIOBEN | RCC_AHB4ENR_GPIODEN;
    RCC_AHB5ENR |= (1UL << 3); // RCC_AHB5ENR_OCTOSPIMEN
    RCC_AHB3ENR |= (1UL << 14); // RCC_AHB3ENR_OSPI1EN

    // 2. Configure GPIOs for Alternate Function (AF)
    // PE2: AF9 (CLK), PA1: AF9 (NCS), PB1: AF11 (IO0), PB2: AF9 (IO1), PE11: AF11 (IO2), PD12: AF9 (IO3)
    GPIO_MODER_SET(GPIOE, 2, GPIO_MODER_ALT); GPIO_AFR_SET(GPIOE, 2, 9);
    GPIO_MODER_SET(GPIOA, 1, GPIO_MODER_ALT); GPIO_AFR_SET(GPIOA, 1, 9);
    GPIO_MODER_SET(GPIOB, 1, GPIO_MODER_ALT); GPIO_AFR_SET(GPIOB, 1, 11);
    GPIO_MODER_SET(GPIOB, 2, GPIO_MODER_ALT); GPIO_AFR_SET(GPIOB, 2, 9);
    GPIO_MODER_SET(GPIOE, 11, GPIO_MODER_ALT); GPIO_AFR_SET(GPIOE, 11, 11);
    GPIO_MODER_SET(GPIOD, 12, GPIO_MODER_ALT); GPIO_AFR_SET(GPIOD, 12, 9);

    // 3. Configure OSPI controller
    OCTOSPI1->CR = OCTOSPI_CR_EN;
    OCTOSPI1->DCR1 = (27 << OCTOSPI_DCR1_DEVSIZE_Pos) | (2 << OCTOSPI_DCR1_CSHT_Pos);
    
    // 4. Set to Memory Mapped Mode
    OCTOSPI1->CCR = (3 << OCTOSPI_CCR_IMODE_Pos) | (3 << OCTOSPI_CCR_ADMODE_Pos) | (3 << OCTOSPI_CCR_DMODE_Pos) | 0xEB;
    OCTOSPI1->CR |= OCTOSPI_CR_FMODE;
}
